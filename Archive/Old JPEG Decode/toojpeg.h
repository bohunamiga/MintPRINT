#ifndef TOOJPEG_H
#define TOOJPEG_H

#include <exec/types.h>  /* For BYTE type */

/* Callback function type for writing JPEG data */
typedef void (*toojpeg_write_func)(void* context, BYTE byte);

/* Main JPEG encoding function 
 * Parameters:
 * - context: User-defined context passed to write callback
 * - write: Callback function to write each byte
 * - pixels: Image pixel data (RGB or grayscale)
 * - width: Image width in pixels
 * - height: Image height in pixels
 * - is_rgb: 1 for RGB data (3 bytes per pixel), 0 for grayscale (1 byte per pixel)
 * - quality: JPEG quality (1-100, higher is better quality)
 * Returns: 1 on success, 0 on failure
 */
int toojpeg_write_jpeg(void* context, toojpeg_write_func write, 
                      BYTE* pixels, int width, int height, 
                      int is_rgb, int quality);

#endif /* TOOJPEG_H */