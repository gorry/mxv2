// mxv2 - 文字だけを出力解像度で描くレイヤー

#include "textlayer.h"

#include <algorithm>
#include <cstring>

#include "message.h"
#include "screen.h"
#include "textrender.h"

namespace mxv2 {

namespace {

// 論理座標 -> 出力座標。四捨五入で寄せる。
inline int ToOut(int v, float scale) {
	return (int)(v * scale + 0.5f);
}

}  // namespace

TextLayer::TextLayer()
    : text_(0),
      texture_(0),
      width_(0),
      height_(0),
      scaleX_(1.0f),
      scaleY_(1.0f),
      dirty_(false) {}

TextLayer::~TextLayer() {
	Shutdown();
}

bool TextLayer::Init(Screen *screen, const std::vector<std::string> &fontDirs, std::string *err) {
	if (screen == 0 || screen->renderer() == 0) {
		*err = Msg("Error.TextLayerNeedWindow");
		return false;
	}
	fontDirs_ = fontDirs;
	if (text_ == 0) text_ = CreateTextRenderer(fontDirs_);
	return Resize(screen, err);
}

void TextLayer::SetFontDirs(const std::vector<std::string> &fontDirs) {
	if (fontDirs == fontDirs_ && text_ != 0) return;
	fontDirs_ = fontDirs;
	delete text_;
	text_ = CreateTextRenderer(fontDirs_);
}

void TextLayer::Shutdown() {
	Release();
	delete text_;
	text_ = 0;
}

void TextLayer::Release() {
	if (texture_ != 0) {
		SDL_DestroyTexture(texture_);
		texture_ = 0;
	}
	pixels_.clear();
	width_ = 0;
	height_ = 0;
}

bool TextLayer::available() const {
	return text_ != 0 && text_->available() && !pixels_.empty();
}

bool TextLayer::Resize(Screen *screen, std::string *err) {
	float sx = 1.0f, sy = 1.0f;
	screen->GetRenderScale(&sx, &sy);
	if (sx <= 0.0f) sx = 1.0f;
	if (sy <= 0.0f) sy = 1.0f;

	const int w = ToOut(screen->width(), sx);
	const int h = ToOut(screen->height(), sy);
	if (w <= 0 || h <= 0) {
		*err = Msg("Error.TextLayerSize");
		return false;
	}

	Release();
	scaleX_ = sx;
	scaleY_ = sy;
	width_ = w;
	height_ = h;

	texture_ = SDL_CreateTexture(screen->renderer(), SDL_PIXELFORMAT_ARGB8888,
	                             SDL_TEXTUREACCESS_STREAMING, w, h);
	if (texture_ == 0) {
		*err = MsgF("Error.TextLayerTexture", SDL_GetError());
		return false;
	}
	SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);

	pixels_.assign((size_t)w * h, 0);
	// 1 行分のカバレッジ置き場。曲名の帯が一番大きい。
	scratch_.Create(w, ToOut(32, sy), 8);
	dirty_ = true;
	return true;
}

bool TextLayer::SyncToScreen(Screen *screen) {
	if (screen == 0 || screen->renderer() == 0) return false;

	float sx = 1.0f, sy = 1.0f;
	screen->GetRenderScale(&sx, &sy);
	if (ToOut(screen->width(), sx) == width_ && ToOut(screen->height(), sy) == height_) {
		return false;
	}

	std::string err;
	Resize(screen, &err);
	return true;
}

void TextLayer::ClearAll() {
	if (pixels_.empty()) return;
	std::fill(pixels_.begin(), pixels_.end(), 0u);
	dirty_ = true;
}

void TextLayer::ClearRect(int x, int y, int width, int height) {
	if (pixels_.empty()) return;

	int x0 = ToOut(x, scaleX_);
	int y0 = ToOut(y, scaleY_);
	int x1 = ToOut(x + width, scaleX_);
	int y1 = ToOut(y + height, scaleY_);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > width_) x1 = width_;
	if (y1 > height_) y1 = height_;
	if (x0 >= x1 || y0 >= y1) return;

	for (int yy = y0; yy < y1; yy++) {
		memset(&pixels_[(size_t)yy * width_ + x0], 0, (size_t)(x1 - x0) * sizeof(uint32_t));
	}
	dirty_ = true;
}

void TextLayer::DrawText(int x, int y, int maxWidth, int cellHeight, const std::string &utf8,
                         const Rgb &color, int bright, int clipY, int clipH) {
	if (!available() || utf8.empty() || cellHeight <= 0) return;

	// 書き込んでよい縦の範囲。指定が無ければ 1 行ぶん。
	if (clipH <= 0) {
		clipY = y;
		clipH = cellHeight;
	}
	int clipTop = ToOut(clipY, scaleY_);
	int clipBottom = ToOut(clipY + clipH, scaleY_);
	if (clipTop < 0) clipTop = 0;
	if (clipBottom > height_) clipBottom = height_;
	if (clipTop >= clipBottom) return;

	const int ox = ToOut(x, scaleX_);
	const int oy = ToOut(y, scaleY_);
	const int ow = ToOut(maxWidth, scaleX_);
	const float oh = cellHeight * scaleY_;

	const int rows = (int)(oh + 1.5f);
	if (!scratch_.valid() || scratch_.height() < rows) return;

	// カバレッジを一旦 8bpp へ焼いてから、色を付けて重ねる。
	const int clip = std::min(ow, scratch_.width());
	for (int yy = 0; yy < rows; yy++) {
		memset(scratch_.RowFromTop(yy), 0, (size_t)scratch_.width());
	}
	text_->Draw(&scratch_, 0, 0, clip, oh, utf8);

	if (bright < 0) bright = 0;
	if (bright > 100) bright = 100;

	for (int yy = 0; yy < rows; yy++) {
		const int py = oy + yy;
		if (py < clipTop || py >= clipBottom) continue;
		const uint8_t *p = scratch_.RowFromTop(yy);
		uint32_t *q = &pixels_[(size_t)py * width_];
		for (int xx = 0; xx < clip; xx++) {
			const int px = ox + xx;
			if (px < 0 || px >= width_) continue;
			int a = p[xx] * bright / 100;
			if (a <= 0) continue;
			if (a > 255) a = 255;
			// 同じ画素に重ねて描くことは無いので、濃い方を残すだけでよい。
			const uint32_t old = q[px];
			if ((int)(old >> 24) >= a) continue;
			q[px] = ((uint32_t)a << 24) | ((uint32_t)color.r << 16) |
			        ((uint32_t)color.g << 8) | color.b;
		}
	}
	dirty_ = true;
}

void TextLayer::Render(Screen *screen) {
	if (texture_ == 0 || pixels_.empty() || screen == 0 || screen->renderer() == 0) return;

	if (dirty_) {
		SDL_UpdateTexture(texture_, NULL, &pixels_[0], width_ * (int)sizeof(uint32_t));
		dirty_ = false;
	}

	// 論理座標いっぱいに貼る。テクスチャは既に出力解像度なので等倍で載る。
	SDL_Rect dst;
	dst.x = 0;
	dst.y = 0;
	dst.w = screen->width();
	dst.h = screen->height();
	SDL_RenderCopy(screen->renderer(), texture_, NULL, &dst);
}

}  // namespace mxv2
