// mxv2 - 設定 UI: [ブックマークの設定] ダイアログ (F4) と Shift+M の確認
//
// 控えるのはフォルダの ref で、実体は Settings::bookmarks（ファイルシステムの
// 設定と違って Vfs 側には持たない。UI が直に触っても設定と食い違わないように
// するため）。管理が主で、ジャンプもできる。実装は settingsui_bookmarks.cpp。
// ref を控えられるか (SettingsUi::CanBookmark) と一覧の中を探す
// (SettingsUi::FindBookmark) は、フォルダ選択やメニューからも使うので
// SettingsUi の持ち物。クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_BOOKMARKS_H
#define MXV2_SETTINGSUI_BOOKMARKS_H

#include <string>
#include <vector>

#include "settingsui_dialog.h"

namespace mxv2 {

class Filer;
class SettingsUi;
struct Settings;

class BookmarksDialog : public ModalDialog {
public:
	BookmarksDialog() : selected_(-1), jumpPending_(false) {}

	void Open() {
		show_ = true;
		selected_ = 0;
		error_.clear();
	}
	// ModalDialog::CloseTop を隠す。入れ子の確認を先に閉じる。
	bool CloseTop() {
		if (remove_.CloseTop()) return true;
		if (toggle_.CloseTop()) return true;  // 単独で開くが、閉じ方は同じ
		if (!show_) return false;
		show_ = false;
		return true;
	}
	// ファイラーの "Bookmarks>" で選んだブックマークへ、次の Build() で移る。
	void RequestJump(const std::string &ref) {
		jumpRef_ = ref;
		jumpPending_ = true;
	}
	// カレントフォルダをブックマークへ追加する / から削除する (Shift+M) の
	// 確認を次のフレームで開く。ダイアログを開かずにメイン画面から直に使う
	// ので、確認だけが単独のモーダルとして出る。
	void AskToggle() { toggle_.ask = true; }
	bool toggleOpen() const { return toggle_.open; }

	// RequestJump の行き先へ移る。OpenBookmark と同じだが、開けないときは
	// そのままファイラーに開かせて、あちらの「開けなければ元の場所に留まる」に
	// 任せる（ダイアログは出ていないので error_ は見せられない）。
	void JumpToRef(SettingsUi &ui, Settings *settings, const std::string &ref);
	// フォルダ選択で選んだ ref を、一覧で選んでいた行の位置（無ければ末尾）へ
	// 挿し、その行を選ぶ（FolderDialog から）。
	void Insert(std::vector<std::string> *list, const std::string &ref);
	void Build(SettingsUi &ui, Settings *settings, Filer *filer);
	// Shift+M の確認。メイン画面から単独で開くので、入れ子の削除確認とは
	// ポップアップの id を分けてある（同じ id を 2 か所から開こうとすると
	// 開き直しに失敗する）。
	void BuildToggleWindow(SettingsUi &ui, Settings *settings, Filer *filer);

private:
	friend class SettingsUi;

	// index のブックマークを開く。ファイルを指していたら「そのファイルの
	// あるフォルダ」へ直してから開く（開けなければ error_ に理由）。
	void OpenBookmark(SettingsUi &ui, Settings *settings, int index);
	// 削除の確認。このダイアログの中に入れ子で開く。
	void BuildRemoveWindow(SettingsUi &ui, Settings *settings);

	int selected_;           // 一覧で選んでいる行。空のときは -1
	std::string error_;      // 「そのフォルダは見つかりません。」など
	NestedPopup remove_;     // 削除の確認（ダイアログの中に入れ子）
	NestedPopup toggle_;     // Shift+M の確認（メイン画面から単独で開く）
	std::string toggleRef_;  // Shift+M の確認にかけている場所
	bool jumpPending_;       // 次の Build() で jumpRef_ へ移る
	std::string jumpRef_;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_BOOKMARKS_H
