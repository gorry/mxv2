// mxv2 - ファイルシステムの抽象化 (VFS)

#include "vfs.h"

#include <cstring>

namespace mxv2 {

namespace {

const char kLocalId[] = "localfs";
const char kAssetsId[] = "assets";
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
	std::string label() const { return "ローカルファイルシステム"; }
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
	if (!assetsDir.empty()) {
		FileSystem *fs = new RootedFileSystem(kAssetsId, "同梱アセット", "Assets>",
		                                      JoinPath(assetsDir, "mdx"));
		owned_.push_back(fs);
		all_.push_back(fs);
	}
	if (!userDir.empty()) {
		FileSystem *fs = new RootedFileSystem(kUserDirId, "ユーザーフォルダ", "UserDir>",
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

FileSystem *Vfs::FindById(const std::string &id) const {
	for (size_t i = 0; i < all_.size(); i++) {
		if (CompareNoCase(all_[i]->id(), id) == 0) return all_[i];
	}
	return 0;
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
		if (IsMounted(all_[i])) continue;
		mounted_.push_back(all_[i]);
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
	FileSystem *f = FindById(scheme);
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
