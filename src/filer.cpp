// mxv2 - ファイラー（旧 mxv/Filer.cpp の移植）

#include "filer.h"

#include <algorithm>
#include <cstring>

#include "fileutil.h"
#include "mdxsong.h"
#include "message.h"
#include "text.h"
#include "vfs.h"

#include <mdx_util.h>

namespace mxv2 {

namespace {

// 並び順は「見た目の順」なので、どのファイルシステムでも大文字小文字を
// 無視して並べる（同じものかどうかの判定は FileSystem::SamePath）。
bool LessNoCase(const FileItem &a, const FileItem &b) {
	return CompareNoCase(a.baseName, b.baseName) < 0;
}

}  // namespace

Filer::Filer()
    : vfs_(0),
      fs_(0),
      cursor_(0),
      topPx_(0),
      rowHeightPx_(1),
      visibleRows_(11),
      folderFirst_(false) {}

void Filer::SetFolderFirst(bool on) {
	folderFirst_ = on;
}

void Filer::SetCurrentRef(const std::string &ref) {
	fs_ = 0;
	rel_.clear();
	currentRef_.clear();
	if (vfs_ != 0) {
		FileSystem *fs = 0;
		std::string rel;
		if (vfs_->Parse(ref, &fs, &rel) && fs != 0) {
			fs_ = fs;
			rel_ = rel;
			currentRef_ = Vfs::MakeRef(fs, rel);
		}
	}
	Refresh();
	topPx_ = 0;
	// 旧 mxv と同じく、開いた直後は 1 番目（".." の次）にカーソルを置く。
	// ファイルシステムの選択には ".." が無いので、そちらは先頭。
	cursor_ = (fs_ == 0) ? 0 : std::min((int)items_.size() - 1, 1);
	if (cursor_ < 0) cursor_ = 0;
}

void Filer::Refresh() {
	items_.clear();
	if (vfs_ == 0) return;

	// ファイルシステムの選択。画面固定の行ではなく「ref が空のときの一覧」
	// として作るので、カーソルもスクロールも当たり判定もそのまま使える。
	if (fs_ == 0) {
		AppendFileSystems(&items_);
		if (cursor_ >= (int)items_.size()) cursor_ = (int)items_.size() - 1;
		if (cursor_ < 0) cursor_ = 0;
		EnsureCursorVisible();
		return;
	}

	// 先頭は必ず親ディレクトリ。ルートでは「ファイルシステムの選択」へ抜ける
	// 行になる（旧 mxv はここが "\" で、押しても何も起きなかった）。
	{
		FileItem f;
		const bool atRoot = fs_->IsRoot(rel_);
		f.baseName = atRoot ? "[FS]" : "..";
		f.path = atRoot ? std::string() : Vfs::MakeRef(fs_, fs_->Parent(rel_));
		f.title = fs_->DisplayPath(rel_);
		f.type = atRoot ? kFileItemFileSystem : kFileItemDir;
		items_.push_back(f);
	}

	if (folderFirst_) {
		AppendDirs(&items_);
		AppendMdx(&items_);
	} else {
		AppendMdx(&items_);
		AppendDirs(&items_);
	}
	AppendExtras(&items_);

	ReadTitles();

	if (cursor_ >= (int)items_.size()) cursor_ = (int)items_.size() - 1;
	if (cursor_ < 0) cursor_ = 0;
	EnsureCursorVisible();
}

// ファイルシステムの選択。並び順は [ファイルシステムの設定] で決めたもの。
void Filer::AppendFileSystems(std::vector<FileItem> *out) {
	for (int i = 0; i < vfs_->count(); i++) {
		const FileSystem *fs = vfs_->at(i);
		if (!fs->available()) continue;  // Android のローカル FS など
		FileItem f;
		f.baseName = fs->prefix();
		f.title = fs->label();
		f.path = Vfs::MakeRef(fs, fs->Root());
		f.type = kFileItemFileSystem;
		out->push_back(f);
	}
	{
		FileItem f;
		f.baseName = "[Setting]";
		f.title = Msg("Filer.SettingTitle");
		f.type = kFileItemSetting;
		out->push_back(f);
	}
}

void Filer::AppendDirs(std::vector<FileItem> *out) {
	std::vector<DirEntry> entries;
	if (!fs_->List(rel_, &entries)) return;

	std::vector<FileItem> dirs;
	for (size_t i = 0; i < entries.size(); i++) {
		if (!entries[i].isDir) continue;
		FileItem f;
		f.baseName = entries[i].name;
		f.path = Vfs::MakeRef(fs_, fs_->Join(rel_, entries[i].name));
		f.type = kFileItemDir;
		dirs.push_back(f);
	}
	std::sort(dirs.begin(), dirs.end(), LessNoCase);
	out->insert(out->end(), dirs.begin(), dirs.end());
}

void Filer::AppendMdx(std::vector<FileItem> *out) {
	std::vector<DirEntry> entries;
	if (!fs_->List(rel_, &entries)) return;

	std::vector<FileItem> files;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].isDir) continue;
		// 一覧では拡張子だけで判断する（中身まで見ると全部読むことになる）。
		if (!IsMdxFileName(entries[i].name)) continue;
		FileItem f;
		f.baseName = entries[i].name;
		f.path = Vfs::MakeRef(fs_, fs_->Join(rel_, entries[i].name));
		f.type = kFileItemMdx;
		files.push_back(f);
	}
	std::sort(files.begin(), files.end(), LessNoCase);
	out->insert(out->end(), files.begin(), files.end());
}

// ファイルシステムが足す項目（ローカル FS のドライブ一覧）。
void Filer::AppendExtras(std::vector<FileItem> *out) {
	std::vector<FsExtraItem> extras;
	fs_->AppendExtraItems(rel_, &extras);
	for (size_t i = 0; i < extras.size(); i++) {
		FileItem f;
		f.baseName = extras[i].name;
		f.path = Vfs::MakeRef(fs_, extras[i].rel);
		f.type = kFileItemDrive;
		out->push_back(f);
	}
}

void Filer::ReadTitles() {
	for (size_t i = 0; i < items_.size(); i++) {
		if ((items_[i].type & kFileItemMdx) == 0) continue;

		std::vector<uint8_t> data;
		if (!vfs_->Read(items_[i].path, &data) || data.empty()) continue;

		char title[512];
		if (!MdxGetTitle(&data[0], (uint32_t)data.size(), title, sizeof(title))) continue;
		items_[i].title = SjisToUtf8(TrimTrailingControl(std::string(title)));
	}
}

void Filer::SetCursor(int i) {
	if (items_.empty()) {
		cursor_ = 0;
		return;
	}
	if (i < 0) i = 0;
	if (i >= (int)items_.size()) i = (int)items_.size() - 1;
	cursor_ = i;
	EnsureCursorVisible();
}

void Filer::MoveCursor(int delta) {
	SetCursor(cursor_ + delta);
}

int Filer::maxTopPx() const {
	const int maxTop = std::max(0, (int)items_.size() - visibleRows_);
	return maxTop * rowHeightPx_;
}

// 行単位の指定。端数は落とすので、キー移動・ホイール・スクロールバーは
// 必ず行の切れ目に揃う。
void Filer::SetTop(int t) {
	SetTopPx(t * rowHeightPx_);
}

void Filer::SetTopPx(int px) {
	const int maxPx = maxTopPx();
	if (px < 0) px = 0;
	if (px > maxPx) px = maxPx;
	topPx_ = px;
}

void Filer::SetViewMetrics(int rows, int rowHeightPx) {
	// 行の高さが変わると画素位置の意味も変わるので、今の先頭項目を保って
	// 測り直す（文字サイズの切り替えで表示が飛ばないように）。
	const int keepTop = top();
	visibleRows_ = (rows > 0) ? rows : 1;
	rowHeightPx_ = (rowHeightPx > 0) ? rowHeightPx : 1;
	SetTop(keepTop);
	EnsureCursorVisible();
}

void Filer::EnsureCursorVisible() {
	const int top = topPx_ / rowHeightPx_;
	if (cursor_ < top) {
		SetTop(cursor_);
	} else if (cursor_ >= top + visibleRows_) {
		SetTop(cursor_ - visibleRows_ + 1);
	} else if (topOffsetPx() != 0) {
		// 端数が残っていると先頭と末尾の行が欠けて見える。カーソルを
		// 動かしたときは行に揃え直す。
		SetTop(top);
	} else {
		SetTopPx(topPx_);  // 項目が減ったときの詰め直し
	}
}

FilerOpen Filer::Open(std::string *playPath) {
	playPath->clear();
	if (items_.empty()) return kFilerOpenNone;
	const FileItem f = items_[cursor_];  // SetCurrentRef が items_ を作り直す

	if (f.type & kFileItemMdx) {
		*playPath = f.path;
		return kFilerOpenPlay;
	}
	if (f.type & kFileItemSetting) {
		return kFilerOpenSettings;
	}
	if (f.type & kFileItemFileSystem) {
		// 選択画面の 1 行ならその FS のルートへ、ルートの "[FS]" なら
		// 選択画面へ（path が空）。
		const std::string leaving = currentRef_;
		SetCurrentRef(f.path);
		if (f.path.empty()) SelectByPath(leaving);
		return kFilerOpenMoved;
	}
	if (f.type & (kFileItemDir | kFileItemDrive)) {
		if (!vfs_->IsDir(f.path)) return kFilerOpenNone;
		SetCurrentRef(f.path);
		return kFilerOpenMoved;
	}
	return kFilerOpenNone;
}

void Filer::GoParent() {
	if (fs_ == 0) return;  // 選択画面より上は無い
	const std::string leaving = currentRef_;
	// ルートの 1 つ上は「ファイルシステムの選択」(ref が空)。
	SetCurrentRef(fs_->IsRoot(rel_) ? std::string()
	                                : Vfs::MakeRef(fs_, fs_->Parent(rel_)));
	// 出てきたディレクトリ（またはファイルシステム）にカーソルを合わせる
	SelectByPath(leaving);
}

void Filer::GoRoot() {
	if (fs_ == 0 || fs_->IsRoot(rel_)) return;
	// 旧 mxv はカレントを 3 文字 ("C:\") へ切り詰めていたが、それだと
	// Windows のドライブ名前提になる。変わらなくなるまで親を辿れば
	// 他のプラットフォームでも同じ場所に行き着く。
	std::string rel = rel_;
	for (int i = 0; i < 64; i++) {
		const std::string up = fs_->Parent(rel);
		if (up == rel) break;
		rel = up;
		if (fs_->IsRoot(rel)) break;
	}
	const std::string leaving = currentRef_;
	SetCurrentRef(Vfs::MakeRef(fs_, rel));
	SelectByPath(leaving);
}

bool Filer::SelectByPath(const std::string &ref) {
	if (vfs_ == 0) return false;
	FileSystem *wantFs = 0;
	std::string wantRel;
	if (!vfs_->Parse(ref, &wantFs, &wantRel) || wantFs == 0) return false;

	// 選択画面には ".." が無いので先頭から見る。そちらはファイルシステムが
	// 合っていればよい（どこから戻ってきても、その FS の行に合わせたい）。
	const bool picking = (fs_ == 0);
	for (size_t i = picking ? 0 : 1; i < items_.size(); i++) {
		FileSystem *haveFs = 0;
		std::string haveRel;
		if (!vfs_->Parse(items_[i].path, &haveFs, &haveRel)) continue;
		if (haveFs != wantFs) continue;
		if (!picking && !wantFs->SamePath(haveRel, wantRel)) continue;
		SetCursor((int)i);
		return true;
	}
	return false;
}

bool Filer::NextMdx(std::string *playPath) {
	for (int i = cursor_ + 1; i < (int)items_.size(); i++) {
		if (items_[i].type & kFileItemMdx) {
			SetCursor(i);
			*playPath = items_[i].path;
			return true;
		}
	}
	return false;
}

bool Filer::PrevMdx(std::string *playPath) {
	for (int i = cursor_ - 1; i >= 0; i--) {
		if (items_[i].type & kFileItemMdx) {
			SetCursor(i);
			*playPath = items_[i].path;
			return true;
		}
	}
	return false;
}

}  // namespace mxv2
