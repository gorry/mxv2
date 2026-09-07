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
// 出力レートの選択肢を知るために直接読む。X68SOUND_SUPPORT_96KHZ が
// 定義されていれば、その portable_mdx は 96kHz 出力に対応している。
#include <x68sound.h>

#include "dispqueue.h"
#include "mdxsong.h"
#include "statuswatch.h"

namespace mxv2 {

class Player {
public:
	// 出力サンプリングレート。
	//
	// x68sound は OPM も ADPCM も常に 62500Hz で合成し、最終段のポリフェーズ
	// FIR だけで出力レートへ変換する。持っている FIR の表が対応レートを決める
	// ので、96kHz を選べるかどうかは portable_mdx 側の版による。
	// `X68SOUND_SUPPORT_96KHZ` を持たない版（本家など）と繋いでも、そのまま
	// 48kHz で動く。
	//
	// 既定は 48kHz。96kHz はユーザーが [mxv2 の設定] で選んだときだけ。
	static const int kDefaultSampleRate = 48000;
#ifdef X68SOUND_SUPPORT_96KHZ
	static const bool kSupports96kHz = true;
#else
	static const bool kSupports96kHz = false;
#endif
	// 指定できるレートか。x68sound は知らない値を黙って 22050 に落とすので、
	// 渡す前にここで弾く。
	static bool IsSupportedSampleRate(int rate) {
		if (rate == 44100 || rate == 48000) return true;
		return kSupports96kHz && rate == 96000;
	}

	struct Config {
		// 出力サンプリングレート。既定は kDefaultSampleRate。
		int sampleRate;
		int audioBlockFrames;   // SDL コールバック 1 回分のフレーム数
		// リングバッファのブロック数。**下限**で、Open() が出力レートから
		// 「一定の長さ (kRingMs) ぶん溜まる深さ」を計算して必要なら増やす。
		int numAudioBlocks;
		int memoryPoolBytes;    // MxdrvContext のメモリプール
		int mdxBufferBytes;
		int pdxBufferBytes;
		bool pcm8;
		int masterVolume;       // マスター音量 -100..+100。既定は中央の 0
		                        // （メイン音量は常に 0 から始まるので持たない）
		int maxLoops;           // 自動フェードアウトまでのループ数
		bool autoFadeout;
		// 表示を遅らせるフレーム数（旧 mxv の DISP_LATE 相当）。
		//
		// イベントはデコード時のサンプル位置で打刻してあるので、
		// 「SDL へ渡した位置」との同期は取れている。ただし渡した音が実際に
		// 鳴るのはオーディオ装置のバッファを通り抜けたあとで、そのぶん
		// **画面が音より先に進む**。ここでその差を戻す。
		// 正の値で表示が遅れ、負の値で先に進む。
		//
		// displayLatencyAuto が true なら、開いたときの装置のバッファ長を
		// そのまま使う（既定）。false のときだけ displayLatencyFrames を見る。
		bool displayLatencyAuto;
		int displayLatencyFrames;

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
	// 一時停止中に呼んだときは、飛んだ先でも一時停止のまま止まっている。
	bool SeekMs(uint32_t ms);

	// 画面を作り直したあと、全ステータスを積み直させる。
	// 次のポーリング (最大 1/50 秒後) で反映される。
	void RequestStatusRefresh() { statusRefresh_.store(true, std::memory_order_relaxed); }

	// 「画面に出ている鍵盤とレベルメーターを消してほしい」という要求。
	// **演奏位置が飛ぶとき**（曲の切り替え・シーク）に立てる。
	//
	// 消すのは Visualizer の仕事で、Player からは頼むだけ。イベントは
	// デコード位置で打刻され、画面に出るのは再生位置まで追いついた分だけ
	// なので、**Player 側からは「いま画面に何が出ているか」が分からない**
	// （StatusWatch が覚えているのはデコード位置の状態）。
	bool TakeDisplayReset() const {
		return displayReset_.exchange(false, std::memory_order_relaxed);
	}

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
	// ToggleChannelGroup に渡すビット。
	static const uint16_t kChannelMaskFm = 0x00ff;   // ch.1-8
	static const uint16_t kChannelMaskPcm = 0xff00;  // ch.P-W
	static const uint16_t kChannelMaskAll = 0xffff;
	// まとめてマスク / 解除する。bits は 0x00ff = FM、0xff00 = PCM、0xffff = 全部。
	// 「1 つでも鳴っていれば全部止め、全部止まっていれば全部鳴らす」トグルで、
	// 旧 mxv の IDM_MASKFM / IDM_MASKPCM / IDM_MASKCLEAR と同じ挙動。
	void ToggleChannelGroup(uint16_t bits);
	void SetFastPlay(bool on);

	// 表示用の現在位置（サンプル）。ここに追いついたイベントだけを描画する。
	uint64_t visualFrame() const;

	// 実際に使っている表示の遅らせ量（フレーム）。Open() で決まる。
	int displayLatencyFrames() const { return displayLatencyFrames_; }
	// オーディオ装置が実際に返してきたバッファ長（フレーム）。
	int audioBufferFrames() const { return audioBufferFrames_; }
	int sampleRate() const { return config_.sampleRate; }
	// 表示の遅らせ量を後から変える（設定ウィンドウ用）。auto なら装置の
	// バッファ長を使い、frames は見ない。次のフレームから効く。
	void SetDisplayLatency(bool useAuto, int frames);

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
	void WaitForPrefill();
	void ResumeAudioDevice();

	static void SDLCALL AudioCallbackTrampoline(void *userdata, uint8_t *stream, int len);
	static int DecodeThreadTrampoline(void *arg);
	static void OpmIntTrampoline(MxdrvContext *context);

	void AudioCallback(uint8_t *stream, int len);

	int displayLatencyFrames_;  // Open() で決めた実効値
	int audioBufferFrames_;     // SDL が返してきたバッファ長
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
	// 鍵盤を消してほしいという要求。読む側 (TakeDisplayReset) が const な
	// 参照しか持たないので mutable。立てるのは PlaySong / SeekMs。
	mutable std::atomic<bool> displayReset_;

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
