// mxv2 - 演奏エンジン
//
// portable_mdx (MXDRV) + SDL2 オーディオ。旧 mxv の mx.cpp のうち、MXDRV.DLL の
// ロードと演奏制御にあたる部分を置き換える。
//
// スレッド構成:
//   デコードスレッド - MXDRV_GetPCM でリングバッファを埋める。合わせて
//                      StatusWatch を 1/kPollHz 秒ごとに回し、ビジュアライズ
//                      イベントをサンプル位置付きで DispQueue に積む。
//   SDL オーディオ    - リングバッファから取り出すだけ。取り出した分だけ
//                      playedFrames を進める。これが表示用の時計になる。
//   メインスレッド    - 操作と描画。
//
// MXDRV の制御呼び出しは MxdrvContext のクリティカルセクションで囲む
// (OPM 割り込みコールバックと競合するため)。ただしコールバックの内側では
// 既にロック済みなので二重には取らない (std::mutex は再帰不可)。

#ifndef MXV2_PLAYER_H
#define MXV2_PLAYER_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>
#include <mxdrv.h>
#include <mxdrv_context.h>

#include "dispqueue.h"
#include "mdxsong.h"
#include "statuswatch.h"

namespace mxv2 {

class Player {
public:
	struct Config {
		int sampleRate;         // 48000 固定（CLAUDE.md の確定事項）
		int audioBlockFrames;   // SDL コールバック 1 回分のフレーム数
		int numAudioBlocks;     // リングバッファのブロック数
		int memoryPoolBytes;    // MxdrvContext のメモリプール
		int mdxBufferBytes;
		int pdxBufferBytes;
		bool pcm8;
		int masterVolume;       // マスター音量 -100..+100。既定は中央の 0
		                        // （メイン音量は常に 0 から始まるので持たない）
		int maxLoops;           // 自動フェードアウトまでのループ数
		bool autoFadeout;
		int displayLatencyFrames;  // 表示を遅らせる量。旧 mxv の DISP_LATE 相当
		                           // だが既定 0 (サンプル位置同期なので不要)

		Config();
	};

	Player();
	~Player();

	bool Open(const Config &config, std::string *err);
	void Close();

	// 曲を差し替えて先頭から演奏する。song の中身は内部にコピーされる。
	bool PlaySong(const MdxSong &song, std::string *err);

	void Stop();
	void Pause();
	void Resume();
	void Fadeout();

	// 演奏位置を ms 単位で移動する。旧 mxv の MX_PlayAt_A。
	bool SeekMs(uint32_t ms);

	// 画面を作り直したあと、全ステータスを積み直させる。
	// 次のポーリング (最大 1/50 秒後) で反映される。
	void RequestStatusRefresh() { statusRefresh_.store(true, std::memory_order_relaxed); }

	// ループ数と自動フェードアウト。設定 UI から変えられる。
	// 総演奏時間の計算に効くので、反映は次に曲を読み込んだときから。
	void SetLoopConfig(int maxLoops, bool autoFadeout);
	int maxLoops() const { return maxLoops_.load(std::memory_order_relaxed); }
	bool autoFadeout() const { return autoFadeout_.load(std::memory_order_relaxed); }

	// 音量は 2 つある。どちらも -100..+100 に正規化してあり、
	// **実際の音量はこの 2 つの和**（-100..+100 に丸める）。
	//   マスター音量 … 設定ウィンドウで決める。mxv2.ini に記録する。
	//   メイン音量   … メイン画面の音量バーと -/+ キー。
	//                   その場かぎりの調整なので、起動時は必ず 0 から始まる。
	// 和が 0 のとき MXDRV 音量 192（旧 mxv の既定値）、-100 で無音、
	// +100 で最大 (4288)。旧 mxv (mxv.cpp:1239-1244) と同じ二次曲線で写す。
	// 以前は音量バーの画素幅をそのまま持っていたので、スキンによって段数が
	// 変わってしまっていた。バーの見た目とは切り離してある。
	static const int kVolumeMin = -100;
	static const int kVolumeMax = 100;
	void SetMasterVolume(int volume);
	int masterVolume() const { return masterVolume_; }
	void SetMainVolume(int volume);
	int mainVolume() const { return mainVolume_; }
	// 実際に鳴っている音量（マスター + メイン を丸めたもの）。
	int effectiveVolume() const;

	void SetTotalVolume(int vol);
	int totalVolume() const;
	void SetChannelMask(uint16_t mask);
	uint16_t channelMask() const;
	void ToggleChannel(int ch);
	// まとめてマスク / 解除する。bits は 0x00ff = FM、0xff00 = PCM、0xffff = 全部。
	// 「1 つでも鳴っていれば全部止め、全部止まっていれば全部鳴らす」トグルで、
	// 旧 mxv の IDM_MASKFM / IDM_MASKPCM / IDM_MASKCLEAR と同じ挙動。
	void ToggleChannelGroup(uint16_t bits);
	void SetFastPlay(bool on);

	// 表示用の現在位置（サンプル）。ここに追いついたイベントだけを描画する。
	uint64_t visualFrame() const;

	uint64_t playedFrames() const { return playedFrames_.load(std::memory_order_acquire); }
	uint64_t decodedFrames() const { return decodedFrames_.load(std::memory_order_acquire); }

	DispQueue &dispQueue() { return dispQueue_; }

	uint32_t playTimeMs() const { return playTimeMs_; }
	uint32_t nowTimeMs() const { return nowTimeMs_.load(std::memory_order_relaxed); }
	bool playTerminated() const { return playTerminate_.load(std::memory_order_relaxed); }
	bool paused() const { return paused_; }
	bool playing() const { return playing_; }
	uint32_t underruns() const { return underruns_.load(std::memory_order_relaxed); }

	const MdxSong &song() const { return song_; }

private:
	static void SDLCALL AudioCallbackTrampoline(void *userdata, uint8_t *stream, int len);
	static int DecodeThreadTrampoline(void *arg);
	static void OpmIntTrampoline(MxdrvContext *context);

	void AudioCallback(uint8_t *stream, int len);
	int DecodeThreadMain();
	void PollStep(uint64_t frame);

	void StartDecodeThread();
	void StopDecodeThread();
	void ResetClocks();

	// MXDRV 制御をクリティカルセクションで囲む小道具
	void Lock() { MxdrvContext_EnterCriticalSection(&context_); }
	void Unlock() { MxdrvContext_LeaveCriticalSection(&context_); }

	Config config_;
	bool opened_;

	MxdrvContext context_;
	bool contextReady_;
	bool mxdrvStarted_;

	SDL_AudioDeviceID audioDevice_;

	// オーディオリングバッファ（ブロック単位の SPSC）
	std::vector<int16_t> ring_;
	SDL_sem *readableSem_;
	SDL_sem *writableSem_;
	uint32_t readBlock_;
	uint32_t writeBlock_;
	int readOffsetFrames_;
	bool haveReadBlock_;

	SDL_Thread *decodeThread_;
	std::atomic<bool> decodeRunning_;

	std::atomic<uint64_t> playedFrames_;
	std::atomic<uint64_t> decodedFrames_;
	std::atomic<uint32_t> underruns_;
	uint64_t decodeCursor_;   // デコードスレッド専用
	uint64_t nextPollFrame_;  // デコードスレッド専用
	int framesPerPoll_;

	StatusWatch watch_;
	DispQueue dispQueue_;

	// config_ の同名フィールドの実行時版。デコードスレッドが読むので atomic。
	std::atomic<int> maxLoops_;
	std::atomic<bool> autoFadeout_;
	std::atomic<bool> statusRefresh_;

	MdxSong song_;
	int masterVolume_;
	int mainVolume_;
	void ApplyVolume();
	bool playing_;
	bool paused_;
	bool fadeoutStarted_;
	uint32_t playTimeMs_;
	std::atomic<uint32_t> nowTimeMs_;
	std::atomic<bool> playTerminate_;

	// OPMINT コールバックはコンテキストしか受け取らないため、Player を
	// 見つける手掛かりが要る。mxv2 は Player 単一インスタンス前提。
	static Player *s_instance;

	Player(const Player &);
	Player &operator=(const Player &);
};

}  // namespace mxv2

#endif  // MXV2_PLAYER_H
