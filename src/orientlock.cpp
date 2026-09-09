// mxv2 - 端末の向きの固定（Android）

#include "orientlock.h"

#ifdef __ANDROID__

#include <cstdio>

#include <jni.h>

#include <SDL.h>

namespace mxv2 {
namespace orientlock {

namespace {

// Java 側 (net.gorry.mxv2.OrientationBridge) の窓口。
// **メインスレッドからだけ呼ぶこと。** FindClass はスレッドのクラスローダを
// 使うので、SDL が後から attach した作業スレッドからではアプリのクラスが
// 見えない（nowplaying.cpp と同じ事情）。
struct OrientJni {
	bool tried;
	bool ok;
	jclass cls;
	jmethodID available;
	jmethodID setOrientation;
};

OrientJni g = { false, false, 0, 0, 0 };

// 最後に出した要求。同じものを出し直さない。
int g_last = -1;

JNIEnv *Env() {
	return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

bool EnsureJni() {
	if (g.tried) return g.ok;
	g.tried = true;

	JNIEnv *env = Env();
	if (env == 0) return false;

	jclass local = env->FindClass("net/gorry/mxv2/OrientationBridge");
	if (local == 0) {
		env->ExceptionClear();
		printf("warning  : OrientationBridge class not found (cannot lock the orientation)\n");
		return false;
	}
	g.cls = (jclass)env->NewGlobalRef(local);
	env->DeleteLocalRef(local);

	g.available = env->GetStaticMethodID(g.cls, "available", "()Z");
	g.setOrientation = env->GetStaticMethodID(g.cls, "setOrientation", "(I)V");

	g.ok = (g.available != 0 && g.setOrientation != 0);
	if (!g.ok) {
		env->ExceptionClear();
		printf("warning  : OrientationBridge methods not found (cannot lock the orientation)\n");
	}
	return g.ok;
}

}  // namespace

bool Available() {
	if (!EnsureJni()) return false;
	JNIEnv *env = Env();
	return env->CallStaticBooleanMethod(g.cls, g.available) != JNI_FALSE;
}

void Set(Request request) {
	if ((int)request == g_last) return;
	if (!Available()) return;
	g_last = (int)request;
	Env()->CallStaticVoidMethod(g.cls, g.setOrientation, (jint)request);
}

}  // namespace orientlock
}  // namespace mxv2

#else  // __ANDROID__

namespace mxv2 {
namespace orientlock {

bool Available() {
	return false;
}

void Set(Request request) {
	(void)request;
}

}  // namespace orientlock
}  // namespace mxv2

#endif  // __ANDROID__
