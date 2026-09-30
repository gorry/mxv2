// mxv2 - 設定 UI: [PDX の探索先] ダイアログ
//
// 設定ウィンドウの [編集…] から開く。作りはブックマークの設定と同じ
// （一覧・[上へ][下へ][追加…][削除]、削除は入れ子の確認）で、[開く] は無い。
// モーダル同士は入れ子にしないので、設定ウィンドウが閉じきってから開き
// （handoff_.openPending）、閉じたら設定ウィンドウを開き直す
// （handoff_.returnToCaller。[追加…] のフォルダ選択の往復をまたいでも保つ。
// 配線は SettingsUi::Build）。2026-09-16、ユーザーの指示（それまでは設定
// ウィンドウの打ち込み欄 1 本）。実装は settingsui_pdx.cpp。
// クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_PDX_H
#define MXV2_SETTINGSUI_PDX_H

#include <string>
#include <vector>

#include "settingsui_dialog.h"

namespace mxv2 {

class Filer;
class SettingsUi;
struct Settings;

class PdxPathsDialog : public ModalDialog {
public:
	PdxPathsDialog() : selected_(0) {}

	// ModalDialog::CloseTop を隠す。入れ子の確認を先に閉じる。
	bool CloseTop() {
		if (remove_.CloseTop()) return true;
		if (!show_) return false;
		show_ = false;
		return true;
	}
	// 設定ウィンドウの [編集…] から。設定ウィンドウを閉じ、閉じきったら開く。
	void OpenFromSettings(SettingsUi &ui);
	// フォルダ選択で選んだ ref を、一覧で選んでいた行の位置（無ければ末尾）へ
	// 挿し、その行を選ぶ（FolderDialog から）。
	void Insert(std::vector<std::string> *list, const std::string &ref);
	void Build(SettingsUi &ui, Settings *settings, Filer *filer);

private:
	friend class SettingsUi;

	// 「本当に削除するか」。このダイアログの中に入れ子で開く。
	void BuildRemoveWindow(SettingsUi &ui, Settings *settings);

	Handoff handoff_;     // 設定ウィンドウとの往復
	int selected_;        // 一覧で選んでいる行。空のときは -1
	std::string error_;
	NestedPopup remove_;  // 削除の確認
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_PDX_H
