// mxv2 - ファイラ（旧 mxv/Filer.cpp の移植）
//
// カレントディレクトリの中身を「.. / MDX ファイル / ディレクトリ / ドライブ」
// の順に並べたリストを持つ。並び順と既定値は旧 mxv に合わせてある
// (FolderFirst=0 のとき MDX が先)。
//
// MDX のタイトルは読み込み時にファイル先頭から取り出して UTF-8 にしておく。
// 旧 mxv は別スレッドで少しずつ埋めていたが、mxv2 はまとめて読む。

#ifndef MXV2_FILER_H
#define MXV2_FILER_H

#include <string>
#include <vector>

namespace mxv2 {

enum FileItemType {
	kFileItemDir = 1,
	kFileItemDrive = 2,
	kFileItemMdx = 4,
};

struct FileItem {
	std::string baseName;  // 表示名 (UTF-8)
	std::string path;      // フルパス (UTF-8)
	std::string title;     // MDX のタイトル (UTF-8)。それ以外は空
	int type;

	FileItem() : type(0) {}
};

class Filer {
public:
	Filer();

	// カレントディレクトリを設定して一覧を作り直す。
	void SetCurrentDir(const std::string &dir);
	const std::string &currentDir() const { return currentDir_; }

	// 一覧を作り直す（タイトルも読み直す）。
	void Refresh();

	int itemCount() const { return (int)items_.size(); }
	const FileItem &item(int i) const { return items_[i]; }

	int cursor() const { return cursor_; }
	void SetCursor(int i);
	void MoveCursor(int delta);

	int top() const { return top_; }
	void SetTop(int t);

	// 画面に見えている行数。スクロール量の計算に使う。
	void SetVisibleRows(int rows);
	int visibleRows() const { return visibleRows_; }

	// カーソルが画面外に出ていたら top を調整する。
	void EnsureCursorVisible();

	// カーソル位置の項目を「開く」。
	//   ディレクトリ / ドライブなら移動して true を返す (playPath は空)。
	//   MDX ならそのパスを playPath に入れて true を返す。
	bool Open(std::string *playPath);

	// 次 / 前の MDX へカーソルを進めてそのパスを返す。無ければ false。
	bool NextMdx(std::string *playPath);
	bool PrevMdx(std::string *playPath);

	// 親ディレクトリへ。
	void GoParent();

	// フォルダを先に並べるか（旧 mxv の Filer/FolderFirst、既定 0）。
	void SetFolderFirst(bool on);

	// リストの中で path と一致する項目にカーソルを合わせる。
	bool SelectByPath(const std::string &path);

private:
	void AppendDirs(std::vector<FileItem> *out);
	void AppendMdx(std::vector<FileItem> *out);
	void AppendDrives(std::vector<FileItem> *out);
	void ReadTitles();

	std::string currentDir_;
	std::vector<FileItem> items_;
	int cursor_;
	int top_;
	int visibleRows_;
	bool folderFirst_;
};

}  // namespace mxv2

#endif  // MXV2_FILER_H
