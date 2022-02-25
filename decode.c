
#include <math.h>
#include "bitmap.h"
#include "unpack.h"

static const bool kCheckSinCosTable = false;

static const uint8_t kPaletteIcons[16 * 3] = {
	0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0x44, 0x22, 0x00, 0x44, 0x44, 0xaa,
	0xcc, 0x66, 0x66, 0x88, 0x88, 0x88, 0x66, 0x66, 0xcc, 0x66, 0x44, 0x22,
	0xee, 0x00, 0x44, 0x88, 0x00, 0x44, 0x44, 0x44, 0x44, 0x00, 0xee, 0x00,
	0x00, 0x66, 0x00, 0xee, 0xee, 0x00, 0xaa, 0x22, 0xee, 0x00, 0x00, 0x00
};

static void decodeCT(const uint8_t *src, uint32_t size) {
	const uint32_t uncompressedSize = READ_BE_UINT32(src + size - 4);
	assert(uncompressedSize == 0x1D00);
	uint8_t *buf = (uint8_t *)malloc(uncompressedSize);
	if (buf) {
		const uint32_t ret = bytekiller_unpack(buf, uncompressedSize, src, size);
		assert(ret == 0);
		free(buf);
	}
}

static void decodeFNT(const uint8_t *src, uint32_t size) {
	static const int W = 8;
	static const int H = 8;
	const int count = size / 32;
	uint8_t *bitmap = (uint8_t *)malloc(W * H * count);
	if (bitmap) {
		for (int i = 0; i < count; ++i) {
			for (int y = 0; y < H; ++y) {
				uint8_t *dst = bitmap + y * W * count + i * W;
				for (int x = 0; x < 4; ++x) {
					const uint8_t color = *src++;
					*dst++ = color >> 4;
					*dst++ = color & 15;
				}
			}
		}
		uint8_t palette[16 * 3];
		for (int i = 0; i < 16; ++i) {
			palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = (i << 4) | i;
		}
		saveBMP("font.bmp", bitmap, W * count, H, palette, 16);
		free(bitmap);
	}
}

static void decodeICN(const uint8_t *src, uint32_t size) {
	static const int W = 16;
	static const int H = 16;
	const int count = size / 128;
	uint8_t *bitmap = (uint8_t *)malloc(W * H * count);
	if (bitmap) {
		int offset = 0;
		for (int i = 0; i < count; ++i) {
			for (int y = 0; y < H; ++y) {
				uint8_t *dst = bitmap + y * W * count + i * W;
				for (int x = 0; x < 4; ++x) {
					const uint8_t color = src[offset + x];
					*dst++ = color >> 4;
					*dst++ = color & 15;
				}
				for (int x = 0; x < 4; ++x) {
					const uint8_t color = src[offset + 64 + x];
					*dst++ = color >> 4;
					*dst++ = color & 15;
				}
				offset += 4;
			}
			offset += 64;
		}
		saveBMP("icons.bmp", bitmap, W * count, H, kPaletteIcons, 16);
		free(bitmap);
	}
}

static void decodeOBJ(const uint8_t *src, uint32_t size) {
	uint32_t offset = READ_BE_UINT32(src); /* offset to first object_t */
	while (offset < size) {
		const int count = READ_BE_UINT16(src + offset); offset += 2;
		offset += sizeof(struct object_t) * count;
	}
	assert(offset == size);
}

static void decodePGE(const uint8_t *src, uint32_t size) {
	const int count = READ_BE_UINT16(src); src += 2;
	assert((size - 2) == count * sizeof(struct piege_t));
}

struct {
	const char *ext;
	void (*decode)(const uint8_t *data, uint32_t size);
} _decoders[] = {
	{ "CT",  decodeCT },
	{ "FNT", decodeFNT },
	{ "ICN", decodeICN },
	{ "OBJ", decodeOBJ },
	{ "PGE", decodePGE },
	{ 0, 0 }
};

void decode(const char *name, const uint8_t *data, uint32_t size) {
	const char *ext = strrchr(name, '.');
	if (ext) {
		++ext;
		for (int i = 0; _decoders[i].ext; ++i) {
			if (strcasecmp(ext, _decoders[i].ext) == 0) {
				(_decoders[i].decode)(data, size);
				return;
			}
		}
	}
	if (kCheckSinCosTable && strcmp(name, "TRIGO.BIN") == 0) {
		assert(size == 360 * 2 * sizeof(int16_t));
		for (int a = 0; a < 360; ++a) {
			const int16_t cos_t = READ_BE_UINT16(data + 2 * a);
			const int16_t cos_m = (int16_t)(cos(a * M_PI / 180) * 256);
			if (cos_t != cos_m) {
				fprintf(stdout, "cos a=%d %d %d\n", a, cos_t, cos_m);
			}
			const int16_t sin_t = READ_BE_UINT16(data + 2 * (360 + a));
			const int16_t sin_m = (int16_t)(sin(a * M_PI / 180) * 256);
			if (sin_t != sin_m) {
				fprintf(stdout, "sin a=%d %d %d\n", a, sin_t, sin_m);
			}
		}
	}
}
