# AGENTS.md — MintPRINT (AROS aarch64)

## Cel projektu
Kolorowa drukarka sieciowa IPP/MDNS + GUI znane z klasycznego AmigaOS/Mint —
repo utrzymywane na **AROS aarch64** (to jest domyślny target, odpowiednik
struktury z forka AROS z ~2016). Build `make` (domyślnie) produkuje:
- `MintPrintSettings` — aplikacja GUI ("MintPrintSettings"/per-job defaults),
- `build/driver/MintPRINT` — sterownik `DEVS:Printers/MintPRINT`.

Obydwa to ELF 64-bit ARM aarch64, format **AROS (AROS Research Operating
System)**, nie-stripped (mają sekcje symboli → `nm`/ABI dostępne).

## CRITICAL — dlaczego build "na swojej maszynie" wymaga kontenera
Crosstools ELF (ze ścierki `.../bin/aarch64-aros-gcc`) mają **załańcuchowany
--sysroot** wskazujący oryginalne drzewo Linux-buildera (`/work/build-aarch64-
main/...`), które NIE istnieje na żadnej innej maszynie. Bez nadpisania:

- nagłówki SDK (np. `proto/exec.h`, `proto/graphics.h`, `string.h`) nie mogą
  się rozwiązać → GUI w ogóle się nie kompiluje;
- **linker `collect-aros`** szuka `aarch64-aros-ld/as/ar` pod **bake'owym**
  ścieżkami `/work/build-aarch64-main/bin/linux-aarch64/tools/crosstools/...`
  (PATH nie ma wpływu — ścieżka bezwzględna w ELF).

### Jak to naprawiliśmy (2 sztuczki, obie w Makefile/GNUmakefile)
1. **--sysroot z SDK toolchaina**: Makefile liczy `AROS_SDK := $(wildcard
   $(dir $(shell command -v $(CC)))/../sysroot)` — jeśli katalog realnie
   istnieje, dodaje `CFLAGS += --sysroot="$(AROS_SDK)"`. CLI `--sysroot`
   **unieważnia** bake'owy i poprawia zarówno `-I` dla nagłówków, jak i
   `-L`/`-laros` dla `libaros.a` (który jest w `sysroot/lib/`).
2. **Farmy `/work/.../crosstools/`**: w kontenerze (gdzie PLIK systemowy
   pozwala `ln -s`) tworzymy to drzewo jako **symlinki od `bin/` toolchaina**
   do `build/.../crosstools/aarch64-aros-<tool>`. Dzięki temu `collect-aros`
   znajduje `ld/as/ar/...` i **link przechodzi**.

> UWAGA: `/work/...` NIE da się zrobić na macOS (ro → read-only /). Dlatego
> cały build wykonujemy w **Docker arm64v8/ubuntu:24.04** — na tym kontenerze
> `/work` jest PIERWSZYM miejscem które tworzymy, a potem `make`.

## Zbudować (przepis działający, container)
```bash
# w repo (host mac)
docker run --rm \
  -v "$PWD:/src" \
  -v "$HOME/aros-toolchains:/tc" \
  arm64v8/ubuntu:24.04 bash -lc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq && apt-get install -y -qq make >/dev/null 2>&1
    # farma crosstools — NIEZBĘDNA do linku ELF
    WORK=/work/build-aarch64-main/bin/linux-aarch64/tools/crosstools
    TC=/tc/aros-toolchain-aarch64
    mkdir -p "$WORK"
    for t in ld as ar nm objcopy strip ranlib size objdump; do
      ln -sf "$TC/bin/aarch64-aros-$t" "$WORK/aarch64-aros-$t"
    done
    export PATH="$TC/bin:$PATH"
    export CFLAGS="--sysroot=$TC/sysroot -I$TC/sysroot/include"
    cd /src && make'
```

## Co już zrobione (nie zaczynaj od zera!)
- Makefile ma rule `gui` (MintPrintSettings) i `driver` (build/driver/MintPRINT)
  + `--sysroot` + farmę w komentarzu (zob. nagłówek `AROS SDK --sysroot`).
- Ostatnie kompilacje w kontenerze przeszły: GUI `rc=0`, DRIVER `rc=0`,
  RELEASE `rc=0`. Artefakty: 567712 B (GUI) i 262720 B (driver) ELF aarch64
  AROS.

## ABI: czego się trzymamy (symbol tekstowy do przyszłych zmian)
Driver (AROS aarch64) to `TextDriver` z `CommandTable` — sygnatury matchyrą te
z `origin/main` (PrinterDriver V44+). Przy zmianach trzymać:
- `TextDriverOpen/TextDriverClose/TextDriverDoSpecial/TextDriverRender/...`
  nazwy funkcji w `src/` i `driver/` bez zmiany (ELF relocatable — symbol
  boundaries muszą się zgadzać z tabelą komend AmigaOS driverroot).
- `CommandTable` w driver (patrz Makefile `driver-symbols`/nm).

## Help / building alternatywny
```
make help          # wszystko o targetach (gui/driver/release/clean)
make gui           # tylko GUI (AROS aarch64)
make driver        # tylko driver (AROS aarch64)
make release       # bundle do release/MintPRINT/
```
Domyślny Makefile (bez `CROSS=`): AROS aarch64. Klasyczny AmigaOS m68k
(AmigaOS 3.x) dostępny przez nadpisanie:
```
make CROSS=m68k-amigaos- CFLAGS='-Os -m68000 -Wall -Wextra' gui driver
```
