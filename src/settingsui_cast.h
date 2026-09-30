// mxv2 - 設定 UI: [キャスト] ダイアログ (F9)
//
// Chromecast（Google Cast 対応機器）へ画面と音を送る（cast.h）。[表示] の
// [キャスト…] から。実装は settingsui_cast.cpp。
//
// ダイアログのクラスの作法（memo/settingsui-split.md）: 状態と補助関数は
// このクラスの private。共通の道具（SyncModal / BeginDialog / DialogFlags …）と
// main への出力（changedFields_ / request_）は Build() に渡される SettingsUi を
// 通して使う（SettingsUi が friend にしてくれている）。逆向きは無い:
// SettingsUi も他のダイアログも、このクラスの中へは public の関数でしか
// 触らない（往復の配線も PollOpen / TakeReturn のような関数を通す）。

#ifndef MXV2_SETTINGSUI_CAST_H
#define MXV2_SETTINGSUI_CAST_H

#include "imgui.h"

#include "settingsui_dialog.h"

namespace mxv2 {

class SettingsUi;
struct Settings;

class CastDialog : public ModalDialog {
public:
	CastDialog()
	    : wasOpen_(false), selected_(-1), lastSize_(0.0f, 0.0f), recenter_(false),
	      scrollY_(false) {}

	void Open() { show_ = true; }
	// SettingsUi::CloseDialog() 用。開いていれば閉じて true。
	bool CloseTop() { return CloseSelf(); }
	// 1 フレームぶん。設定ウィンドウが閉じていても毎フレーム呼ぶ。
	void Build(SettingsUi &ui, Settings *settings);

private:
	bool wasOpen_;     // 前のフレームで開いていた（開いた・閉じた瞬間を拾う）
	int selected_;     // 一覧で選んでいる行
	ImVec2 lastSize_;  // 前のフレームのダイアログの大きさ（変わったら中央へ置き直す）
	bool recenter_;    // 次のフレームで中央へ置き直す
	bool scrollY_;     // 前のフレームで縦のスクロールバーが出ていた（その幅を空ける）
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_CAST_H
