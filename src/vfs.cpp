// mxv2 - ファイルシステムの抽象化 (VFS)

#include "vfs.h"

#include <cstring>

#include "message.h"
#include "safaccess.h"

namespace mxv2 {

namespace {

const char kLocalId[] = "localfs";
const char kDirId[] = "dir";
const char kAssetsId[] = "assets";
const char kBookmarkId[] = "bookmark";
const char kSafId[] = "saf";
const char kUserDirId[] = "userdir";

bool IsSep(char c) {
	return c == '\\' || c == '/';
}

#ifdef _WIN32
const char kNativeSep = '\\';
#else
const char kNativeSep = '/';
#endif

// 名前の突き合わせ。ホストのファイルシステムの規則に合わせる。
bool SameNameNative(const std::string &a, const std::string &b) {
#ifdef _WIN32
	return CompareNoCase(a, b) == 0;
#else
	return a == b;
#endif
}

// ローカル FS の「フルパス表記」か。
//   Windows: "C:\..." / "C:/..." / "\\server\share"
//   その他 : "/..."
bool IsFullLocalPath(const std::string &s) {
#ifdef _WIN32
	if (s.size() >= 2 && s[1] == ':') {
		const char c = s[0];
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	}
	return s.size() >= 2 && IsSep(s[0]) && IsSep(s[1]);
#else
	return !s.empty() && s[0] == '/';
#endif
}

// OS のシステムドライブのルート。"localfs:" だけを渡されたときの行き先。
std::string SystemRootPath() {
#ifdef _WIN32
	const std::string drive = SystemDriveRoot();
	if (!drive.empty()) return drive;
	return std::string("C:\\");
#else
	return std::string("/");
#endif
}

// -------------------------------------------------------------------------
// ローカルファイルシステム
//
// rel はネイティブのフルパスそのもの。VFS を通す前の mxv2 と同じ動きになる。
// -------------------------------------------------------------------------
class LocalFileSystem : public FileSystem {
public:
	const char *id() const { return kLocalId; }
	std::string label() const { return Msg("Fs.Local"); }
	const char *prefix() const { return "Local>"; }

	bool available() const {
#if defined(__ANDROID__)
		// Android は素のパスでは外部ストレージを触れない (SAF 経由になる)。
		return false;
#else
		return true;
#endif
	}

	bool hasPdxDir() const { return false; }

	std::string Root() const { return SystemRootPath(); }

	std::string Normalize(const std::string &rel) const {
		if (rel.empty()) return Root();
		std::string s = rel;
#ifdef _WIN32
		for (size_t i = 0; i < s.size(); i++) {
			if (s[i] == '/') s[i] = '\\';
		}
#endif
		// 末尾の区切りは落とす。ただし "C:\" や "/" は落とすと意味が変わる。
		while (s.size() > 1 && IsSep(s[s.size() - 1])) {
			const char prev = s[s.size() - 2];
			if (IsSep(prev) || prev == ':') break;
			s.erase(s.size() - 1);
		}
		return s;
	}

	bool IsRoot(const std::string &rel) const {
		const std::string s = Normalize(rel);
		return Parent(s) == s;
	}

	std::string Parent(const std::string &rel) const {
		const std::string s = Normalize(rel);
		const std::string up = Normalize(ParentDir(s));
		return up.empty() ? s : up;
	}

	std::string Join(const std::string &dir, const std::string &name) const {
		return JoinPath(dir, name);
	}

	std::string DisplayPath(const std::string &rel) const {
		std::string s = Normalize(rel);
		if (!s.empty() && !IsSep(s[s.size() - 1])) s += kNativeSep;
		return s;
	}

	std::string ResolveInput(const std::string &input, const std::string &base) const {
		if (input.empty()) return base.empty() ? Root() : Normalize(base);
		if (IsFullLocalPath(input)) return Normalize(AbsolutePath(input));
		const std::string dir = base.empty() ? CurrentDir() : Normalize(base);
		return Normalize(AbsolutePath(JoinPath(dir, input)));
	}

	bool List(const std::string &rel, std::vector<DirEntry> *out) const {
		return ListDirectory(Normalize(rel), out);
	}
	bool Read(const std::string &rel, std::vector<uint8_t> *out) const {
		return ReadWholeFile(Normalize(rel), out);
	}
	bool Exists(const std::string &rel) const { return FileExists(Normalize(rel)); }
	bool IsDir(const std::string &rel) const { return IsDirectory(Normalize(rel)); }

	bool SamePath(const std::string &a, const std::string &b) const {
		return SameNameNative(Normalize(a), Normalize(b));
	}

	void AppendExtraItems(const std::string &rel, std::vector<FsExtraItem> *out) const {
		(void)rel;
		const std::vector<std::string> drives = ListDrives();
		for (size_t i = 0; i < drives.size(); i++) {
			FsExtraItem e;
			e.name = drives[i].substr(0, 2);  // "C:"
			e.rel = drives[i];
			out->push_back(e);
		}
	}
};

// -------------------------------------------------------------------------
// フォルダマウント (dir:)
//
// OS ネイティブの任意のフォルダを根に据える。ユーザーが
// [ファイルシステムの設定] から足すもので、初回起動時には無い。
//
// rel は**ネイティブのフルパスそのもの**（ローカル FS と同じ流儀）。
// 同じ dir: を複数マウントできるので、ref は場所まで含んでいないと
// どのマウントのものか決められない。Contains() が根の前方一致で選ぶ。
//
// Windows の UNC ("\\server\share") もそのまま根にできる。この場合は
// 1 段ごとに通信が要るので parentIsCheap() を false にして、起動時の
// フォールバックが途中を飛ばすようにする。
// -------------------------------------------------------------------------
class DirFileSystem : public FileSystem {
public:
	explicit DirFileSystem(const std::string &root) : root_(NormalizeNative(root)) {}

	const char *id() const { return kDirId; }
	// 種類は prefix ("DIR>") で分かるので、こちらは場所を出す。
	std::string label() const { return root_; }
	const char *prefix() const { return "DIR>"; }

	bool removable() const { return true; }
	bool hasPdxDir() const { return true; }
	bool parentIsCheap() const { return !IsUnc(root_); }
	std::string mountRef() const { return std::string(id()) + ":" + root_; }

	// **Normalize() を呼んではいけない**（あちらが Contains() を呼ぶ）。
	// 区切りを揃えるだけの NormalizeNative() で見る。
	bool Contains(const std::string &rel) const {
		const std::string s = NormalizeNative(rel);
		if (s.size() < root_.size()) return false;
		if (!SameNameNative(s.substr(0, root_.size()), root_)) return false;
		if (s.size() == root_.size()) return true;
		// 根が "C:\" のように区切りで終わっているときは、そのぶんを見ない。
		return IsSep(root_[root_.size() - 1]) || IsSep(s[root_.size()]);
	}

	std::string Root() const { return root_; }

	std::string Normalize(const std::string &rel) const {
		const std::string s = NormalizeNative(rel);
		// 根の外は指せない。おかしな指定は根へ寄せる。
		return Contains(s) ? s : root_;
	}

	bool IsRoot(const std::string &rel) const {
		return SameNameNative(Normalize(rel), root_);
	}

	std::string Parent(const std::string &rel) const {
		const std::string s = Normalize(rel);
		if (IsRoot(s)) return root_;
		const std::string up = NormalizeNative(ParentDir(s));
		return Contains(up) ? up : root_;
	}

	std::string Join(const std::string &dir, const std::string &name) const {
		return Normalize(JoinPath(Normalize(dir), name));
	}

	std::string DisplayPath(const std::string &rel) const {
		std::string s = Normalize(rel);
		if (!s.empty() && !IsSep(s[s.size() - 1])) s += kNativeSep;
		return s;
	}

	std::string ResolveInput(const std::string &input, const std::string &base) const {
		if (input.empty()) return base.empty() ? root_ : Normalize(base);
		if (IsFullLocalPath(input)) return Normalize(AbsolutePath(input));
		const std::string dir = base.empty() ? root_ : Normalize(base);
		return Normalize(AbsolutePath(JoinPath(dir, input)));
	}

	bool List(const std::string &rel, std::vector<DirEntry> *out) const {
		return ListDirectory(Normalize(rel), out);
	}
	bool Read(const std::string &rel, std::vector<uint8_t> *out) const {
		return ReadWholeFile(Normalize(rel), out);
	}
	bool Exists(const std::string &rel) const { return FileExists(Normalize(rel)); }
	bool IsDir(const std::string &rel) const { return IsDirectory(Normalize(rel)); }

	bool SamePath(const std::string &a, const std::string &b) const {
		return SameNameNative(Normalize(a), Normalize(b));
	}

	std::string nativeRoot() const { return root_; }

private:
	// 区切りを揃えて、末尾の区切りを落とす（"C:\" のように意味が変わる
	// ものは残す）。LocalFileSystem::Normalize と同じ規則。
	static std::string NormalizeNative(const std::string &path) {
		std::string s = path;
#ifdef _WIN32
		for (size_t i = 0; i < s.size(); i++) {
			if (s[i] == '/') s[i] = '\\';
		}
#endif
		while (s.size() > 1 && IsSep(s[s.size() - 1])) {
			const char prev = s[s.size() - 2];
			if (IsSep(prev) || prev == ':') break;
			s.erase(s.size() - 1);
		}
		return s;
	}

	static bool IsUnc(const std::string &path) {
		return path.size() >= 2 && IsSep(path[0]) && IsSep(path[1]);
	}

	std::string root_;
};

// -------------------------------------------------------------------------
// ネイティブのフォルダを 1 つ根に据えた FS（同梱アセット / ユーザーフォルダ）
//
// rel は根からの相対パスで、区切りは '/'。根は空文字列。".." で根の外へは
// 出られない（Normalize が畳んで捨てる）。
// -------------------------------------------------------------------------
class RootedFileSystem : public FileSystem {
public:
	RootedFileSystem(const char *id, const char *label, const char *prefix,
	                 const std::string &nativeRoot)
	    : id_(id), label_(label), prefix_(prefix), nativeRoot_(nativeRoot) {}

	const char *id() const { return id_; }
	std::string label() const { return label_; }
	const char *prefix() const { return prefix_; }

	std::string Root() const { return std::string(); }

	std::string Normalize(const std::string &rel) const {
		std::vector<std::string> parts;
		std::string cur;
		for (size_t i = 0; i <= rel.size(); i++) {
			const char c = (i < rel.size()) ? rel[i] : '/';
			if (!IsSep(c)) {
				cur += c;
				continue;
			}
			if (cur.empty() || cur == ".") {
				cur.clear();
				continue;
			}
			if (cur == "..") {
				if (!parts.empty()) parts.pop_back();
				cur.clear();
				continue;
			}
			parts.push_back(cur);
			cur.clear();
		}
		std::string out;
		for (size_t i = 0; i < parts.size(); i++) {
			if (i != 0) out += '/';
			out += parts[i];
		}
		return out;
	}

	bool IsRoot(const std::string &rel) const { return Normalize(rel).empty(); }

	std::string Parent(const std::string &rel) const {
		const std::string s = Normalize(rel);
		const size_t slash = s.rfind('/');
		if (slash == std::string::npos) return std::string();
		return s.substr(0, slash);
	}

	std::string Join(const std::string &dir, const std::string &name) const {
		const std::string d = Normalize(dir);
		if (d.empty()) return Normalize(name);
		return Normalize(d + "/" + name);
	}

	std::string DisplayPath(const std::string &rel) const {
		return std::string(prefix_) + Normalize(rel);
	}

	std::string ResolveInput(const std::string &input, const std::string &base) const {
		if (input.empty()) return Normalize(base);
		// 先頭が区切りなら根からの指定。そうでなければ base からの相対。
		if (IsSep(input[0])) return Normalize(input);
		return Join(base, input);
	}

	bool List(const std::string &rel, std::vector<DirEntry> *out) const {
		return ListDirectory(Native(rel), out);
	}
	bool Read(const std::string &rel, std::vector<uint8_t> *out) const {
		return ReadWholeFile(Native(rel), out);
	}
	bool Exists(const std::string &rel) const { return FileExists(Native(rel)); }
	bool IsDir(const std::string &rel) const { return IsDirectory(Native(rel)); }

	bool SamePath(const std::string &a, const std::string &b) const {
		return SameNameNative(Normalize(a), Normalize(b));
	}

	std::string nativeRoot() const { return nativeRoot_; }

private:
	std::string Native(const std::string &rel) const {
		std::string s = Normalize(rel);
#ifdef _WIN32
		for (size_t i = 0; i < s.size(); i++) {
			if (s[i] == '/') s[i] = '\\';
		}
#endif
		return s.empty() ? nativeRoot_ : JoinPath(nativeRoot_, s);
	}

	const char *id_;
	const char *label_;
	const char *prefix_;
	std::string nativeRoot_;
};

// -------------------------------------------------------------------------
// ブックマーク (bookmark:)
//
// ファイラーのルートに "Bookmarks>" として並ぶ、**行き先だけを並べる**
// ファイルシステム。中に入るとブックマーク（他の FS の ref）が一覧になり、
// 選ぶとそこへ移る。ジャンプ専用で、管理は [ブックマークの設定] (F4) の
// 仕事（あちらからも開ける）。
//
// 場所は根しか無い（rel は常に空）。List は空の一覧を「開けた」として返し、
// 実際の行は Filer が JumpTargets() から組み立てる（他の FS を指す行なので
// DirEntry では表せない）。一覧の実体は Settings::bookmarks で、ここは
// それを指しているだけ。
//
// 初回起動時から使えるので削除できず、ini に無ければ**先頭**に足される。
// -------------------------------------------------------------------------
class BookmarkFileSystem : public FileSystem {
public:
	BookmarkFileSystem() : list_(0) {}

	void SetList(const std::vector<std::string> *list) { list_ = list; }

	const char *id() const { return kBookmarkId; }
	std::string label() const { return Msg("Fs.Bookmark"); }
	const char *prefix() const { return "Bookmarks>"; }

	bool hasPdxDir() const { return false; }
	bool isJumpList() const { return true; }
	bool mountFirst() const { return true; }

	void JumpTargets(std::vector<std::string> *refs) const {
		refs->clear();
		if (list_ != 0) *refs = *list_;
	}

	std::string Root() const { return std::string(); }
	std::string Normalize(const std::string &rel) const {
		(void)rel;
		return std::string();  // 根しか無い
	}
	bool IsRoot(const std::string &rel) const {
		(void)rel;
		return true;
	}
	std::string Parent(const std::string &rel) const {
		(void)rel;
		return std::string();
	}
	std::string Join(const std::string &dir, const std::string &name) const {
		(void)dir;
		(void)name;
		return std::string();
	}
	std::string DisplayPath(const std::string &rel) const {
		(void)rel;
		return std::string(prefix());
	}
	std::string ResolveInput(const std::string &input, const std::string &base) const {
		(void)input;
		(void)base;
		return std::string();
	}

	// 根は「開ける」が中身は無い（行は Filer が JumpTargets から作る）。
	bool List(const std::string &rel, std::vector<DirEntry> *out) const {
		(void)rel;
		out->clear();
		return true;
	}
	bool Read(const std::string &rel, std::vector<uint8_t> *out) const {
		(void)rel;
		(void)out;
		return false;
	}
	bool Exists(const std::string &rel) const {
		(void)rel;
		return true;
	}
	bool IsDir(const std::string &rel) const {
		(void)rel;
		return true;
	}
	bool SamePath(const std::string &a, const std::string &b) const {
		(void)a;
		(void)b;
		return true;
	}

private:
	const std::vector<std::string> *list_;
};

}  // namespace

// ---------------------------------------------------------------------------
// Vfs
// ---------------------------------------------------------------------------

Vfs::Vfs() {}

Vfs::~Vfs() {
	for (size_t i = 0; i < owned_.size(); i++) delete owned_[i];
}

void Vfs::Configure(const std::string &assetsDir, const std::string &userDir) {
	for (size_t i = 0; i < owned_.size(); i++) delete owned_[i];
	owned_.clear();
	all_.clear();
	mounted_.clear();

	// 並び順は filesystem.md の記述順。設定が無いときの初期値になる。
	// ブックマークは先頭（初期状態で最上部に置く。bookmark.md）。
	{
		FileSystem *fs = new BookmarkFileSystem();
		owned_.push_back(fs);
		all_.push_back(fs);
	}
	if (!assetsDir.empty()) {
		FileSystem *fs = new RootedFileSystem(kAssetsId, Msg("Fs.Assets"), "Assets>",
		                                      JoinPath(assetsDir, "mdx"));
		owned_.push_back(fs);
		all_.push_back(fs);
	}
	if (!userDir.empty()) {
		FileSystem *fs = new RootedFileSystem(kUserDirId, Msg("Fs.UserDir"), "UserDir>",
		                                      JoinPath(userDir, "mdx"));
		owned_.push_back(fs);
		all_.push_back(fs);
	}
	{
		FileSystem *fs = new LocalFileSystem();
		owned_.push_back(fs);
		all_.push_back(fs);
	}
}

void Vfs::SetBookmarks(const std::vector<std::string> *list) {
	for (size_t i = 0; i < all_.size(); i++) {
		if (!all_[i]->isJumpList()) continue;
		static_cast<BookmarkFileSystem *>(all_[i])->SetList(list);
	}
}

std::string Vfs::BookmarkRootRef() const {
	const FileSystem *fs = FindById(kBookmarkId);
	return (fs == 0) ? std::string() : MakeRef(fs, fs->Root());
}

FileSystem *Vfs::FindById(const std::string &id) const {
	for (size_t i = 0; i < all_.size(); i++) {
		if (CompareNoCase(all_[i]->id(), id) == 0) return all_[i];
	}
	return 0;
}

FileSystem *Vfs::FindByMountRef(const std::string &ref) const {
	for (size_t i = 0; i < all_.size(); i++) {
		if (CompareNoCase(all_[i]->mountRef(), ref) == 0) return all_[i];
	}
	return 0;
}

// 同じ id のファイルシステムが複数あるとき、rel を持っているものを選ぶ。
// 1 つしか無ければ（同梱の 3 つはすべてそう）これまでと同じ。
FileSystem *Vfs::FindForRef(const std::string &id, const std::string &rel) const {
	FileSystem *first = 0;
	for (size_t i = 0; i < all_.size(); i++) {
		if (CompareNoCase(all_[i]->id(), id) != 0) continue;
		if (first == 0) first = all_[i];
		// 正規化は FS 側に任せる（Normalize() は「根の外なら根へ寄せる」
		// ような手当てをすることがあるので、選ぶ前に通してはいけない）。
		if (all_[i]->Contains(rel)) return all_[i];
	}
	// どのマウントの持ち物でもない ref は、とりあえず最初のものに割り当てる
	// （読めなければ、いつもどおり「見つかりません」になる）。
	return first;
}

FileSystem *Vfs::CreateFromMountRef(const std::string &ref) const {
	std::string scheme, rest;
	if (!SplitRef(ref, &scheme, &rest)) return 0;
	if (CompareNoCase(scheme, kDirId) == 0) {
		if (rest.empty()) return 0;
		return new DirFileSystem(rest);
	}
	if (CompareNoCase(scheme, kSafId) == 0) {
		// 端末のフォルダ (SAF)。Android 以外では作れない。権限が切れて
		// いるときも 0 なので、呼んだ側が「見つかりません」として捨てる。
		if (rest.empty()) return 0;
		return CreateSafFileSystem(rest);
	}
	// 外部ファイルシステム (web: / smb: …) はここへ足す。
	return 0;
}

// **呼ぶ前に、all_ を読んでいるスレッドの手を離させること**
// （SettingsUi::QuiesceVfsReaders）。push_back の再確保が、別スレッドの
// Parse が all_ を舐めている最中に起きると落ちる。RemoveMounted と同じ作法。
bool Vfs::Add(FileSystem *fs) {
	if (fs == 0) return false;
	// 同じ場所を二重に足さない（filesystem.md）。
	for (size_t i = 0; i < all_.size(); i++) {
		if (CompareNoCase(all_[i]->mountRef(), fs->mountRef()) == 0) return false;
	}
	owned_.push_back(fs);
	all_.push_back(fs);
	return true;
}

void Vfs::RemoveMounted(int index) {
	if (index < 0 || index >= (int)mounted_.size()) return;
	FileSystem *fs = mounted_[index];
	mounted_.erase(mounted_.begin() + index);
	if (!fs->removable()) return;  // 初回から使えるものは実体を残す

	for (size_t i = 0; i < all_.size(); i++) {
		if (all_[i] == fs) {
			all_.erase(all_.begin() + i);
			break;
		}
	}
	for (size_t i = 0; i < owned_.size(); i++) {
		if (owned_[i] == fs) {
			owned_.erase(owned_.begin() + i);
			delete fs;
			break;
		}
	}
}

bool Vfs::ParentIsCheap(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return true;
	return fs->parentIsCheap();
}

int Vfs::IndexOf(const FileSystem *fs) const {
	for (size_t i = 0; i < mounted_.size(); i++) {
		if (mounted_[i] == fs) return (int)i;
	}
	return -1;
}

bool Vfs::Mount(FileSystem *fs) {
	return MountAt((int)mounted_.size(), fs);
}

bool Vfs::MountAt(int pos, FileSystem *fs) {
	if (fs == 0 || IsMounted(fs)) return false;
	// 使えないもの（Android のローカル FS）はマウントしない。ここで弾けば
	// ファイラーの選択画面にも [ファイルシステムの設定] にも出ない
	// （ini に書かれていても LoadFileSystems が「直した」として書き戻す）。
	if (!fs->available()) return false;
	if (pos < 0) pos = 0;
	if (pos > (int)mounted_.size()) pos = (int)mounted_.size();
	mounted_.insert(mounted_.begin() + pos, fs);
	return true;
}

void Vfs::Unmount(int index) {
	if (index < 0 || index >= (int)mounted_.size()) return;
	mounted_.erase(mounted_.begin() + index);
}

void Vfs::Move(int index, int delta) {
	const int to = index + delta;
	if (index < 0 || index >= (int)mounted_.size()) return;
	if (to < 0 || to >= (int)mounted_.size()) return;
	FileSystem *fs = mounted_[index];
	mounted_.erase(mounted_.begin() + index);
	mounted_.insert(mounted_.begin() + to, fs);
}

bool Vfs::EnsureRequired() {
	bool added = false;
	for (size_t i = 0; i < all_.size(); i++) {
		if (all_[i]->removable()) continue;
		if (!all_[i]->available()) continue;  // MountAt と同じ理由
		if (IsMounted(all_[i])) continue;
		if (all_[i]->mountFirst()) {
			mounted_.insert(mounted_.begin(), all_[i]);
		} else {
			mounted_.push_back(all_[i]);
		}
		added = true;
	}
	return added;
}

bool Vfs::SplitRef(const std::string &ref, std::string *scheme, std::string *rest) {
	const size_t colon = ref.find(':');
	// 1 文字 + ':' は Windows のドライブレター。接頭辞としては扱わない。
	if (colon == std::string::npos || colon < 2) return false;
	for (size_t i = 0; i < colon; i++) {
		const char c = ref[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
	}
	*scheme = ref.substr(0, colon);
	*rest = ref.substr(colon + 1);
	return true;
}

bool Vfs::Parse(const std::string &ref, FileSystem **fs, std::string *rel) const {
	*fs = 0;
	rel->clear();
	if (ref.empty()) return true;

	std::string scheme, rest;
	if (!SplitRef(ref, &scheme, &rest)) {
		FileSystem *local = FindById(kLocalId);
		if (local == 0) return false;
		*fs = local;
		*rel = local->Normalize(ref);
		return true;
	}
	FileSystem *f = FindForRef(scheme, rest);
	if (f == 0) return false;
	*fs = f;
	*rel = f->Normalize(rest);
	return true;
}

bool Vfs::SameRef(const std::string &a, const std::string &b) const {
	FileSystem *fa = 0;
	FileSystem *fb = 0;
	std::string ra, rb;
	if (!Parse(a, &fa, &ra) || !Parse(b, &fb, &rb)) return false;
	if (fa != fb) return false;
	if (fa == 0) return true;  // どちらもファイルシステムの選択
	return fa->SamePath(ra, rb);
}

std::string Vfs::MakeRef(const FileSystem *fs, const std::string &rel) {
	if (fs == 0) return std::string();
	return std::string(fs->id()) + ":" + rel;
}

bool Vfs::Resolve(const std::string &input, const std::string &baseRef,
                  std::string *outRef) const {
	outRef->clear();
	if (input.empty()) return true;  // ファイルシステムの選択画面

	std::string scheme, rest;
	if (SplitRef(input, &scheme, &rest)) {
		FileSystem *f = FindById(scheme);
		if (f == 0) return false;  // 未知の接頭辞はエラー
		*outRef = MakeRef(f, f->ResolveInput(rest, std::string()));
		return true;
	}

	FileSystem *local = FindById(kLocalId);
	if (IsFullLocalPath(input)) {
		if (local == 0) return false;
		*outRef = MakeRef(local, local->ResolveInput(input, std::string()));
		return true;
	}

	// 相対パス。どこからの相対かは基準の ref による（無ければローカルの
	// カレントディレクトリ）。
	FileSystem *baseFs = 0;
	std::string baseRel;
	if (Parse(baseRef, &baseFs, &baseRel) && baseFs != 0) {
		*outRef = MakeRef(baseFs, baseFs->ResolveInput(input, baseRel));
		return true;
	}
	if (local == 0) return false;
	*outRef = MakeRef(local, local->ResolveInput(input, std::string()));
	return true;
}

bool Vfs::Read(const std::string &ref, std::vector<uint8_t> *out) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return false;
	return fs->Read(rel, out);
}

bool Vfs::Exists(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return false;
	return fs->Exists(rel);
}

bool Vfs::IsDir(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return false;
	return fs->IsDir(rel);
}

std::string Vfs::Parent(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return std::string();
	if (fs->IsRoot(rel)) return std::string();  // 1 つ上は選択画面
	return MakeRef(fs, fs->Parent(rel));
}

std::string Vfs::Join(const std::string &dirRef, const std::string &name) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(dirRef, &fs, &rel) || fs == 0) return std::string();
	return MakeRef(fs, fs->Join(rel, name));
}

std::string Vfs::DisplayPath(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return std::string();
	return fs->DisplayPath(rel);
}

std::string Vfs::RootRef(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return std::string();
	return MakeRef(fs, fs->Root());
}

std::string Vfs::PdxDirRef(const std::string &ref) const {
	FileSystem *fs = 0;
	std::string rel;
	if (!Parse(ref, &fs, &rel) || fs == 0) return std::string();
	if (!fs->hasPdxDir()) return std::string();
	return MakeRef(fs, fs->Join(fs->Root(), "pdx"));
}

}  // namespace mxv2
