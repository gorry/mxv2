// mxv2 - 画面

#include "screen.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <SDL_syswm.h>

#include "message.h"

namespace mxv2 {

namespace {

// 画面の dpi として信じてよい範囲。大画面のテレビ (50 前後) から
// 高精細の携帯 (600 前後) まで入る幅を採ってある。
const float kDpiMin = 40.0f;
const float kDpiMax = 1000.0f;

}  // namespace

Screen::Screen()
    : window_(0),
      renderer_(0),
      texture_(0),
      width_(0),
      height_(0),
      zoom_(100),
      orientLock_(kOrientLockNone),
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

float Screen::PixelsPerMm() {
	// SDL は 3 つ返す。**使うのは hdpi / vdpi**（画面の実寸から出た値）で、
	// ddpi は当てにしない。Android の ddpi は「密度の区分」で、
	//   ・実寸とずれる（区分 320 に対して実寸 250dpi の 12" タブレットがあった）
	//   ・**ユーザーが「画面サイズ」の設定で変えられる**（実寸 270dpi に対して
	//     189 を返す 8" タブレットがあった）
	// ので、mm の物差しにはできない。
	// 指の大きさは向きに関係ないので、横と縦は平均でよい。
	//
	// ただし xdpi / ydpi にでたらめを入れている端末もあるので、
	// **ありえない値かどうかだけ**は見る。判断はこの 2 つ:
	//   ・40〜1000 dpi に収まっているか（大画面のテレビから高精細の携帯まで）
	//   ・横と縦がかけ離れていないか（画素はふつう正方形）
	// 外れていたら ddpi へ落とし、それも駄目なら決め打ちにする。
	float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
	if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0) {
		const float phys = (hdpi + vdpi) * 0.5f;
		if (phys >= kDpiMin && phys <= kDpiMax &&
		    (hdpi - vdpi) < phys * 0.5f && (vdpi - hdpi) < phys * 0.5f) {
			return phys / 25.4f;
		}
		if (ddpi >= kDpiMin && ddpi <= kDpiMax) return ddpi / 25.4f;
	}
#ifdef __ANDROID__
	return 160.0f / 25.4f;  // mdpi
#else
	return 96.0f / 25.4f;  // Windows の 100%
#endif
}

bool Screen::TouchPreferred() {
#if defined(__ANDROID__) || defined(__IPHONEOS__)
	return true;
#elif defined(__EMSCRIPTEN__)
	// ブラウザは同じものが PC でも携帯でも動く。タッチ装置があるかで決める。
	return SDL_GetNumTouchDevices() > 0;
#else
	return false;
#endif
}

std::string Screen::SystemLocale() {
	// SDL は好みの順に並べて返す。先頭だけ見れば足りる
	// （合うものが無ければ MatchLocale が落とし先へ落とす）。
	SDL_Locale *locales = SDL_GetPreferredLocales();
	std::string out;
	if (locales != 0) {
		if (locales[0].language != 0 && locales[0].language[0] != 0) {
			out = locales[0].language;
			if (locales[0].country != 0 && locales[0].country[0] != 0) {
				out += "-";
				out += locales[0].country;
			}
		}
		SDL_free(locales);
	}
	if (!out.empty()) return out;

#ifdef _WIN32
	// SDL が答えられなかったときの保険。SDL_Init の前でも効く。
	wchar_t buf[LOCALE_NAME_MAX_LENGTH];
	const int n = GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH);
	if (n > 0) {
		for (int i = 0; i < n && buf[i] != 0; i++) out += (char)buf[i];  // ASCII のみ
	}
#else
	const char *env = getenv("LC_ALL");
	if (env == 0 || env[0] == 0) env = getenv("LC_MESSAGES");
	if (env == 0 || env[0] == 0) env = getenv("LANG");
	if (env != 0) {
		// "ja_JP.UTF-8" のような形。文字集合と修飾は落とす。
		for (const char *p = env; *p != 0 && *p != '.' && *p != '@'; p++) out += *p;
		if (out == "C" || out == "POSIX") out.clear();
	}
#endif
	return out;
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

	// **SDL_RenderSetLogicalSize は使わない。**
	// あれを掛けると SDL がマウスイベントの座標を論理座標へ直してくれるが、
	// その変換は「いま論理サイズが入っているか」を見て行われる。mxv2 は
	// 文字と設定 UI を実解像度で描くために毎フレーム論理サイズを外して
	// 戻しており、**Android はタッチが Java の UI スレッドから飛んでくる**
	// ので、外している隙に届いたイベントだけ変換されずに素の窓の座標で
	// 入ってくる（同じ場所を叩いても効いたり効かなかったりする）。
	// 拡大もマウス座標の変換も自分で行う（CanvasRect / WindowToCanvas）。
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

	const SDL_Rect dst = CanvasRect();

	if (scaleMode_ == kScaleSharp && EnsurePreTexture()) {
		// 1 段目: 最近傍で整数倍へ（中間テクスチャいっぱいに描く）。
		SDL_SetRenderTarget(renderer_, preTexture_);
		SDL_RenderCopy(renderer_, texture_, NULL, NULL);
		SDL_SetRenderTarget(renderer_, NULL);

		// 2 段目: バイリニアで目的の大きさへ。
		SDL_RenderCopy(renderer_, preTexture_, NULL, &dst);
		return;
	}

	SDL_RenderCopy(renderer_, texture_, NULL, &dst);
}

void Screen::Present() {
	if (renderer_ == 0) return;
	SDL_RenderPresent(renderer_);
}

bool Screen::ResetTextures(std::string *err) {
	if (renderer_ == 0) {
		*err = Msg("Error.ScreenNotOpen");
		return false;
	}
	// 中身だけでなく器も無効になっている（GL のオブジェクトごと失われる）
	// ので、作り直す。キャンバスの中身は毎フレーム丸ごと転送しているから、
	// 描き直しはいつもの経路に任せてよい。
	SDL_Texture *tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
	                                     SDL_TEXTUREACCESS_STREAMING, width_, height_);
	if (tex == 0) {
		*err = MsgF("Error.CreateTexture", SDL_GetError());
		return false;
	}
	if (texture_ != 0) SDL_DestroyTexture(texture_);
	texture_ = tex;
	ReleasePreTexture();  // 2 段拡大の中間テクスチャも作り直す
	SetScaleMode(scaleMode_);
	return true;
}

// ---------------------------------------------------------------------------
// 画面の向き（screen_orientation.md）
// ---------------------------------------------------------------------------

Screen::Orientation Screen::OrientationOf(int w, int h) const {
	// -orientlock が指定されていれば、実物は見ない（デバッグ用）。
	if (orientLock_ == kOrientLockPortrait) return kPortrait;
	if (orientLock_ == kOrientLockLandscape) return kLandscape;

#ifdef __ANDROID__
	// 正方形は縦扱い（screen_orientation.md）。スキンの分け方と揃える。
	if (w > 0 && h > 0) return (w > h) ? kLandscape : kPortrait;
	return kPortrait;
#else
	// デスクトップに「画面の向き」は無い。窓は自由に変形できるので
	// 縦横比では決められず、常に横を返す。
	(void)w;
	(void)h;
	return kLandscape;
#endif
}

Screen::Orientation Screen::orientation() const {
	int w = 0, h = 0;
	GetOutputSize(&w, &h);
	return OrientationOf(w, h);
}

Screen::Orientation Screen::displayOrientation() const {
	SDL_Rect r;
	SDL_zero(r);
	if (SDL_GetDisplayBounds(0, &r) != 0) {
		r.w = 0;
		r.h = 0;
	}
	return OrientationOf(r.w, r.h);
}

bool Screen::CanResizeWindow() {
#ifdef __ANDROID__
	return false;
#else
	return true;
#endif
}

bool Screen::CanQuitApp() {
	return !IsMobile();
}

bool Screen::IsMobile() {
#if defined(__ANDROID__) || defined(__IPHONEOS__)
	return true;
#else
	return false;
#endif
}

bool Screen::Resize(int width, int height, std::string *err) {
	if (!SetCanvasSize(width, height, err)) return false;
	if (window_ != 0 && CanResizeWindow()) {
		SDL_SetWindowSize(window_, width_ * zoom_ / 100, height_ * zoom_ / 100);
	}
	return true;
}

bool Screen::SetCanvasSize(int width, int height, std::string *err) {
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
	SetScaleMode(scaleMode_);
	return true;
}

void Screen::SetZoom(int zoomPercent) {
	if (zoomPercent < kZoomMin) zoomPercent = kZoomMin;
	if (zoomPercent > kZoomMax) zoomPercent = kZoomMax;
	zoom_ = zoomPercent;
	if (window_ == 0 || !CanResizeWindow()) return;
	SDL_SetWindowSize(window_, width_ * zoom_ / 100, height_ * zoom_ / 100);
}

void Screen::GetOutputSize(int *w, int *h) const {
	if (w != 0) *w = width_;
	if (h != 0) *h = height_;
	if (renderer_ == 0) return;
	int outW = 0, outH = 0;
	if (SDL_GetRendererOutputSize(renderer_, &outW, &outH) != 0) return;
	if (outW <= 0 || outH <= 0) return;
	if (w != 0) *w = outW;
	if (h != 0) *h = outH;
}

void Screen::GetRenderOffset(float *ox, float *oy) const {
	if (ox != 0) *ox = 0.0f;
	if (oy != 0) *oy = 0.0f;
	if (renderer_ == 0 || width_ <= 0 || height_ <= 0) return;

	int outW = 0, outH = 0;
	GetOutputSize(&outW, &outH);
	float s = 1.0f;
	GetRenderScale(&s, 0);
	// SDL はキャンバスを実出力の真ん中へ置く（アスペクト比を保った残りが
	// 上下または左右の帯になる）。
	if (ox != 0) *ox = (outW - width_ * s) * 0.5f;
	if (oy != 0) *oy = (outH - height_ * s) * 0.5f;
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

SDL_Rect Screen::CanvasRect() const {
	SDL_Rect r;
	r.x = 0;
	r.y = 0;
	r.w = width_;
	r.h = height_;
	if (renderer_ == 0 || width_ <= 0 || height_ <= 0) return r;

	float s = 1.0f, ox = 0.0f, oy = 0.0f;
	GetRenderScale(&s, 0);
	GetRenderOffset(&ox, &oy);
	r.x = (int)(ox + 0.5f);
	r.y = (int)(oy + 0.5f);
	r.w = (int)(width_ * s + 0.5f);
	r.h = (int)(height_ * s + 0.5f);
	return r;
}

float Screen::WindowToOutputScale() const {
	if (window_ == 0) return 1.0f;
	int ww = 0, wh = 0;
	SDL_GetWindowSize(window_, &ww, &wh);
	if (ww <= 0) return 1.0f;
	int ow = 0, oh = 0;
	GetOutputSize(&ow, &oh);
	if (ow <= 0) return 1.0f;
	return (float)ow / (float)ww;
}

void Screen::WindowToOutput(int wx, int wy, int *ox, int *oy) const {
	const float d = WindowToOutputScale();
	if (ox != 0) *ox = (int)(wx * d + 0.5f);
	if (oy != 0) *oy = (int)(wy * d + 0.5f);
}

void Screen::WindowToCanvas(int wx, int wy, int *cx, int *cy) const {
	int px = wx, py = wy;
	WindowToOutput(wx, wy, &px, &py);

	float s = 1.0f, offX = 0.0f, offY = 0.0f;
	GetRenderScale(&s, 0);
	GetRenderOffset(&offX, &offY);
	if (s <= 0.0f) s = 1.0f;
	if (cx != 0) *cx = (int)((px - offX) / s);
	if (cy != 0) *cy = (int)((py - offY) / s);
}

void Screen::WindowEventToCanvas(SDL_Event *ev) const {
	if (ev == 0) return;
	switch (ev->type) {
		case SDL_MOUSEMOTION:
			WindowToCanvas(ev->motion.x, ev->motion.y, &ev->motion.x, &ev->motion.y);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			WindowToCanvas(ev->button.x, ev->button.y, &ev->button.x, &ev->button.y);
			break;
		default:
			break;
	}
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
