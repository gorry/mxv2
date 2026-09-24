// mxv2 - HTTPS で 1 つの URL を取ってくる（更新チェック用）

#include "httpget.h"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif defined(__ANDROID__)
#include <jni.h>
#include <SDL.h>
#endif

namespace mxv2 {
namespace httpget {

namespace {

// 相手が黙ったときに諦めるまでの時間。更新チェックは作業スレッドで回すので
// 画面は止まらないが、終了のときに待たされないよう長くはしない。
const int kTimeoutMs = 15000;

}  // namespace

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows: WinHTTP
// ---------------------------------------------------------------------------
namespace {

std::wstring Widen(const std::string &s) {
	if (s.empty()) return std::wstring();
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

std::string LastError(const char *what) {
	char buf[96];
	snprintf(buf, sizeof(buf), "%s failed (error %lu)", what, (unsigned long)GetLastError());
	return buf;
}

// WinHTTP のハンドルを閉じ忘れないための入れ物。
struct Handle {
	HINTERNET h;
	explicit Handle(HINTERNET v) : h(v) {}
	~Handle() {
		if (h != NULL) WinHttpCloseHandle(h);
	}
};

}  // namespace

bool Available() { return true; }
void Prepare() {}

bool Get(const std::string &url, const std::string &userAgent, const std::string &accept,
         int *status, std::string *body, std::string *err) {
	*status = 0;
	body->clear();

	const std::wstring wurl = Widen(url);
	URL_COMPONENTS uc;
	ZeroMemory(&uc, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
		*err = LastError("WinHttpCrackUrl");
		return false;
	}
	const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
	std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo != NULL) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

	// プロキシは OS の設定に従う。AUTOMATIC_PROXY は Windows 8.1 以降なので、
	// 開けなければ従来の DEFAULT_PROXY（netsh winhttp の設定）で開き直す。
	const std::wstring ua = Widen(userAgent);
	Handle session(WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (session.h == NULL) {
		session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	if (session.h == NULL) {
		*err = LastError("WinHttpOpen");
		return false;
	}
	WinHttpSetTimeouts(session.h, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

	Handle connect(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
	if (connect.h == NULL) {
		*err = LastError("WinHttpConnect");
		return false;
	}
	const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	Handle request(WinHttpOpenRequest(connect.h, L"GET", path.c_str(), NULL,
	                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
	if (request.h == NULL) {
		*err = LastError("WinHttpOpenRequest");
		return false;
	}
	if (!accept.empty()) {
		const std::wstring header = L"Accept: " + Widen(accept);
		WinHttpAddRequestHeaders(request.h, header.c_str(), (DWORD)-1L,
		                         WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
	}
	if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
	                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		*err = LastError("WinHttpSendRequest");
		return false;
	}
	if (!WinHttpReceiveResponse(request.h, NULL)) {
		*err = LastError("WinHttpReceiveResponse");
		return false;
	}

	DWORD code = 0;
	DWORD size = sizeof(code);
	if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                         WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
	                         WINHTTP_NO_HEADER_INDEX)) {
		*err = LastError("WinHttpQueryHeaders");
		return false;
	}
	*status = (int)code;

	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request.h, &avail)) {
			*err = LastError("WinHttpQueryDataAvailable");
			return false;
		}
		if (avail == 0) break;
		const size_t old = body->size();
		body->resize(old + avail);
		DWORD got = 0;
		if (!WinHttpReadData(request.h, &(*body)[old], avail, &got)) {
			*err = LastError("WinHttpReadData");
			return false;
		}
		body->resize(old + got);
		if (body->size() > kMaxBodyBytes) {
			*err = "response too large";
			return false;
		}
	}
	return true;
}

#elif defined(__ANDROID__)

// ---------------------------------------------------------------------------
// Android: net.gorry.mxv2.HttpBridge (HttpURLConnection)
//
// **Prepare() はメインスレッドから。** FindClass はスレッドのクラスローダを
// 使うので、SDL が後から attach した作業スレッドからではアプリのクラスが
// 見えない（safaccess.cpp と同じ）。掴んだ jclass はグローバル参照で持ち続け、
// 以降はどのスレッドからでも呼ぶ。
// ---------------------------------------------------------------------------
namespace {

struct HttpJni {
	bool tried;
	bool ok;
	jclass cls;
	jmethodID get;
};

HttpJni g = { false, false, 0, 0 };

JNIEnv *Env() { return (JNIEnv *)SDL_AndroidGetJNIEnv(); }

std::string JString(JNIEnv *env, jobject obj) {
	if (obj == 0) return std::string();
	const char *s = env->GetStringUTFChars((jstring)obj, 0);
	std::string out = (s != 0) ? s : "";
	if (s != 0) env->ReleaseStringUTFChars((jstring)obj, s);
	return out;
}

}  // namespace

bool Available() { return true; }

void Prepare() {
	if (g.tried) return;
	g.tried = true;
	JNIEnv *env = Env();
	if (env == 0) return;
	jclass local = env->FindClass("net/gorry/mxv2/HttpBridge");
	if (local == 0) {
		env->ExceptionClear();
		printf("warning  : HttpBridge class not found (update check disabled)\n");
		return;
	}
	g.cls = (jclass)env->NewGlobalRef(local);
	env->DeleteLocalRef(local);
	g.get = env->GetStaticMethodID(
	    g.cls, "get",
	    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;");
	if (g.get == 0) {
		env->ExceptionClear();
		return;
	}
	g.ok = true;
}

bool Get(const std::string &url, const std::string &userAgent, const std::string &accept,
         int *status, std::string *body, std::string *err) {
	*status = 0;
	body->clear();
	if (!g.ok) {
		*err = "HttpBridge is not available";
		return false;
	}
	JNIEnv *env = Env();
	if (env == 0) {
		*err = "no JNIEnv";
		return false;
	}
	jstring jurl = env->NewStringUTF(url.c_str());
	jstring jua = env->NewStringUTF(userAgent.c_str());
	jstring jaccept = env->NewStringUTF(accept.c_str());
	// 戻り値は { 状態番号, 本文, 誤り } の 3 つ（HttpBridge.get）。
	jobjectArray res = (jobjectArray)env->CallStaticObjectMethod(g.cls, g.get, jurl, jua, jaccept,
	                                                            (jint)kTimeoutMs);
	env->DeleteLocalRef(jurl);
	env->DeleteLocalRef(jua);
	env->DeleteLocalRef(jaccept);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		*err = "HttpBridge.get threw";
		return false;
	}
	if (res == 0 || env->GetArrayLength(res) < 3) {
		*err = "HttpBridge.get returned nothing";
		return false;
	}
	jobject o0 = env->GetObjectArrayElement(res, 0);
	jobject o1 = env->GetObjectArrayElement(res, 1);
	jobject o2 = env->GetObjectArrayElement(res, 2);
	const std::string code = JString(env, o0);
	*body = JString(env, o1);
	const std::string e = JString(env, o2);
	if (o0 != 0) env->DeleteLocalRef(o0);
	if (o1 != 0) env->DeleteLocalRef(o1);
	if (o2 != 0) env->DeleteLocalRef(o2);
	env->DeleteLocalRef(res);

	*status = atoi(code.c_str());
	if (!e.empty()) {
		*err = e;
		return false;
	}
	return true;
}

#else

// ---------------------------------------------------------------------------
// そのほかの環境: 使えない
// ---------------------------------------------------------------------------
bool Available() { return false; }
void Prepare() {}

bool Get(const std::string &, const std::string &, const std::string &, int *status,
         std::string *body, std::string *err) {
	*status = 0;
	body->clear();
	*err = "HTTP is not supported on this platform";
	return false;
}

#endif

}  // namespace httpget
}  // namespace mxv2
