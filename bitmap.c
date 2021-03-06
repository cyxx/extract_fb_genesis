
#include <assert.h>
#include <stdio.h>
#include "bitmap.h"

static void fwriteUint16LE(FILE *fp, uint16_t value) {
	fputc(value & 255, fp);
	fputc(value >> 8, fp);
}

static void fwriteUint32LE(FILE *fp, uint32_t value) {
	fwriteUint16LE(fp, value & 0xFFFF);
	fwriteUint16LE(fp, value >> 16);
}

static const uint16_t TAG_BM = 0x4D42;

void saveBMP(const char *filename, const uint8_t *bits, int w, int h, const uint8_t *pal, int colors) {
	FILE *fp = fopen(filename, "wb");
	if (fp) {
		const int alignWidth = (w + 3) & ~3;
		const int imageSize = alignWidth * h;

		// Write file header
		fwriteUint16LE(fp, TAG_BM);
		fwriteUint32LE(fp, 14 + 40 + 4 * 256 + imageSize);
		fwriteUint16LE(fp, 0); // reserved1
		fwriteUint16LE(fp, 0); // reserved2
		fwriteUint32LE(fp, 14 + 40 + 4 * 256);

		// Write info header
		fwriteUint32LE(fp, 40);
		fwriteUint32LE(fp, w);
		fwriteUint32LE(fp, h);
		fwriteUint16LE(fp, 1); // planes
		fwriteUint16LE(fp, 8); // bit_count
		fwriteUint32LE(fp, 0); // compression
		fwriteUint32LE(fp, imageSize); // size_image
		fwriteUint32LE(fp, 0); // x_pels_per_meter
		fwriteUint32LE(fp, 0); // y_pels_per_meter
		fwriteUint32LE(fp, 0); // num_colors_used
		fwriteUint32LE(fp, 0); // num_colors_important

		// Write palette data
		for (int i = 0; i < colors; ++i) {
			fputc(pal[2], fp);
			fputc(pal[1], fp);
			fputc(pal[0], fp);
			fputc(0, fp);
			pal += 3;
		}
		// Pad palette to 256 colors
		for (int i = colors; i < 256; ++i) {
			fwriteUint32LE(fp, 0);
		}

		// Write bitmap data
		const int pitch = w;
		bits += h * pitch;
		for (int i = 0; i < h; ++i) {
			bits -= pitch;
			fwrite(bits, w, 1, fp);
			int pad = alignWidth - w;
			while (pad--) {
				fputc(0, fp);
			}
		}

		fclose(fp);
	}
}
