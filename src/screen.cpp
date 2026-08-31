// mxv2 - 画面

#include "screen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL_syswm.h>

#include "message.h"

namespace mxv2 {

Screen::Screen()
    : window_(0),
      renderer_(0),
      texture_(0),
      width_(0),
      height_(0),
      zoom_(100),
      scaleMode_(kScaleSharp),
      preTexture_(0),
      preScale_(0) {}

Screen::~Screen() {
	Close();
}

int Screen::SystemZoomPercent() {
#ifdef __ANDROID__
	// Android の窓は画面いっぱいで、拡大は SDL_RenderSetLogicalSize が
	// 面倒を見る（余った側は帯になる）。DPI から出すと 400% などになって
	// 意味を持たないので、等倍を既定にする。
	return 100;
#else
	float hdpi = 0.0f;
	if (SDL_GetDisplayDPI(0, NULL, &hdpi, NULL) != 0) return 100;
	if (hdpi <= 0.0f) return 100;
	// Windows の 100% は 96dpi。
	int percent = (int)(hdpi / 96.0f * 100.0f + 0.5f);
	if (percent < kZoomMin) percent = kZoomMin;
	if (percent > kZoomMax) percent = kZoomMax;
	return percent;
#endif
}

bool Screen::Open(const std::string &title, int width, int height, int zoomPercent,
                  std::string *err) {
	if (width <= 0 || height <= 0) {
		*err = Msg("Error.ScreenSize");
		return false;
	}
	if (zoomPercent < kZoomMin) zoomPercent = kZoomMin;
	if (zoomPercent > kZoomMax) zoomPercent = kZoomMax;
	zoom_ = zoomPercent;
	width_ = width;
	height_ = height;

	window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                           width_ * zoom_ / 100, height_ * zoom_ / 100,
	                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (window_ == 0) {
		*err = MsgF("Error.CreateWindow", SDL_GetError());
		return false;
	}

	renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer_ == 0) {
		// アクセラレータが無い環境ではソフトウェアへ落とす
		renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
	}
	if (renderer_ == 0) {
		*err = MsgF("Error.CreateRenderer", SDL_GetError());
		Close();
		return false;
	}

	// ウィンドウを伸ばしてもアスペクト比を保つ
	SDL_RenderSetLogicalSize(renderer_, width_, height_);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // ドット絵なので最近傍

	texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
	                             SDL_TEXTUREACCESS_STREAMING, width_, height_);
	if (texture_ == 0) {
		*err = MsgF("Error.CreateTexture", SDL_GetError());
		Close();
		return false;
	}

	pixels_.assign((size_t)width_ * height_, 0xff000000u);
	SetScaleMode(scaleMode_);
	return true;
}

void *Screen::nativeWindowHandle() const {
	if (window_ == 0) return 0;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window_, &info)) return 0;
#ifdef _WIN32
	return (void *)info.info.win.window;
#else
	return 0;
#endif
}

void Screen::Close() {
	ReleasePreTexture();
	if (texture_ != 0) {
		SDL_DestroyTexture(texture_);
		texture_ = 0;
	}
	if (renderer_ != 0) {
		SDL_DestroyRenderer(renderer_);
		renderer_ = 0;
	}
	if (window_ != 0) {
		SDL_DestroyWindow(window_);
		window_ = 0;
	}
	pixels_.clear();
}

void Screen::SetTitle(const std::string &title) {
	if (window_ != 0) SDL_SetWindowTitle(window_, title.c_str());
}

void Screen::Clear(uint32_t argb) {
	if (pixels_.empty()) return;
	std::fill(pixels_.begin(), pixels_.end(), argb);
}

void Screen::FillRect(int x, int y, int w, int h, uint32_t argb) {
	if (pixels_.empty()) return;
	int x0 = std::max(0, x);
	int y0 = std::max(0, y);
	int x1 = std::min(width_, x + w);
	int y1 = std::min(height_, y + h);
	for (int yy = y0; yy < y1; yy++) {
		uint32_t *row = &pixels_[(size_t)yy * width_];
		for (int xx = x0; xx < x1; xx++) row[xx] = argb;
	}
}

const char *Screen::ScaleModeName(ScaleMode mode) {
	switch (mode) {
		case kScaleNearest:
			return "nearest";
		case kScaleLinear:
			return "linear";
		default:
			return "sharp";
	}
}

Screen::ScaleMode Screen::ScaleModeFromName(const std::string &name, ScaleMode fallback) {
	if (name == "nearest") return kScaleNearest;
	if (name == "linear") return kScaleLinear;
	if (name == "sharp" || name == "sharp-bilinear") return kScaleSharp;
	return fallback;
}

void Screen::SetScaleMode(ScaleMode mode) {
	scaleMode_ = mode;
	// キャンバスは sharp のときも最近傍。ぼかすのは 2 段目だけ。
	if (texture_ != 0) {
		SDL_SetTextureScaleMode(
		    texture_, (mode == kScaleLinear) ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
	}
	if (mode != kScaleSharp) ReleasePreTexture();
}

void Screen::ReleasePreTexture() {
	if (preTexture_ != 0) {
		SDL_DestroyTexture(preTexture_);
		preTexture_ = 0;
	}
	preScale_ = 0;
}

// sharp-bilinear の 1 段目の受け皿を用意する。倍率は出力倍率の切り上げ。
// 例: 1.75 倍なら 2 倍に最近傍拡大してから 0.875 倍へバイリニア縮小する。
bool Screen::EnsurePreTexture() {
	if (renderer_ == 0 || width_ <= 0 || height_ <= 0) return false;
	if (SDL_RenderTargetSupported(renderer_) == SDL_FALSE) return false;

	float sx = 1.0f;
	GetRenderScale(&sx, 0);
	// ちょうど整数倍のときに 1 つ上へ行かないよう、わずかに引いてから切り上げる。
	int p = (int)ceilf(sx - 0.001f);
	if (p < 1) p = 1;
	if (p > 8) p = 8;

	if (preTexture_ != 0 && preScale_ == p) return true;

	ReleasePreTexture();
	preTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
	                                SDL_TEXTUREACCESS_TARGET, width_ * p, height_ * p);
	if (preTexture_ == 0) return false;
	SDL_SetTextureScaleMode(preTexture_, SDL_ScaleModeLinear);
	preScale_ = p;
	return true;
}

void Screen::Draw() {
	if (texture_ == 0 || renderer_ == 0 || pixels_.empty()) return;
	SDL_UpdateTexture(texture_, NULL, &pixels_[0], width_ * (int)sizeof(uint32_t));
	SDL_RenderClear(renderer_);

	if (scaleMode_ == kScaleSharp && EnsurePreTexture()) {
		// 1 段目: 最近傍で整数倍へ。論理サイズは外しておく
		// (中間テクスチャいっぱいに描きたいので)。
		BeginNativeScale();
		SDL_SetRenderTarget(renderer_, preTexture_);
		SDL_RenderCopy(renderer_, texture_, NULL, NULL);
		SDL_SetRenderTarget(renderer_, NULL);
		EndNativeScale();

		// 2 段目: バイリニアで目的の大きさへ。
		SDL_Rect dst;
		dst.x = 0;
		dst.y = 0;
		dst.w = width_;
		dst.h = height_;
		SDL_RenderCopy(renderer_, preTexture_, NULL, &dst);
		return;
	}

	SDL_RenderCopy(renderer_, texture_, NULL, NULL);
}

void Screen::Present() {
	if (renderer_ == 0) return;
	SDL_RenderPresent(renderer_);
}

bool Screen::Resize(int width, int height, std::string *err) {
	if (width <= 0 || height <= 0) {
		*err = Msg("Error.ScreenSize");
		return false;
	}
	if (renderer_ == 0) {
		*err = Msg("Error.ScreenNotOpen");
		return false;
	}
	if (width == width_ && height == height_) return true;

	SDL_Texture *tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
	                                     SDL_TEXTUREACCESS_STREAMING, width, height);
	if (tex == 0) {
		*err = MsgF("Error.CreateTexture", SDL_GetError());
		return false;
	}
	if (texture_ != 0) SDL_DestroyTexture(texture_);
	texture_ = tex;
	ReleasePreTexture();  // キャンバスの大きさが変わったので作り直す

	width_ = width;
	height_ = height;
	pixels_.assign((size_t)width_ * height_, 0xff000000u);
	SDL_RenderSetLogicalSize(renderer_, width_, height_);
	SetScaleMode(scaleMode_);
	if (window_ != 0) SDL_SetWindowSize(window_, width_ * zoom_ / 100, height_ * zoom_ / 100);
	return true;
}

void Screen::SetZoom(int zoomPercent) {
	if (zoomPercent < kZoomMin) zoomPercent = kZoomMin;
	if (zoomPercent > kZoomMax) zoomPercent = kZoomMax;
	zoom_ = zoomPercent;
	if (window_ == 0) return;
	SDL_SetWindowSize(window_, width_ * zoom_ / 100, height_ * zoom_ / 100);
}

void Screen::GetRenderScale(float *sx, float *sy) const {
	if (sx != 0) *sx = 1.0f;
	if (sy != 0) *sy = 1.0f;
	if (renderer_ == 0) return;

	// SDL_RenderGetScale は論理サイズの分を返さない (2.0.18 以降は
	// viewport/scale ではなく logical_dst_rect で処理しているため)。
	// 実出力サイズから自分で求める。アスペクト比は保たれるので等方。
	int outW = 0, outH = 0;
	if (SDL_GetRendererOutputSize(renderer_, &outW, &outH) != 0) return;
	if (outW <= 0 || outH <= 0) return;

	float s = (float)outW / width_;
	const float sh = (float)outH / height_;
	if (sh < s) s = sh;
	if (s <= 0.0f) s = 1.0f;
	if (sx != 0) *sx = s;
	if (sy != 0) *sy = s;
}

void Screen::BeginNativeScale() {
	if (renderer_ == 0) return;
	SDL_RenderSetLogicalSize(renderer_, 0, 0);
}

void Screen::EndNativeScale() {
	if (renderer_ == 0) return;
	SDL_RenderSetLogicalSize(renderer_, width_, height_);
}

void Screen::GetWindowRect(int *x, int *y, int *w, int *h) const {
	if (x != 0) *x = 0;
	if (y != 0) *y = 0;
	if (w != 0) *w = width_;
	if (h != 0) *h = height_;
	if (window_ == 0) return;
	if (x != 0 && y != 0) SDL_GetWindowPosition(window_, x, y);
	if (w != 0 && h != 0) SDL_GetWindowSize(window_, w, h);
}

void Screen::SetWindowPos(int x, int y) {
	if (window_ == 0) return;
	SDL_SetWindowPosition(window_, x, y);
}

}  // namespace mxv2
