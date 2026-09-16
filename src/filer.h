// mxv2 - ファイラー（旧 mxv/Filer.cpp の移植）
//
// カレントディレクトリの中身を「.. / MDX ファイル / ディレクトリ / ドライブ」
// の順に並べたリストを持つ。並び順と既定値は旧 mxv に合わせてある
// (FolderFirst=0 のとき MDX が先)。
//
// 場所は全て **ref**（"localfs:C:\mdx" のような文字列）で持ち、実際の
// 読み書きは Vfs 経由で行う。裸のパスをここへ持ち込まないこと（vfs.h）。
//
// **フォルダの読み出しとタイトルの読み出しは、どちらも別スレッド**で行う。
// 外部ファイルシステム (SAF / Web / SMB) では 1 回ごとに通信が要るので、
// メインスレッドで回すと開いた瞬間に画面が固まる。
//   - フォルダの中身 … DirLister。届いたら PollDir() が一覧を組み立てる
//   - MDX のタイトル  … TitleReader。届いたぶんを PollTitles() が入れる
// どちらも毎フレーム呼ぶこと。読めている間の一覧は「先頭の 1 行だけ」で、
// 読むのに手間取っているときだけ「読み込み中」の行が足される。

#ifndef MXV2_FILER_H
#define MXV2_FILER_H

#include <cstdint>
#include <string>
#include <vector>

#include "fileutil.h"

namespace mxv2 {

class DirLister;
class FileSystem;
class TitleReader;
class Vfs;

enum FileItemType {
	kFileItemDir = 1,
	kFileItemDrive = 2,
	kFileItemMdx = 4,
	// ファイルシステム。選択画面の 1 行と、各 FS のルートに出る "[FS]"
	// （どちらも開くとファイルシステムの選択へ行く／から行く）。
	kFileItemFileSystem = 8,
	// 選択画面の末尾の "[Setting]"。開くと設定ダイアログ。
	kFileItemSetting = 16,
	// 読み込みに手間取っているときだけ出る「読み込み中」の行。
	// 開いても何も起きない。
	kFileItemLoading = 32,
	// ブックマークの一覧 ("Bookmarks>" の中) の 1 行。path は行き先の ref
	// （他のファイルシステムを指す）。開くとそこへ移る。
	kFileItemBookmark = 64,
	// ブックマークの一覧の末尾の "[Setting]"。開くとブックマークの設定。
	kFileItemBookmarkSetting = 128,
};

struct FileItem {
	std::string baseName;  // 表示名 (UTF-8)
	std::string path;      // ref (UTF-8)
	std::string title;     // MDX のタイトル (UTF-8)。それ以外は空
	int type;

	FileItem() : type(0) {}
};

// Filer::Open() の結果。
enum FilerOpen {
	kFilerOpenNone = 0,
	kFilerOpenPlay,      // playPath に曲の ref が入っている
	kFilerOpenMoved,     // 場所が変わった
	kFilerOpenSettings,  // ファイルシステムの設定を開いてほしい
	kFilerOpenBookmark,  // playPath のブックマークへ移ってほしい
	kFilerOpenBookmarkSettings,  // ブックマークの設定を開いてほしい
	// playPath のファイルシステム（mountRef）はアクセス許可が失われている。
	// [ファイルシステムの設定] を開いて取り直させてほしい（SAF。vfs.h の accessible）。
	kFilerOpenNeedsAccess,
};

class Filer {
public:
	Filer();
	~Filer();

	// 読み書きに使う VFS。一覧を作る前に必ず渡すこと。
	void SetVfs(const Vfs *vfs) { vfs_ = vfs; }

	// カレントディレクトリを設定して一覧を作り直す。**中身が届くのは
	// あとのフレーム**（PollDir が組み立てる）。currentRef() だけは
	// すぐ変わるので、場所の表示や記録はそのまま使ってよい。
	void SetCurrentRef(const std::string &ref);
	const std::string &currentRef() const { return currentRef_; }
	// 今いるファイルシステム。
	const FileSystem *fs() const { return fs_; }

	// 場所は変えずに一覧を読み直す。カーソルとスクロール位置は保つ。
	void Refresh();

	// 読み込み中か（一覧がまだ揃っていない）。
	bool loading() const { return loading_; }

	// 別スレッドが読み終えたフォルダの中身を一覧へ組み立てる。毎フレーム
	// 呼ぶこと。一覧が変わったら true（ファイラーを描き直す合図）。
	// **開けなかったときは元の場所へ戻す**ので、読めないドライブに入って
	// しまうことはない。
	bool PollDir();

	// 別スレッドが読み終えたタイトルを一覧へ入れる。毎フレーム呼ぶこと。
	// 入れるものがあれば true（ファイラーを描き直す合図）。
	bool PollTitles();

	// 読みかけを捨てて、読み出しのスレッドが手を離すまで待つ。
	// **ファイルシステムを取り外す前に呼ぶこと**（読んでいる最中に
	// 実体が消えると落ちる）。呼んだあとは Refresh() で読み直すこと。
	void WaitIo();

	int itemCount() const { return (int)items_.size(); }
	const FileItem &item(int i) const { return items_[i]; }

	int cursor() const { return cursor_; }
	void SetCursor(int i);
	void MoveCursor(int delta);

	// 先頭に見えている項目と、そこからのずれ (px)。
	//
	// スクロール位置は画素で持つ。ドラッグを指に追従させるためで、
	// 行番号しか要らない側は今までどおり top() / SetTop() を使えばよい
	// （SetTop はずれを 0 に戻すので、キー移動・ホイール・スクロールバーは
	// 自動的に行の切れ目に揃う）。
	int top() const { return topPx_ / rowHeightPx_; }
	int topOffsetPx() const { return topPx_ % rowHeightPx_; }
	void SetTop(int t);
	void SetTopPx(int px);
	int topPx() const { return topPx_; }
	// スクロールできる最大の画素位置。
	int maxTopPx() const;

	// 画面に見えている行数と 1 行の高さ (px)。スクロール量の計算に使う。
	void SetViewMetrics(int rows, int rowHeightPx);
	int visibleRows() const { return visibleRows_; }

	// カーソルが画面外に出ていたら top を調整する。
	void EnsureCursorVisible();

	// カーソル位置の項目を「開く」。何が起きたかを返す。
	// MDX のときだけ playPath にその ref が入る。
	FilerOpen Open(std::string *playPath);

	// 次 / 前の MDX へカーソルを進めてその ref を返す。無ければ false。
	bool NextMdx(std::string *playPath);
	bool PrevMdx(std::string *playPath);

	// 親ディレクトリへ。ファイルシステムのルートに居るときは
	// 「ファイルシステムの選択」へ抜ける。
	void GoParent();

	// 今いるファイルシステムのルートへ（旧 mxv の "\" キー）。
	void GoRoot();

	// フォルダを先に並べるか（旧 mxv の Filer/FolderFirst、既定 0）。
	void SetFolderFirst(bool on);

	// リストの中で ref と一致する項目にカーソルを合わせる。
	// **読み込み中なら覚えておいて、一覧が届いてから合わせる**ので、
	// SetCurrentRef の直後に呼んでよい。
	bool SelectByPath(const std::string &ref);

private:
	// 読み込みの前後で一覧を丸ごと控えておく入れ物。開けなかったときに
	// ここへ戻す（読めないドライブを選んでも、それまでの場所に留まる）。
	// **FileSystem * は持たない。** ファイルシステムを取り外したあとに
	// 戻すことがあるので、場所は ref だけ控えて、戻すときに引き直す。
	struct View {
		std::string ref;
		std::vector<FileItem> items;
		int cursor;
		int topPx;
		bool valid;

		View() : cursor(0), topPx(0), valid(false) {}
	};

	// 読み込みを始める / 届いた中身で一覧を組み立てる / 読み終わりの後始末。
	void BeginLoad(bool resetCursor);
	void BuildItems(const std::vector<DirEntry> &entries);
	void FinishLoad();

	void SaveView();
	void RestoreView();

	// 先頭の行（".." またはルートの "[FS]"）。読み込み中でもこれだけは出す。
	void AppendParentRow(std::vector<FileItem> *out);
	// ファイルシステムの選択（ref が空のとき）の一覧。
	void AppendFileSystems(std::vector<FileItem> *out);
	// ブックマークの一覧（FileSystem::isJumpList() の中）。
	void AppendBookmarks(std::vector<FileItem> *out);
	void AppendDirs(const std::vector<DirEntry> &entries, std::vector<FileItem> *out);
	void AppendMdx(const std::vector<DirEntry> &entries, std::vector<FileItem> *out);
	void AppendExtras(std::vector<FileItem> *out);
	// 今の一覧のタイトルを読み直させる（読むのは別スレッド）。
	void StartReadTitles();

	const Vfs *vfs_;
	const FileSystem *fs_;  // 今いるファイルシステム
	std::string rel_;       // その中での位置
	std::string currentRef_;
	std::vector<FileItem> items_;
	int cursor_;
	int topPx_;        // スクロール位置 (画素)
	int rowHeightPx_;  // 1 行の高さ。0 にはしない（除算に使う）
	int visibleRows_;
	bool folderFirst_;
	TitleReader *titles_;
	DirLister *lister_;

	View saved_;                 // 開けなかったときの戻り先
	bool loading_;               // 一覧がまだ届いていない
	bool loadingRow_;            // 「読み込み中」の行を足したか
	uint32_t loadTicks_;         // 読み始めた時刻 (SDL_GetTicks)
	bool resetCursor_;           // 届いたらカーソルを頭に戻すか
	int keepCursor_;             // 戻さないときの控え
	int keepTopPx_;
	std::string pendingSelect_;  // 届いてから合わせる ref
	bool hasPendingSelect_;

	Filer(const Filer &);
	Filer &operator=(const Filer &);
};

}  // namespace mxv2

#endif  // MXV2_FILER_H
