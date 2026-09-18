// mxv2 - 外から渡された MDX を開く（Android の VIEW インテント）

#include "openintent.h"

#ifdef __ANDROID__

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <jni.h>

#include <SDL.h>

#include "fileutil.h"
#include "message.h"
#include "vfs.h"

namespace mxv2 {
namespace openintent {

namespace {

// 写す先（ユーザーフォルダの mdx/ の下）。ファイラーに見えるので、
// 何が入っているか分かる名前にしてある。
const char kInboxDir[] = "inbox";

// ユーザーフォルダのファイルシステムの id（vfs.cpp の kUserDirId）。
const char kUserDirId[] = "userdir";

bool StartsWith(const std::string &s, const char *prefix) {
	const size_t n = strlen(prefix);
	return s.size() >= n && s.compare(0, n, prefix) == 0;
}

// -------------------------------------------------------------------------
// Java 側 (net.gorry.mxv2.OpenIntentBridge) の窓口
//
// **最初の呼び出しはメインスレッドから行うこと**（safaccess.cpp と同じ事情。
// FindClass はスレッドのクラスローダを使う）。ここは起動時と毎フレームの
// ポーリングからしか呼ばないので、どちらもメインスレッド。
// -------------------------------------------------------------------------
struct OpenIntentJni {
	bool tried;
	bool ok;
	jclass cls;
	jmethodID available;
	jmethodID poll;
	jmethodID resolveInTree;
	jmethodID copyToDir;
	jmethodID displayName;
	jmethodID parentDocUri;
};

OpenIntentJni g = { false, false, 0, 0, 0, 0, 0, 0, 0 };

JNIEnv *Env() {
	return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

bool EnsureJni() {
	if (g.tried) return g.ok;
	g.tried = true;

	JNIEnv *env = Env();
	if (env == 0) return false;

	jclass local = env->FindClass("net/gorry/mxv2/OpenIntentBridge");
	if (local == 0) {
		env->ExceptionClear();
		printf("warning  : OpenIntentBridge class not found (cannot open handed files)\n");
		return false;
	}
	g.cls = (jclass)env->NewGlobalRef(local);
	env->DeleteLocalRef(local);

	g.available = env->GetStaticMethodID(g.cls, "available", "()Z");
	g.poll = env->GetStaticMethodID(g.cls, "poll", "()Ljava/lang/String;");
	g.resolveInTree = env->GetStaticMethodID(g.cls, "resolveInTree",
	                                         "(Ljava/lang/String;)Ljava/lang/String;");
	g.copyToDir = env->GetStaticMethodID(
	    g.cls, "copyToDir", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
	g.displayName = env->GetStaticMethodID(g.cls, "displayName",
	                                       "(Ljava/lang/String;)Ljava/lang/String;");
	g.parentDocUri = env->GetStaticMethodID(g.cls, "parentDocUri",
	                                        "(Ljava/lang/String;)Ljava/lang/String;");

	g.ok = (g.available != 0 && g.poll != 0 && g.resolveInTree != 0 && g.copyToDir != 0 &&
	        g.displayName != 0 && g.parentDocUri != 0);
	if (!g.ok) {
		env->ExceptionClear();
		printf("warning  : OpenIntentBridge methods not found (cannot open handed files)\n");
	}
	return g.ok;
}

// JNI の文字列は「修正 UTF-8」なので、素の UTF-8 とは 4 バイト文字の扱いが
// 違う。ファイル名がそのまま画面に出るので、UTF-16 を経由して自分で直す
// （safaccess.cpp / nowplaying.cpp と同じ理屈。JNI の窓口ごとに持っている）。
std::string FromJava(JNIEnv *env, jstring s) {
	if (s == 0) return std::string();
	const jsize len = env->GetStringLength(s);
	const jchar *p = env->GetStringChars(s, 0);
	if (p == 0) return std::string();

	std::string out;
	out.reserve((size_t)len);
	for (jsize i = 0; i < len; i++) {
		uint32_t c = p[i];
		if (c >= 0xd800 && c <= 0xdbff && i + 1 < len) {
			const uint32_t lo = p[i + 1];
			if (lo >= 0xdc00 && lo <= 0xdfff) {
				c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
				i++;
			}
		}
		if (c < 0x80) {
			out += (char)c;
		} else if (c < 0x800) {
			out += (char)(0xc0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3f));
		} else if (c < 0x10000) {
			out += (char)(0xe0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3f));
			out += (char)(0x80 | (c & 0x3f));
		} else {
			out += (char)(0xf0 | (c >> 18));
			out += (char)(0x80 | ((c >> 12) & 0x3f));
			out += (char)(0x80 | ((c >> 6) & 0x3f));
			out += (char)(0x80 | (c & 0x3f));
		}
	}
	env->ReleaseStringChars(s, p);
	return out;
}

jstring ToJava(JNIEnv *env, const std::string &s) {
	std::vector<jchar> u16;
	u16.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = (unsigned char)s[i];
		uint32_t cp = 0;
		int extra = 0;
		if (c < 0x80) {
			cp = c;
		} else if ((c & 0xe0) == 0xc0) {
			cp = c & 0x1f;
			extra = 1;
		} else if ((c & 0xf0) == 0xe0) {
			cp = c & 0x0f;
			extra = 2;
		} else if ((c & 0xf8) == 0xf0) {
			cp = c & 0x07;
			extra = 3;
		} else {
			cp = '?';
		}
		i++;
		for (int k = 0; k < extra && i < s.size(); k++, i++) {
			cp = (cp << 6) | ((unsigned char)s[i] & 0x3f);
		}
		if (cp >= 0x10000) {
			cp -= 0x10000;
			u16.push_back((jchar)(0xd800 + (cp >> 10)));
			u16.push_back((jchar)(0xdc00 + (cp & 0x3ff)));
		} else {
			u16.push_back((jchar)cp);
		}
	}
	return env->NewString(u16.empty() ? (const jchar *)"" : &u16[0], (jsize)u16.size());
}

// Java の String を返す静的メソッドを 1 つ呼ぶ（例外は握りつぶして空文字列）。
std::string CallString(jmethodID method, jstring a0, jstring a1, int argc) {
	JNIEnv *env = Env();
	if (env == 0) return std::string();
	jstring js = 0;
	if (argc == 0) {
		js = (jstring)env->CallStaticObjectMethod(g.cls, method);
	} else if (argc == 1) {
		js = (jstring)env->CallStaticObjectMethod(g.cls, method, a0);
	} else {
		js = (jstring)env->CallStaticObjectMethod(g.cls, method, a0, a1);
	}
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return std::string();
	}
	std::string out;
	if (js != 0) {
		out = FromJava(env, js);
		env->DeleteLocalRef(js);
	}
	return out;
}

// 文字列を 1 つ渡して文字列を 1 つ受け取る窓口（displayName / parentDocUri /
// resolveInTree に共通）。
std::string CallWithUri(jmethodID method, const std::string &uri) {
	JNIEnv *env = Env();
	if (env == 0) return std::string();
	jstring juri = ToJava(env, uri);
	const std::string s = CallString(method, juri, 0, 1);
	env->DeleteLocalRef(juri);
	return s;
}

}  // namespace

bool IsUri(const std::string &s) {
	return StartsWith(s, "content://") || StartsWith(s, "file://");
}

bool Available() {
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	if (env == 0) return false;
	const jboolean ok = env->CallStaticBooleanMethod(g.cls, g.available);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	return ok != 0;
}

bool Poll(std::string *uri) {
	uri->clear();
	if (!EnsureJni()) return false;
	const std::string s = CallString(g.poll, 0, 0, 0);
	if (s.empty()) return false;
	*uri = s;
	return true;
}

bool GrantedTree(const std::string &uri, std::string *treeUri, std::string *rel) {
	treeUri->clear();
	rel->clear();
	if (uri.empty() || !EnsureJni()) return false;

	const std::string s = CallWithUri(g.resolveInTree, uri);
	if (s.empty()) return false;

	const size_t nl = s.find('\n');
	*treeUri = (nl == std::string::npos) ? s : s.substr(0, nl);
	*rel = (nl == std::string::npos) ? std::string() : s.substr(nl + 1);
	if (treeUri->empty()) {
		rel->clear();
		return false;
	}
	return true;
}

bool ResolveInTree(const Vfs &vfs, const std::string &uri, std::string *ref) {
	ref->clear();

	std::string tree, rel;
	if (!GrantedTree(uri, &tree, &rel)) return false;

	// **そのツリーのマウントを名指しで探す**（openintent.h の注意書き）。
	FileSystem *fs = vfs.FindByMountRef(std::string("saf:") + tree);
	if (fs == 0) return false;
	if (!fs->accessible() && !fs->Reconnect()) return false;

	// 実際に在るかまで見る。ドキュメント ID や実パスの綴りが SAF の表示名と
	// 食い違う提供元では、ここで外れて「どうするか」を尋ねる道へ回る。
	const std::string target = rel.empty() ? fs->Root() : fs->Join(fs->Root(), rel);
	if (!fs->Exists(target)) return false;
	*ref = Vfs::MakeRef(fs, target);
	return true;
}

std::string DisplayName(const std::string &uri) {
	if (uri.empty() || !EnsureJni()) return std::string();
	return CallWithUri(g.displayName, uri);
}

std::string ParentDocUri(const std::string &uri) {
	if (uri.empty() || !EnsureJni()) return std::string();
	return CallWithUri(g.parentDocUri, uri);
}

bool CopyToUserDir(const Vfs &vfs, const std::string &uri, std::string *ref) {
	ref->clear();
	if (uri.empty() || !EnsureJni()) return false;

	// 写す先はユーザーフォルダの mdx/inbox/。**隣の PDX は付いてこない**
	// （単独のドキュメントの許可では隣を読めない）。
	const FileSystem *fs = vfs.FindById(kUserDirId);
	if (fs == 0 || fs->nativeRoot().empty()) {
		printf("warning  : %s\n", MsgF("Log.HandedUnreadable", uri).c_str());
		fflush(stdout);
		return false;
	}
	const std::string dir = JoinPath(fs->nativeRoot(), kInboxDir);

	JNIEnv *env = Env();
	if (env == 0) return false;
	jstring juri = ToJava(env, uri);
	jstring jdir = ToJava(env, dir);
	const std::string name = CallString(g.copyToDir, juri, jdir, 2);
	env->DeleteLocalRef(juri);
	env->DeleteLocalRef(jdir);
	if (name.empty()) {
		printf("warning  : %s\n", MsgF("Log.HandedUnreadable", uri).c_str());
		fflush(stdout);
		return false;
	}

	*ref = std::string(kUserDirId) + ":" + kInboxDir + "/" + name;
	printf("info     : %s\n", MsgF("Log.HandedCopied", *ref).c_str());
	fflush(stdout);
	return true;
}

}  // namespace openintent
}  // namespace mxv2

#else  // __ANDROID__

#include <string>

#include "vfs.h"

namespace mxv2 {
namespace openintent {

// 外からファイルを渡されるのは今のところ Android だけ。Windows は
// ドラッグ＆ドロップと 2 つめの mxv2（src/singleinstance.cpp）が
// 同じ役目を果たしていて、どちらも素のパスで届く。

bool Available() {
	return false;
}

bool IsUri(const std::string &s) {
	// 素のパスと ref しか来ない環境では、URI という形そのものを知らない
	// ことにしておく（渡されても「知らない場所」として断られる）。
	(void)s;
	return false;
}

bool Poll(std::string *uri) {
	uri->clear();
	return false;
}

bool GrantedTree(const std::string &uri, std::string *treeUri, std::string *rel) {
	(void)uri;
	treeUri->clear();
	rel->clear();
	return false;
}

bool ResolveInTree(const Vfs &vfs, const std::string &uri, std::string *ref) {
	(void)vfs;
	(void)uri;
	ref->clear();
	return false;
}

bool CopyToUserDir(const Vfs &vfs, const std::string &uri, std::string *ref) {
	(void)vfs;
	(void)uri;
	ref->clear();
	return false;
}

std::string DisplayName(const std::string &uri) {
	(void)uri;
	return std::string();
}

std::string ParentDocUri(const std::string &uri) {
	(void)uri;
	return std::string();
}

}  // namespace openintent
}  // namespace mxv2

#endif  // __ANDROID__
