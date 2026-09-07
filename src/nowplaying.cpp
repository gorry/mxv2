// mxv2 - 演奏状態の通知（Android の通知領域）

#include "nowplaying.h"

#ifdef __ANDROID__

#include <cstdio>
#include <vector>

#include <jni.h>

#include <SDL.h>

namespace mxv2 {
namespace nowplaying {

namespace {

// -------------------------------------------------------------------------
// Java 側 (net.gorry.mxv2.PlaybackBridge) の窓口
//
// **メインスレッドからだけ呼ぶこと。** FindClass はスレッドのクラスローダを
// 使うので、SDL が後から attach した作業スレッドからではアプリのクラスが
// 見えない（safaccess.cpp と同じ事情）。
// -------------------------------------------------------------------------
struct PlaybackJni {
	bool tried;
	bool ok;
	jclass cls;
	jmethodID available;
	jmethodID setLabels;
	jmethodID update;
	jmethodID shutdown;
	jmethodID takeRequest;
};

PlaybackJni g = { false, false, 0, 0, 0, 0, 0, 0 };

// 出している内容。同じものを出し直さないために覚えておく。
struct Shown {
	bool valid;
	bool active;
	bool playing;
	std::string title;
	std::string text;
	uint32_t posMs;    // 最後に渡した演奏位置
	uint32_t atTicks;  // それを渡した時刻 (SDL_GetTicks)
};

Shown g_shown = { false, false, false, std::string(), std::string(), 0, 0 };

// 演奏位置が「そのまま進んだ場合」からこれだけ外れたら出し直す (ms)。
// ロック画面のシークバーは渡した位置から自分で進むので、ふだんは放って
// おけばよい。シークや掛け直しで飛んだときだけ渡し直す。
const uint32_t kPosSlackMs = 1500;

// Java 側の窓口が使える（＝Activity が居る）と分かったら立てる。
bool g_available = false;

JNIEnv *Env() {
	return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

bool EnsureJni() {
	if (g.tried) return g.ok;
	g.tried = true;

	JNIEnv *env = Env();
	if (env == 0) return false;

	jclass local = env->FindClass("net/gorry/mxv2/PlaybackBridge");
	if (local == 0) {
		env->ExceptionClear();
		printf("warning  : PlaybackBridge class not found (no playback notification)\n");
		return false;
	}
	g.cls = (jclass)env->NewGlobalRef(local);
	env->DeleteLocalRef(local);

	g.available = env->GetStaticMethodID(g.cls, "available", "()Z");
	g.setLabels = env->GetStaticMethodID(
	    g.cls, "setLabels",
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
	    "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	g.update = env->GetStaticMethodID(g.cls, "update",
	                                  "(Ljava/lang/String;Ljava/lang/String;ZJJ)V");
	g.shutdown = env->GetStaticMethodID(g.cls, "shutdown", "()V");
	g.takeRequest = env->GetStaticMethodID(g.cls, "takeRequest", "()I");

	g.ok = (g.available != 0 && g.setLabels != 0 && g.update != 0 && g.shutdown != 0 &&
	        g.takeRequest != 0);
	if (!g.ok) {
		env->ExceptionClear();
		printf("warning  : PlaybackBridge methods not found (no playback notification)\n");
	}
	return g.ok;
}

// JNI の NewStringUTF は「修正 UTF-8」を取るので、素の UTF-8 をそのまま
// 渡すと絵文字などの 4 バイト文字で崩れる。UTF-16 を経由して自分で作る
// （safaccess.cpp の Utf8ToUtf16 と同じ理屈）。
jstring NewJString(JNIEnv *env, const std::string &s) {
	std::vector<jchar> u16;
	u16.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = (unsigned char)s[i];
		uint32_t cp = 0;
		int len = 1;
		if (c < 0x80) {
			cp = c;
		} else if ((c & 0xe0) == 0xc0) {
			cp = c & 0x1f;
			len = 2;
		} else if ((c & 0xf0) == 0xe0) {
			cp = c & 0x0f;
			len = 3;
		} else if ((c & 0xf8) == 0xf0) {
			cp = c & 0x07;
			len = 4;
		} else {
			// 壊れた並び。1 バイト捨てて続ける。
			i++;
			continue;
		}
		if (i + (size_t)len > s.size()) break;
		for (int k = 1; k < len; k++) {
			cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3f);
		}
		i += (size_t)len;
		if (cp < 0x10000) {
			u16.push_back((jchar)cp);
		} else {
			cp -= 0x10000;
			u16.push_back((jchar)(0xd800 + (cp >> 10)));
			u16.push_back((jchar)(0xdc00 + (cp & 0x3ff)));
		}
	}
	static const jchar kEmpty = 0;
	return env->NewString(u16.empty() ? &kEmpty : &u16[0], (jsize)u16.size());
}

}  // namespace

// Activity が居るかどうかは起動直後の 1 回しか変わらない（onCreate で
// 渡している）ので、一度 true になったらそのまま覚えておく。Update は
// 毎フレーム呼ばれるので、そのたびに JNI を跨がないようにする。
bool Available() {
	if (g_available) return true;
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	if (env == 0) return false;
	g_available = (env->CallStaticBooleanMethod(g.cls, g.available) != JNI_FALSE);
	return g_available;
}

void SetLabels(const Labels &labels) {
	if (!Available()) return;
	JNIEnv *env = Env();

	jstring a = NewJString(env, labels.channel);
	jstring b = NewJString(env, labels.channelDesc);
	jstring c = NewJString(env, labels.prev);
	jstring d = NewJString(env, labels.play);
	jstring e = NewJString(env, labels.pause);
	jstring f = NewJString(env, labels.next);
	jstring h = NewJString(env, labels.stop);
	env->CallStaticVoidMethod(g.cls, g.setLabels, a, b, c, d, e, f, h);
	env->DeleteLocalRef(a);
	env->DeleteLocalRef(b);
	env->DeleteLocalRef(c);
	env->DeleteLocalRef(d);
	env->DeleteLocalRef(e);
	env->DeleteLocalRef(f);
	env->DeleteLocalRef(h);
}

void Update(const State &state) {
	if (!Available()) return;

	const uint32_t now = SDL_GetTicks();

	// 位置 (posMs) は毎フレーム変わるが、通知側が自分で進めるので、ふだんは
	// 見比べない。**飛んだとき**（シーク・掛け直し）だけ渡し直す。
	bool same = g_shown.valid && g_shown.active == state.active &&
	            g_shown.playing == state.playing && g_shown.title == state.title &&
	            g_shown.text == state.text;
	if (same && state.active && state.playing) {
		const uint32_t expect = g_shown.posMs + (now - g_shown.atTicks);
		const uint32_t diff =
		    (state.posMs > expect) ? (state.posMs - expect) : (expect - state.posMs);
		if (diff > kPosSlackMs) same = false;
	}
	if (same) return;

	g_shown.valid = true;
	g_shown.active = state.active;
	g_shown.playing = state.playing;
	g_shown.title = state.title;
	g_shown.text = state.text;
	g_shown.posMs = state.posMs;
	g_shown.atTicks = now;

	JNIEnv *env = Env();
	if (!state.active) {
		env->CallStaticVoidMethod(g.cls, g.shutdown);
		return;
	}

	jstring title = NewJString(env, state.title);
	jstring text = NewJString(env, state.text);
	env->CallStaticVoidMethod(g.cls, g.update, title, text,
	                          state.playing ? JNI_TRUE : JNI_FALSE, (jlong)state.posMs,
	                          (jlong)state.durMs);
	env->DeleteLocalRef(title);
	env->DeleteLocalRef(text);
}

void Shutdown() {
	if (!Available()) return;
	g_shown.valid = true;
	g_shown.active = false;
	Env()->CallStaticVoidMethod(g.cls, g.shutdown);
}

Request TakeRequest() {
	if (!Available()) return kRequestNone;
	const jint r = Env()->CallStaticIntMethod(g.cls, g.takeRequest);
	// Java 側の定数はこの enum と同じ並び (PlaybackBridge.REQ_*)。
	if (r <= kRequestNone || r > kRequestFocusGained) return kRequestNone;
	return (Request)r;
}

}  // namespace nowplaying
}  // namespace mxv2

#else  // __ANDROID__

// Android 以外は何もしない。通知の仕組みが無い環境でも、呼ぶ側は同じ形で
// 書けるようにしておく。
namespace mxv2 {
namespace nowplaying {

bool Available() {
	return false;
}
void SetLabels(const Labels &) {}
void Update(const State &) {}
void Shutdown() {}
Request TakeRequest() {
	return kRequestNone;
}

}  // namespace nowplaying
}  // namespace mxv2

#endif  // __ANDROID__
