// mxv2 - 出力先の遅れを測る（outputlatency.h）

#include "outputlatency.h"

// BluetoothName() のキャッシュ時刻は全プラットフォームで SDL を使う。
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <jni.h>
#include <time.h>
#include <android/api-level.h>
#include <SDL.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <SDL.h>
#endif

namespace mxv2 {
namespace outputlatency {

namespace {

// 測り始めてから値を採るまでの待ち。鳴り始めは遅れが落ち着かない
// （Bluetooth は送り始めに 100ms 前後かかる。AudioFlinger の Start latency）。
const int kWarmupMs = 1000;
// この間に採った値の中央値を 1 つの結果にする。
const int kWindowMs = 500;
// しくじったとき（出力先が消えた・開けない）に開き直すまでの待ち。
const int kRetryMs = 1000;

struct State {
	std::mutex mutex;  // thread の起こし・止めだけ
	std::thread thread;
	std::atomic<bool> stop;
	std::atomic<int> latencyMs;
	int sampleRate;
	bool running;
	State() : stop(false), latencyMs(-1), sampleRate(48000), running(false) {}
};

State &G() {
	static State s;
	return s;
}

int64_t NowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

// 採った値（ナノ秒）を窓ごとに中央値にして渡す。
class Collector {
public:
	Collector() : start_(NowMs()), windowStart_(0) {}
	void Add(int64_t latencyNs) {
		const int64_t now = NowMs();
		if (now - start_ < kWarmupMs) return;
		if (windowStart_ == 0) windowStart_ = now;
		// 負の値や 2 秒を超える値は、時刻がまだ来ていないなど採り方の不具合。
		if (latencyNs < 0 || latencyNs > 2000000000LL) return;
		samples_.push_back(latencyNs);
		if (now - windowStart_ >= kWindowMs) {
			std::sort(samples_.begin(), samples_.end());
			const int64_t median = samples_[samples_.size() / 2];
			G().latencyMs.store((int)((median + 500000) / 1000000), std::memory_order_relaxed);
			samples_.clear();
			windowStart_ = now;
		}
	}

private:
	int64_t start_;
	int64_t windowStart_;
	std::vector<int64_t> samples_;
};

void SleepMs(int ms) {
	// 止めるときに待たせないよう、細かく区切って眠る。
	for (int i = 0; i < ms && !G().stop.load(); i += 10) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

#if defined(__ANDROID__)

// AAudio は API 26 から。minSdk が 21 なので、SDL と同じく dlopen で引く。
struct AAudioApi {
	bool tried;
	bool ok;
	aaudio_result_t (*createStreamBuilder)(AAudioStreamBuilder **);
	void (*setSampleRate)(AAudioStreamBuilder *, int32_t);
	void (*setChannelCount)(AAudioStreamBuilder *, int32_t);
	void (*setFormat)(AAudioStreamBuilder *, aaudio_format_t);
	void (*setDirection)(AAudioStreamBuilder *, aaudio_direction_t);
	aaudio_result_t (*openStream)(AAudioStreamBuilder *, AAudioStream **);
	aaudio_result_t (*builderDelete)(AAudioStreamBuilder *);
	aaudio_result_t (*requestStart)(AAudioStream *);
	aaudio_result_t (*requestStop)(AAudioStream *);
	aaudio_result_t (*close)(AAudioStream *);
	aaudio_result_t (*write)(AAudioStream *, const void *, int32_t, int64_t);
	aaudio_result_t (*getTimestamp)(AAudioStream *, clockid_t, int64_t *, int64_t *);
	int64_t (*getFramesWritten)(AAudioStream *);
	int32_t (*getSampleRate)(AAudioStream *);
};

AAudioApi g_aa;

bool LoadAAudio() {
	AAudioApi &a = g_aa;
	if (a.tried) return a.ok;
	a.tried = true;
	if (android_get_device_api_level() < 26) return false;
	void *lib = dlopen("libaaudio.so", RTLD_NOW);
	if (lib == 0) return false;
#define MXV2_AA(field, name)                                   \
	*(void **)(&a.field) = dlsym(lib, name);                   \
	if (a.field == 0) return false;
	MXV2_AA(createStreamBuilder, "AAudio_createStreamBuilder")
	MXV2_AA(setSampleRate, "AAudioStreamBuilder_setSampleRate")
	MXV2_AA(setChannelCount, "AAudioStreamBuilder_setChannelCount")
	MXV2_AA(setFormat, "AAudioStreamBuilder_setFormat")
	MXV2_AA(setDirection, "AAudioStreamBuilder_setDirection")
	MXV2_AA(openStream, "AAudioStreamBuilder_openStream")
	MXV2_AA(builderDelete, "AAudioStreamBuilder_delete")
	MXV2_AA(requestStart, "AAudioStream_requestStart")
	MXV2_AA(requestStop, "AAudioStream_requestStop")
	MXV2_AA(close, "AAudioStream_close")
	MXV2_AA(write, "AAudioStream_write")
	MXV2_AA(getTimestamp, "AAudioStream_getTimestamp")
	MXV2_AA(getFramesWritten, "AAudioStream_getFramesWritten")
	MXV2_AA(getSampleRate, "AAudioStream_getSampleRate")
#undef MXV2_AA
	a.ok = true;
	return true;
}

int64_t MonoNs() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// ストリームを 1 本開いて、止めるかしくじるまで測る。
void MeasureOnce(int sampleRate) {
	AAudioApi &a = g_aa;
	AAudioStreamBuilder *builder = 0;
	if (a.createStreamBuilder(&builder) != AAUDIO_OK) return;
	// SDL（SDL_aaudio.c）と同じ条件にする。性能の段・共有の仕方・用途は
	// SDL も指定していないので既定のまま（AudioTrack の経路になり、
	// バッファの長さも SDL のものとそろう）。
	a.setSampleRate(builder, sampleRate);
	a.setChannelCount(builder, 2);
	a.setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	a.setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStream *stream = 0;
	const aaudio_result_t opened = a.openStream(builder, &stream);
	a.builderDelete(builder);
	if (opened != AAUDIO_OK || stream == 0) return;

	const int rate = a.getSampleRate(stream) > 0 ? a.getSampleRate(stream) : sampleRate;
	const int chunk = rate / 100;  // 10ms
	std::vector<int16_t> silence((size_t)chunk * 2, 0);
	Collector collector;
	if (a.requestStart(stream) == AAUDIO_OK) {
		while (!G().stop.load()) {
			// 書けるまで待つ（最大 100ms）。バッファはいつも満ちている。
			const aaudio_result_t r = a.write(stream, silence.data(), chunk, 100000000LL);
			if (r < 0) break;  // 出力先が消えた（MMAP の経路など）→ 開き直す
			int64_t pos = 0;
			int64_t presentedNs = 0;
			if (a.getTimestamp(stream, CLOCK_MONOTONIC, &pos, &presentedNs) != AAUDIO_OK) {
				continue;
			}
			// pos のフレームが presentedNs に鳴った。いま書き終えた端のフレームは
			// (written - pos) フレームあとに鳴る。
			const int64_t written = a.getFramesWritten(stream);
			const int64_t playAtNs = presentedNs + (written - pos) * 1000000000LL / rate;
			collector.Add(playAtNs - MonoNs());
		}
		a.requestStop(stream);
	}
	a.close(stream);
}

bool PlatformAvailable() { return LoadAAudio(); }

#elif defined(_WIN32)

template <class T>
void SafeRelease(T **p) {
	if (*p != 0) {
		(*p)->Release();
		*p = 0;
	}
}

// 既定の出力の ID。替わったら開き直す（SDL も既定の出力を追いかける）。
std::wstring DefaultDeviceId(IMMDeviceEnumerator *enumerator) {
	std::wstring out;
	IMMDevice *device = 0;
	if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
		LPWSTR id = 0;
		if (SUCCEEDED(device->GetId(&id)) && id != 0) {
			out = id;
			CoTaskMemFree(id);
		}
		device->Release();
	}
	return out;
}

// 出力の装置 (endpoint) が Bluetooth の機器のものか。endpoint の親の
// デバイスノードが BTHENUM（A2DP）・BTHHFENUM（ハンズフリー）・BTHLE…（LE
// Audio）なら Bluetooth。
bool IsBluetoothEndpoint(const std::wstring &endpointId) {
	std::wstring instance = L"SWD\\MMDEVAPI\\" + endpointId;
	DEVINST node = 0;
	if (CM_Locate_DevNodeW(&node, (DEVINSTID_W)&instance[0], CM_LOCATE_DEVNODE_NORMAL) !=
	    CR_SUCCESS) {
		return false;
	}
	DEVINST parent = 0;
	if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) return false;
	wchar_t id[MAX_DEVICE_ID_LEN + 1];
	if (CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return false;
	id[MAX_DEVICE_ID_LEN] = 0;
	return _wcsnicmp(id, L"BTH", 3) == 0;
}

// endpoint の「機器の名前」（PKEY_DeviceInterface_FriendlyName。サウンドの
// 設定で「ヘッドホン (BT525 FM)」のかっこの中に出る名前）。UTF-8。
// functiondiscoverykeys_devpkey.h の定義は INITGUID 無しでは実体が無いので、
// 値をここに写してある。
std::string EndpointDeviceName(IMMDevice *device) {
	static const PROPERTYKEY kDeviceInterfaceFriendlyName = {
	    {0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}}, 2};
	std::string out;
	IPropertyStore *props = 0;
	if (FAILED(device->OpenPropertyStore(STGM_READ, &props))) return out;
	PROPVARIANT v;
	PropVariantInit(&v);
	if (SUCCEEDED(props->GetValue(kDeviceInterfaceFriendlyName, &v)) && v.vt == VT_LPWSTR &&
	    v.pwszVal != 0) {
		const int n = WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, NULL, 0, NULL, NULL);
		if (n > 1) {
			out.resize((size_t)n);
			WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, &out[0], n, NULL, NULL);
			out.resize((size_t)n - 1);
		}
	}
	PropVariantClear(&v);
	props->Release();
	return out;
}

double NowSec() {
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	return (double)c.QuadPart / (double)f.QuadPart;
}

void MeasureOnce(int) {
	IMMDeviceEnumerator *enumerator = 0;
	IMMDevice *device = 0;
	IAudioClient *client = 0;
	IAudioRenderClient *render = 0;
	IAudioClock *clock = 0;
	WAVEFORMATEX *format = 0;
	HANDLE event = 0;
	std::wstring deviceId;
	UINT32 bufferFrames = 0;
	UINT64 frequency = 0;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
	                              IID_PPV_ARGS(&enumerator));
	if (FAILED(hr)) goto done;
	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(hr)) goto done;
	{
		LPWSTR id = 0;
		if (SUCCEEDED(device->GetId(&id)) && id != 0) {
			deviceId = id;
			CoTaskMemFree(id);
		}
	}
	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&client);
	if (FAILED(hr)) goto done;
	hr = client->GetMixFormat(&format);
	if (FAILED(hr)) goto done;
	// SDL（SDL_wasapi.c）と同じく、共有・イベント駆動・バッファ長は既定。
	// 形式は変換の段を増やさないよう装置の形式そのまま（無音なので中身は 0）。
	hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0,
	                        format, NULL);
	if (FAILED(hr)) goto done;
	event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (event == 0) goto done;
	if (FAILED(client->SetEventHandle(event))) goto done;
	if (FAILED(client->GetBufferSize(&bufferFrames))) goto done;
	if (FAILED(client->GetService(IID_PPV_ARGS(&render)))) goto done;
	if (FAILED(client->GetService(IID_PPV_ARGS(&clock)))) goto done;
	if (FAILED(clock->GetFrequency(&frequency)) || frequency == 0) goto done;
	{
		const double rate = (double)format->nSamplesPerSec;
		UINT64 written = 0;
		BYTE *data = 0;
		if (FAILED(render->GetBuffer(bufferFrames, &data))) goto done;
		render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
		written += bufferFrames;
		if (FAILED(client->Start())) goto done;
		Collector collector;
		int64_t nextDeviceCheck = NowMs() + 1000;
		while (!G().stop.load()) {
			if (WaitForSingleObject(event, 200) != WAIT_OBJECT_0) continue;
			UINT32 padding = 0;
			if (FAILED(client->GetCurrentPadding(&padding))) break;  // 出力先が消えた
			const UINT32 avail = bufferFrames - padding;
			if (avail > 0) {
				if (FAILED(render->GetBuffer(avail, &data))) break;
				render->ReleaseBuffer(avail, AUDCLNT_BUFFERFLAGS_SILENT);
				written += avail;
			}
			// pos（frequency 単位）が qpc（100ns 単位）の時刻に鳴っていた。
			// いま書き終えた端は、そこから (written - pos) ぶんあとに鳴る。
			UINT64 pos = 0, qpc = 0;
			if (FAILED(clock->GetPosition(&pos, &qpc))) break;
			const double playAt =
			    (double)qpc * 1e-7 + (double)written / rate - (double)pos / (double)frequency;
			collector.Add((int64_t)((playAt - NowSec()) * 1e9));
			if (NowMs() >= nextDeviceCheck) {
				nextDeviceCheck = NowMs() + 1000;
				if (DefaultDeviceId(enumerator) != deviceId) break;  // 既定が替わった
			}
		}
		client->Stop();
	}
done:
	if (format != 0) CoTaskMemFree(format);
	if (event != 0) CloseHandle(event);
	SafeRelease(&clock);
	SafeRelease(&render);
	SafeRelease(&client);
	SafeRelease(&device);
	SafeRelease(&enumerator);
}

bool PlatformAvailable() { return true; }

#else

void MeasureOnce(int) {}
bool PlatformAvailable() { return false; }

#endif

void ThreadMain(int sampleRate) {
#if defined(_WIN32)
	const bool com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
#endif
	while (!G().stop.load()) {
		MeasureOnce(sampleRate);
		SleepMs(kRetryMs);
	}
#if defined(_WIN32)
	if (com) CoUninitialize();
#endif
}

void StopThread() {
	State &g = G();
	if (!g.running) return;
	g.stop.store(true);
	if (g.thread.joinable()) g.thread.join();
	g.running = false;
}

}  // namespace

bool Available() {
	static int cached = -1;
	if (cached < 0) cached = PlatformAvailable() ? 1 : 0;
	return cached != 0;
}

void SetActive(bool active, int sampleRate) {
	State &g = G();
	std::lock_guard<std::mutex> lock(g.mutex);
	if (active && !Available()) return;
	if (active && g.running && g.sampleRate != sampleRate) StopThread();
	if (active == g.running) return;
	if (!active) {
		StopThread();
		return;
	}
	g.sampleRate = sampleRate;
	g.stop.store(false);
	g.thread = std::thread(ThreadMain, sampleRate);
	g.running = true;
}

int LatencyMs() { return G().latencyMs.load(std::memory_order_relaxed); }

#if defined(__ANDROID__)

namespace {

// Java 側 (net.gorry.mxv2.AudioRouteBridge) の窓口。FindClass はスレッドの
// クラスローダを使うので、メインスレッドからだけ引く（nowplaying.cpp と同じ）。
struct RouteJni {
	bool tried;
	bool ok;
	jclass cls;
	jmethodID name;
};
RouteJni g_route = { false, false, 0, 0 };

bool EnsureRouteJni(JNIEnv *env) {
	if (g_route.tried) return g_route.ok;
	g_route.tried = true;
	jclass local = env->FindClass("net/gorry/mxv2/AudioRouteBridge");
	if (local == 0) {
		env->ExceptionClear();
		printf("warning  : AudioRouteBridge class not found (no Bluetooth device name)\n");
		return false;
	}
	g_route.cls = (jclass)env->NewGlobalRef(local);
	env->DeleteLocalRef(local);
	g_route.name = env->GetStaticMethodID(g_route.cls, "bluetoothOutputName", "()Ljava/lang/String;");
	if (g_route.name == 0) {
		env->ExceptionClear();
		printf("warning  : AudioRouteBridge methods not found (no Bluetooth device name)\n");
		return false;
	}
	g_route.ok = true;
	return true;
}

std::string QueryRouteName() {
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	if (env == 0 || !EnsureRouteJni(env)) return std::string();
	jstring js = (jstring)env->CallStaticObjectMethod(g_route.cls, g_route.name);
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return std::string();
	}
	// GetStringUTFChars は「修正 UTF-8」を返し、絵文字などの 4 バイト文字が
	// 崩れる（機器の名前は利用者が付けられる）ので、UTF-16 から自分で作る。
	std::string out;
	if (js != 0) {
		const jsize len = env->GetStringLength(js);
		std::vector<jchar> u16((size_t)len);
		if (len > 0) env->GetStringRegion(js, 0, len, &u16[0]);
		env->DeleteLocalRef(js);
		for (size_t i = 0; i < u16.size(); i++) {
			uint32_t cp = u16[i];
			if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < u16.size() && u16[i + 1] >= 0xdc00 &&
			    u16[i + 1] < 0xe000) {
				cp = 0x10000 + ((cp - 0xd800) << 10) + (u16[i + 1] - 0xdc00);
				i++;
			}
			if (cp < 0x80) {
				out += (char)cp;
			} else if (cp < 0x800) {
				out += (char)(0xc0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3f));
			} else if (cp < 0x10000) {
				out += (char)(0xe0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3f));
				out += (char)(0x80 | (cp & 0x3f));
			} else {
				out += (char)(0xf0 | (cp >> 18));
				out += (char)(0x80 | ((cp >> 12) & 0x3f));
				out += (char)(0x80 | ((cp >> 6) & 0x3f));
				out += (char)(0x80 | (cp & 0x3f));
			}
		}
	}
	return out;
}

}  // namespace

#elif defined(_WIN32)

namespace {

// 既定の出力が Bluetooth の機器ならその名前。測る作業スレッドとは別に、
// メインスレッドで引く（演奏していなくても分かるように。設定の
// [Bluetooth（機器名）] は、つないでいる機器があるときだけ出す）。
std::string QueryRouteName() {
	// SDL もメインスレッドで COM を STA で使うので、それに合わせる。
	// 対にする CoUninitialize は呼ばない（終わるまで使う）。
	static bool comTried = false;
	static IMMDeviceEnumerator *enumerator = 0;
	if (!comTried) {
		comTried = true;
		const HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		// RPC_E_CHANGED_MODE は「別の形で初期化済み」。それでも使える。
		if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
			CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			                 IID_PPV_ARGS(&enumerator));
		}
	}
	if (enumerator == 0) return std::string();

	std::string name;
	IMMDevice *device = 0;
	if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return name;
	LPWSTR id = 0;
	if (SUCCEEDED(device->GetId(&id)) && id != 0) {
		if (IsBluetoothEndpoint(id)) {
			name = EndpointDeviceName(device);
			if (name.empty()) name = "Bluetooth";  // 空だと Bluetooth でないと読める
		}
		CoTaskMemFree(id);
	}
	device->Release();
	return name;
}

}  // namespace

#else

namespace {
std::string QueryRouteName() { return std::string(); }
}  // namespace

#endif

namespace {

// 引き直す間隔と、最後に引いた結果。
const uint32_t kRouteCheckMs = 1000;
bool g_routeValid = false;
uint32_t g_routeTicks = 0;
std::string g_routeName;

}  // namespace

std::string BluetoothName() {
	const uint32_t now = SDL_GetTicks();
	if (!g_routeValid || now - g_routeTicks >= kRouteCheckMs) {
		g_routeName = QueryRouteName();
		g_routeValid = true;
		g_routeTicks = now;
	}
	return g_routeName;
}

bool OutputIsBluetooth() { return !BluetoothName().empty(); }

void RefreshBluetoothName() { g_routeValid = false; }

void Shutdown() {
	State &g = G();
	std::lock_guard<std::mutex> lock(g.mutex);
	StopThread();
}

}  // namespace outputlatency
}  // namespace mxv2
