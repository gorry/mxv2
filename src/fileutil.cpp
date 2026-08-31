// mxv2 - ファイル / パスユーティリティ

#include "fileutil.h"

#include <cstdio>
#include <cstring>

#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>  // IFileOpenDialog（フォルダを探すダイアログ）
#else
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Android と Web には「ユーザーフォルダ」の決まった作法が無いので、
// そこだけ SDL に聞く。デスクトップは環境変数から自分で組み立てるので、
// SDL を使わないツール (mxv2_chunktest) からもこのファイルを使える。
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include <SDL.h>
#endif

namespace mxv2 {

namespace {

#ifdef __ANDROID__
// 新しく作るディレクトリのモード。**グループに書き込みを持たせる**。
//
// Android の外部アプリ領域 (/sdcard/Android/data/<パッケージ>/files/) は、
// パソコンのエクスプローラや端末のファイルマネージャからは
// **ext_data_rw グループ**として見える。0755 で作るとグループに書き込みが
// 無いので、**その中へ外からファイルを置けない**（フォルダは見えるのに
// 書き込めない、という症状になる）。
//
// setgid は親から引き継がれるので、こちらで立てる必要はない。
const int kNewDirMode = 0770;
#elif !defined(_WIN32)
const int kNewDirMode = 0755;
#endif

// グループの書き込みが落ちているディレクトリを直す。umask に削られたぶんと、
// 前の版が 0755 で作ってしまったものが対象。
//
// **足りないビットを足すだけにして、モードを丸ごと渡さないこと。**
// Android 自身が作る files/ には setgid が立っており (drwxrws---)、
// chmod に 0770 を渡すとそれを落としてしまう。しかも FUSE 越しでは
// setgid を立て直せない（chmod 02770 は通るが立たない）ので、落とすと戻せない。
void FixSharedDirMode(const std::string &path) {
#ifdef __ANDROID__
	struct stat st;
	if (stat(path.c_str(), &st) != 0) return;
	if ((st.st_mode & S_IWGRP) != 0) return;  // すでに書ける。触らない
	(void)chmod(path.c_str(), (st.st_mode & 07777) | S_IWGRP);
#else
	(void)path;
#endif
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string &s) {
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	if (n <= 0) return std::wstring();
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

std::string WideToUtf8(const std::wstring &w) {
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	if (n <= 0) return std::string();
	std::string s((size_t)n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}
#endif

FILE *OpenRead(const std::string &path) {
#ifdef _WIN32
	std::wstring wpath = Utf8ToWide(path);
	if (wpath.empty()) return NULL;
	FILE *fp = NULL;
	if (_wfopen_s(&fp, wpath.c_str(), L"rb") != 0) return NULL;
	return fp;
#else
	return fopen(path.c_str(), "rb");
#endif
}

FILE *OpenWrite(const std::string &path) {
#ifdef _WIN32
	std::wstring wpath = Utf8ToWide(path);
	if (wpath.empty()) return NULL;
	FILE *fp = NULL;
	if (_wfopen_s(&fp, wpath.c_str(), L"wb") != 0) return NULL;
	return fp;
#else
	return fopen(path.c_str(), "wb");
#endif
}

bool IsSeparator(char c) {
#ifdef _WIN32
	return c == '\\' || c == '/';
#else
	return c == '/';
#endif
}

// 末尾に区切りを 1 つだけ付ける。
std::string WithSeparator(const std::string &dir) {
	if (dir.empty()) return dir;
	if (IsSeparator(dir[dir.size() - 1])) return dir;
#ifdef _WIN32
	return dir + "\\";
#else
	return dir + "/";
#endif
}

// 環境変数。無ければ空文字列。Windows はワイド版で読んで UTF-8 に直す
// （ユーザー名に非 ASCII が入っていると %APPDATA% がそうなる）。
std::string EnvVar(const char *name) {
#ifdef _WIN32
	std::wstring wname(name, name + strlen(name));
	DWORD n = GetEnvironmentVariableW(wname.c_str(), NULL, 0);
	if (n == 0) return std::string();
	std::wstring buf(n, L'\0');
	DWORD got = GetEnvironmentVariableW(wname.c_str(), &buf[0], n);
	if (got == 0 || got >= n) return std::string();
	buf.resize(got);
	return WideToUtf8(buf);
#else
	const char *v = getenv(name);
	return (v != NULL) ? std::string(v) : std::string();
#endif
}

}  // namespace

bool ReadWholeFile(const std::string &path, std::vector<uint8_t> *out) {
	FILE *fp = OpenRead(path);
	if (fp == NULL) return false;

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	long size = ftell(fp);
	if (size < 0) {
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}

	out->resize((size_t)size);
	if (size > 0) {
		size_t got = fread(&(*out)[0], 1, (size_t)size, fp);
		if (got != (size_t)size) {
			fclose(fp);
			out->clear();
			return false;
		}
	}
	fclose(fp);
	return true;
}

bool WriteWholeFile(const std::string &path, const std::vector<uint8_t> &data) {
	FILE *fp = OpenWrite(path);
	if (fp == NULL) return false;

	bool ok = true;
	if (!data.empty()) {
		ok = (fwrite(&data[0], 1, data.size(), fp) == data.size());
	}
	if (fclose(fp) != 0) ok = false;
	return ok;
}

bool FileExists(const std::string &path) {
	FILE *fp = OpenRead(path);
	if (fp == NULL) return false;
	fclose(fp);
	return true;
}

bool RemoveFile(const std::string &path) {
	if (!FileExists(path)) return true;
#ifdef _WIN32
	std::wstring w = Utf8ToWide(path);
	if (w.empty()) return false;
	return DeleteFileW(w.c_str()) != 0;
#else
	return unlink(path.c_str()) == 0;
#endif
}

std::string DirNameOf(const std::string &path) {
	for (size_t i = path.size(); i > 0; i--) {
		if (IsSeparator(path[i - 1])) return path.substr(0, i);
	}
	return std::string();
}

std::string BaseNameOf(const std::string &path) {
	for (size_t i = path.size(); i > 0; i--) {
		if (IsSeparator(path[i - 1])) return path.substr(i);
	}
	return path;
}

std::string StemOf(const std::string &path) {
	std::string base = BaseNameOf(path);
	size_t dot = base.rfind('.');
	if (dot == std::string::npos || dot == 0) return base;
	return base.substr(0, dot);
}

std::string JoinPath(const std::string &dir, const std::string &name) {
	if (dir.empty()) return name;
	if (IsSeparator(dir[dir.size() - 1])) return dir + name;
#ifdef _WIN32
	return dir + "\\" + name;
#else
	return dir + "/" + name;
#endif
}

std::string ExecutableDir() {
#if defined(__ANDROID__)
	// apk の中に「実行ファイルの隣」は無い。同梱素材は初回起動時に内部
	// ストレージへ展開する（androidassets.cpp）ので、その置き場所を返す。
	//
	// **内部ストレージの根そのものではなく、その下の bundled/ にする。**
	// SDL_RWFromFile は相対パスを渡されると、まず
	// <内部ストレージ>/<相対パス> を開きにいき、無いときだけ apk の assets を
	// 見る。根に展開すると、展開したものが apk の中身を覆い隠してしまい、
	// 2 回目からは**古い展開結果をそのまま読み直すだけ**になる
	// （素材を差し替えても新しいものが取り出せない）。
	const char *base = SDL_AndroidGetInternalStoragePath();
	if (base == NULL) return std::string("./");
	return WithSeparator(JoinPath(std::string(base), "bundled"));
#elif defined(_WIN32)
	std::wstring buf(MAX_PATH, L'\0');
	for (;;) {
		DWORD n = GetModuleFileNameW(NULL, &buf[0], (DWORD)buf.size());
		if (n == 0) return std::string("./");
		if (n < buf.size()) {
			buf.resize(n);
			break;
		}
		buf.resize(buf.size() * 2);
	}
	std::string dir = DirNameOf(WideToUtf8(buf));
	return dir.empty() ? std::string("./") : dir;
#else
	char buf[4096];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0) return std::string("./");
	buf[n] = '\0';
	std::string dir = DirNameOf(std::string(buf));
	return dir.empty() ? std::string("./") : dir;
#endif
}

std::string UserDataDir(const std::string &appName) {
#if defined(__ANDROID__)
	// Android のアプリ専用の置き場所は 2 つある。
	//   内部 /data/data/<パッケージ>/files/
	//     … 他のアプリからもパソコンからも**見えない**。SDL_GetPrefPath は
	//        こちらを返す。
	//   外部 /sdcard/Android/data/<パッケージ>/files/
	//     … USB でパソコンから見えるし adb push でも入る。権限は要らず、
	//        アンインストールで消える。
	// ユーザーフォルダは font.ttf やスキンや曲を**ユーザーが置く場所**なので、
	// 見えなければ意味がない。外部を使う。
	//
	// なお、このフォルダは getExternalFilesDir を呼ぶまで作られない。
	// SDL_AndroidGetExternalStoragePath() がそれを呼ぶので、**ここを通ること
	// 自体がフォルダを作ることでもある**（呼ばなければ、パソコンから見ても
	// そんなフォルダは無い、と言われる）。
	if ((SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) != 0) {
		const char *ext = SDL_AndroidGetExternalStoragePath();
		if (ext != NULL && ext[0] != '\0') return WithSeparator(std::string(ext));
	}
	// 外部が使えない端末では内部へ落とす（見えないが、動きはする）。
	return LegacyUserDataDir(appName);
#elif defined(__EMSCRIPTEN__)
	// 内部ストレージの場所は OS ごとに違ううえに実行時にしか分からない。
	char *pref = SDL_GetPrefPath("", appName.c_str());
	if (pref == NULL) return ExecutableDir();
	std::string dir = WithSeparator(std::string(pref));
	SDL_free(pref);
	return dir;
#elif defined(_WIN32)
	// %APPDATA%\<appName>\ （ローミングする側。設定ファイルの定位置）。
	const std::string base = EnvVar("APPDATA");
	if (base.empty()) return ExecutableDir();
	return WithSeparator(JoinPath(base, appName));
#elif defined(__APPLE__)
	const std::string home = EnvVar("HOME");
	if (home.empty()) return ExecutableDir();
	return WithSeparator(JoinPath(JoinPath(home, "Library/Application Support"), appName));
#else
	// XDG Base Directory の作法。$XDG_DATA_HOME が無ければ ~/.local/share。
	std::string base = EnvVar("XDG_DATA_HOME");
	if (base.empty()) {
		const std::string home = EnvVar("HOME");
		if (home.empty()) return ExecutableDir();
		base = JoinPath(home, ".local/share");
	}
	return WithSeparator(JoinPath(base, appName));
#endif
}

std::string LegacyUserDataDir(const std::string &appName) {
#if defined(__ANDROID__)
	// 2026-08-31 より前は、ここ（外から見えない内部ストレージ）に
	// mxv2.ini を置いていた。
	char *pref = SDL_GetPrefPath("", appName.c_str());
	if (pref == NULL) return ExecutableDir();
	std::string dir = WithSeparator(std::string(pref));
	SDL_free(pref);
	return dir;
#else
	(void)appName;
	return std::string();
#endif
}

bool IsDirectory(const std::string &path) {
#ifdef _WIN32
	std::wstring w = Utf8ToWide(path);
	if (w.empty()) return false;
	DWORD attr = GetFileAttributesW(w.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct stat st;
	if (stat(path.c_str(), &st) != 0) return false;
	return S_ISDIR(st.st_mode);
#endif
}

bool MakeDirectories(const std::string &pathIn) {
	if (pathIn.empty()) return false;

	// 末尾の区切りは落としておく（"C:\" や "/" のように、落とすと意味が
	// 変わるものは除く）。
	std::string path = pathIn;
	while (path.size() > 1 && IsSeparator(path[path.size() - 1])) {
		const char prev = path[path.size() - 2];
		if (IsSeparator(prev) || prev == ':') break;
		path.erase(path.size() - 1);
	}

	if (IsDirectory(path)) {
		FixSharedDirMode(path);  // 前の版が作ったものも直す
		return true;
	}

	// 親を先に作る。ParentDir はルートまで来ると同じものを返すので、
	// それを打ち止めにする。
	const std::string parent = ParentDir(path);
	if (parent != path && !parent.empty() && !IsDirectory(parent)) {
		if (!MakeDirectories(parent)) return false;
	}

#ifdef _WIN32
	std::wstring w = Utf8ToWide(path);
	if (w.empty()) return false;
	if (CreateDirectoryW(w.c_str(), NULL)) return true;
	return GetLastError() == ERROR_ALREADY_EXISTS;
#else
	if (mkdir(path.c_str(), kNewDirMode) == 0) {
		// umask に削られたぶんを戻す。
		FixSharedDirMode(path);
		return true;
	}
	return IsDirectory(path);
#endif
}

bool ListDirectory(const std::string &dir, std::vector<DirEntry> *out) {
	out->clear();
#ifdef _WIN32
	std::wstring pattern = Utf8ToWide(JoinPath(dir, "*"));
	if (pattern.empty()) return false;

	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	do {
		std::string name = WideToUtf8(fd.cFileName);
		if (name == "." || name == "..") continue;
		DirEntry e;
		e.name = name;
		e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		out->push_back(e);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return true;
#else
	DIR *d = opendir(dir.c_str());
	if (d == NULL) return false;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		std::string name(ent->d_name);
		if (name == "." || name == "..") continue;
		DirEntry e;
		e.name = name;
		e.isDir = IsDirectory(JoinPath(dir, name));
		out->push_back(e);
	}
	closedir(d);
	return true;
#endif
}

std::vector<std::string> ListDrives() {
	std::vector<std::string> out;
#ifdef _WIN32
	DWORD mask = GetLogicalDrives();
	for (int i = 0; i < 26; i++) {
		if ((mask & (1u << i)) == 0) continue;
		char root[4] = { (char)('A' + i), ':', '\\', '\0' };
		out.push_back(std::string(root));
	}
#endif
	return out;
}

std::string SystemDriveRoot() {
#ifdef _WIN32
	const std::string drive = EnvVar("SystemDrive");  // ふつうは "C:"
	if (drive.size() >= 2 && drive[1] == ':') return drive.substr(0, 2) + "\\";
	return std::string("C:\\");
#else
	return std::string();
#endif
}

std::string AbsolutePath(const std::string &path) {
#ifdef _WIN32
	std::wstring w = Utf8ToWide(path);
	if (w.empty()) return path;
	DWORD n = GetFullPathNameW(w.c_str(), 0, NULL, NULL);
	if (n == 0) return path;
	std::wstring buf(n, L'\0');
	DWORD got = GetFullPathNameW(w.c_str(), n, &buf[0], NULL);
	if (got == 0 || got >= n) return path;
	buf.resize(got);
	return WideToUtf8(buf);
#else
	char buf[PATH_MAX];
	if (realpath(path.c_str(), buf) == NULL) return path;
	return std::string(buf);
#endif
}

std::string CurrentDir() {
#ifdef _WIN32
	DWORD n = GetCurrentDirectoryW(0, NULL);
	if (n == 0) return std::string(".\\");
	std::wstring buf(n, L'\0');
	DWORD got = GetCurrentDirectoryW(n, &buf[0]);
	if (got == 0) return std::string(".\\");
	buf.resize(got);
	std::string s = WideToUtf8(buf);
	if (!s.empty() && !IsSeparator(s[s.size() - 1])) s += "\\";
	return s;
#else
	char buf[PATH_MAX];
	if (getcwd(buf, sizeof(buf)) == NULL) return std::string("./");
	std::string s(buf);
	if (!s.empty() && !IsSeparator(s[s.size() - 1])) s += "/";
	return s;
#endif
}

std::string ParentDir(const std::string &dir) {
	// 末尾の区切りを落としてから、その 1 つ上を取る。
	std::string s = dir;
	while (!s.empty() && IsSeparator(s[s.size() - 1])) s.erase(s.size() - 1);
	std::string parent = DirNameOf(s);
	if (parent.empty()) return dir;
	return parent;
}

// OS の「フォルダを探す」ダイアログ。
//
// フォルダマウント (dir:) の場所は **OS ネイティブのパス**なので、選ぶのも
// OS のダイアログに任せる。ファイラーのフォルダ選択（自前・VFS の中を辿る）
// とは別物で、あちらは ref を選ぶためのもの。
#ifdef _WIN32
bool HasFolderBrowser() {
	return true;
}

bool BrowseForFolder(const std::string &title, const std::string &start, void *owner,
                     std::string *out) {
	out->clear();

	// SDL が別のモードで初期化していることがあるので、失敗しても続ける
	// （その場合は解放もしない）。
	const HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	const bool weInitialized = SUCCEEDED(init);

	IFileOpenDialog *dialog = NULL;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
	                              IID_IFileOpenDialog, (void **)&dialog);
	if (SUCCEEDED(hr) && dialog != NULL) {
		DWORD options = 0;
		if (SUCCEEDED(dialog->GetOptions(&options))) {
			// フォルダを選ぶ / 実在するもの / ファイルシステム上のものだけ
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST |
			                   FOS_FORCEFILESYSTEM);
		}
		if (!title.empty()) dialog->SetTitle(Utf8ToWide(title).c_str());
		if (!start.empty()) {
			IShellItem *item = NULL;
			if (SUCCEEDED(SHCreateItemFromParsingName(Utf8ToWide(start).c_str(), NULL,
			                                          IID_IShellItem, (void **)&item)) &&
			    item != NULL) {
				dialog->SetFolder(item);
				item->Release();
			}
		}

		if (SUCCEEDED(dialog->Show((HWND)owner))) {
			IShellItem *item = NULL;
			if (SUCCEEDED(dialog->GetResult(&item)) && item != NULL) {
				PWSTR path = NULL;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
				    path != NULL) {
					*out = WideToUtf8(path);
					CoTaskMemFree(path);
				}
				item->Release();
			}
		}
		dialog->Release();
	}

	if (weInitialized) CoUninitialize();
	return !out->empty();
}
#else
bool HasFolderBrowser() {
	return false;
}

bool BrowseForFolder(const std::string &title, const std::string &start, void *owner,
                     std::string *out) {
	(void)title;
	(void)start;
	(void)owner;
	out->clear();
	return false;
}
#endif

int CompareNoCase(const std::string &a, const std::string &b) {
	const size_t n = (a.size() < b.size()) ? a.size() : b.size();
	for (size_t i = 0; i < n; i++) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb) return (ca < cb) ? -1 : 1;
	}
	if (a.size() == b.size()) return 0;
	return (a.size() < b.size()) ? -1 : 1;
}

}  // namespace mxv2
