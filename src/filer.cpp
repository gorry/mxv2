// mxv2 - ファイラ（旧 mxv/Filer.cpp の移植）

#include "filer.h"

#include <algorithm>
#include <cstring>

#include "fileutil.h"
#include "text.h"

#include <mdx_util.h>

namespace mxv2 {

namespace {

bool LessNoCase(const FileItem &a, const FileItem &b) {
	return CompareNoCase(a.baseName, b.baseName) < 0;
}

bool HasMdxExtension(const std::string &name) {
	if (name.size() < 4) return false;
	const std::string ext = name.substr(name.size() - 4);
	return CompareNoCase(ext, ".mdx") == 0;
}

}  // namespace

Filer::Filer() : cursor_(0), top_(0), visibleRows_(11), folderFirst_(false) {}

void Filer::SetFolderFirst(bool on) {
	folderFirst_ = on;
}

void Filer::SetCurrentDir(const std::string &dir) {
	currentDir_ = dir;
	if (!currentDir_.empty()) {
		const char last = currentDir_[currentDir_.size() - 1];
		if (last != '\\' && last != '/') {
#ifdef _WIN32
			currentDir_ += '\\';
#else
			currentDir_ += '/';
#endif
		}
	}
	Refresh();
	top_ = 0;
	// 旧 mxv と同じく、開いた直後は 1 番目（".." の次）にカーソルを置く。
	cursor_ = std::min((int)items_.size() - 1, 1);
	if (cursor_ < 0) cursor_ = 0;
}

void Filer::Refresh() {
	items_.clear();

	// 先頭は必ず親ディレクトリ
	{
		FileItem f;
		const std::string parent = ParentDir(currentDir_);
		const bool atRoot = (parent == currentDir_);
		f.baseName = atRoot ? "\\" : "..";
		f.path = atRoot ? currentDir_ : parent;
		f.title = currentDir_;
		f.type = kFileItemDir;
		items_.push_back(f);
	}

	if (folderFirst_) {
		AppendDirs(&items_);
		AppendMdx(&items_);
	} else {
		AppendMdx(&items_);
		AppendDirs(&items_);
	}
	AppendDrives(&items_);

	ReadTitles();

	if (cursor_ >= (int)items_.size()) cursor_ = (int)items_.size() - 1;
	if (cursor_ < 0) cursor_ = 0;
	EnsureCursorVisible();
}

void Filer::AppendDirs(std::vector<FileItem> *out) {
	std::vector<DirEntry> entries;
	if (!ListDirectory(currentDir_, &entries)) return;

	std::vector<FileItem> dirs;
	for (size_t i = 0; i < entries.size(); i++) {
		if (!entries[i].isDir) continue;
		FileItem f;
		f.baseName = entries[i].name;
		f.path = JoinPath(currentDir_, entries[i].name);
		f.type = kFileItemDir;
		dirs.push_back(f);
	}
	std::sort(dirs.begin(), dirs.end(), LessNoCase);
	out->insert(out->end(), dirs.begin(), dirs.end());
}

void Filer::AppendMdx(std::vector<FileItem> *out) {
	std::vector<DirEntry> entries;
	if (!ListDirectory(currentDir_, &entries)) return;

	std::vector<FileItem> files;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].isDir) continue;
		if (!HasMdxExtension(entries[i].name)) continue;
		FileItem f;
		f.baseName = entries[i].name;
		f.path = JoinPath(currentDir_, entries[i].name);
		f.type = kFileItemMdx;
		files.push_back(f);
	}
	std::sort(files.begin(), files.end(), LessNoCase);
	out->insert(out->end(), files.begin(), files.end());
}

void Filer::AppendDrives(std::vector<FileItem> *out) {
	std::vector<std::string> drives = ListDrives();
	for (size_t i = 0; i < drives.size(); i++) {
		FileItem f;
		f.baseName = drives[i].substr(0, 2);  // "C:"
		f.path = drives[i];
		f.type = kFileItemDrive;
		out->push_back(f);
	}
}

void Filer::ReadTitles() {
	for (size_t i = 0; i < items_.size(); i++) {
		if ((items_[i].type & kFileItemMdx) == 0) continue;

		std::vector<uint8_t> data;
		if (!ReadWholeFile(items_[i].path, &data) || data.empty()) continue;

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

void Filer::SetTop(int t) {
	const int maxTop = std::max(0, (int)items_.size() - visibleRows_);
	if (t < 0) t = 0;
	if (t > maxTop) t = maxTop;
	top_ = t;
}

void Filer::SetVisibleRows(int rows) {
	visibleRows_ = (rows > 0) ? rows : 1;
	EnsureCursorVisible();
}

void Filer::EnsureCursorVisible() {
	if (cursor_ < top_) {
		SetTop(cursor_);
	} else if (cursor_ >= top_ + visibleRows_) {
		SetTop(cursor_ - visibleRows_ + 1);
	} else {
		SetTop(top_);
	}
}

bool Filer::Open(std::string *playPath) {
	playPath->clear();
	if (items_.empty()) return false;
	const FileItem &f = items_[cursor_];

	if (f.type & kFileItemMdx) {
		*playPath = f.path;
		return true;
	}
	if (f.type & (kFileItemDir | kFileItemDrive)) {
		if (!IsDirectory(f.path)) return false;
		SetCurrentDir(AbsolutePath(f.path));
		return true;
	}
	return false;
}

void Filer::GoParent() {
	const std::string parent = ParentDir(currentDir_);
	if (parent == currentDir_) return;
	const std::string leaving = currentDir_;
	SetCurrentDir(parent);
	// 出てきたディレクトリにカーソルを合わせる
	SelectByPath(leaving);
}

void Filer::GoRoot() {
	// 旧 mxv はカレントを 3 文字 ("C:\") へ切り詰めていたが、それだと
	// Windows のドライブ名前提になる。変わらなくなるまで親を辿れば
	// 他のプラットフォームでも "/" に行き着く。
	std::string dir = currentDir_;
	for (int i = 0; i < 64; i++) {
		const std::string up = ParentDir(dir);
		if (up.empty() || up == dir) break;
		dir = up;
	}
	if (dir == currentDir_) return;
	const std::string leaving = currentDir_;
	SetCurrentDir(dir);
	SelectByPath(leaving);
}

bool Filer::SelectByPath(const std::string &path) {
	std::string want = path;
	while (!want.empty() && (want[want.size() - 1] == '\\' || want[want.size() - 1] == '/')) {
		want.erase(want.size() - 1);
	}
	for (size_t i = 1; i < items_.size(); i++) {
		std::string have = items_[i].path;
		while (!have.empty() && (have[have.size() - 1] == '\\' || have[have.size() - 1] == '/')) {
			have.erase(have.size() - 1);
		}
		if (CompareNoCase(have, want) == 0) {
			SetCursor((int)i);
			return true;
		}
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
