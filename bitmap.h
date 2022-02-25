
#ifndef BITMAP_H__
#define BITMAP_H__

#include <stdint.h>

void saveBMP(const char *filename, const uint8_t *bits, int w, int h, const uint8_t *pal, int colors);

#endif /* BITMAP_H__ */
