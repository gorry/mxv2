// mxv2 - SAF (Storage Access Framework) 越しのファイルシステム（Android 専用）

#include "safaccess.h"

#ifdef __ANDROID__

#include <cstdio>
#include <cstdlib>

#include <jni.h>

#include <SDL.h>

#include "message.h"

namespace mxv2 {

namespace {

const char kSafId[] = "saf";

// -------------------------------------------------------------------------
// Java 側 (net.gorry.mxv2.SafBridge) の窓口
//
// **最初の呼び出しはメインスレッドから行うこと。** FindClass はスレッドの
// クラスローダを使うので、SDL が後から attach した作業スレッド
// （フォルダ読み・曲名読み・曲の読み込み）からではアプリのクラスが見えない。
// 一度掴んだ jclass はグローバル参照で持ち続けるので、以降はどのスレッドから
// でも呼べる。
// -------------------------------------------------------------------------
struct SafJni {
	bool tried;
	bool ok;
	jclass cls;
	jmethodID available;
	jmethodID pickTree;
	jmethodID takeResult;
	jmethodID hasPermission;
	jmethodID rootName;
	jmethodID list;
	jmethodID read;
	jmethodID stat;
	jmethodID forget;
};

SafJni g = { false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

JNIEnv *Env() {
	return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

bool EnsureJni() {
	if (g.tried) return g.ok;
	g.tried = true;

	JNIEnv *env = Env();
	if (env == 0) return false;

	jclass local = env->FindClass("net/gorry/mxv2/SafBridge");
	if (local == 0) {
		env->ExceptionClear();
		printf("warning  : SafBridge class not found (SAF disabled)\n");
		return false;
	}
	g.cls = (jclass)env->NewGlobalRef(local);
	env->DeleteLocalRef(local);

	g.available = env->GetStaticMethodID(g.cls, "available", "()Z");
	g.pickTree = env->GetStaticMethodID(g.cls, "pickTree", "(Ljava/lang/String;)Z");
	g.takeResult = env->GetStaticMethodID(g.cls, "takeResult", "()Ljava/lang/String;");
	g.hasPermission = env->GetStaticMethodID(g.cls, "hasPermission", "(Ljava/lang/String;)Z");
	g.rootName = env->GetStaticMethodID(g.cls, "rootName",
	                                    "(Ljava/lang/String;)Ljava/lang/String;");
	g.list = env->GetStaticMethodID(
	    g.cls, "list", "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
	g.read = env->GetStaticMethodID(g.cls, "read", "(Ljava/lang/String;Ljava/lang/String;)[B");
	g.stat = env->GetStaticMethodID(g.cls, "stat", "(Ljava/lang/String;Ljava/lang/String;)I");
	g.forget = env->GetStaticMethodID(g.cls, "forget", "(Ljava/lang/String;)V");

	g.ok = (g.available != 0 && g.pickTree != 0 && g.takeResult != 0 && g.hasPermission != 0 &&
	        g.rootName != 0 && g.list != 0 && g.read != 0 && g.stat != 0 && g.forget != 0);
	if (!g.ok) {
		env->ExceptionClear();
		printf("warning  : SafBridge methods not found (SAF disabled)\n");
	}
	return g.ok;
}

// JNI の文字列は「修正 UTF-8」で、絵文字などの 4 バイト文字が素の UTF-8 と
// 食い違う。名前をそのまま画面へ出すので、UTF-16 を経由して自分で直す。
std::string Utf16ToUtf8(const jchar *s, jsize len) {
	std::string out;
	out.reserve((size_t)len);
	for (jsize i = 0; i < len; i++) {
		uint32_t c = s[i];
		if (c >= 0xd800 && c <= 0xdbff && i + 1 < len) {
			const uint32_t lo = s[i + 1];
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
	return out;
}

void Utf8ToUtf16(const std::string &s, std::vector<jchar> *out) {
	out->clear();
	out->reserve(s.size());
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
			cp = 0xfffd;
		}
		i++;
		for (int k = 0; k < extra; k++) {
			if (i >= s.size()) {
				cp = 0xfffd;
				break;
			}
			cp = (cp << 6) | ((unsigned char)s[i] & 0x3f);
			i++;
		}
		if (cp >= 0x10000) {
			cp -= 0x10000;
			out->push_back((jchar)(0xd800 + (cp >> 10)));
			out->push_back((jchar)(0xdc00 + (cp & 0x3ff)));
		} else {
			out->push_back((jchar)cp);
		}
	}
}

std::string FromJava(JNIEnv *env, jstring s) {
	if (s == 0) return std::string();
	const jsize len = env->GetStringLength(s);
	const jchar *chars = env->GetStringChars(s, 0);
	if (chars == 0) return std::string();
	std::string out = Utf16ToUtf8(chars, len);
	env->ReleaseStringChars(s, chars);
	return out;
}

jstring ToJava(JNIEnv *env, const std::string &s) {
	std::vector<jchar> buf;
	Utf8ToUtf16(s, &buf);
	return env->NewString(buf.empty() ? (const jchar *)"" : &buf[0], (jsize)buf.size());
}

// -------------------------------------------------------------------------
// ツリーを 1 つ持つファイルシステム
//
// rel は「ツリーの URI + '/' + 相対パス」。dir: と同じで、根の外は指せない。
// -------------------------------------------------------------------------
// ツリーの URI の権限が生きているか（次の起動で確かめる）。
bool QueryPermission(const std::string &treeUri) {
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	if (env == 0) return false;
	jstring jtree = ToJava(env, treeUri);
	const jboolean ok = env->CallStaticBooleanMethod(g.cls, g.hasPermission, jtree);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(jtree);
	return ok != JNI_FALSE;
}

// ツリーの根の表示名。権限が無いなどで取れなければ空。
std::string QueryRootName(const std::string &treeUri) {
	if (!EnsureJni()) return std::string();
	JNIEnv *env = Env();
	if (env == 0) return std::string();
	jstring jtree = ToJava(env, treeUri);
	jstring jname = (jstring)env->CallStaticObjectMethod(g.cls, g.rootName, jtree);
	if (env->ExceptionCheck()) env->ExceptionClear();
	std::string name;
	if (jname != 0) {
		name = FromJava(env, jname);
		env->DeleteLocalRef(jname);
	}
	env->DeleteLocalRef(jtree);
	return name;
}

// 権限が無くて名前を聞けないときの代わり。ツリーの URI の末尾
// ("tree/primary%3Amdx") をデコードして "primary:mdx" のように出す。
std::string NameFromTreeUri(const std::string &treeUri) {
	std::string tail = treeUri;
	const size_t slash = tail.rfind('/');
	if (slash != std::string::npos) tail = tail.substr(slash + 1);
	std::string out;
	for (size_t i = 0; i < tail.size(); i++) {
		if (tail[i] == '%' && i + 2 < tail.size()) {
			const char hex[3] = { tail[i + 1], tail[i + 2], '\0' };
			char *end = 0;
			const long v = strtol(hex, &end, 16);
			if (end != 0 && *end == '\0') {
				out += (char)v;
				i += 2;
				continue;
			}
		}
		out += tail[i];
	}
	return out.empty() ? treeUri : out;
}

class SafFileSystem : public FileSystem {
public:
	SafFileSystem(const std::string &treeUri, const std::string &name, bool permitted)
	    : root_(Trim(treeUri)), name_(name), permitted_(permitted) {}

	const char *id() const { return kSafId; }
	// 種類は prefix ("SAF>") で分かるので、こちらは選んだフォルダの名前。
	// 許可が無いときは名前を聞けないので URI の末尾から作る（「許可なし」の
	// 注記は出す側が accessible() を見て添える。ここに入れると警告文や
	// ブックマークの行まで長くなる）。
	std::string label() const { return name_.empty() ? NameFromTreeUri(root_) : name_; }
	const char *prefix() const { return "SAF>"; }

	// 許可が失われている間は読めない（マウントには残す。vfs.h の accessible）。
	bool accessible() const { return permitted_; }
	bool Reconnect() {
		permitted_ = QueryPermission(root_);
		if (permitted_ && name_.empty()) name_ = QueryRootName(root_);
		return permitted_;
	}

	bool removable() const { return true; }
	bool hasPdxDir() const { return true; }
	// 1 段ごとに問い合わせが要るので、起動時のフォールバックは根まで戻す。
	bool parentIsCheap() const { return false; }

	std::string mountRef() const { return std::string(kSafId) + ":" + root_; }

	// **Normalize() を呼んではいけない**（あちらがこれを呼ぶ）。
	bool Contains(const std::string &rel) const {
		const std::string s = Trim(rel);
		if (s.size() < root_.size()) return false;
		if (s.compare(0, root_.size(), root_) != 0) return false;
		return s.size() == root_.size() || s[root_.size()] == '/';
	}

	std::string Root() const { return root_; }

	std::string Normalize(const std::string &rel) const {
		const std::string s = Trim(rel);
		if (!Contains(s)) return root_;
		const std::string sub = NormalizeSub(s.substr(root_.size()));
		return sub.empty() ? root_ : (root_ + "/" + sub);
	}

	bool IsRoot(const std::string &rel) const { return Normalize(rel) == root_; }

	std::string Parent(const std::string &rel) const {
		const std::string s = Normalize(rel);
		if (s == root_) return root_;
		const size_t slash = s.rfind('/');
		if (slash == std::string::npos || slash < root_.size()) return root_;
		return s.substr(0, slash);
	}

	std::string Join(const std::string &dir, const std::string &name) const {
		return Normalize(Normalize(dir) + "/" + name);
	}

	std::string DisplayPath(const std::string &rel) const {
		const std::string sub = Sub(Normalize(rel));
		std::string out = std::string(prefix()) + label();
		if (!sub.empty()) out += "/" + sub;
		return out;
	}

	std::string ResolveInput(const std::string &input, const std::string &base) const {
		if (input.empty()) return base.empty() ? root_ : Normalize(base);
		// すでにツリーの URI から始まっているなら、整えるだけ（コマンドラインや
		// ini から来た ref はこの形。相対として扱うと URI が二重になる）。
		if (Contains(input)) return Normalize(input);
		// 先頭が区切りなら根から。そうでなければ base からの相対。
		if (input[0] == '/' || input[0] == '\\') return Normalize(root_ + "/" + input);
		const std::string dir = base.empty() ? root_ : Normalize(base);
		return Normalize(dir + "/" + input);
	}

	bool List(const std::string &rel, std::vector<DirEntry> *out) const {
		out->clear();
		if (!permitted_ || !EnsureJni()) return false;
		JNIEnv *env = Env();
		if (env == 0) return false;

		jstring jtree = ToJava(env, root_);
		jstring jrel = ToJava(env, Sub(Normalize(rel)));
		jobjectArray arr =
		    (jobjectArray)env->CallStaticObjectMethod(g.cls, g.list, jtree, jrel);
		env->DeleteLocalRef(jtree);
		env->DeleteLocalRef(jrel);
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			return false;
		}
		if (arr == 0) return false;

		const jsize n = env->GetArrayLength(arr);
		for (jsize i = 0; i < n; i++) {
			jstring js = (jstring)env->GetObjectArrayElement(arr, i);
			const std::string s = FromJava(env, js);
			env->DeleteLocalRef(js);
			if (s.size() < 2) continue;
			DirEntry e;
			e.isDir = (s[0] == 'D');
			e.name = s.substr(1);
			out->push_back(e);
		}
		env->DeleteLocalRef(arr);
		return true;
	}

	bool Read(const std::string &rel, std::vector<uint8_t> *out) const {
		out->clear();
		if (!permitted_ || !EnsureJni()) return false;
		JNIEnv *env = Env();
		if (env == 0) return false;

		jstring jtree = ToJava(env, root_);
		jstring jrel = ToJava(env, Sub(Normalize(rel)));
		jbyteArray arr = (jbyteArray)env->CallStaticObjectMethod(g.cls, g.read, jtree, jrel);
		env->DeleteLocalRef(jtree);
		env->DeleteLocalRef(jrel);
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			return false;
		}
		if (arr == 0) return false;

		const jsize n = env->GetArrayLength(arr);
		out->resize((size_t)n);
		if (n > 0) env->GetByteArrayRegion(arr, 0, n, (jbyte *)&(*out)[0]);
		env->DeleteLocalRef(arr);
		return true;
	}

	bool Exists(const std::string &rel) const { return Stat(rel) >= 0; }
	bool IsDir(const std::string &rel) const { return Stat(rel) == 1; }

	bool SamePath(const std::string &a, const std::string &b) const {
		return Normalize(a) == Normalize(b);
	}

private:
	// 末尾の区切りを落とす。
	static std::string Trim(const std::string &s) {
		std::string out = s;
		while (!out.empty() && (out[out.size() - 1] == '/' || out[out.size() - 1] == '\\')) {
			out.erase(out.size() - 1);
		}
		return out;
	}

	// 根からの相対パス（"a/b"）。根そのものなら空。
	std::string Sub(const std::string &normalized) const {
		if (normalized.size() <= root_.size()) return std::string();
		return normalized.substr(root_.size() + 1);
	}

	// "." ".." と余分な区切りを畳む。根より上へは出さない。
	static std::string NormalizeSub(const std::string &raw) {
		std::vector<std::string> parts;
		std::string cur;
		for (size_t i = 0; i <= raw.size(); i++) {
			const char c = (i < raw.size()) ? raw[i] : '/';
			if (c != '/' && c != '\\') {
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

	int Stat(const std::string &rel) const {
		if (!permitted_ || !EnsureJni()) return -1;
		JNIEnv *env = Env();
		if (env == 0) return -1;
		jstring jtree = ToJava(env, root_);
		jstring jrel = ToJava(env, Sub(Normalize(rel)));
		const jint r = env->CallStaticIntMethod(g.cls, g.stat, jtree, jrel);
		env->DeleteLocalRef(jtree);
		env->DeleteLocalRef(jrel);
		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			return -1;
		}
		return (int)r;
	}

	std::string root_;  // ツリーの URI（末尾に '/' は付けない）
	std::string name_;  // 根の表示名（許可が無いと取れないので空のこともある）
	bool permitted_;    // 永続 URI 権限が生きているか
};

}  // namespace

bool SafAvailable() {
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	if (env == 0) return false;
	const jboolean r = env->CallStaticBooleanMethod(g.cls, g.available);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	return r != JNI_FALSE;
}

bool SafPickTree(const std::string &initialTreeUri) {
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	if (env == 0) return false;
	jstring jinit = initialTreeUri.empty() ? 0 : ToJava(env, initialTreeUri);
	const jboolean r = env->CallStaticBooleanMethod(g.cls, g.pickTree, jinit);
	if (jinit != 0) env->DeleteLocalRef(jinit);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	return r != JNI_FALSE;
}

bool SafPollPicked(std::string *uri) {
	uri->clear();
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	if (env == 0) return false;
	jstring js = (jstring)env->CallStaticObjectMethod(g.cls, g.takeResult);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	if (js == 0) return false;  // まだ選び終わっていない
	*uri = FromJava(env, js);
	env->DeleteLocalRef(js);
	return true;  // 取り消したときは空文字列
}

FileSystem *CreateSafFileSystem(const std::string &treeUri) {
	if (treeUri.empty() || !EnsureJni()) return 0;

	// 権限は前の起動から持ち越しているはず。アンインストールで OS が捨てて
	// いる（クラウドバックアップから戻った ini）こともあるので、切れていても
	// **マウントは作る**。読めない状態 (accessible() = false) で一覧に残し、
	// ユーザーが許可を取り直せば Reconnect() で戻る。
	const bool permitted = QueryPermission(treeUri);
	const std::string name = permitted ? QueryRootName(treeUri) : std::string();
	return new SafFileSystem(treeUri, name, permitted);
}

}  // namespace mxv2

#else  // __ANDROID__

namespace mxv2 {

bool SafAvailable() {
	return false;
}

bool SafPickTree(const std::string &initialTreeUri) {
	(void)initialTreeUri;
	return false;
}

bool SafPollPicked(std::string *uri) {
	uri->clear();
	return false;
}

FileSystem *CreateSafFileSystem(const std::string &treeUri) {
	(void)treeUri;
	return 0;
}

}  // namespace mxv2

#endif  // __ANDROID__
