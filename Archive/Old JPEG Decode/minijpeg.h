// minijpeg.h
#ifndef MINIJPEG_H
#define MINIJPEG_H

// Save RGB image as JPEG (baseline, non-progressive, 3-channel, integer-only)
// Inputs:
// - filename: path to write JPEG (e.g., "T:output.jpg")
// - rgb: pointer to RGB24 pixel buffer (width * height * 3)
// - width, height: image dimensions in pixels
// Output:
// - 0 on success, -1 on error
int save_minimal_jpeg(const char *filename, const unsigned char *rgb, int width, int height);

#endif
