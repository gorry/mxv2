// mxv2 - ビットマップとブリッタ
//
// 旧 mxv/draw.cpp は Windows の BITMAPINFO (DIB) を直接叩いていた。ここでは
// その**メモリレイアウトをそのまま**プラットフォーム非依存の Bitmap として
// 再実装する。具体的には
//   - 行は下から上へ並ぶ (ボトムアップ DIB)
//   - 行の長さは 4 バイト境界に切り上げ (linewidth)
//   - 8bpp はパレット付き、24bpp は B,G,R の順
// を維持する。こうすることで draw.cpp のブリッタ本体をほぼそのまま移植でき、
// 見た目の互換性を保てる。
//
// 対応する色深度は 8 と 24 のみ。BMP ファイルの 1/4/16/32bpp は
// bmpfile.cpp が読み込み時に 8 か 24 へ展開する。

#ifndef MXV2_BITMAP_H
#define MXV2_BITMAP_H

#include <cstdint>
#include <vector>

namespace mxv2 {

// アルファ指定の特殊値。旧 mxv の BMPCOPY_ALPHA_* と同じ値。
enum BlendMode {
	kBlendSub = 101,
	kBlendAdd = 102,
	kBlendMul = 103,
};

// RGBQUAD 相当。DIB と同じ B,G,R,X の並び。
struct Rgb {
	uint8_t b;
	uint8_t g;
	uint8_t r;
	uint8_t x;
};

inline Rgb MakeRgb(int r, int g, int b) {
	Rgb c;
	c.b = (uint8_t)b;
	c.g = (uint8_t)g;
	c.r = (uint8_t)r;
	c.x = 0;
	return c;
}

// 4 バイト境界への切り上げ。旧 mxv の linewidth() マクロ。
inline int LineWidth(int bytes) {
	return (bytes + 3) & (-4);
}

class Bitmap {
public:
	Bitmap();

	// bitCount は 8 か 24。既存の内容は破棄される。
	bool Create(int width, int height, int bitCount);
	void Destroy();

	bool valid() const { return !bits_.empty(); }
	int width() const { return width_; }
	int height() const { return height_; }
	int bitCount() const { return bitCount_; }
	int stride() const { return stride_; }

	uint8_t *bits() { return bits_.empty() ? 0 : &bits_[0]; }
	const uint8_t *bits() const { return bits_.empty() ? 0 : &bits_[0]; }

	// 8bpp のときだけ意味を持つ。常に 256 要素。
	Rgb *palette() { return &palette_[0]; }
	const Rgb *palette() const { return &palette_[0]; }
	void SetPalette(int index, int r, int g, int b);

	// ボトムアップなので、論理的な y 行目の先頭はここ。
	uint8_t *RowFromTop(int y) { return bits() + (size_t)(height_ - 1 - y) * stride_; }
	const uint8_t *RowFromTop(int y) const {
		return bits() + (size_t)(height_ - 1 - y) * stride_;
	}

private:
	int width_;
	int height_;
	int bitCount_;
	int stride_;
	std::vector<uint8_t> bits_;
	Rgb palette_[256];
};

// アルファ合成テーブルの初期化。ブリッタを使う前に一度呼ぶ。
void InitBlendTables();

// 矩形塗り潰し。alpha は 0..100 または kBlend*。
void BmpFill(Bitmap *dst, int xdst, int ydst, int width, int height,
             int r, int g, int b, int alpha);

// 矩形コピー。
void BmpCopy(Bitmap *dst, int xdst, int ydst, int width, int height,
             const Bitmap *src, int xsrc, int ysrc, int alpha);

// パレット 0 を透明として扱う矩形コピー。
void BmpCopyTransparent(Bitmap *dst, int xdst, int ydst, int width, int height,
                        const Bitmap *src, int xsrc, int ysrc, int alpha);

// mask の画素値をカバレッジ (0=透明 .. 255=不透明) とみなして、単色 color を
// dst へ合成する。アンチエイリアスの効いた文字を置くために使う。
// bright は旧 mxv の Bright 値 (0..100 が濃さ)。100 を超える指定は
// 100 として扱う (文字色の Bright は既定 100)。
void BmpBlendMask(Bitmap *dst, int xdst, int ydst, int width, int height,
                  const Bitmap *mask, int xsrc, int ysrc, const Rgb &color, int bright);

// src1 のパレット 0 の位置に src2 を敷きながら合成する。
// 旧 mxv がステータス文字とレベルメータを背景と一発で合成するのに使っていた。
void BmpCopyComposite(Bitmap *dst, int xdst, int ydst, int width, int height,
                      const Bitmap *src1, int xsrc1, int ysrc1,
                      const Bitmap *src2, int xsrc2, int ysrc2, int alpha);

}  // namespace mxv2

#endif  // MXV2_BITMAP_H
