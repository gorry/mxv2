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

	// 画面 1mm あたりの実ピクセル数。押せるところの大きさを「指で押せる
	// 何 mm」で決めるために使う。取れなければ、Android は mdpi (160dpi)、
	// それ以外は Windows の 100% にあたる 96dpi を仮に使う。
	// SDL_Init 後に呼ぶこと。
	static float PixelsPerMm();

	// この端末が指での操作を前提にしているか。設定の「自動」の既定になる。
	// Web は端末で決まるので、SDL がタッチ装置を数えられるかで見る。
	static bool TouchPreferred();

	// 動作環境の言語。"ja-JP" のような形で、国が分からなければ "ja" だけ。
	// 設定の [言語] が「自動」のときの既定に使う。分からなければ空。
	// **SDL_Init の前に呼んでもよい**（起動時のカタログを読む前に要る）。
	static std::string SystemLocale();

	// 表示倍率の範囲。
	static const int kZoomMin = 100;
	static const int kZoomMax = 400;

	// ---- 画面の向き（screen_orientation.md） --------------------------
	// スキンを選ぶためだけの向き。上下反転は区別しない（反転しても
	// 縦は縦、横は横なので、スキンを替える理由が無い）。
	enum Orientation {
		kPortrait = 0,
		kLandscape,
	};

	// -orientlock の値。0 なら実物（デスクトップではダミー）を見る。
	enum OrientationLock {
		kOrientLockNone = 0,
		kOrientLockPortrait,
		kOrientLockLandscape,
	};
	void SetOrientationLock(int lock) { orientLock_ = lock; }
	int orientationLock() const { return orientLock_; }

	// いまの向き。**端末の「自然な向き」ではなく出力の縦横比で決める。**
	// SDL_GetDisplayOrientation は SDLActivity が display.getRotation() を
	// そのまま写しているので、横が自然な向きのタブレットでは逆に出る。
	//
	// デスクトップでは常に横（-orientlock で変えられる）。窓を自由に
	// 変形できるので縦横比では決められない——変形するたびにスキンが
	// 入れ替わってしまう。
	Orientation orientation() const;

	// ウィンドウを作る前に使う版。起動時にどちらのスキンを読むかを
	// 決めるのに要る。**SDL_Init(SDL_INIT_VIDEO) の後に呼ぶこと。**
	Orientation displayOrientation() const;

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

	// レンダラの実出力サイズ（実ピクセル）。窓の大きさとは限らない（HiDPI）。
	void GetOutputSize(int *w, int *h) const;

	// キャンバスを実出力の真ん中へ置いたときの、左と上の余白（レターボックス）。
	// 窓の縦横比がキャンバスと違うと余りが出る。Android のように窓が画面
	// いっぱいのときは必ず出るので、実ピクセルで座標を扱うところ
	// （Dear ImGui）はこのぶんずらして考える必要がある。
	void GetRenderOffset(float *ox, float *oy) const;

	// テクスチャを作り直す。GL コンテキストが失われたあと
	// （Android でバックグラウンドから戻ったとき。SDL_RENDER_DEVICE_RESET /
	// SDL_RENDER_TARGETS_RESET）に呼ぶ。器ごと無効になっているので、
	// 中身を描き直すだけでは戻らない。
	bool ResetTextures(std::string *err);

	// 論理サイズを変える（スキンの切り替え）。ウィンドウとレンダラは
	// 作り直さないので、Dear ImGui のバックエンドを繋いだままでよい。
	// **ウィンドウの大きさも「キャンバス x 表示倍率」に揃え直す。**
	bool Resize(int width, int height, std::string *err);

	// 窓の大きさをこちらから決めてよいか。
	//
	// **Android では駄目。** 窓＝画面で、大きさを決めるのは OS のほう。
	// SDL の Android バックエンドは SetWindowSize を持たないので、
	// SDL_SetWindowSize を呼ぶと **SDL 側の記録（window->w/h）だけが変わり**、
	// 実際の描画面と食い違う。レンダラの出力サイズもその記録から来るので、
	// 絵が「実物より小さいビューポート」に描かれて画面の隅に寄る
	// （実機で踏んだ: 2400x1080 の画面に 720x480 が左下に出た）。
	static bool CanResizeWindow();

	// アプリを自分から終わらせる導線を出してよいか。
	//
	// **モバイルでは駄目。** 終わらせるのは OS の仕事で、メニューに [終了] を
	// 置くのが作法に合わない。**あってはならないと定めている配布先もある**
	// （2026-09-10、ユーザーの指示）。**戻るキーからの終了は残す**——
	// あちらは OS 側の作法そのもの。
	static bool CanQuitApp();

	// モバイル（Android / iOS）で動いているか。メニューの品揃えを変える
	// （[フォルダを開く…] はモバイルでは出さない。2026-09-15、ユーザーの指示。
	// パスを打ち込む導線はキーボードの無い端末では使いづらく、L キーも無い）。
	static bool IsMobile();

	// キャンバスだけ変える。ウィンドウには触らない。
	// 窓のリサイズに追いかけてキャンバスを作り直すとき（fullscreen.md）は
	// こちらを使う。Resize から呼ぶと窓の大きさを取り合いになるため。
	bool SetCanvasSize(int width, int height, std::string *err);

	// 表示倍率 (%) を変える。論理サイズは変わらない。
	void SetZoom(int zoomPercent);
	int zoomPercent() const { return zoom_; }

	// 論理サイズが実際に何倍へ拡大されるか。
	// 文字を出力解像度で描くために使う (textlayer.cpp)。
	void GetRenderScale(float *sx, float *sy) const;

	// キャンバスを実出力のどこへ貼るか（実ピクセル）。窓の縦横比が
	// キャンバスと違うと、余った側が帯になって中央に寄る。
	SDL_Rect CanvasRect() const;

	// 窓の座標（SDL のマウスイベントが持っている座標）を実ピクセルへ。
	// HiDPI で「窓の大きさ」と「実出力の大きさ」が違う環境のための倍率で、
	// ふつうは 1 倍。Dear ImGui は実ピクセルで動かしているのでこちらを使う。
	float WindowToOutputScale() const;
	void WindowToOutput(int wx, int wy, int *ox, int *oy) const;

	// 窓の座標をキャンバスの論理座標へ。帯のぶんを引いて倍率で割る。
	// **当たり判定は全部この座標系**なので、マウスのイベントは
	// WindowEventToCanvas() を通してから配ること。
	void WindowToCanvas(int wx, int wy, int *cx, int *cy) const;
	void WindowEventToCanvas(SDL_Event *ev) const;

	// ウィンドウ位置と大きさ。設定の保存・復元に使う。
	void GetWindowRect(int *x, int *y, int *w, int *h) const;
	void SetWindowPos(int x, int y);

	SDL_Window *window() { return window_; }

	// OS のウィンドウハンドル（Windows なら HWND）。OS のダイアログを
	// こちらの窓の上に出すために渡す。取れなければ 0。
	void *nativeWindowHandle() const;
	SDL_Renderer *renderer() { return renderer_; }

private:
	SDL_Window *window_;
	SDL_Renderer *renderer_;
	SDL_Texture *texture_;
	std::vector<uint32_t> pixels_;
	int width_, height_;
	int zoom_;  // 表示倍率 (%)

	int orientLock_;  // OrientationLock
	// 実物の向きを見るところ（Android だけ）。w/h の大小で決める。
	Orientation OrientationOf(int w, int h) const;

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
