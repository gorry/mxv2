// mxv2 - Windows BMP ファイルの読み込み

#include "bmpfile.h"

#include <cstring>
#include <vector>

#include "fileutil.h"
#include "message.h"

namespace mxv2 {

namespace {

uint16_t ReadU16(const uint8_t *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

int32_t ReadS32(const uint8_t *p) {
	return (int32_t)ReadU32(p);
}

}  // namespace

bool LoadBmpFile(const std::string &path, Bitmap *out, std::string *err) {
	std::vector<uint8_t> file;
	if (!ReadWholeFile(path, &file)) {
		*err = MsgF("Error.BmpRead", path);
		return false;
	}
	if (file.size() < 14 + 40) {
		*err = MsgF("Error.BmpShort", path);
		return false;
	}
	if (file[0] != 'B' || file[1] != 'M') {
		*err = MsgF("Error.BmpNotBmp", path);
		return false;
	}

	const uint32_t dataOffset = ReadU32(&file[10]);
	const uint8_t *ih = &file[14];
	const uint32_t headerSize = ReadU32(ih);
	if (headerSize < 40) {
		*err = MsgF("Error.BmpHeader", path);
		return false;
	}

	const int32_t width = ReadS32(ih + 4);
	const int32_t rawHeight = ReadS32(ih + 8);
	const int bpp = ReadU16(ih + 14);
	const uint32_t compression = ReadU32(ih + 16);
	uint32_t clrUsed = ReadU32(ih + 32);

	const bool topDown = (rawHeight < 0);
	const int32_t height = topDown ? -rawHeight : rawHeight;

	if (width <= 0 || height <= 0) {
		*err = MsgF("Error.BmpSize", path);
		return false;
	}
	if (compression != 0) {
		*err = MsgF("Error.BmpCompressed", path);
		return false;
	}
	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
		*err = MsgF("Error.BmpDepth", path);
		return false;
	}

	const bool paletted = (bpp <= 8);
	if (paletted && clrUsed == 0) clrUsed = 1u << bpp;

	// パレット
	Rgb palette[256];
	memset(palette, 0, sizeof(palette));
	if (paletted) {
		const size_t palOffset = 14 + headerSize;
		if (palOffset + (size_t)clrUsed * 4 > file.size()) {
			*err = MsgF("Error.BmpPalette", path);
			return false;
		}
		const uint8_t *p = &file[palOffset];
		for (uint32_t i = 0; i < clrUsed && i < 256; i++) {
			palette[i].b = p[i * 4 + 0];
			palette[i].g = p[i * 4 + 1];
			palette[i].r = p[i * 4 + 2];
			palette[i].x = 0;
		}
	}

	const int srcStride = LineWidth(width * bpp / 8 + ((width * bpp % 8) ? 1 : 0));
	if ((size_t)dataOffset + (size_t)srcStride * height > file.size()) {
		*err = MsgF("Error.BmpPixels", path);
		return false;
	}
	const uint8_t *data = &file[dataOffset];

	if (!out->Create(width, height, paletted ? 8 : 24)) {
		*err = MsgF("Error.BmpAlloc", path);
		return false;
	}
	if (paletted) memcpy(out->palette(), palette, sizeof(palette));

	for (int y = 0; y < height; y++) {
		// BMP はボトムアップ (負の高さならトップダウン)。
		const int srcRow = topDown ? y : (height - 1 - y);
		const uint8_t *p = data + (size_t)srcRow * srcStride;
		uint8_t *q = out->RowFromTop(y);

		switch (bpp) {
			case 1:
				for (int x = 0; x < width; x++) {
					q[x] = (uint8_t)((p[x >> 3] >> (7 - (x & 7))) & 1);
				}
				break;
			case 4:
				for (int x = 0; x < width; x++) {
					q[x] = (uint8_t)((x & 1) ? (p[x >> 1] & 0x0f) : (p[x >> 1] >> 4));
				}
				break;
			case 8:
				memcpy(q, p, (size_t)width);
				break;
			case 16:
				// 既定は X1R5G5B5
				for (int x = 0; x < width; x++) {
					uint16_t cc = ReadU16(p + x * 2);
					int b5 = cc & 0x1f;
					int g5 = (cc >> 5) & 0x1f;
					int r5 = (cc >> 10) & 0x1f;
					q[x * 3 + 0] = (uint8_t)((b5 << 3) | (b5 >> 2));
					q[x * 3 + 1] = (uint8_t)((g5 << 3) | (g5 >> 2));
					q[x * 3 + 2] = (uint8_t)((r5 << 3) | (r5 >> 2));
				}
				break;
			case 24:
				memcpy(q, p, (size_t)width * 3);
				break;
			case 32:
				for (int x = 0; x < width; x++) {
					q[x * 3 + 0] = p[x * 4 + 0];
					q[x * 3 + 1] = p[x * 4 + 1];
					q[x * 3 + 2] = p[x * 4 + 2];
				}
				break;
		}
	}

	return true;
}

}  // namespace mxv2
