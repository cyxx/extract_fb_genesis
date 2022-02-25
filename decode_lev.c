
#include "bitmap.h"
#include "unpack.h"

static const int kRoomW = 256;
static const int kRoomH = 224;

static const bool kDumpSGD = true;
static const bool kDrawPalettes = false;

static const bool kFixLevel1Room26PlantYPos = true;

static const char *kNames[] = {
	"level1",
	"level2",
	"dt",
	"level3",
	"level4",
	"present",
	0
};

/* bit 15: foreground */
/* bits 14 and 13: palette (0-3) */
static const uint16_t kFlagFlipY = (1 << 12);
static const uint16_t kFlagFlipX = (1 << 11);
/* bit 10: */
/* bits 9..0: tile index, -0x380 if .SGD */

#define DECODE_BUFSIZE 0xFFFF

struct decodelev_t {
	int level, room;
	bool sgd;
	uint8_t roomPalette[16 * 3 * 4];
	uint8_t decodeLevBuf[4096];
	uint8_t xTile[32], yTile[32];
	uint8_t roomBitmap[256 * 224];
	uint16_t roomOffset10, roomOffset12;
	uint8_t sgdDecodeBuf[7174 * 16];
	uint8_t tmp[DECODE_BUFSIZE];
	uint8_t uncompressedMbkBuffer[DECODE_BUFSIZE]; /* 8x8 tiles, 32 bytes */
};

static void fillRect(uint8_t *dst, int x, int y, int w, int h, uint8_t color) {
        dst += y * kRoomW + x;
        for (int i = 0; i < h; ++i) {
                memset(dst, color, w);
                dst += kRoomW;
        }
}

static void decodeTileSGD(uint8_t *dst, int dstPitch, int x, int y, int w, int h, const uint8_t *src, const uint8_t *mask, int size) {
	const int x0 = x;
	const int y0 = y;

	++w;
	++h;
	const int planarSize = w * 2 * h;
	assert(planarSize == size);

	dst += y * dstPitch + x;

	for (y = 0; y < h; ++y) {
		for (x = 0; x < w; ++x) {
			const uint16_t bits = READ_BE_UINT16(src); src += 2;
			uint16_t bitmask = 0x8000;
			for (int b = 0; b < 16; b += 2) {
				const int offset = x * 16 + b;
				const uint8_t color = *mask++;
				if (y0 + y < 0 || y0 + y >= kRoomH) {
					bitmask >>= 2;
					continue;
				}
				if (x0 + offset < 0 || x0 + offset + 1 >= kRoomW) {
					bitmask >>= 2;
					continue;
				}
				if (bits & bitmask) {
					dst[offset] = color >> 4;
				}
				bitmask >>= 1;
				if (bits & bitmask) {
					dst[offset + 1] = color & 15;
				}
				bitmask >>= 1;
			}
		}
		dst += dstPitch;
	}
}

static void decodeTile8x8(uint8_t *dst, int x, int y, uint8_t *src, int mask) {
	dst += (y * kRoomW + x) * 8;
	for (y = 0; y < 8; ++y, dst += kRoomW) {
		for (int i = 0; i < 4; ++i) {
			dst[2 * i]     = (src[i] >> 4) + mask;
			dst[2 * i + 1] = (src[i] & 15) + mask;
		}
		src += 4;
	}
}

static void decodeMaskTile8x8(uint8_t *dst, int x, int y, uint8_t *src, uint8_t *src_mask, int mask) {
	dst += (y * kRoomW + x) * 8;
	for (y = 0; y < 8; ++y, dst += kRoomW) {
		for (int i = 0; i < 4; ++i) {
			if ((src[i] >> 4) != 0) {
				dst[2 * i] = (src[i] >> 4) + mask;
			}
			if ((src[i] & 15) != 0) {
				dst[2 * i + 1] = (src[i] & 15) + mask;
			}
		}
		src += 4;
	}
}

static int decodeRLE(const uint8_t *src, uint8_t *dst) {
	int uncompressedSize = 0;
	const uint16_t compressedSize = READ_BE_UINT16(src) & 0x7FFF; src += 2;
	const uint8_t *src_end = src + compressedSize;
	do {
		int8_t code = *src++;
		if (code < 0) {
			code = -code;
			for (int i = 0; i < code + 1; ++i) {
				*dst++ = *src;
			}
			++src;
		} else {
			for (int i = 0; i < code + 1; ++i) {
				*dst++ = *src++;
			}
		}
		uncompressedSize += code + 1;
	} while (src < src_end);
	assert(src == src_end);
	return uncompressedSize;
}

static uint8_t *flipTileY(uint8_t *a2, uint8_t *yTile) {
	for (int y = 0; y < 8; ++y) {
		memcpy(yTile + (7 - y) * 4, a2, 4);
		a2 += 4;
	}
	return yTile;
}

static uint8_t *flipTileX(uint8_t *a2, uint8_t *xTile) {
	uint8_t *a0 = &xTile[0];
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 4; ++x) {
			const uint8_t b = a2[3 - x];
			*a0++ = (b >> 4) | ((b & 15) << 4);
		}
		a2 += 4;
	}
	return xTile;
}

static void decodeLevRoomHelper(struct decodelev_t *d, const uint8_t *lev) {
	if (!d->sgd) {
		const uint8_t *a0 = lev + d->roomOffset10;
		for (int y = 0; y < kRoomH / 8; ++y) {
			for (int x = 0; x < kRoomW / 8; ++x) {
				const uint16_t flags = READ_BE_UINT16(a0); a0 += 2;
				uint16_t tileNum = flags & 0x7FF;
				if (tileNum != 0) {
					uint8_t *a2 = d->uncompressedMbkBuffer + tileNum * 32;
					if (flags & kFlagFlipY) {
						a2 = flipTileY(a2, d->yTile);
					}
					if (flags & kFlagFlipX) {
						a2 = flipTileX(a2, d->xTile);
					}
					const int mask = (flags >> 9) & 0x30;
					decodeTile8x8(d->roomBitmap, x, y, a2, mask);
				}
			}
		}
	}
	const uint8_t *a0 = lev + d->roomOffset12;
	for (int y = 0; y < kRoomH / 8; ++y) {
		for (int x = 0; x < kRoomW / 8; ++x) {
			const uint16_t flags = READ_BE_UINT16(a0); a0 += 2;
			uint16_t tileNum = flags & 0x7FF;
			if (tileNum != 0 && d->sgd) {
				tileNum -= 0x380;
			}
			if (tileNum != 0) {
				uint8_t *a2 = d->uncompressedMbkBuffer + tileNum * 32;
				if (flags & kFlagFlipY) {
					a2 = flipTileY(a2, d->yTile);
				}
				if (flags & kFlagFlipX) {
					a2 = flipTileX(a2, d->xTile);
				}
				const int mask = (flags >> 9) & 0x30;
				decodeMaskTile8x8(d->roomBitmap, x, y, a2, 0, mask);
			}
		}
	}
}

static void loadSGD(struct decodelev_t *d, const uint8_t *a1, const uint8_t *sgd) {
	int d2, len;

	const int sgdCount = (READ_BE_UINT32(sgd) / 4) - 1;

	int tileNum = -1;
	int count = READ_BE_UINT16(a1); a1 += 2;
	--count;
	do {
		d2 = READ_BE_UINT16(a1); a1 += 2;
		int x_pos = (int16_t)READ_BE_UINT16(a1); a1 += 2;
		int y_pos = (int16_t)READ_BE_UINT16(a1); a1 += 2;
		if (d2 != 0xFFFF) {
			d2 &= ~0x8000;
			assert(d2 < sgdCount);
			const uint8_t *a4 = sgd;
			int d3 = (int32_t)READ_BE_UINT32(a4 + d2 * 4);
			if (d3 < 0) {
				d3 = -d3;
				a4 += d3;
				d3 = READ_BE_UINT16(a4); a4 += 2;
				if (tileNum != d2) {
					tileNum = d2;
					memcpy(d->sgdDecodeBuf, a4, d3);
					len = d3;
				}
			} else {
				a4 += d3;
				if (tileNum != d2) {
					tileNum = d2;
					len = decodeRLE(a4, d->sgdDecodeBuf);
				}
			}
			if (kFixLevel1Room26PlantYPos && d->level == 0 && d->room == 26 && d2 == 38) {
				y_pos += 8;
			}
			const uint8_t *a0 = d->sgdDecodeBuf;
			d2 = a0[0];
			++d2; // w
			d2 >>= 1;
			--d2;
			d3 = a0[1]; // h
			const uint8_t *src = a0 + 4;
			const int size = READ_BE_UINT16(a0 + 2);
			const uint8_t *mask = a0 + size + 4;
			assert(len == size * 5 + 4);
			decodeTileSGD(d->roomBitmap, kRoomW, x_pos, y_pos, d2, d3, src, mask, size);
		}
		--count;
	} while (count >= 0);
}

static void convertColor444(uint8_t *pal, int offset, int color) {
	const int b = (color & 0xF00) >> 8;
	const int g = (color & 0xF0)  >> 4;
	const int r =  color & 0xF;
	pal[offset * 3]     = (r << 4) | r;
	pal[offset * 3 + 1] = (g << 4) | g;
	pal[offset * 3 + 2] = (b << 4) | b;
}

static void dumpSGD(struct decodelev_t *d, const uint8_t *sgd) {
	int d3, d2, len;

	const int count = (READ_BE_UINT32(sgd) / 4) - 1; /* last offset is end of file */
	int tileNum = -1;

	for (int num = 0; num < count; ++num) {
		const uint8_t *a4 = sgd;
		d2 = num;
		d3 = READ_BE_UINT32(a4 + d2 * 4);
		if (d3 < 0) {
			d3 = -d3;
			a4 += d3;
			d3 = READ_BE_UINT16(a4); a4 += 2;
			if (tileNum != d2) {
				tileNum = d2;
				memcpy(d->sgdDecodeBuf, a4, d3);
				len = d3;
			}
		} else {
			a4 += d3;
			if (tileNum != d2) {
				tileNum = d2;
				len = decodeRLE(a4, d->sgdDecodeBuf);
			}
		}
		uint8_t *a0 = d->sgdDecodeBuf;
		d2 = a0[0];
		++d2; // w
		d2 >>= 1;
		--d2;
		d3 = a0[1]; // h
		const uint8_t *a5 = a0 + 4; // src
		const int size = READ_BE_UINT16(a0 + 2);
		const uint8_t *a2 = a0 + size + 4; // mask
		assert(len == size * 5 + 4);
		const int w = (d2 + 1) * 16;
		const int h =  d3 + 1;
		memset(d->roomBitmap, 0, w * h);
		decodeTileSGD(d->roomBitmap, w, 0, 0, d2, d3, a5, a2, size);

		char name[32];
		snprintf(name, sizeof(name), "sgd%03d.bmp", num);
		saveBMP(name, d->roomBitmap, w, h, d->roomPalette, 64);
	}
}

static void decodeLevRoom(struct decodelev_t *d, const char *name, const uint8_t *p, const uint8_t *mbk, const uint8_t *pal, const uint8_t *sgd) {
	d->sgd = false;
	d->roomOffset12 = READ_BE_UINT16(p + 12);
	if (p[1] == 0) {
		d->roomOffset10 = READ_BE_UINT16(p + 10);
	}
	int offset = READ_BE_UINT16(p + 14);
	memset(d->uncompressedMbkBuffer, 0, 8 * 4);
	int uncompressedMbkOffset = 32;
	bool end = false;
	do {
		int mbk_num = READ_BE_UINT16(p + offset); offset += 2;
		if (mbk_num & 0x8000) {
			mbk_num &= ~0x8000;
			end = true;
		}
		const uint8_t *a6;
		int len, size = (int16_t)READ_BE_UINT16(mbk + mbk_num * 6 + 4);
		if (size < 0) {
			size = -size;
			len = READ_BE_UINT32(mbk + mbk_num * 6);
			a6 = mbk + len;
		} else {
			len = READ_BE_UINT32(mbk + mbk_num * 6);
			const int ret = bytekiller_unpack(d->tmp, DECODE_BUFSIZE, mbk, len);
			assert(ret == 0);
			a6 = d->tmp;
		}
		const int count = p[offset++];
		if (count == 255) {
			size *= 32;
			memcpy(d->uncompressedMbkBuffer + uncompressedMbkOffset, a6, size);
			uncompressedMbkOffset += size;
		} else {
			for (int i = 0; i < count + 1; ++i) {
				const int num = p[offset++];
				memcpy(d->uncompressedMbkBuffer + uncompressedMbkOffset, a6 + num * 32, 32);
				uncompressedMbkOffset += 32;
			}
		}
	} while (!end);
	memset(d->roomBitmap, 0, kRoomW * kRoomH);
	if (p[1] != 0) {
		offset = READ_BE_UINT16(p + 10);
		loadSGD(d, p + offset, sgd);
		d->sgd = true;
	}
	uint8_t *a1 = d->sgdDecodeBuf + 6;
	memset(a1, 0xFF, kRoomH * 8 * 8);
	decodeLevRoomHelper(d, p);
	const uint8_t *palettes = p + 2;
	for (int j = 0; j < 4; ++j) {
		const int num = READ_BE_UINT16(palettes + j * 2);
		for (int i = 0; i < 16; ++i) {
			const uint16_t color = READ_BE_UINT16(pal + num * 32 + i * 2);
			convertColor444(d->roomPalette, j * 16 + i, color);
		}
	}
	if (kDrawPalettes) {
		for (int j = 0; j < 4; ++j) {
			for (int i = 0; i < 16; ++i) {
				fillRect(d->roomBitmap, i * 8, j * 6, 8, 4, j * 16 + i);
			}
		}
	}
	char filename[64];
	snprintf(filename, sizeof(filename), "%s_room%02d.bmp", name, d->room);
	saveBMP(filename, d->roomBitmap, kRoomW, kRoomH, d->roomPalette, 64);

	if (kDumpSGD && sgd) {
		dumpSGD(d, sgd);
	}
}

static void decodeLevRooms(struct decodelev_t *d, const char *name, const uint8_t *lev, const uint8_t *mbk, const uint8_t *pal, const uint8_t *sgd) {
	uint32_t offsets[64];
	for (int i = 0; i < 64; ++i) {
		offsets[i] = READ_BE_UINT32(lev + 4 * i);
	}
	uint32_t offset_prev = 64 * 4;
	for (int i = 0; i < 64; ++i) {
		if (offset_prev != 0) {
			const int size = offsets[i] - offset_prev;
			if (size != 0) {
				assert(size < 4096);
				const int ret = bytekiller_unpack(d->decodeLevBuf, 4096, lev, offsets[i]);
				assert(ret == 0);
				d->room = i;
				decodeLevRoom(d, name, d->decodeLevBuf, mbk, pal, sgd);
			}
		}
		offset_prev = offsets[i];
	}
}

void decodeLEV(const char *name, const uint8_t *lev, const uint8_t *mbk, const uint8_t *pal, const uint8_t *sgd) {
	struct decodelev_t *d = (struct decodelev_t *)calloc(1, sizeof(struct decodelev_t));
	if (d) {
		for (int i = 0; kNames[i]; ++i) {
			const int len = strlen(kNames[i]);
			if (strncasecmp(name, kNames[i], len) == 0) {
				d->level = i;
				decodeLevRooms(d, kNames[i], lev, mbk, pal, sgd);
				break;
			}
		}
		free(d);
	}
}
