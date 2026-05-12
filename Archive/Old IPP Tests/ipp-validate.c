#include <stdio.h>
#include <string.h>

unsigned char data[32768];

int read_be_int(unsigned char *ptr) {
    return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
}

int match_string(unsigned char *ptr, const char *text, int len) {
    return (memcmp(ptr, text, len) == 0);
}

int main() {
    FILE *f = fopen("ipp_output.log", "rb");
    if (!f) {
        puts("Can't open ipp_output.log");
        return 1;
    }

    int len = fread(data, 1, sizeof(data), f);
    fclose(f);

    printf("Scanning for media-col-ready...\n");

    for (int i = 0; i < len - 14; i++) {
        if (match_string(&data[i], "media-col-ready", 15)) {
            printf("Found media-col-ready at offset %d\n", i);

            int tray_found = 0, x = -1, y = -1;
            char tray[64] = {0};

            for (int j = i; j < i + 300; j++) {
                if (match_string(&data[j], "media-source", 12)) {
                    int val_len = (data[j+12] << 8) | data[j+13];
                    if (val_len > 0 && val_len < sizeof(tray)) {
                        memcpy(tray, &data[j+14], val_len);
                        tray[val_len] = '\0';
                        tray_found = 1;
                        printf("  Tray: %s\n", tray);
                    }
                }

                if (match_string(&data[j], "x-dimension", 11)) {
                    x = read_be_int(&data[j + 13]);
                    printf("  x = %d\n", x);
                }

                if (match_string(&data[j], "y-dimension", 11)) {
                    y = read_be_int(&data[j + 13]);
                    printf("  y = %d\n", y);
                }

                if (match_string(&data[j], "tray", 11)) {
                    y = read_be_int(&data[j + 13]);
                    printf("  tray = %d\n", y);
                }

                if (tray_found && x > 0 && y > 0) {
                    printf("✅ Tray: %s, Size: %.1f x %.1f mm\n", tray, x / 100.0, y / 100.0);
                    break;
                }
            }
        }
    }

    return 0;
}
