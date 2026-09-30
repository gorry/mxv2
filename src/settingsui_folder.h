// mxv2 - 設定 UI: フォルダを選ぶダイアログ (L)
//
// 旧 mxv の MX_GetNewDirFileList にあたる。原典は SHBrowseForFolder だったが
// Windows 専用なので、パスの打ち込みとフォルダ一覧を持つ自前のダイアログに
// してある。選んだ結果の行き先は 3 つ（Target）。ブックマークの設定 / PDX の
// 探索先の [追加…] からは、モーダルを入れ子にせず、呼び出し元が閉じきってから
// 出し、閉じたら returnTo_ の呼び出し元を開き直す（配線は SettingsUi::Build）。
// 実装は settingsui_folder.cpp。クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_FOLDER_H
#define MXV2_SETTINGSUI_FOLDER_H

#include <cstdint>
#include <string>
#include <vector>

#include "settingsui_dialog.h"

namespace mxv2 {

class DirLister;
class Filer;
class SettingsUi;
struct Settings;

class FolderDialog : public ModalDialog {
public:
	// 選んだ結果の行き先。
	enum class Target {
		Filer = 0,  // ファイラーを動かす (L キー / メニュー)
		Pdx,        // PDX の探索先に足す ([PDX の探索先] の [追加…])
		Bookmark,   // ブックマークに足す ([ブックマークの設定] の [追加])
	};
	// 閉じたら開き直す呼び出し元。
	enum class Return {
		None = 0,
		Bookmarks,
		Pdx,
	};

	FolderDialog();
	~FolderDialog();

	// ファイラーを動かすために開く。dir は最初に見せるフォルダの ref
	// （ふつうはファイラーの今の場所）。
	void Open(SettingsUi &ui, const std::string &dir);
	// [追加…] から。呼び出し元が閉じきったら SettingsUi::Build が開く。
	// 呼び出し元は自分を閉じること。
	void OpenFor(Target target, Return returnTo) {
		target_ = target;
		returnTo_ = returnTo;
		openPending_ = true;
	}
	// ダイアログの題名。"###" 以降が ImGui の id なので、見出しを変えても
	// 同じポップアップとして扱われる。
	const char *Title() const;
	// 一覧を読んでいるスレッドの手を離させる（Vfs の一覧を触る前に。
	// FileSystemsDialog::QuiesceVfsReaders から）。
	void Quiesce();
	// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
	void SetDir(SettingsUi &ui, const std::string &dir);
	void Build(SettingsUi &ui, Settings *settings, Filer *filer);

private:
	friend class SettingsUi;

	// 子フォルダ（表示名と ref）。ファイルシステムの選択のときはマウントされて
	// いる FS が並ぶ。
	struct FolderEntry {
		std::string name;
		std::string ref;
	};

	// 子フォルダの一覧だけ作り直す（入力欄には触らない）。**読むのは
	// 別スレッド**なので、中身が入るのはあとのフレーム (PollDir)。
	void Relist(SettingsUi &ui, const std::string &dir);
	// 別スレッドが読み終えた中身を取り込む。ダイアログを組み立てる前に
	// 毎フレーム呼ぶこと。開けなかったときは元の場所へ戻す。
	void PollDir(SettingsUi &ui);
	// 一覧で選んだものを入力欄へ移す。中へは入らない（そこはダブルクリック）。
	void SelectEntry(const std::string &path);

	Target target_;
	Return returnTo_;
	bool openPending_;      // 呼び出し元が閉じきったら開く（OpenFor）
	// 一覧に出しているフォルダの ref。空ならファイルシステムの選択。
	std::string dir_;
	std::string selected_;  // 一覧で選ばれている行の ref
	std::vector<FolderEntry> entries_;
	// 一覧は別スレッドで読む。外部ファイルシステムでは 1 回の List に
	// 通信が要るので、打ち込むたびにここで待つと入力ごと固まる。
	DirLister *lister_;
	bool loading_;
	uint32_t ticks_;        // 読み始めた時刻
	// 開けなかったときの戻り先（打ち込みの途中は開けない場所を通るので、
	// そのたびに一覧が消えないよう、読めるまで前のものを出しておく）。
	bool hasPrev_;
	std::string prevDir_;
	std::string prevSelected_;
	std::vector<FolderEntry> prevEntries_;
	std::string error_;     // 開けなかったときの文言
	char pathBuf_[512];     // パスの入力欄

	FolderDialog(const FolderDialog &);
	FolderDialog &operator=(const FolderDialog &);
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_FOLDER_H
