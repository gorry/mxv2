// mxv2 - ビットマップとブリッタ
//
// 旧 mxv/draw.cpp の BMPFill / BMPCopy / BMPCopyTransparent / BMPCopyComposite
// を移植したもの。合成テーブルの中身と画素の並びは原典と同じ。

#include "bitmap.h"

#include <cstring>

namespace mxv2 {

namespace {

// ---------------------------------------------------------------------------
// 合成テーブル
//
// 原典 (InitAlphaTable) と同じ内容。ただし配列サイズは原典が境界を 1 バイト
// 超えて読んでいたので、正しく収まる大きさに直してある。
// ---------------------------------------------------------------------------

// Alpha[i*511 + j] (i=0..100, j=-255..255): j*i/100
uint8_t g_alphaStorage[101 * 511 + 512];
uint8_t *g_alpha = 0;

// AlphaMul[i*256 + j] (i=0..255, j=0..255)
//   i<=100 : j*i/100            (暗くする)
//   i> 100 : j+(255-j)*(i-100)/100 (明るくする、255 で頭打ち)
// 原典は i を 0..200 しか埋めておらず、素材のパレット値が 200 を超えると
// 範囲外を読んでいた。ここでは同じ式を 255 まで延長して埋める。
uint8_t g_alphaMul[256 * 256];

// Clip[i] (i=-256..511): 0..255 に飽和
uint8_t g_clipStorage[256 * 3];
uint8_t *g_clip = 0;

bool g_tablesReady = false;

inline int ClampByte(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

// 1 チャンネル分の合成。mode は 0..100 か kBlend*。
inline uint8_t BlendChannel(int mode, int d, int s) {
	if (mode == 100) return (uint8_t)s;
	if (mode < 100) return (uint8_t)(d - g_alpha[mode * 511 + (d - s)]);
	if (mode == kBlendSub) return g_clip[d - s];
	if (mode == kBlendAdd) return g_clip[d + s];
	return g_alphaMul[s * 256 + d];
}

// コピー系の座標クリップ。原典 BMPCopy と同じ手順。
// (原典には ydst<0 のとき ysrc から xdst を引く書き間違いがあったので直した。)
struct ClipRect {
	int xdst, ydst, xsrc, ysrc, width, height;
	bool empty() const { return width <= 0 || height <= 0; }
};

bool ClipCopy(const Bitmap *dst, int xdst, int ydst, int width, int height,
              const Bitmap *src, int xsrc, int ysrc, ClipRect *out) {
	if (xdst < 0) {
		width += xdst;
		xsrc -= xdst;
		xdst = 0;
	}
	if (ydst < 0) {
		height += ydst;
		ysrc -= ydst;
		ydst = 0;
	}
	if (xsrc < 0) {
		width += xsrc;
		xdst -= xsrc;
		xsrc = 0;
	}
	if (ysrc < 0) {
		height += ysrc;
		ydst -= ysrc;
		ysrc = 0;
	}
	if (width > dst->width() - xdst) width = dst->width() - xdst;
	if (width > src->width() - xsrc) width = src->width() - xsrc;
	if (height > dst->height() - ydst) height = dst->height() - ydst;
	if (height > src->height() - ysrc) height = src->height() - ysrc;

	out->xdst = xdst;
	out->ydst = ydst;
	out->xsrc = xsrc;
	out->ysrc = ysrc;
	out->width = width;
	out->height = height;
	return !out->empty();
}

}  // namespace

// ---------------------------------------------------------------------------
// Bitmap
// ---------------------------------------------------------------------------

Bitmap::Bitmap() : width_(0), height_(0), bitCount_(0), stride_(0) {
	memset(palette_, 0, sizeof(palette_));
}

bool Bitmap::Create(int width, int height, int bitCount) {
	Destroy();
	if (width <= 0 || height <= 0) return false;
	if (bitCount != 8 && bitCount != 24) return false;

	width_ = width;
	height_ = height;
	bitCount_ = bitCount;
	stride_ = LineWidth(width * (bitCount / 8));
	bits_.assign((size_t)stride_ * height, 0);
	memset(palette_, 0, sizeof(palette_));
	return true;
}

void Bitmap::Destroy() {
	width_ = 0;
	height_ = 0;
	bitCount_ = 0;
	stride_ = 0;
	bits_.clear();
}

void Bitmap::SetPalette(int index, int r, int g, int b) {
	if (index < 0 || index > 255) return;
	palette_[index] = MakeRgb(r, g, b);
}

// ---------------------------------------------------------------------------
// 合成テーブル初期化
// ---------------------------------------------------------------------------

void InitBlendTables() {
	if (g_tablesReady) return;
	g_tablesReady = true;

	g_alpha = &g_alphaStorage[256];
	for (int i = 0; i <= 100; i++) {
		for (int j = -255; j <= 255; j++) {
			g_alpha[i * 511 + j] = (uint8_t)(j * i / 100);
		}
	}

	for (int i = 0; i <= 100; i++) {
		for (int j = 0; j < 256; j++) {
			g_alphaMul[i * 256 + j] = (uint8_t)(i * j / 100);
		}
	}
	for (int i = 101; i < 256; i++) {
		for (int j = 0; j < 256; j++) {
			g_alphaMul[i * 256 + j] = (uint8_t)ClampByte(j + (255 - j) * (i - 100) / 100);
		}
	}

	g_clip = &g_clipStorage[256];
	for (int i = -256; i < 256 * 2; i++) {
		g_clip[i] = (uint8_t)ClampByte(i);
	}
}

// ---------------------------------------------------------------------------
// 矩形塗り潰し
// ---------------------------------------------------------------------------

void BmpFill(Bitmap *dst, int xdst, int ydst, int width, int height,
             int r, int g, int b, int alpha) {
	if (dst == 0 || !dst->valid()) return;

	if (xdst < 0) {
		width += xdst;
		xdst = 0;
	}
	if (ydst < 0) {
		height += ydst;
		ydst = 0;
	}
	if (width > dst->width() - xdst) width = dst->width() - xdst;
	if (height > dst->height() - ydst) height = dst->height() - ydst;
	if (width <= 0 || height <= 0) return;

	if (dst->bitCount() == 8) {
		// 原典と同じく 8bpp はパレット番号 r をそのまま敷く（合成はしない）。
		for (int y = 0; y < height; y++) {
			memset(dst->RowFromTop(ydst + y) + xdst, (uint8_t)r, (size_t)width);
		}
		return;
	}

	if (alpha == 100) {
		for (int y = 0; y < height; y++) {
			uint8_t *q = dst->RowFromTop(ydst + y) + (size_t)xdst * 3;
			for (int x = 0; x < width; x++) {
				*(q++) = (uint8_t)b;
				*(q++) = (uint8_t)g;
				*(q++) = (uint8_t)r;
			}
		}
		return;
	}

	for (int y = 0; y < height; y++) {
		uint8_t *q = dst->RowFromTop(ydst + y) + (size_t)xdst * 3;
		for (int x = 0; x < width; x++) {
			q[0] = BlendChannel(alpha, q[0], b);
			q[1] = BlendChannel(alpha, q[1], g);
			q[2] = BlendChannel(alpha, q[2], r);
			q += 3;
		}
	}
}

// ---------------------------------------------------------------------------
// カバレッジ合成（アンチエイリアス文字）
// ---------------------------------------------------------------------------

void BmpBlendMask(Bitmap *dst, int xdst, int ydst, int width, int height,
                  const Bitmap *mask, int xsrc, int ysrc, const Rgb &color, int bright) {
	if (dst == 0 || mask == 0 || !dst->valid() || !mask->valid()) return;
	if (dst->bitCount() != 24 || mask->bitCount() != 8) return;

	ClipRect c;
	if (!ClipCopy(dst, xdst, ydst, width, height, mask, xsrc, ysrc, &c)) return;

	if (bright < 0) bright = 0;
	if (bright > 100) bright = 100;

	for (int y = 0; y < c.height; y++) {
		uint8_t *q = dst->RowFromTop(c.ydst + y) + (size_t)c.xdst * 3;
		const uint8_t *p = mask->RowFromTop(c.ysrc + y) + c.xsrc;
		for (int x = 0; x < c.width; x++) {
			const int a = *(p++) * bright / 100;
			if (a > 0) {
				if (a >= 255) {
					q[0] = color.b;
					q[1] = color.g;
					q[2] = color.r;
				} else {
					q[0] = (uint8_t)(q[0] + (color.b - q[0]) * a / 255);
					q[1] = (uint8_t)(q[1] + (color.g - q[1]) * a / 255);
					q[2] = (uint8_t)(q[2] + (color.r - q[2]) * a / 255);
				}
			}
			q += 3;
		}
	}
}

// ---------------------------------------------------------------------------
// 矩形コピー
// ---------------------------------------------------------------------------

void BmpCopy(Bitmap *dst, int xdst, int ydst, int width, int height,
             const Bitmap *src, int xsrc, int ysrc, int alpha) {
	if (dst == 0 || src == 0 || !dst->valid() || !src->valid()) return;

	ClipRect c;
	if (!ClipCopy(dst, xdst, ydst, width, height, src, xsrc, ysrc, &c)) return;

	// 8bpp 同士はパレット番号をそのまま転送する（原典と同じ）。
	if (dst->bitCount() == 8) {
		if (src->bitCount() != 8) return;
		for (int y = 0; y < c.height; y++) {
			memcpy(dst->RowFromTop(c.ydst + y) + c.xdst,
			       src->RowFromTop(c.ysrc + y) + c.xsrc, (size_t)c.width);
		}
		return;
	}

	if (src->bitCount() == 24) {
		if (alpha == 100) {
			for (int y = 0; y < c.height; y++) {
				memcpy(dst->RowFromTop(c.ydst + y) + (size_t)c.xdst * 3,
				       src->RowFromTop(c.ysrc + y) + (size_t)c.xsrc * 3,
				       (size_t)c.width * 3);
			}
			return;
		}
		for (int y = 0; y < c.height; y++) {
			uint8_t *q = dst->RowFromTop(c.ydst + y) + (size_t)c.xdst * 3;
			const uint8_t *p = src->RowFromTop(c.ysrc + y) + (size_t)c.xsrc * 3;
			for (int x = 0; x < c.width; x++) {
				q[0] = BlendChannel(alpha, q[0], p[0]);
				q[1] = BlendChannel(alpha, q[1], p[1]);
				q[2] = BlendChannel(alpha, q[2], p[2]);
				q += 3;
				p += 3;
			}
		}
		return;
	}

	// 8bpp -> 24bpp
	const Rgb *pal = src->palette();
	for (int y = 0; y < c.height; y++) {
		uint8_t *q = dst->RowFromTop(c.ydst + y) + (size_t)c.xdst * 3;
		const uint8_t *p = src->RowFromTop(c.ysrc + y) + c.xsrc;
		if (alpha == 100) {
			for (int x = 0; x < c.width; x++) {
				const Rgb &col = pal[*(p++)];
				*(q++) = col.b;
				*(q++) = col.g;
				*(q++) = col.r;
			}
		} else {
			for (int x = 0; x < c.width; x++) {
				const Rgb &col = pal[*(p++)];
				q[0] = BlendChannel(alpha, q[0], col.b);
				q[1] = BlendChannel(alpha, q[1], col.g);
				q[2] = BlendChannel(alpha, q[2], col.r);
				q += 3;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// 透過矩形コピー (パレット 0 が透明)
// ---------------------------------------------------------------------------

void BmpCopyTransparent(Bitmap *dst, int xdst, int ydst, int width, int height,
                        const Bitmap *src, int xsrc, int ysrc, int alpha) {
	if (dst == 0 || src == 0 || !dst->valid() || !src->valid()) return;

	ClipRect c;
	if (!ClipCopy(dst, xdst, ydst, width, height, src, xsrc, ysrc, &c)) return;

	if (dst->bitCount() == 8) {
		if (src->bitCount() != 8) return;
		for (int y = 0; y < c.height; y++) {
			uint8_t *q = dst->RowFromTop(c.ydst + y) + c.xdst;
			const uint8_t *p = src->RowFromTop(c.ysrc + y) + c.xsrc;
			for (int x = 0; x < c.width; x++) {
				uint8_t v = *(p++);
				if (v) *q = v;
				q++;
			}
		}
		return;
	}
	if (src->bitCount() != 8) return;

	const Rgb *pal = src->palette();
	for (int y = 0; y < c.height; y++) {
		uint8_t *q = dst->RowFromTop(c.ydst + y) + (size_t)c.xdst * 3;
		const uint8_t *p = src->RowFromTop(c.ysrc + y) + c.xsrc;
		for (int x = 0; x < c.width; x++) {
			uint8_t v = *(p++);
			if (v) {
				const Rgb &col = pal[v];
				if (alpha == 100) {
					q[0] = col.b;
					q[1] = col.g;
					q[2] = col.r;
				} else {
					q[0] = BlendChannel(alpha, q[0], col.b);
					q[1] = BlendChannel(alpha, q[1], col.g);
					q[2] = BlendChannel(alpha, q[2], col.r);
				}
			}
			q += 3;
		}
	}
}

// ---------------------------------------------------------------------------
// 合成矩形コピー
//   src1 (8bpp) のパレット 0 の画素は src2 (24bpp) をそのまま通す。
//   それ以外は src2 を下地として src1 を alpha 合成する。
// ---------------------------------------------------------------------------

void BmpCopyComposite(Bitmap *dst, int xdst, int ydst, int width, int height,
                      const Bitmap *src1, int xsrc1, int ysrc1,
                      const Bitmap *src2, int xsrc2, int ysrc2, int alpha) {
	if (dst == 0 || src1 == 0 || src2 == 0) return;
	if (!dst->valid() || !src1->valid() || !src2->valid()) return;
	if (dst->bitCount() != 24 || src1->bitCount() != 8 || src2->bitCount() != 24) return;

	// 3 枚あるので原典と同じ手順で順番にクリップする。
	if (xdst < 0) {
		width += xdst;
		xsrc1 -= xdst;
		xsrc2 -= xdst;
		xdst = 0;
	}
	if (ydst < 0) {
		height += ydst;
		ysrc1 -= ydst;
		ysrc2 -= ydst;
		ydst = 0;
	}
	if (xsrc1 < 0) {
		width += xsrc1;
		xdst -= xsrc1;
		xsrc2 -= xsrc1;
		xsrc1 = 0;
	}
	if (ysrc1 < 0) {
		height += ysrc1;
		ydst -= ysrc1;
		ysrc2 -= ysrc1;
		ysrc1 = 0;
	}
	if (xsrc2 < 0) {
		width += xsrc2;
		xdst -= xsrc2;
		xsrc1 -= xsrc2;
		xsrc2 = 0;
	}
	if (ysrc2 < 0) {
		height += ysrc2;
		ydst -= ysrc2;
		ysrc1 -= ysrc2;
		ysrc2 = 0;
	}
	if (width > dst->width() - xdst) width = dst->width() - xdst;
	if (width > src1->width() - xsrc1) width = src1->width() - xsrc1;
	if (width > src2->width() - xsrc2) width = src2->width() - xsrc2;
	if (height > dst->height() - ydst) height = dst->height() - ydst;
	if (height > src1->height() - ysrc1) height = src1->height() - ysrc1;
	if (height > src2->height() - ysrc2) height = src2->height() - ysrc2;
	if (width <= 0 || height <= 0) return;

	const Rgb *pal = src1->palette();
	for (int y = 0; y < height; y++) {
		uint8_t *q = dst->RowFromTop(ydst + y) + (size_t)xdst * 3;
		const uint8_t *p = src1->RowFromTop(ysrc1 + y) + xsrc1;
		const uint8_t *r = src2->RowFromTop(ysrc2 + y) + (size_t)xsrc2 * 3;
		for (int x = 0; x < width; x++) {
			uint8_t v = *(p++);
			if (v) {
				const Rgb &col = pal[v];
				if (alpha == 100) {
					q[0] = col.b;
					q[1] = col.g;
					q[2] = col.r;
				} else {
					q[0] = BlendChannel(alpha, r[0], col.b);
					q[1] = BlendChannel(alpha, r[1], col.g);
					q[2] = BlendChannel(alpha, r[2], col.r);
				}
			} else {
				q[0] = r[0];
				q[1] = r[1];
				q[2] = r[2];
			}
			q += 3;
			r += 3;
		}
	}
}

}  // namespace mxv2
