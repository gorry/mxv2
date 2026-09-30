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

	// SettingsUi::CloseDialog() 用。入れ子の確認を先に閉じる。
	bool CloseTop() {
		if (remove_.CloseTop()) return true;
		return CloseSelf();
	}
	// 設定ウィンドウの [編集…] から。設定ウィンドウを閉じ、閉じきったら開く。
	void OpenFromSettings(SettingsUi &ui);
	// 設定ウィンドウとの往復（SettingsUi::Build から毎フレーム）。PollOpen は
	// 設定ウィンドウが閉じきったら開く。TakeReturn は自分が閉じきったら 1 度だけ
	// true を返す（呼んだ側が設定ウィンドウを開き直す）。folderBusy の間
	// （[追加…] のフォルダ選択へ出ている間と、そこから戻ってくる途中）は返さない。
	void PollOpen();
	bool TakeReturn(bool folderBusy);
	// 一覧で選んでいる行。空のときは -1（FolderDialog が最初に見せる場所に使う）。
	int selected() const { return selected_; }
	// フォルダ選択で選んだ ref を、一覧で選んでいた行の位置（無ければ末尾）へ
	// 挿し、その行を選ぶ（FolderDialog から）。
	void Insert(std::vector<std::string> *list, const std::string &ref);
	void Build(SettingsUi &ui, Settings *settings, Filer *filer);

private:
	// 「本当に削除するか」。このダイアログの中に入れ子で開く。
	void BuildRemoveWindow(SettingsUi &ui, Settings *settings);

	Handoff handoff_;     // 設定ウィンドウとの往復
	int selected_;        // 一覧で選んでいる行。空のときは -1
	std::string error_;
	NestedPopup remove_;  // 削除の確認
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_PDX_H
