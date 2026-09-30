// mxv2 - 設定 UI: [mxv2 の設定] ウィンドウ (F1)
//
// 旧 mxv のプロパティシート (mxvprop.cpp) にあたる。スキンの一覧と言語の
// 入れ替え、表示倍率の適用待ちもここ。実装は settingsui_settings.cpp。
// クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_SETTINGS_H
#define MXV2_SETTINGSUI_SETTINGS_H

#include <cstdint>
#include <string>
#include <vector>

#include "message.h"

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
class Screen;
class SettingsUi;
struct Settings;

class SettingsWindow {
public:
	SettingsWindow();

	void Open() { visible_ = true; }
	bool isOpen() const { return visible_; }
	// 別のダイアログとの往復で、閉じきってから相手を開くときに使う
	// （PdxPathsDialog / LatencyDialog / SettingsUi::Build）。
	void SetVisible(bool v) { visible_ = v; }
	bool CloseTop() {
		if (!visible_) return false;
		visible_ = false;
		return true;
	}
	// 閉じた瞬間かどうかを 1 度だけ返す。「縦横の切り替えかた」は**閉じてから**
	// 効かせる決まりなので、その合図に使う（SettingsUi::Build が立てる）。
	bool TakeClosed() {
		const bool v = closed_;
		closed_ = false;
		return v;
	}

	// 縦横切り替えの状態。main が毎フレーム教える（screen_orientation.md）。
	void SetOrientationState(bool enabled, int orientation) {
		orientEnabled_ = enabled;
		orientation_ = orientation;
	}
	// キーから表示倍率を 1 段変える（Ctrl と +/-）。実際の計算と適用は次の
	// Build()（[表示倍率] とまったく同じ経路を通るので、少し待ってから
	// ウィンドウへ掛かり、ini にも保存される）。
	void RequestZoomStep(int step) { zoomStepRequest_ += step; }
	// RequestZoomStep で頼まれた増減を settings へ入れる。SettingsUi::Build の
	// 頭から 1 回だけ呼ぶ（倍率まわりを 1 か所に集める）。
	void ApplyZoomStep(SettingsUi &ui, Settings *settings);
	// 言語が入れ替わったフレームで 1 度だけ true。読んだら消える。
	bool TakeLocaleChanged() {
		const bool c = localeChanged_;
		localeChanged_ = false;
		return c;
	}
	// 出力サンプリングレートが選ばれたらここに入る。0 なら変更なし。
	int pendingSampleRate() const { return pendingSampleRate_; }
	void ClearPendingSampleRate() { pendingSampleRate_ = 0; }
	// スキンの一覧を取り直す（フォルダを足したとき用）。
	void ScanSkins(SettingsUi &ui);
	// [言語] で選ばれた言語を実際に読み込む。**設定ウィンドウが閉じきって
	// から**（題名も文言なので、開いたまま替えると ImGui のポップアップの id が
	// 途中で変わる）。SettingsUi::Build が呼ぶ。
	void ApplyLocale(SettingsUi &ui);
	void Build(SettingsUi &ui, Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
	           Screen *screen);

private:
	friend class SettingsUi;

	// 選べるスキン。縦横切り替え（screen_orientation.md）のために、
	// 名前だけでなく**縦横どちら向けか**も持つ。
	struct SkinItem {
		std::string ref;
		bool portrait;  // 正方形は縦扱い
	};
	// スキン 1 つぶんの表示名。頭に縦横の印を付ける。
	std::string SkinLabel(SettingsUi &ui, const SkinItem &item) const;
	// スキンを選ぶドロップダウン。portraitSlot が true なら縦向きのスキンを
	// 上へまとめる。選び直されたら true。
	bool SkinCombo(SettingsUi &ui, const char *label, bool portraitSlot, std::string *value);
	// 言語を選んだときの控え（入れ替えは ApplyLocale）。
	void SelectLocale(SettingsUi &ui, Settings *settings, const std::string &name);

	bool visible_;
	bool wasVisible_;  // 前のフレームの visible_（閉じた瞬間を拾う）
	bool closed_;      // 閉じた瞬間（TakeClosed で 1 度だけ読める）
	// 表示倍率 (%) は「入力を確定してから少し待って」適用する。
	// 操作中に適用するとウィンドウの大きさが変わり、それに合わせて
	// コントロール自身の座標も変わるので、同じ場所を押しているだけで値が
	// 行き来してしまう。
	int pendingZoom_;         // 0 = 適用待ちなし
	uint32_t zoomApplyAtMs_;  // 0 = 適用待ちなし
	// キー (Ctrl +/-) から頼まれた増減 (%)。0 = 頼まれていない。
	// 押しっぱなしのリピートで何回来ても、次の Build() でまとめて足す。
	int zoomStepRequest_;
	std::vector<SkinItem> skins_;
	int pendingSampleRate_;  // 選ばれた出力レート。0 なら変更なし
	// 縦横切り替えが有効か（起動オプションで決まる）と、いまの向き。
	bool orientEnabled_;
	int orientation_;  // Screen::Orientation
	// 言語。選べるのは同梱ぶんだけ（SettingsUi::Init で数え上げる）。
	std::vector<LocaleInfo> locales_;
	// 選ばれた言語を実際に読み込むまでの控え。
	std::string pendingLocale_;
	bool localeApplyPending_;
	bool localeChanged_;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_SETTINGS_H
