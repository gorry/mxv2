// mxv2 - 多重起動の抑止（OS ごとの実装。方針は singleinstance.h）

#include "singleinstance.h"

#include <cstring>
#include <vector>

#include "appprofile.h"  // CMake が Profile.ini から生成する

#ifdef _WIN32
#include <windows.h>
#endif

namespace mxv2 {
namespace singleinstance {

#ifdef _WIN32

namespace {

// 名前は AppId から作る。`Local\` はログオンセッションごとの名前空間で、
// **別のユーザーが同時に使っていてもぶつからない**（Global\ にすると、
// ユーザー切り替えの相手の mxv2 を見つけてしまう）。
// デバッグ構成は別名にして、Release 版と並べて動かせるようにしておく。
#ifdef NDEBUG
const char kMutexName[] = "Local\\" MXV2_APP_ID_WINDOWS "-singleinstance";
const char kIpcClassName[] = "mxv2-singleinstance-ipc";
#else
const char kMutexName[] = "Local\\" MXV2_APP_ID_WINDOWS ".debug-singleinstance";
const char kIpcClassName[] = "mxv2-singleinstance-ipc-debug";
#endif

// WM_COPYDATA の合図。中身は UTF-8 の「開いてほしいもの」（空でもよい）。
const ULONG_PTR kCopyDataId = 0x6d787632;  // 'mxv2'

// 受け口が立ち上がるのを待つ時間。相手が起動しかけだと、ミューテックスは
// あるのにウィンドウがまだ無い、という隙間がある。
const int kWaitForIpcMs = 2000;
const int kWaitStepMs = 50;

HANDLE g_mutex = NULL;
HWND g_ipcWindow = NULL;
HWND g_mainWindow = NULL;
std::vector<std::string> g_pending;

LRESULT CALLBACK IpcWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_COPYDATA) {
		const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lp;
		if (cds != 0 && cds->dwData == kCopyDataId) {
			std::string s;
			if (cds->cbData > 0 && cds->lpData != 0) {
				s.assign((const char *)cds->lpData, cds->cbData);
				const size_t z = s.find('\0');  // 終端が入っていても落とす
				if (z != std::string::npos) s.resize(z);
			}
			g_pending.push_back(s);
			// 旧 mxv と同じで、渡されたら前面へ出る。最小化していたら戻す
			// （mxv2 は最小化のまま起動できるので、そのままだと見えない）。
			if (g_mainWindow != NULL) {
				if (IsIconic(g_mainWindow)) ShowWindow(g_mainWindow, SW_RESTORE);
				SetForegroundWindow(g_mainWindow);
				SetFocus(g_mainWindow);
			}
			return TRUE;
		}
	}
	return DefWindowProcA(hwnd, msg, wp, lp);
}

}  // namespace

bool HandOffToExisting(const std::string &target) {
	if (g_mutex != NULL) return false;  // 二度は呼ばない

	g_mutex = CreateMutexA(NULL, FALSE, kMutexName);
	const DWORD err = GetLastError();
	// 作れなかったら諦めて普通に起動する（起動できないより、2 つ出るほうがまし）。
	if (g_mutex == NULL) return false;
	if (err != ERROR_ALREADY_EXISTS) return false;  // 自分が 1 つめ

	// すでに居る。受け口を探す（起動しかけならまだ無いので少し待つ）。
	HWND peer = NULL;
	for (int waited = 0; waited < kWaitForIpcMs; waited += kWaitStepMs) {
		peer = FindWindowExA(HWND_MESSAGE, NULL, kIpcClassName, NULL);
		if (peer != NULL) break;
		Sleep(kWaitStepMs);
	}
	// **ミューテックスは他が持っているので手放さない**（閉じるだけ）。
	CloseHandle(g_mutex);
	g_mutex = NULL;
	if (peer == NULL) return false;  // 見つからない。普通に起動する

	// 相手が前面へ出られるように許可を渡してから送る。これが無いと Windows が
	// 「フォアグラウンドの横取り」として弾き、題名バーが点滅するだけになる。
	DWORD pid = 0;
	GetWindowThreadProcessId(peer, &pid);
	AllowSetForegroundWindow(pid);

	COPYDATASTRUCT cds;
	cds.dwData = kCopyDataId;
	cds.cbData = (DWORD)target.size();
	cds.lpData = (PVOID)target.c_str();  // 空でも終端があるので NULL にはならない
	// SendMessage（同期）で送る。相手が受け取り終えてからこちらが終わるので、
	// 渡したものが取りこぼされない。
	SendMessageA(peer, WM_COPYDATA, 0, (LPARAM)&cds);
	return true;
}

void Start(void *nativeWindowHandle) {
	g_mainWindow = (HWND)nativeWindowHandle;
	if (g_mutex == NULL) return;      // 唯一のインスタンスではない（-multi など）
	if (g_ipcWindow != NULL) return;  // 二度は作らない

	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = IpcWndProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = kIpcClassName;
	RegisterClassA(&wc);  // 2 度目は失敗するが、それで構わない

	// **メッセージ専用ウィンドウ**（HWND_MESSAGE の子）。画面には出ず、
	// タスクバーにも並ばない。SDL と同じスレッドに作るので、届いた
	// メッセージは SDL_PumpEvents の PeekMessage が運んでくる。
	g_ipcWindow = CreateWindowExA(0, kIpcClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
	                              wc.hInstance, NULL);
}

bool Poll(std::string *target) {
	if (g_pending.empty()) return false;
	*target = g_pending.front();
	g_pending.erase(g_pending.begin());
	return true;
}

void Shutdown() {
	if (g_ipcWindow != NULL) {
		DestroyWindow(g_ipcWindow);
		g_ipcWindow = NULL;
	}
	if (g_mutex != NULL) {
		CloseHandle(g_mutex);
		g_mutex = NULL;
	}
	g_mainWindow = NULL;
	g_pending.clear();
}

#else  // _WIN32

// Android / iOS は不要、それ以外は未実装（singleinstance.h の説明）。
// 何もしないので、今までどおり何個でも起動できる。

bool HandOffToExisting(const std::string &target) {
	(void)target;
	return false;
}

void Start(void *nativeWindowHandle) {
	(void)nativeWindowHandle;
}

bool Poll(std::string *target) {
	(void)target;
	return false;
}

void Shutdown() {}

#endif  // _WIN32

}  // namespace singleinstance
}  // namespace mxv2
