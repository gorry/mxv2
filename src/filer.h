// mxv2 - ファイラー（旧 mxv/Filer.cpp の移植）
//
// カレントディレクトリの中身を「.. / MDX ファイル / ディレクトリ / ドライブ」
// の順に並べたリストを持つ。並び順と既定値は旧 mxv に合わせてある
// (FolderFirst=0 のとき MDX が先)。
//
// 場所は全て **ref**（"localfs:C:\mdx" のような文字列）で持ち、実際の
// 読み書きは Vfs 経由で行う。裸のパスをここへ持ち込まないこと（vfs.h）。
//
// MDX のタイトルはファイル先頭から取り出して UTF-8 にしておく。**読むのは
// 別スレッド**で、届いたぶんを PollTitles() で一覧へ入れる（旧 mxv も別
// スレッドだった）。フォルダの中の MDX を全部開くことになるので、外部
// ファイルシステムでは 1 曲 1 リクエストになり、メインスレッドで回すと
// 画面が止まってしまう。

#ifndef MXV2_FILER_H
#define MXV2_FILER_H

#include <string>
#include <vector>

namespace mxv2 {

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
};

class Filer {
public:
	Filer();
	~Filer();

	// 読み書きに使う VFS。一覧を作る前に必ず渡すこと。
	void SetVfs(const Vfs *vfs) { vfs_ = vfs; }

	// カレントディレクトリを設定して一覧を作り直す。
	void SetCurrentRef(const std::string &ref);
	const std::string &currentRef() const { return currentRef_; }
	// 今いるファイルシステム。
	const FileSystem *fs() const { return fs_; }

	// 一覧を作り直す（タイトルの読み直しもここから始まる）。
	void Refresh();

	// 別スレッドが読み終えたタイトルを一覧へ入れる。毎フレーム呼ぶこと。
	// 入れるものがあれば true（ファイラーを描き直す合図）。
	bool PollTitles();

	// 読みかけを捨てて、タイトル読みのスレッドが手を離すまで待つ。
	// **ファイルシステムを取り外す前に呼ぶこと**（読んでいる最中に
	// 実体が消えると落ちる）。
	void WaitTitles();

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
	bool SelectByPath(const std::string &ref);

private:
	// ファイルシステムの選択（ref が空のとき）の一覧。
	void AppendFileSystems(std::vector<FileItem> *out);
	void AppendDirs(std::vector<FileItem> *out);
	void AppendMdx(std::vector<FileItem> *out);
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

	Filer(const Filer &);
	Filer &operator=(const Filer &);
};

}  // namespace mxv2

#endif  // MXV2_FILER_H
