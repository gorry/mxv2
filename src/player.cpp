// mxv2 - 演奏エンジン

#include "player.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "message.h"

namespace mxv2 {

Player *Player::s_instance = 0;

namespace {

// 音量 (-100..+100) -> MXDRV 音量。旧 mxv/mxv.cpp:1239-1244 と同じ形の
// 二次曲線を、バーの画素幅に依存しない形へ正規化したもの。
//   -100 -> 0 / 0 -> 192 (旧 mxv の既定) / +100 -> 4288
int MxdrvVolumeFromNormalized(int volume) {
	if (volume < Player::kVolumeMin) volume = Player::kVolumeMin;
	if (volume > Player::kVolumeMax) volume = Player::kVolumeMax;
	const double t = volume / 100.0;
	if (volume <= 0) {
		const double u = 1.0 + t;  // -100..0 を 0..1 へ
		return (int)(u * u * 192);
	}
	return (int)(192 + t * t * 4096);
}

}  // namespace

Player::Config::Config()
    : sampleRate(kDefaultSampleRate),
      audioBlockFrames(512),
      numAudioBlocks(4),
      memoryPoolBytes(8 * 1024 * 1024),
      mdxBufferBytes(1 * 1024 * 1024),
      pdxBufferBytes(2 * 1024 * 1024),
      pcm8(true),
      masterVolume(0),
      maxLoops(2),
      autoFadeout(true),
      displayLatencyAuto(true),
      displayLatencyFrames(0) {}

Player::Player()
    : opened_(false),
      contextReady_(false),
      mxdrvStarted_(false),
      audioDevice_(0),
      readableSem_(0),
      writableSem_(0),
      readBlock_(0),
      writeBlock_(0),
      readOffsetFrames_(0),
      haveReadBlock_(false),
      decodeThread_(0),
      decodeRunning_(false),
      playedFrames_(0),
      decodedFrames_(0),
      underruns_(0),
      decodeCursor_(0),
      nextPollFrame_(0),
      framesPerPoll_(0),
      maxLoops_(2),
      autoFadeout_(true),
      displayLatencyFrames_(0),
      audioBufferFrames_(0),
      statusRefresh_(false),
      masterVolume_(0),
      mainVolume_(0),
      playing_(false),
      paused_(false),
      fadeoutStarted_(false),
      playTimeMs_(0),
      nowTimeMs_(0),
      playTerminate_(false) {
	memset(&context_, 0, sizeof(context_));
}

Player::~Player() {
	Close();
}

// ---------------------------------------------------------------------------
// 初期化 / 終了
// ---------------------------------------------------------------------------

bool Player::Open(const Config &config, std::string *err) {
	if (opened_) {
		*err = Msg("Error.PlayerOpened");
		return false;
	}
	if (s_instance != 0) {
		*err = Msg("Error.PlayerSingle");
		return false;
	}

	config_ = config;
	// 対応していないレートは黙って 22050 に落とされてしまうので、ここで
	// 既定へ戻す（x68sound は不正な値でもエラーを返さない）。
	if (!IsSupportedSampleRate(config_.sampleRate)) {
		config_.sampleRate = kDefaultSampleRate;
	}
	framesPerPoll_ = config_.sampleRate / StatusWatch::kPollHz;
	if (framesPerPoll_ < 1) framesPerPoll_ = 1;
	SetLoopConfig(config_.maxLoops, config_.autoFadeout);

	// MXDRV コンテキスト
	if (!MxdrvContext_Initialize(&context_, config_.memoryPoolBytes)) {
		*err = Msg("Error.PlayerContext");
		return false;
	}
	contextReady_ = true;

	int ret = MXDRV_Start(&context_, config_.sampleRate, 0, 0, 0,
	                      config_.mdxBufferBytes, config_.pdxBufferBytes, 0);
	if (ret != 0) {
		*err = MsgF("Error.PlayerStart", MsgNum("%d", ret));
		Close();
		return false;
	}
	mxdrvStarted_ = true;

	MXDRV_PCM8Enable(&context_, config_.pcm8 ? 1 : 0);
	// メイン音量は記録しないので、起動時は必ず 0（マスターのみ）から始まる。
	masterVolume_ = config_.masterVolume;
	mainVolume_ = 0;
	MXDRV_TotalVolume(&context_, MxdrvVolumeFromNormalized(effectiveVolume()));

	watch_.Bind(&context_);
	watch_.Reset();

	// オーディオリングバッファ
	ring_.assign((size_t)config_.numAudioBlocks * config_.audioBlockFrames * 2, 0);
	readableSem_ = SDL_CreateSemaphore(0);
	writableSem_ = SDL_CreateSemaphore((Uint32)config_.numAudioBlocks);
	if (readableSem_ == 0 || writableSem_ == 0) {
		*err = MsgF("Error.PlayerSemaphore", SDL_GetError());
		Close();
		return false;
	}

	// SDL オーディオ
	SDL_AudioSpec want;
	memset(&want, 0, sizeof(want));
	want.freq = config_.sampleRate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = (Uint16)config_.audioBlockFrames;
	want.callback = &Player::AudioCallbackTrampoline;
	want.userdata = this;

	SDL_AudioSpec have;
	memset(&have, 0, sizeof(have));
	audioDevice_ = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audioDevice_ == 0) {
		*err = MsgF("Error.PlayerAudioDevice", SDL_GetError());
		Close();
		return false;
	}

	// 表示を遅らせる量を決める。
	//
	// コールバックへ渡した音は、装置のバッファを 1 つぶん通り抜けてから鳴る。
	// playedFrames_ は「渡した位置」なので、そのまま表示に使うと画面が音より
	// バッファ 1 つぶん先に進む。既定ではその差をそのまま戻す。
	// 実際の値は SDL が返してきた have.samples を使う（要求どおりとは限らない）。
	audioBufferFrames_ = (have.samples > 0) ? (int)have.samples : config_.audioBlockFrames;
	displayLatencyFrames_ =
	    config_.displayLatencyAuto ? audioBufferFrames_ : config_.displayLatencyFrames;

	// OPM 割り込みコールバックを登録。
	// portable_mdx (MXDRV_ENABLE_PORTABLE_CODE) では MXCALLBACK_OPMINT は
	// コンテキスト単位のメンバで、シグネチャは void(*)(MxdrvContext*)。
	// mxdrv.h の typedef (void(*)(void)) は非 portable 版のものなので使わない。
	{
		typedef void (*OpmIntCallback)(MxdrvContext *);
		OpmIntCallback *slot =
		    (OpmIntCallback *)MXDRV_GetWork(&context_, MXDRV_CALLBACK_OPMINT);
		if (slot == 0) {
			*err = Msg("Error.PlayerOpmInt");
			Close();
			return false;
		}
		s_instance = this;
		*slot = &Player::OpmIntTrampoline;
	}

	opened_ = true;
	return true;
}

void Player::Close() {
	StopDecodeThread();

	if (audioDevice_ != 0) {
		SDL_CloseAudioDevice(audioDevice_);
		audioDevice_ = 0;
	}

	if (contextReady_) {
		if (mxdrvStarted_) {
			typedef void (*OpmIntCallback)(MxdrvContext *);
			OpmIntCallback *slot =
			    (OpmIntCallback *)MXDRV_GetWork(&context_, MXDRV_CALLBACK_OPMINT);
			if (slot != 0) *slot = 0;
			MXDRV_End(&context_);
			mxdrvStarted_ = false;
		}
		MxdrvContext_Terminate(&context_);
		contextReady_ = false;
	}

	if (readableSem_ != 0) {
		SDL_DestroySemaphore(readableSem_);
		readableSem_ = 0;
	}
	if (writableSem_ != 0) {
		SDL_DestroySemaphore(writableSem_);
		writableSem_ = 0;
	}

	if (s_instance == this) s_instance = 0;
	opened_ = false;
	playing_ = false;
	paused_ = false;
}

// ---------------------------------------------------------------------------
// 演奏制御
// ---------------------------------------------------------------------------

void Player::ResetClocks() {
	playedFrames_.store(0, std::memory_order_release);
	decodedFrames_.store(0, std::memory_order_release);
	underruns_.store(0, std::memory_order_relaxed);
	decodeCursor_ = 0;
	nextPollFrame_ = 0;
	readBlock_ = 0;
	writeBlock_ = 0;
	readOffsetFrames_ = 0;
	haveReadBlock_ = false;

	// セマフォを「全ブロック書き込み可」の状態へ戻す
	while (SDL_SemTryWait(readableSem_) == 0) {
	}
	while (SDL_SemValue(writableSem_) < (Uint32)config_.numAudioBlocks) {
		SDL_SemPost(writableSem_);
	}
	while (SDL_SemValue(writableSem_) > (Uint32)config_.numAudioBlocks) {
		SDL_SemWait(writableSem_);
	}
}

bool Player::PlaySong(const MdxSong &song, std::string *err) {
	if (!opened_) {
		*err = Msg("Error.PlayerNotOpened");
		return false;
	}

	// デコードを止めてからデータを差し替える
	SDL_PauseAudioDevice(audioDevice_, 1);
	StopDecodeThread();

	// player.PlaySong(player.song(), ...) で同じ曲を頭から掛け直せるように、
	// 自分自身の代入は避ける。
	if (&song != &song_) song_ = song;

	Lock();
	MXDRV_Stop(&context_);
	Unlock();

	{
		void *mdx = song_.mdxBuffer.empty() ? NULL : &song_.mdxBuffer[0];
		void *pdx = song_.pdxBuffer.empty() ? NULL : &song_.pdxBuffer[0];
		int ret = MXDRV_SetData2(&context_, mdx, (uint32_t)song_.mdxBuffer.size(),
		                         pdx, (uint32_t)song_.pdxBuffer.size());
		if (ret != 0) {
			*err = MsgF("Error.PlayerSetData", MsgNum("%d", ret));
			return false;
		}
	}

	// 総演奏時間。自動フェードアウト有無で数え方を変えるのは旧 mxv と同じ。
	{
		const int loops = maxLoops();
		const bool fade = autoFadeout();
		playTimeMs_ =
		    MXDRV_MeasurePlayTime2(&context_, fade ? loops : loops + 1, fade ? 1 : 0);
	}

	watch_.Bind(&context_);
	watch_.Reset();
	dispQueue_.Clear();
	ResetClocks();

	nowTimeMs_.store(0, std::memory_order_relaxed);
	playTerminate_.store(false, std::memory_order_relaxed);
	fadeoutStarted_ = false;
	playing_ = true;
	paused_ = false;

	MXDRV_Play2(&context_);

	StartDecodeThread();
	SDL_PauseAudioDevice(audioDevice_, 0);
	return true;
}

void Player::Stop() {
	if (!opened_) return;
	Lock();
	MXDRV_Stop(&context_);
	Unlock();
	playing_ = false;
	paused_ = false;
}

void Player::Pause() {
	if (!opened_ || !playing_ || paused_) return;
	Lock();
	MXDRV_Pause(&context_);
	Unlock();
	paused_ = true;
	// オーディオは止めない。止めると playedFrames が進まなくなり、
	// decodedFrames との相対関係（＝表示同期）が崩れるため。
	// 一時停止中は MXDRV が無音を吐くので、そのまま流しておけばよい。
}

void Player::Resume() {
	if (!opened_ || !playing_ || !paused_) return;
	Lock();
	MXDRV_Cont(&context_);
	Unlock();
	paused_ = false;
}

void Player::Fadeout() {
	if (!opened_) return;
	Lock();
	MXDRV_Fadeout(&context_);
	Unlock();
}

// MXDRV_PlayAt は「曲の頭から目的の位置まで OPM 割り込みを空回しする」重い
// 処理で、しかも内部で OPM 割り込みハンドラを差し替える。デコードスレッドと
// 同時に動かせないので、PlaySong と同じ手順で止めてから呼ぶ。
bool Player::SeekMs(uint32_t ms) {
	if (!opened_ || !playing_) return false;
	if (playTimeMs_ != 0 && ms > playTimeMs_) ms = playTimeMs_;

	SDL_PauseAudioDevice(audioDevice_, 1);
	StopDecodeThread();

	// チャンネルのミュート状態は空回しの巻き添えで消えるので取っておく。
	const uint16_t mask = channelMask();
	const int loops = maxLoops();
	const bool fade = autoFadeout();
	MXDRV_PlayAt(&context_, ms, fade ? loops : loops + 1, fade ? 1 : 0);
	SetChannelMask(mask);

	watch_.Reset();
	dispQueue_.Clear();
	ResetClocks();

	nowTimeMs_.store(ms, std::memory_order_relaxed);
	playTerminate_.store(false, std::memory_order_relaxed);
	fadeoutStarted_ = false;
	paused_ = false;  // MXDRV_PlayAt は演奏を掛け直すので一時停止は解ける

	StartDecodeThread();
	SDL_PauseAudioDevice(audioDevice_, 0);
	return true;
}

void Player::SetLoopConfig(int maxLoops, bool autoFadeout) {
	if (maxLoops < 1) maxLoops = 1;
	if (maxLoops > 99) maxLoops = 99;
	maxLoops_.store(maxLoops, std::memory_order_relaxed);
	autoFadeout_.store(autoFadeout, std::memory_order_relaxed);
}

int Player::effectiveVolume() const {
	const int v = masterVolume_ + mainVolume_;
	if (v < kVolumeMin) return kVolumeMin;
	if (v > kVolumeMax) return kVolumeMax;
	return v;
}

void Player::ApplyVolume() {
	SetTotalVolume(MxdrvVolumeFromNormalized(effectiveVolume()));
}

void Player::SetMasterVolume(int volume) {
	if (volume < kVolumeMin) volume = kVolumeMin;
	if (volume > kVolumeMax) volume = kVolumeMax;
	masterVolume_ = volume;
	ApplyVolume();
}

void Player::SetMainVolume(int volume) {
	if (volume < kVolumeMin) volume = kVolumeMin;
	if (volume > kVolumeMax) volume = kVolumeMax;
	mainVolume_ = volume;
	ApplyVolume();
}

void Player::SetTotalVolume(int vol) {
	if (!opened_) return;
	Lock();
	MXDRV_TotalVolume(&context_, vol);
	Unlock();
}

int Player::totalVolume() const {
	if (!opened_) return 0;
	return MXDRV_GetTotalVolume(const_cast<MxdrvContext *>(&context_));
}

void Player::SetChannelMask(uint16_t mask) {
	if (!opened_) return;
	Lock();
	MXWORK_GLOBAL *g = (MXWORK_GLOBAL *)MXDRV_GetWork(&context_, MXDRV_WORK_GLOBAL);
	if (g != 0) g->L001e1c = mask;
	Unlock();
}

uint16_t Player::channelMask() const {
	if (!opened_) return 0;
	const MXWORK_GLOBAL *g =
	    (const MXWORK_GLOBAL *)MXDRV_GetWork(const_cast<MxdrvContext *>(&context_),
	                                         MXDRV_WORK_GLOBAL);
	return (g != 0) ? g->L001e1c : 0;
}

void Player::ToggleChannel(int ch) {
	if (!opened_ || ch < 0 || ch >= 16) return;
	SetChannelMask((uint16_t)(channelMask() ^ (1 << ch)));
}

void Player::ToggleChannelGroup(uint16_t bits) {
	if (!opened_ || bits == 0) return;
	const uint16_t now = channelMask();
	uint16_t next = (uint16_t)(now & ~bits);
	if ((now & bits) == 0) next = (uint16_t)(next | bits);
	SetChannelMask(next);
}

void Player::SetFastPlay(bool on) {
	if (!opened_) return;
	Lock();
	MXWORK_KEY *key = (MXWORK_KEY *)MXDRV_GetWork(&context_, MXDRV_WORK_KEY);
	if (key != 0) {
		key->SHIFT = on ? 1 : 0;
		key->OPT2 = on ? 1 : 0;
	}
	Unlock();
}

void Player::SetDisplayLatency(bool useAuto, int frames) {
	config_.displayLatencyAuto = useAuto;
	config_.displayLatencyFrames = frames;
	displayLatencyFrames_ = useAuto ? audioBufferFrames_ : frames;
}

// 表示に使う再生位置。playedFrames_ は「SDL へ渡した位置」なので、
// 実際に鳴っている位置はそこから装置のバッファぶん手前になる。
// 正の遅らせ量で戻し、負なら逆に進める。
uint64_t Player::visualFrame() const {
	const uint64_t played = playedFrames_.load(std::memory_order_acquire);
	const int late = displayLatencyFrames_;
	if (late <= 0) return played + (uint64_t)(-late);
	const uint64_t back = (uint64_t)late;
	return (played > back) ? (played - back) : 0;
}

// ---------------------------------------------------------------------------
// SDL オーディオコールバック
// ---------------------------------------------------------------------------

void SDLCALL Player::AudioCallbackTrampoline(void *userdata, uint8_t *stream, int len) {
	((Player *)userdata)->AudioCallback(stream, len);
}

void Player::AudioCallback(uint8_t *stream, int len) {
	const int bytesPerFrame = 2 * (int)sizeof(int16_t);
	const int blockFrames = config_.audioBlockFrames;
	int frames = len / bytesPerFrame;

	while (frames > 0) {
		if (!haveReadBlock_) {
			// デコードが間に合わなければ無音を返す。ここでブロックすると
			// 曲の切り替えや終了時にオーディオスレッドが固まる。
			if (SDL_SemTryWait(readableSem_) != 0) {
				memset(stream, 0, (size_t)frames * bytesPerFrame);
				underruns_.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			haveReadBlock_ = true;
			readOffsetFrames_ = 0;
		}

		const int n = std::min(blockFrames - readOffsetFrames_, frames);
		const int16_t *src =
		    &ring_[((size_t)(readBlock_ % (uint32_t)config_.numAudioBlocks) * blockFrames +
		            readOffsetFrames_) * 2];
		memcpy(stream, src, (size_t)n * bytesPerFrame);

		stream += (size_t)n * bytesPerFrame;
		frames -= n;
		readOffsetFrames_ += n;
		playedFrames_.fetch_add((uint64_t)n, std::memory_order_release);

		if (readOffsetFrames_ >= blockFrames) {
			haveReadBlock_ = false;
			readOffsetFrames_ = 0;
			readBlock_++;
			SDL_SemPost(writableSem_);
		}
	}
}

// ---------------------------------------------------------------------------
// デコードスレッド
// ---------------------------------------------------------------------------

void Player::StartDecodeThread() {
	if (decodeThread_ != 0) return;
	decodeRunning_.store(true, std::memory_order_release);
	decodeThread_ = SDL_CreateThread(&Player::DecodeThreadTrampoline, "mxv2Decode", this);
	if (decodeThread_ == 0) decodeRunning_.store(false, std::memory_order_release);
}

void Player::StopDecodeThread() {
	if (decodeThread_ == 0) return;
	decodeRunning_.store(false, std::memory_order_release);
	// 空きブロック待ちで寝ている可能性があるので起こす
	SDL_SemPost(writableSem_);
	SDL_WaitThread(decodeThread_, NULL);
	decodeThread_ = 0;
}

int Player::DecodeThreadTrampoline(void *arg) {
	return ((Player *)arg)->DecodeThreadMain();
}

int Player::DecodeThreadMain() {
	const int blockFrames = config_.audioBlockFrames;

	while (decodeRunning_.load(std::memory_order_acquire)) {
		SDL_SemWait(writableSem_);
		if (!decodeRunning_.load(std::memory_order_acquire)) break;

		int16_t *dst =
		    &ring_[(size_t)(writeBlock_ % (uint32_t)config_.numAudioBlocks) * blockFrames * 2];

		int done = 0;
		while (done < blockFrames) {
			// ポーリング境界にちょうど乗るようにデコード単位を切る。
			// こうすると StatusWatch は常に「サンプル位置 framesPerPoll_ ごと」に
			// 呼ばれ、旧 mxv の 50Hz ポーリングを再生位置基準で再現できる。
			if (decodeCursor_ >= nextPollFrame_) {
				PollStep(decodeCursor_);
				nextPollFrame_ += (uint64_t)framesPerPoll_;
				continue;
			}
			uint64_t untilPoll = nextPollFrame_ - decodeCursor_;
			int n = blockFrames - done;
			if ((uint64_t)n > untilPoll) n = (int)untilPoll;

			MXDRV_GetPCM(&context_, dst + (size_t)done * 2, n);
			decodeCursor_ += (uint64_t)n;
			done += n;
		}

		decodedFrames_.store(decodeCursor_, std::memory_order_release);
		writeBlock_++;
		SDL_SemPost(readableSem_);
	}
	return 0;
}

// StatusWatch のポーリングと、自動フェードアウト判定。
// デコードスレッドから、MXDRV_GetPCM の外側で呼ばれる。この時点では OPM 割り込み
// コールバックは走っていないので、MXDRV への制御呼び出しを安全に行える。
void Player::PollStep(uint64_t frame) {
	// 配色の変更などで画面を作り直したあとは、全ステータスを積み直す。
	if (statusRefresh_.exchange(false, std::memory_order_relaxed)) {
		watch_.ForgetLastValues();
	}
	watch_.Poll(frame, &dispQueue_);

	uint32_t now = watch_.nowTimeMs();
	// 曲が終わったら総演奏時間で頭打ちにする（旧 mxv の OPMINT と同じ）。
	if (watch_.terminated() && playTimeMs_ != 0 && now > playTimeMs_) now = playTimeMs_;
	nowTimeMs_.store(now, std::memory_order_relaxed);

	const MXWORK_GLOBAL *g = (const MXWORK_GLOBAL *)MXDRV_GetWork(&context_, MXDRV_WORK_GLOBAL);
	if (g == 0) return;

	if (autoFadeout()) {
		if (g->L002246 >= maxLoops()) {
			if (!fadeoutStarted_) {
				fadeoutStarted_ = true;
				Fadeout();
			}
		} else if (fadeoutStarted_) {
			fadeoutStarted_ = false;
			playTerminate_.store(false, std::memory_order_relaxed);
		}
		if (playTimeMs_ != 0 && now >= playTimeMs_) {
			playTerminate_.store(true, std::memory_order_relaxed);
		}
	}

	if (watch_.terminated()) {
		playTerminate_.store(true, std::memory_order_relaxed);
	}
}

// ---------------------------------------------------------------------------
// OPM 割り込みコールバック
// ---------------------------------------------------------------------------

// デコードスレッド上、MXDRV_GetPCM の内側から呼ばれる。呼び出し元
// (OPMINTFUNC) が既に MxdrvContext のクリティカルセクションを保持している
// ため、ここで Lock() してはいけない (std::mutex は再帰不可)。
// 重い処理も禁物なので、ポーリングでは取りこぼす情報の記録だけを行う。
void Player::OpmIntTrampoline(MxdrvContext *context) {
	Player *self = s_instance;
	if (self == 0 || &self->context_ != context) return;
	self->watch_.OnOpmInt();
}

}  // namespace mxv2
