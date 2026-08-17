// mxv2 - 画面
//
// 旧 mxv は 640x480 の DIB (Default.bmp と同寸) にブリットして BitBlt で
// 転送していた。mxv2 も同じ形のオフスクリーンバッファを持ち、SDL の
// ストリーミングテクスチャ経由で拡大表示する。大きさはスキンが決めるので
// 固定ではなく、Open() で受け取る。
//
// Phase 2 で移植する draw.cpp のブリッタは、このバッファ (ARGB8888) を
// 直接叩く前提で書く。プラットフォーム依存部はここに閉じ込める。

#ifndef MXV2_SCREEN_H
#define MXV2_SCREEN_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

namespace mxv2 {

class Screen {
public:
	Screen();
	~Screen();

	// width x height はスキンが決める論理サイズ (旧 mxv は 640x480)。
	// zoomPercent は表示倍率 (100 = ドット等倍)。ウィンドウの大きさは
	// 実ピクセルで width * zoomPercent / 100 になる。
	bool Open(const std::string &title, int width, int height, int zoomPercent,
	          std::string *err);

	// このディスプレイのシステム拡大率 (%)。175% なら 175。
	// 拡大率の初期値に使う。取れなければ 100。SDL_Init 後に呼ぶこと。
	static int SystemZoomPercent();

	// 表示倍率の範囲。
	static const int kZoomMin = 100;
	static const int kZoomMax = 400;

	// キャンバスを拡大するときの補間方法。
	enum ScaleMode {
		// 最近傍。ドットがそのまま出るが、非整数倍だと 1px と 2px が混ざって
		// 線の太さがムラになる。
		kScaleNearest = 0,
		// バイリニア。ムラは消えるがドット絵全体がぼやける。
		kScaleLinear,
		// sharp-bilinear。まず最近傍で整数倍 (ceil) に拡大してドットの角を
		// 作ってから、バイリニアで目的の大きさへ縮める。角を保ったまま
		// 非整数倍のムラが消える。整数倍のときは縮小が 1:1 になるので
		// 最近傍と同じ結果になる。
		kScaleSharp,
	};
	void SetScaleMode(ScaleMode mode);
	ScaleMode scaleMode() const { return scaleMode_; }

	// ini や UI で使う名前との変換。
	static const char *ScaleModeName(ScaleMode mode);
	static ScaleMode ScaleModeFromName(const std::string &name, ScaleMode fallback);

	int width() const { return width_; }
	int height() const { return height_; }
	void Close();

	void SetTitle(const std::string &title);

	// オフスクリーンバッファ。ARGB8888、1 行あたり width() ピクセル。
	uint32_t *pixels() { return pixels_.empty() ? 0 : &pixels_[0]; }
	const uint32_t *pixels() const { return pixels_.empty() ? 0 : &pixels_[0]; }

	void Clear(uint32_t argb);
	void FillRect(int x, int y, int w, int h, uint32_t argb);

	// オフスクリーンバッファをレンダラへ転送する（まだ表示はしない）。
	// Draw() と Present() の間に Dear ImGui を重ねる。
	void Draw();
	// レンダラの内容をウィンドウへ出す。
	void Present();

	// 論理サイズを変える（スキンの切り替え）。ウィンドウとレンダラは
	// 作り直さないので、Dear ImGui のバックエンドを繋いだままでよい。
	bool Resize(int width, int height, std::string *err);

	// 表示倍率 (%) を変える。論理サイズは変わらない。
	void SetZoom(int zoomPercent);
	int zoomPercent() const { return zoom_; }

	// 論理サイズが実際に何倍へ拡大されるか。
	// 文字を出力解像度で描くために使う (textlayer.cpp)。
	void GetRenderScale(float *sx, float *sy) const;

	// 論理サイズを一時的に外して、実解像度で描けるようにする。
	// Dear ImGui のように等倍で描きたいものに使う。必ず対で呼ぶ。
	void BeginNativeScale();
	void EndNativeScale();

	// ウィンドウ位置と大きさ。設定の保存・復元に使う。
	void GetWindowRect(int *x, int *y, int *w, int *h) const;
	void SetWindowPos(int x, int y);

	SDL_Window *window() { return window_; }
	SDL_Renderer *renderer() { return renderer_; }

private:
	SDL_Window *window_;
	SDL_Renderer *renderer_;
	SDL_Texture *texture_;
	std::vector<uint32_t> pixels_;
	int width_, height_;
	int zoom_;  // 表示倍率 (%)

	ScaleMode scaleMode_;
	// sharp-bilinear の 1 段目の受け皿。キャンバスの整数倍。
	SDL_Texture *preTexture_;
	int preScale_;

	bool EnsurePreTexture();
	void ReleasePreTexture();

	Screen(const Screen &);
	Screen &operator=(const Screen &);
};

}  // namespace mxv2

#endif  // MXV2_SCREEN_H
