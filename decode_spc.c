
#include "bitmap.h"
#include "unpack.h"

static const uint8_t kSprHeader[] = { 0x53, 0x50, 0x54, 0x00, 0x05, 0x07, 0x00, 0x02, 0x00, 0x20, 0x00, 0x18 };

static const uint8_t kPalettePerso[16 * 3] = {
	0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0x44, 0x22, 0x00, 0x44, 0x44, 0xaa,
	0xcc, 0x66, 0x66, 0x88, 0x88, 0x88, 0x66, 0x66, 0xcc, 0x66, 0x44, 0x22,
	0xee, 0x00, 0x44, 0x88, 0x00, 0x44, 0x44, 0x44, 0x44, 0x00, 0xee, 0x00,
	0x00, 0x66, 0x00, 0xee, 0xee, 0x00, 0xaa, 0x22, 0xee, 0x00, 0x00, 0x00
};

static const uint8_t kPaletteJunky[] = {
	0x00, 0x00, 0x00, 0xaa, 0x66, 0x44, 0x66, 0x44, 0x44, 0x66, 0x22, 0x22,
	0xee, 0x44, 0x00, 0x44, 0x00, 0x22, 0x66, 0x00, 0x44, 0x88, 0x88, 0xaa,
	0x44, 0x00, 0x00, 0x44, 0x44, 0x66, 0x22, 0x22, 0x44, 0x00, 0x00, 0x22,
	0xbb, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t kPaletteMercenaire[] = {
	0x00, 0x00, 0x00, 0xcc, 0x88, 0x66, 0x88, 0x66, 0x66, 0x88, 0x44, 0x44,
	0x22, 0x22, 0x66, 0x44, 0x44, 0xaa, 0x66, 0x66, 0xcc, 0xaa, 0xaa, 0xaa,
	0x00, 0xee, 0x00, 0x00, 0x88, 0x00, 0x88, 0x66, 0x88, 0x22, 0x00, 0x22,
	0x22, 0x22, 0x22, 0x66, 0x44, 0x66, 0x44, 0x22, 0x44, 0xff, 0x00, 0xff
};

static const uint8_t kPaletteReplicant[] = {
	0x00, 0x00, 0x00, 0x44, 0x22, 0x66, 0x66, 0x44, 0x88, 0x88, 0x66, 0xaa,
	0xcc, 0x66, 0x66, 0x66, 0x66, 0x66, 0x88, 0x88, 0x88, 0xaa, 0xaa, 0xaa,
	0x88, 0x44, 0x44, 0xff, 0xaa, 0x88, 0xff, 0xee, 0x00, 0x00, 0x88, 0x00,
	0x00, 0xdd, 0x00, 0x00, 0xcc, 0xcc, 0x00, 0x66, 0xff, 0x00, 0x00, 0xaa
};

static const uint8_t kPaletteGlue[] = {
	0x00, 0x00, 0x00, 0x22, 0x22, 0x88, 0x00, 0x44, 0x88, 0x00, 0x44, 0xcc,
	0x00, 0x66, 0xcc, 0xaa, 0x00, 0x00, 0x22, 0x22, 0x66, 0x66, 0x00, 0x00,
	0x88, 0x00, 0x00, 0xcc, 0x00, 0x00, 0x44, 0x00, 0x00, 0xee, 0xee, 0xee,
	0xcc, 0x00, 0xcc, 0x00, 0x44, 0x00, 0x00, 0x22, 0x00, 0x00, 0x66, 0x00
};

static const struct monster_t {
	uint16_t start, end;
	const char *name;
	const uint8_t *palette;
} kMonsters[] = {
	{ 0x22F, 0x28D, "junky", kPaletteJunky },
	{ 0x2EA, 0x385, "mercenaire", kPaletteMercenaire },
	{ 0x387, 0x42F, "replicant", kPaletteReplicant },
	{ 0x430, 0x4E8, "glue", kPaletteGlue },
	{ 0, 0, 0, 0 }
};

static const int kMbkCount = 84;
static const int kSprCount = 1287;

static uint8_t _buffer[0xFFFFF];
static uint8_t _bitmap[256 * 256];

static int decodeMBK(const uint8_t *mbk, int i) {
	uint32_t mbk_offset = READ_BE_UINT32(mbk + i * 6);
	uint16_t count = READ_BE_UINT16(mbk + i * 6 + 4);
	assert((count & 0x8000) == 0);
	uint32_t uncompressed = READ_BE_UINT32(mbk + mbk_offset - 4);
	// fprintf(stdout, "mbk:%d offset 0x%x size %d %d uncompressed %d\n", i, mbk_offset, count, count * 32, uncompressed);
	assert(uncompressed == 32 * count);
	int ret = bytekiller_unpack(_buffer, sizeof(_buffer), mbk, mbk_offset);
	assert(ret == 0);
	return count;
}

static void decodeTile8x8(const uint8_t *src, int x, int y, uint8_t *dst, int dstPitch) {
	dst += y * dstPitch + x;
	for (int j = 0; j < 8; ++j) {
		for (int i = 0; i < 4; ++i) {
			const uint8_t color = *src++;
			dst[i * 2]     = color >> 4;
			dst[i * 2 + 1] = color & 15;
		}
		dst += dstPitch;
	}
}

static void decodeSpcHelper(const uint8_t *src, int W, int H, uint8_t *bitmap, int dstPitch) {
	for (int x = 0; x < W; x += 8) {
		for (int y = 0; y < H; y += 8) {
			decodeTile8x8(src, x, y, bitmap, dstPitch);
			src += 8 * 8 / 2;
		}
	}
}

void decodeSPC(const char *name, const uint8_t *spc, const uint8_t *mbk) {
	const int count = READ_BE_UINT16(spc) / 2;
	uint32_t prev_offset = READ_BE_UINT16(spc);
	uint32_t next_offset = 0;
	for (int i = 0; i < count; ++i) {
		const uint16_t offset = READ_BE_UINT16(spc + i * 2);
		assert(offset == prev_offset || offset == next_offset);
		const uint8_t *p = spc + offset;
		const uint8_t rp_num = p[0];
		assert(rp_num < 0x4A);
		//const int8_t offs_x = (int8_t)p[1];
		//const int8_t offs_y = (int8_t)p[2];
		const int sz = p[5];
		p += 6;
		prev_offset = offset;
		next_offset = offset + 6 + sz * 4;
	}
	uint8_t palette[16 * 3];
	for (int i = 0; i < 16; ++i) {
		palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = (i << 4) | i;
	}
	for (int i = 0; i < kMbkCount; ++i) {
		const int count = decodeMBK(mbk, i);
		decodeSpcHelper(_buffer, count * 8, 8, _bitmap, count * 8);

		char filename[64];
		snprintf(filename, sizeof(filename), "mbk%03d.bmp", i);
		saveBMP(filename, _bitmap, count * 8, 8, palette, 16);
	}
}

void decodeRP(const char *name, const uint8_t *rp, const uint8_t *spc, const uint8_t *mbk) {
	const int count = READ_BE_UINT16(spc) / 2;
	for (int i = 0; i < count; ++i) {
		const uint16_t offset = READ_BE_UINT16(spc + i * 2);
		const uint8_t *p = spc + offset;
		const uint8_t rp_num = p[0];
		assert(rp_num < 0x4A);
		const int mbk_num = rp[rp_num];
		//const int8_t offs_x = (int8_t)p[1];
		//const int8_t offs_y = (int8_t)p[2];
		const int sz = p[5];
		p += 6;
		//uint32_t mbk_offset = READ_BE_UINT32(mbk + mbk_num * 6);
		uint16_t count = READ_BE_UINT16(mbk + mbk_num * 6 + 4);
		//uint32_t uncompressed = READ_BE_UINT32(mbk + mbk_offset - 4);
		decodeMBK(mbk, mbk_num);
		memset(_bitmap, 0, sizeof(_bitmap));
		for (int j = 0; j < sz; ++j, p += 4) {
			int tile_num = p[0];
			if (tile_num >= count) {
				continue;
			}
			int sprite_x = p[1];
			int sprite_y = p[2];
			uint8_t sprite_flags = p[3];
			uint8_t sprite_h = (((sprite_flags >> 0) & 3) + 1) * 8;
			uint8_t sprite_w = (((sprite_flags >> 2) & 3) + 1) * 8;
			decodeSpcHelper(_buffer + tile_num * 32, sprite_w, sprite_h, _bitmap + sprite_y * 256 + sprite_x, 256);
		}
	}
}

static void decodeSprHelper(const uint8_t *src, const char *name, const uint8_t *palette) {
        static const int W = 32;
        static const int H = 24;
        uint8_t *bitmap = (uint8_t *)malloc(W * H * 2);
        if (bitmap) {
		for (int part = 0; part < 2; ++part) { /* top, bottom */
			for (int x = 0; x < W; x += 8) {
				for (int y = 0; y < H; y += 8) {
					decodeTile8x8(src, x, y, bitmap + part * W * H, W);
					src += 8 * 8 / 2;
				}
			}
		}
		saveBMP(name, bitmap, W, H * 2, palette, 16);
		free(bitmap);
	}
}

void decodeSPR(const char *name, const uint8_t *spr, const uint8_t *tab) {
	assert(memcmp(spr, kSprHeader, sizeof(kSprHeader)) == 0);
	int current_monster = 0;
	for (int i = 0; i < kSprCount; ++i) {
		const uint32_t offset = READ_BE_UINT32(tab + i * 4) + sizeof(kSprHeader);
		const uint8_t *p = spr + offset;
		// const int8_t dx = p[0]; // horizontal position
		// const int8_t dy = p[1]; // vertical position
		uint16_t len = READ_BE_UINT16(p + 2) + 1;
		p += 4;
		memset(_buffer, 0, sizeof(_buffer));
		int uncompressed = 0;
		for (int j = 0; j < len; ++j) {
			if ((p[j] & 0xF0) == 0xF0) {
				const uint8_t color = p[j] & 15;
				++j;
				const int count = p[j] + 1;
				memset(_buffer + uncompressed, (color << 4) | color, count);
				uncompressed += count;
			} else {
				assert((p[j] & 15) != 15);
				_buffer[uncompressed] = p[j];
				++uncompressed;
			}
		}
		const char *name = "perso";
		const uint8_t *palette = kPalettePerso;
		if (i >= kMonsters[current_monster].start && i <= kMonsters[current_monster].end) {
			palette = kMonsters[current_monster].palette;
			name = kMonsters[current_monster].name;
			if (i == kMonsters[current_monster].end) {
				++current_monster;
			}
		}
		// fprintf(stdout, "spr %d offset 0x%x hdr:%d,%d len %d uncompressed %d\n", i, offset, dx, dy, len, uncompressed);
		char filename[64];
		snprintf(filename, sizeof(filename), "spr%04d_%s.bmp", i, name);
		decodeSprHelper(_buffer, filename, palette);
	}
}
