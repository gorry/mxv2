// mxv2 - Chromecast へ送る（cast.h）

#include "cast.h"

namespace mxv2 {
namespace cast {

// 送る品質の段（2026-09-26、ユーザーの指定）。CPU は画素数 × fps にほぼ比例する
// （Pixel 7a で 854x480・30fps が 0.41G サイクル/秒。memo/cast.md の手順 4）。
// 映像のビットレートは画質だけに効き、CPU はほぼ変わらない。音声は「中」以上を
// 192kbps にする（2026-09-26、ユーザーの指定）。
const QualityPreset &GetQualityPreset(int q) {
	static const QualityPreset kPresets[Quality::Count] = {
	    {640, 360, 15, 1000, 128},    // 最低
	    {854, 480, 30, 2000, 128},    // 低（Android の既定）
	    {1280, 720, 30, 2000, 192},   // 中（パソコンの既定）
	    {1920, 1080, 30, 4000, 192},  // 高
	    {1920, 1080, 60, 6000, 192},  // 最高
	};
	if (q < 0) q = 0;
	if (q >= Quality::Count) q = Quality::Count - 1;
	return kPresets[q];
}

}  // namespace cast
}  // namespace mxv2

#ifndef MXV2_CAST

namespace mxv2 {
namespace cast {

bool Available() { return false; }
void Init() {}
void Shutdown() {}
void StartDiscovery() {}
void StopDiscovery() {}
std::vector<Device> Devices() { return std::vector<Device>(); }
void Start(const Device &) {}
void Stop() {}
Event Poll() { return Event::None; }
void ResumeRemote() {}
bool RemotePaused() { return false; }
State GetState() { return State::Idle; }
std::string DeviceName() { return std::string(); }
std::string LastError() { return std::string(); }
std::string LastErrorKey() { return std::string(); }
std::string StatsText() { return std::string(); }
void SetQuality(int) {}
void SetMuteLocal(bool) {}
void SetVideoAdvanceMs(int) {}
void AudioTap(int16_t *, int, int, uint64_t) {}
void BeginFrame(SDL_Renderer *) {}
void EndFrame(SDL_Renderer *, const SDL_Rect &, uint64_t, int) {}
void ResetRendererTextures() {}

}  // namespace cast
}  // namespace mxv2

#else  // MXV2_CAST

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "appprofile.h"  // CMake が Profile.ini から生成する
#include "sdlcastg_sdl.h"

namespace mxv2 {
namespace cast {

namespace {


// 受信側の返事を待つ長さ（つないでから、アプリの状態が分かるまで）。
const int kConnectWaitMs = 10000;
// 流し始めてから、受信側が取りに来るのを待つ長さ。
const uint32_t kNoFetchMs = 20000;
// 受信側を飛ばし直すときの位置: 流れの今の位置からこれだけ手前（受信側がふだん
// 溜めている長さ。遅れは 4 秒ほど）。
const int kLiveBehindMs = 4000;
// 音声がこれより長く来ていなければ（一時停止・停止）、絵の時刻は「いま」にする。
const uint32_t kAudioStaleMs = 150;

struct Global {
	std::mutex mutex;          // 下の文字列と worker
	bool initialized;
	bool discovering;
	std::thread worker;
	std::atomic<bool> workerBusy;
	std::atomic<State> state;    // State
	std::atomic<bool> streaming;  // 音と絵を渡してよい
	std::atomic<bool> muteLocal;
	std::atomic<int> advanceMs;  // 映像の時刻を早める量
	std::atomic<int> quality;    // 送る品質（Quality。送り始めるときに読む）
	std::atomic<bool> cancel;  // つないでいる途中でやめる
	bool endReported;          // 受信側が終わったのを Poll で知らせた（Stop 待ち）
	bool remotePaused;         // 受信側で一時停止されている（再開を待つ）
	bool reloading;            // 読み込み直させた（再生が始まるまで IDLE を無視する）
	int recoverStreak;         // 受信側の再生の誤りから続けて立て直した回数（再生を見たら 0）
	uint32_t bufferingSince;   // 再生のあと溜め直しに入った時刻（SDL_GetTicks。0 なら無し）
	bool seekRescued;          // この溜め直しで、もう飛ばし直した
	std::string deviceName;
	std::string lastError;
	std::string lastErrorKey;  // lastError の利用者向けの説明（メッセージカタログのキー）
	uint32_t streamStartTicks; // 流し始めた時刻（SDL_GetTicks）
	bool fetched;              // 受信側が流れを取りに来た

	// 音の位置と流れの時刻の対応（オーディオのスレッドが書き、メインが読む）。
	std::mutex mapMutex;
	uint64_t mapFrame;         // この Player の再生位置の音が
	int64_t mapStreamMs;       // 流れのこの時刻に入った
	uint32_t mapTicks;         // 書いた時刻（SDL_GetTicks）

	// 重さの記録（メインスレッドだけ）。5 秒ごとにログへ出す。
	uint64_t perfStart;        // 数え始め（SDL_GetPerformanceCounter）
	int perfLoops;             // CaptureFrame が呼ばれた回数（＝描いたフレーム）
	int perfCaptures;          // 読み出して渡した回数
	double perfReadSec;        // 読み出しにかかった時間の合計
	double perfReadMax;
	uint64_t perfVideoFrames;  // 数え始めのときの、エンコードした絵の数（sdlcastg の統計）
	uint64_t perfSameFrames;   // 同じく、同じ枠で捨てた絵の数
	uint64_t lastCapturedFrame;  // 最後に読み出した絵の visualFrame
	bool haveCaptured;

	Global()
	    : initialized(false), discovering(false), workerBusy(false), state(State::Idle),
	      streaming(false), muteLocal(true), advanceMs(0), quality(Quality::Medium), cancel(false),
	      endReported(false),
	      remotePaused(false), reloading(false), recoverStreak(0), bufferingSince(0),
	      seekRescued(false), streamStartTicks(0), fetched(false), mapFrame(0), mapStreamMs(0),
	      mapTicks(0), perfStart(0), perfLoops(0), perfCaptures(0), perfReadSec(0),
	      perfReadMax(0), perfVideoFrames(0), perfSameFrames(0), lastCapturedFrame(0), haveCaptured(false) {}
};

Global &G() {
	static Global g;
	return g;
}

void SetError(const std::string &e) {
	std::lock_guard<std::mutex> lock(G().mutex);
	G().lastError = e;
	G().lastErrorKey.clear();
}

void JoinWorker() {
	Global &g = G();
	if (g.worker.joinable() && !g.workerBusy) g.worker.join();
}

// 受信アプリを止めて切る（裏のスレッドで）。
void StopSequence() {
	Global &g = G();
	g.streaming = false;
	SDLCastG_StopApp();
	// STOP が受信側へ届くのを少し待ってから、流れと接続を畳む。
	for (int i = 0; i < 15; i++) {
		SDLCastG_Status st;
		SDLCastG_GetStatus(&st);
		if (st.state == SDLCASTG_CONNECTED || st.state == SDLCASTG_DISCONNECTED ||
		    st.state == SDLCASTG_ERROR) {
			break;
		}
		SDL_Delay(100);
	}
	SDLCastG_StopStream();
	SDLCastG_Disconnect();
	{
		std::lock_guard<std::mutex> lock(g.mutex);
		g.deviceName.clear();
	}
	g.state = State::Idle;
}

void StartSequence(Device d) {
	Global &g = G();
	g.state = State::Connecting;
	// 遅い端末（XS17）でつながるまでが長かったので、段階ごとの時間を出しておく。
	const uint32_t t0 = SDL_GetTicks();
	printf("cast     : connecting to %s (%s:%d)\n", d.name.c_str(), d.address.c_str(), d.port);
	if (SDLCastG_Connect(d.address.c_str(), d.port) != 0) {
		SetError(SDLCastG_GetError());
		SDLCastG_Disconnect();
		g.state = State::Idle;
		g.workerBusy = false;
		return;
	}
	const uint32_t t1 = SDL_GetTicks();
	// 受信側の状態（RECEIVER_STATUS）が来るまで待つ。
	bool ok = false;
	for (int waited = 0; waited < kConnectWaitMs && !g.cancel; waited += 50) {
		SDLCastG_Status st;
		SDLCastG_GetStatus(&st);
		if (st.state == SDLCASTG_ERROR || st.state == SDLCASTG_DISCONNECTED) {
			SetError(st.error[0] ? st.error : "disconnected");
			break;
		}
		if (st.state != SDLCASTG_CONNECTING) {
			ok = true;
			break;
		}
		SDL_Delay(50);
	}
	const uint32_t t2 = SDL_GetTicks();
	if (ok && !g.cancel) {
		SDLCastG_StreamConfig cfg;
		SDLCastG_DefaultStreamConfig(&cfg);
		const QualityPreset &qp = GetQualityPreset(g.quality);
		cfg.width = qp.width;
		cfg.height = qp.height;
		cfg.fps = qp.fps;
		cfg.videoKbps = qp.videoKbps;
		cfg.audioKbps = qp.audioKbps;
		cfg.title = MXV2_APP_NAME;
		if (SDLCastG_StartStream(&cfg) == 0) {
			g.streaming = true;
			g.state = State::Starting;
			g.streamStartTicks = SDL_GetTicks();
			g.fetched = false;
			printf("cast     : %s (%s) %dx%d %dfps video %dkbps audio %dkbps\n", d.name.c_str(),
			       d.address.c_str(), cfg.width, cfg.height, cfg.fps, cfg.videoKbps, cfg.audioKbps);
			printf("cast     : connect %u ms, receiver status %u ms, stream start %u ms\n",
			       (unsigned)(t1 - t0), (unsigned)(t2 - t1), (unsigned)(SDL_GetTicks() - t2));
		} else {
			SetError(SDLCastG_GetError());
			ok = false;
		}
	} else if (!ok && LastError().empty()) {
		SetError("no answer from the receiver");
	}
	if (!ok || g.cancel) {
		if (ok) {
			StopSequence();
		} else {
			SDLCastG_Disconnect();
			std::lock_guard<std::mutex> lock(g.mutex);
			g.deviceName.clear();
		}
		g.state = State::Idle;
	}
	g.cancel = false;
	g.workerBusy = false;
}

void RunWorker(void (*fn)(Device), const Device &d) {
	Global &g = G();
	if (g.worker.joinable()) g.worker.join();
	g.workerBusy = true;
	g.worker = std::thread(fn, d);
}

void StopWorkerEntry(Device) {
	StopSequence();
	G().workerBusy = false;
}

}  // namespace

bool Available() { return true; }

void Init() {
	Global &g = G();
	if (g.initialized) return;
	if (SDLCastG_Init() != 0) {
		printf("warning  : cast: %s\n", SDLCastG_GetError());
		return;
	}
	g.initialized = true;
}

void Shutdown() {
	Global &g = G();
	if (!g.initialized) return;
	g.cancel = true;
	if (g.worker.joinable()) g.worker.join();
	if (g.state != State::Idle) StopSequence();
	StopDiscovery();
	SDLCastG_Quit();
	g.initialized = false;
}

void StartDiscovery() {
	Global &g = G();
	if (!g.initialized || g.discovering) return;
	if (SDLCastG_StartDiscoverySDL() != 0) {
		printf("warning  : cast: %s\n", SDLCastG_GetError());
		return;
	}
	g.discovering = true;
}

void StopDiscovery() {
	Global &g = G();
	if (!g.discovering) return;
	SDLCastG_StopDiscoverySDL();
	g.discovering = false;
}

std::vector<Device> Devices() {
	std::vector<Device> out;
	if (!G().initialized) return out;
	SDLCastG_Device list[32];
	const int n = std::min(SDLCastG_GetDevices(list, 32), 32);
	for (int i = 0; i < n; i++) {
		const SDLCastG_Device &s = list[i];
		// 画面の無いもの（Google Home Mini など）には送っても何も出ない。
		if (s.capabilities >= 0 && (s.capabilities & SDLCASTG_CAP_VIDEO_OUT) == 0) continue;
		Device d;
		d.id = s.id;
		d.name = s.name[0] ? s.name : s.address;
		d.model = s.model;
		d.address = s.address;
		d.port = s.port;
		out.push_back(d);
	}
	std::sort(out.begin(), out.end(),
	          [](const Device &a, const Device &b) { return a.name < b.name; });
	return out;
}

void Start(const Device &device) {
	Global &g = G();
	if (!g.initialized || g.workerBusy || g.state != State::Idle) return;
	{
		std::lock_guard<std::mutex> lock(g.mutex);
		g.deviceName = device.name;
		g.lastError.clear();
		g.lastErrorKey.clear();
	}
	g.cancel = false;
	g.remotePaused = false;
	g.reloading = false;
	g.recoverStreak = 0;
	g.bufferingSince = 0;
	g.state = State::Connecting;
	RunWorker(&StartSequence, device);
}

void Stop() {
	Global &g = G();
	g.endReported = false;
	g.remotePaused = false;
	g.reloading = false;
	if (g.state == State::Idle || g.state == State::Stopping) return;
	if (g.workerBusy) {
		// つないでいる途中。StartSequence が見て畳む。
		g.cancel = true;
		return;
	}
	g.streaming = false;
	g.state = State::Stopping;
	RunWorker(&StopWorkerEntry, Device());
}

Event Poll() {
	Global &g = G();
	JoinWorker();
	if (!g.streaming || g.workerBusy || g.endReported) return Event::None;

	// 受信側が流れを取りに来ない（Windows のファイアウォールが HTTP の待ち受けを止めて
	// いるなど）。受信側はくるくるのまま何も言ってこないので、流し始めて 20 秒たっても
	// 誰も取りに来なければ、理由を添えて終える。まだ送り始めていないので、手元の
	// 演奏は止めない（手元の消音を外すだけ）。
	if (!g.fetched) {
		SDLCastG_StreamStats ss;
		SDLCastG_GetStreamStats(&ss);
		if (ss.clients > 0) {
			g.fetched = true;
		} else if (SDL_GetTicks() - g.streamStartTicks > kNoFetchMs) {
			SetError("the receiver did not fetch the stream");
			{
				std::lock_guard<std::mutex> lock(g.mutex);
				g.lastErrorKey = "Cast.NoFetch";
			}
			printf("cast     : stopped (%s)\n", LastError().c_str());
			Stop();
			return Event::None;
		}
	}
	SDLCastG_Status st;
	SDLCastG_GetStatus(&st);
	switch (st.state) {
		case SDLCASTG_PAUSED:
			g.state = State::Casting;
			g.reloading = false;
			if (!g.remotePaused) {
				g.remotePaused = true;
				printf("cast     : paused on the receiver\n");
				return Event::RemotePause;
			}
			break;
		case SDLCASTG_PLAYING:
			g.state = State::Casting;
			g.reloading = false;
			g.recoverStreak = 0;
			g.bufferingSince = 0;
			g.seekRescued = false;
			if (g.remotePaused) {
				g.remotePaused = false;
				printf("cast     : resumed on the receiver\n");
				return Event::RemotePlay;
			}
			break;
		case SDLCASTG_CONNECTED:
		case SDLCASTG_IDLE:
		case SDLCASTG_ERROR:
		case SDLCASTG_DISCONNECTED:
			// 受信側で終わった（TV で別のアプリにした・受信アプリを閉じた・切れた）。
			// 受信アプリが消えると CONNECTED に戻る。再生を始める前（起こしている
			// 途中）も CONNECTED を通るので、再生を見てからのものだけ拾う。
			if (st.state == SDLCASTG_CONNECTED && g.state != State::Casting) break;
			if (st.state == SDLCASTG_IDLE && st.idleReason[0] == 0) break;
			// 読み込み直させた直後は、前の再生の終わり（IDLE / INTERRUPTED）が来る。
			if (st.state == SDLCASTG_IDLE && g.reloading) break;
			// 受信側のプレーヤーの誤り・取り消し（TV のリモコンの左右キーで、目次の無い
			// ライブの流れを飛ばそうとすると ERROR 104 や CANCELLED で止まる）。
			// 受信アプリは生きているので、読み込み直して続ける。ただし CANCELLED は
			// **BUFFERING（飛ばそうとしている）からのときだけ**。PLAYING / PAUSED から
			// いきなり CANCELLED になるのは TV のリモコンの停止ボタンなので、受信側で
			// 終えたものとして扱う（ユーザーの確認、2026-09-25）。読み込み直しても
			// 再生に戻らないまま 3 回続いたら、直らないとみて終える（回数で数えると、
			// 左右キーを何度も押しただけで終わってしまった）。
			if (st.state == SDLCASTG_IDLE &&
			    (strcmp(st.idleReason, "ERROR") == 0 ||
			     (strcmp(st.idleReason, "CANCELLED") == 0 &&
			      strcmp(st.prevPlayerState, "BUFFERING") == 0))) {
				if (g.recoverStreak < 3) {
					g.recoverStreak++;
					printf("cast     : receiver %s, reloading\n", st.idleReason);
					ResumeRemote();
					break;
				}
			}
			SetError(st.error[0]         ? std::string(st.error)
			         : st.idleReason[0] ? std::string("receiver: ") + st.idleReason
			                            : std::string("receiver app closed"));
			printf("cast     : stopped (%s)\n", LastError().c_str());
			// 畳むのは呼ぶ側が演奏を止めてから（Stop）。ここで送るのをやめると
			// 手元の消音が外れ、止めるまでの間に手元で鳴ってしまう。
			g.endReported = true;
			return Event::Ended;
		default:
			// 読み込み中・溜めている途中。一時停止中に PLAY が押されると、受信側は
			// まずここを通る（再開の合図として拾う）。
			if (g.remotePaused && st.state == SDLCASTG_BUFFERING) {
				g.remotePaused = false;
				printf("cast     : resumed on the receiver\n");
				return Event::RemotePlay;
			}
			// 再生のあとの溜め直し（TV のリモコンの左右キーで飛ばそうとしたなど）。
			// ライブの流れでは飛べない: 先へ飛ぶとその時刻の中身が来るまで待ち、
			// 前へ飛ぶと二度と来ない中身を待ち続ける（くるくるが出たまま戻らない。
			// 2026-09-25、ユーザーの報告）。
			//   2 秒を超えたら … 受信側に届いているところ（いつもの遅れの位置）へ
			//                    飛ばし直す。受信側の飛ばしが終わるので、くるくると
			//                    「再生中」の札も消える（読み込み直すと、受信側の飛ばしが
			//                    終わらないまま札が残った）
			//   6 秒を超えたら … それでも戻らないので読み込み直す
			if (st.state == SDLCASTG_BUFFERING && g.state == State::Casting && !g.reloading) {
				const uint32_t now = SDL_GetTicks();
				if (g.bufferingSince == 0) {
					g.bufferingSince = now;
				} else if (now - g.bufferingSince > 2000 && !g.seekRescued) {
					g.seekRescued = true;
					printf("cast     : receiver buffering, seeking to live\n");
					SDLCastG_SeekToLive(kLiveBehindMs);
				} else if (now - g.bufferingSince > 6000 && g.recoverStreak < 3) {
					g.bufferingSince = 0;
					g.seekRescued = false;
					g.recoverStreak++;
					printf("cast     : receiver buffering, reloading\n");
					ResumeRemote();
				}
				break;  // 表示は「送っています」のまま
			}
			if (!g.reloading) g.state = State::Starting;
			break;
	}
	return Event::None;
}

bool RemotePaused() { return G().streaming && G().remotePaused; }

void ResumeRemote() {
	Global &g = G();
	if (!g.streaming) return;
	g.reloading = true;
	g.remotePaused = false;
	if (SDLCastG_ReloadStream() != 0) {
		printf("warning  : cast: %s\n", SDLCastG_GetError());
		g.reloading = false;
	}
}

State GetState() { return G().state.load(); }

std::string DeviceName() {
	std::lock_guard<std::mutex> lock(G().mutex);
	return G().deviceName;
}

std::string LastError() {
	std::lock_guard<std::mutex> lock(G().mutex);
	return G().lastError;
}

std::string LastErrorKey() {
	std::lock_guard<std::mutex> lock(G().mutex);
	return G().lastErrorKey;
}

std::string StatsText() {
	if (!G().streaming) return std::string();
	SDLCastG_StreamStats s;
	SDLCastG_GetStreamStats(&s);
	SDLCastG_Status st;
	SDLCastG_GetStatus(&st);
	char buf[160];
	const double lag = (st.state == SDLCASTG_PLAYING)
	                       ? (s.audioMs - s.clientBaseMs) / 1000.0 - st.currentTime
	                       : 0.0;
	snprintf(buf, sizeof(buf), "%s  lag %.1fs  frames %llu (drop %llu, same %llu)  %lldKB",
	         SDLCastG_StateName(st.state), lag, (unsigned long long)s.videoFrames,
	         (unsigned long long)s.droppedFrames, (unsigned long long)s.sameTimeFrames,
	         (long long)(s.bytesSent / 1024));
	return buf;
}

void SetQuality(int q) {
	if (q < 0) q = 0;
	if (q >= Quality::Count) q = Quality::Count - 1;
	G().quality = q;
}

void SetMuteLocal(bool mute) { G().muteLocal = mute; }

void SetVideoAdvanceMs(int ms) { G().advanceMs = (ms < 0) ? 0 : ms; }

void AudioTap(int16_t *pcm, int frames, int sampleRate, uint64_t startFrame) {
	Global &g = G();
	if (!g.streaming || frames <= 0) return;
	// この頭の音が、流れのどの時刻に入るか（渡す前の流れの長さ）。
	const int64_t streamMs = SDLCastG_StreamTimeMs();
	{
		std::lock_guard<std::mutex> lock(g.mapMutex);
		g.mapFrame = startFrame;
		g.mapStreamMs = streamMs;
		g.mapTicks = SDL_GetTicks();
	}
	SDL_AudioSpec spec;
	SDL_zero(spec);
	spec.freq = sampleRate;
	spec.format = AUDIO_S16SYS;
	spec.channels = 2;
	SDLCastG_SubmitAudioSDL(pcm, frames * 4, &spec);
	if (g.muteLocal) memset(pcm, 0, (size_t)frames * 4);
}

void BeginFrame(SDL_Renderer *renderer) {
	// 送っていなければ sdlcastg が何もしない（持っているテクスチャも手放す）。
	SDLCastG_BeginRendererFrame(renderer);
}

void ResetRendererTextures() { SDLCastG_ResetRendererTextures(); }

void EndFrame(SDL_Renderer *renderer, const SDL_Rect &rect, uint64_t visualFrame,
              int sampleRate) {
	Global &g = G();
	if (!g.streaming || renderer == 0 || rect.w <= 0 || rect.h <= 0 || sampleRate <= 0) {
		// Begin で描画先を替えていれば、ウィンドウへ写して戻す。
		SDLCastG_EndRendererFrameNoVideo(renderer);
		return;
	}

	// 5 秒ごとに、描いたフレーム数・渡した数・読み出しの時間をログへ。
	const uint64_t freq = SDL_GetPerformanceFrequency();
	const uint64_t now = SDL_GetPerformanceCounter();
	if (g.perfStart == 0) g.perfStart = now;
	g.perfLoops++;
	const double span = (double)(now - g.perfStart) / (double)freq;
	if (span >= 5.0) {
		// encode … エンコードした絵（TV へ出る絵）、same … 同じ枠で捨てた絵（5 秒の間）。
		SDLCastG_StreamStats ss;
		SDLCastG_GetStreamStats(&ss);
		// 送り直すと統計は 0 から数え直すので、前の控えより小さければ 0 から。
		if (ss.videoFrames < g.perfVideoFrames) g.perfVideoFrames = 0;
		if (ss.sameTimeFrames < g.perfSameFrames) g.perfSameFrames = 0;
		printf("cast     : draw %.1f fps, capture %.1f fps, encode %.1f fps, same %llu, "
		       "read %.1f ms (max %.1f) %dx%d\n",
		       g.perfLoops / span, g.perfCaptures / span,
		       (double)(ss.videoFrames - g.perfVideoFrames) / span,
		       (unsigned long long)(ss.sameTimeFrames - g.perfSameFrames),
		       g.perfCaptures ? g.perfReadSec * 1000.0 / g.perfCaptures : 0.0,
		       g.perfReadMax * 1000.0, rect.w, rect.h);
		g.perfVideoFrames = ss.videoFrames;
		g.perfSameFrames = ss.sameTimeFrames;
		g.perfStart = now;
		g.perfLoops = 0;
		g.perfCaptures = 0;
		g.perfReadSec = 0;
		g.perfReadMax = 0;
	}

	if (!SDLCastG_WantVideoFrame()) {
		SDLCastG_EndRendererFrameNoVideo(renderer);  // ウィンドウへ写すだけ
		return;
	}
	// いま画面に出ているのは visualFrame の音の時点の様子。その音が流れの上で
	// 鳴る時刻を付ける（TV でも画面と音がそろう）。音が止まっていれば「いま」。
	int64_t pts = -1;
	{
		std::lock_guard<std::mutex> lock(g.mapMutex);
		if (g.mapTicks != 0 && SDL_GetTicks() - g.mapTicks <= kAudioStaleMs) {
			pts = g.mapStreamMs +
			      ((int64_t)visualFrame - (int64_t)g.mapFrame) * 1000 / sampleRate -
			      g.advanceMs;
			if (pts < 0) pts = 0;
		}
	}
	// 演奏中に音の位置が前の絵から進んでいなければ、同じ時刻の絵なので読まない
	// （音声のコールバックは Android で 43ms ごと。30fps で読むと同じ時刻が続く）。
	// 止まっているとき（pts が「いま」）は読む。ダイアログの変化を送るため。
	if (pts >= 0 && g.haveCaptured && visualFrame == g.lastCapturedFrame) {
		SDLCastG_EndRendererFrameNoVideo(renderer);  // ウィンドウへ写すだけ（読み出さない）
		return;
	}
	g.lastCapturedFrame = visualFrame;
	g.haveCaptured = true;
	const uint64_t t0 = SDL_GetPerformanceCounter();
	SDLCastG_EndRendererFrame(renderer, &rect, pts);
	const double read = (double)(SDL_GetPerformanceCounter() - t0) / (double)freq;
	g.perfCaptures++;
	g.perfReadSec += read;
	if (read > g.perfReadMax) g.perfReadMax = read;
}

}  // namespace cast
}  // namespace mxv2

#endif  // MXV2_CAST
