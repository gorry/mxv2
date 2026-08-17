// mxv2 - 設定 UI（Dear ImGui）
//
// 旧 mxv のプロパティシート (mxvprop.cpp) と、テーマ (.mxv) の編集にあたる。
// F1 で開閉する。開いている間だけ ImGui がキーとマウスを横取りする。
//
// ImGui は実解像度で描く。SDL_RenderSetLogicalSize が入っているせいで
// マウス座標だけは論理座標で届くので、拡大率を掛け戻して合わせている。
//
// スキンの切り替えは画面サイズごと変わりうるので、ここでは名前を
// pendingSkin() に置くだけにして、実際の作り直しはメインループに任せる。

#ifndef MXV2_SETTINGSUI_H
#define MXV2_SETTINGSUI_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "imgui.h"

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
class Screen;
struct Settings;

class SettingsUi {
public:
	SettingsUi();
	~SettingsUi();

	// ImGui の初期化。assetsDir の下の skin/ からスキンを探す。
	bool Init(Screen *screen, const std::string &assetsDir, std::string *err);
	void Shutdown();

	// SDL イベントを ImGui へ渡す。
	void ProcessEvent(const SDL_Event &ev);

	// 直前のフレームで ImGui が入力を欲しがっているか。true の間、
	// アプリ側は同種のイベントを無視する。
	bool wantCaptureMouse() const;
	bool wantCaptureKeyboard() const;

	void Toggle() { visible_ = !visible_; }
	bool visible() const { return visible_; }

	// 1 フレーム分の UI を組み立てる。設定の変更はその場で反映する。
	// 非表示のときも ImGui のフレームは回す必要があるので毎フレーム呼ぶ。
	void Build(Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
	           Screen *screen);

	// 組み立てた UI を今のレンダラへ描く。Screen::Draw と Present の間で呼ぶ。
	void Render(Screen *screen);

	// スキンが選ばれたらここに名前が入る。メインループが拾って画面を
	// 作り直し、済んだら ClearPendingSkin() を呼ぶ。
	const std::string &pendingSkin() const { return pendingSkin_; }
	void ClearPendingSkin() { pendingSkin_.clear(); }

	// スキンの一覧を取り直す（フォルダを足したとき用）。
	void ScanSkins();

	// 日本語フォントが読めたか。読めなければ ImGui 既定の ASCII フォント。
	bool hasJapaneseFont() const { return hasJapaneseFont_; }

private:
	// 倍率が変わったら true（ダイアログの大きさも作り直すため）。
	bool ApplyScale(float scale);
	// 画面を作り直す。ステータス欄は変化があったときしか描かないので、
	// 作り直したあとは Player に積み直しを頼む。
	void Rebuild(DrawScreen *draw, Player *player);

	std::string SkinDir(const std::string &name) const;

	bool ready_;
	bool visible_;
	bool hasJapaneseFont_;
	float styleScale_;
	ImGuiStyle baseStyle_;

	// ImGui は実解像度で動かすので、SDL から届く論理座標のマウス位置を
	// この倍率で直してからバックエンドへ渡す。Build で毎フレーム更新する。
	float inputScale_;

	// 表示倍率 (%) は「入力を確定してから少し待って」適用する。
	// 操作中に適用するとウィンドウの大きさが変わり、それに合わせて
	// コントロール自身の座標も変わるので、同じ場所を押しているだけで値が
	// 行き来してしまう。
	int pendingZoom_;         // 0 = 適用待ちなし
	uint32_t zoomApplyAtMs_;  // 0 = 適用待ちなし

	std::string assetsDir_;
	std::string skinRootDir_;
	std::vector<std::string> skinNames_;
	std::string pendingSkin_;

	// PDX パスの入力欄。std::string を直接は編集できないので固定長で持つ。
	char pdxPathBuf_[512];

	SettingsUi(const SettingsUi &);
	SettingsUi &operator=(const SettingsUi &);
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_H
