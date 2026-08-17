// mxv2 - MXDRV ワークの監視とビジュアライズイベントの生成
//
// 旧 mxv の MX_UpdateStatusDisplay() の「生成側」に相当する。
//
// 旧 mxv では 20ms の WM_TIMER (= 50Hz) からポーリングしており、レベルメータ
// の減衰定数などがその 50Hz を前提に調整されている。mxv2 ではデコードスレッド
// が「サンプル位置 kPollHz 分ごと」にポーリングすることで、この 50Hz を実時間
// ではなく再生位置基準で再現する。こうすると早送りや停止の影響を受けない。
//
// OPM 割り込みコールバック (OnOpmInt) は、ポーリング間隔の間に起きた「同じ音程
// の打ち直し」を取りこぼさないためだけに使う。旧 mxv の Hit フラグと同じ役割。
// OnOpmInt / Poll はどちらもデコードスレッドから呼ばれるので、両者の間に排他は
// 要らない。

#ifndef MXV2_STATUSWATCH_H
#define MXV2_STATUSWATCH_H

#include <cstdint>

#include <mxdrv.h>
#include <mxdrv_context.h>

#include "dispqueue.h"

namespace mxv2 {

// 旧 mxv の W_LEVELMETERINFO。
static const int kLevelMeterWidth = 64;

class StatusWatch {
public:
	// 旧 mxv の WM_TIMER 周期 20ms に対応するポーリング周波数。
	static const int kPollHz = 50;

	StatusWatch();

	// MXDRV の各ワークを結びつける。曲の切り替え時にも呼んでよい。
	void Bind(MxdrvContext *context);

	// 内部状態を初期化する（旧 mxv の MX_InitDisplay 相当）。
	void Reset();

	// 「前回値」だけ捨てる。次の Poll で全ステータスを積み直すので、
	// 画面を作り直したあとの復元に使う（旧 mxv の MX_ReqInit*Display 相当）。
	// 演奏時間は保つ。
	void ForgetLastValues();

	// OPM 割り込みコールバックから呼ぶ。デコードスレッド上で動く。
	void OnOpmInt();

	// 再生位置 frame の時点のワークを見て、変化をイベントとして queue に積む。
	// デコードスレッドから 1/kPollHz 秒ごとに呼ぶ。
	void Poll(uint64_t frame, DispQueue *queue);

	// 演奏時間（ms）。旧 mxv の MX_NowTime 相当。
	uint32_t nowTimeMs() const { return nowTimeMs_; }

	// 曲が終端に達したか（G->L001e13）。
	bool terminated() const;

private:
	struct ChannelState {
		uint8_t noteOnKey;
		uint8_t noteOnKeyCount;
		uint8_t bendKey;
		uint8_t gateBack;  // 旧 mxv の S001b_back
		uint8_t hit;
		int volume;
		int panpot;
		long detune;
		int voice;
		int q;
		uint32_t ptr;
		long lfoPitch;
		int lfoPitch1;
		int lfoPitch2;
		int lfoPitch3;
		int lfoPitch4;
		long lfoVolume;
		int lfoVolume1;
		int lfoVolume2;
		int lfoVolume3;
		int levelMeterInit;
		int levelMeterPeak;
		int levelMeterPeakLast;
		int levelMeterPeakLate;
		int levelMeterPeakDecCt;
		int levelMeterNow;
		int levelMeterNowLast;
	};

	const MXWORK_CH *ChannelWork(int ch) const;

	// PCM 鍵盤（8ch を 1 段に重ねて描く）で、消した鍵の隣接黒鍵を描き直す。
	void RedrawNeighborBlackKeys(uint64_t frame, int ch, DispQueue *queue);

	MxdrvContext *context_;
	const MXWORK_CH *fm_;      // FM 8ch + PCM 1ch
	const MXWORK_CH *pcm_;     // PCM 7ch
	const MXWORK_GLOBAL *g_;
	ChannelState ch_[16];
	uint32_t nowTimeMs_;
};

}  // namespace mxv2

#endif  // MXV2_STATUSWATCH_H
