// mxv2 - MXDRV ワークの監視とビジュアライズイベントの生成
//
// 旧 mxv/mx.cpp の MX_UpdateStatusDisplay() をほぼそのまま移植したもの。
// 打刻を実時間からサンプル位置へ変えた点、および描画呼び出し（消費側）を
// 分離した点だけが異なる。

#include "statuswatch.h"

#include <climits>
#include <cmath>
#include <cstring>

#include "mxdrvptr.h"

namespace mxv2 {

namespace {

// 旧 mxv の VolTable。@v 値をレベルメータ用の 0..127 へ写す。
const uint8_t kVolTable[16] = {
	0x2a, 0x28, 0x25, 0x22, 0x20, 0x1d, 0x1a, 0x18,
	0x15, 0x12, 0x10, 0x0d, 0x0a, 0x08, 0x05, 0x02,
};

// 鍵盤表示用。12 音のうち黒鍵はどれか。
const uint8_t kIsBlack[12] = { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 };

// PCM 8ch それぞれの鍵盤色。
const uint8_t kKeyboardColor[8] = { 0, 1, 2, 4, 6, 8, 10, 12 };

inline int Clamp(int v, int lo, int hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

}  // namespace

StatusWatch::StatusWatch()
    : context_(0), fm_(0), pcm_(0), g_(0), opmWork_(0), nowTimeMs_(0),
      opmCallbackActive_(false), opmPmd_(-1), opmAmd_(-1) {
	memset(opmReg_, 0, sizeof(opmReg_));
	Reset();
}

void StatusWatch::Bind(MxdrvContext *context) {
	context_ = context;
	fm_ = (const MXWORK_CH *)MXDRV_GetWork(context, MXDRV_WORK_FM);
	pcm_ = (const MXWORK_CH *)MXDRV_GetWork(context, MXDRV_WORK_PCM);
	g_ = (const MXWORK_GLOBAL *)MXDRV_GetWork(context, MXDRV_WORK_GLOBAL);
	opmWork_ = (const volatile int8_t *)MXDRV_GetWork(context, MXDRV_WORK_OPM);
}

void StatusWatch::OnOpmWrite(uint8_t reg, uint8_t data) {
	opmReg_[reg] = data;
	// $19 は bit7 で PMD (1) / AMD (0) を書き分ける。両方を別に覚える。
	if (reg == 0x19) {
		if (data & 0x80) {
			opmPmd_ = data & 0x7f;
		} else {
			opmAmd_ = data & 0x7f;
		}
	}
}

uint8_t StatusWatch::OpmReg(int no) const {
	if (opmCallbackActive_) return opmReg_[no & 0xff];
	if (opmWork_ == 0) return 0;
	return (uint8_t)opmWork_[no & 0xff];
}

void StatusWatch::Reset() {
	nowTimeMs_ = 0;
	// レジスタの写しは曲の切り替えで消さない（MXDRV は Play2 で書き直すし、
	// 書かれなかったレジスタは前の値のまま OPM に残っている）。前回値だけ捨てる。
	for (int ch = 0; ch < 8; ch++) {
		tone_[ch].chReg = -1;
		for (int s = 0; s < 4; s++) {
			for (int g = 0; g < 6; g++) tone_[ch].op[s][g] = -1;
		}
	}
	for (int i = 0; i < 6; i++) toneGlobal_[i] = -1;
	for (int ch = 0; ch < 16; ch++) {
		ChannelState *l = &ch_[ch];
		memset(l, 0, sizeof(*l));
		l->noteOnKey = (uint8_t)-1;
		l->bendKey = (uint8_t)-1;
		l->volume = INT_MAX;
		l->panpot = INT_MAX;
		l->detune = LONG_MAX;
		l->voice = INT_MAX;
		l->q = INT_MAX;
		l->ptr = (uint32_t)-1;
		l->lfoPitch = LONG_MAX;
		l->lfoPitch1 = INT_MAX;
		l->lfoPitch2 = INT_MAX;
		l->lfoPitch3 = INT_MAX;
		l->lfoPitch4 = INT_MAX;
		l->lfoVolume = LONG_MAX;
		l->lfoVolume1 = INT_MAX;
		l->lfoVolume2 = INT_MAX;
		l->lfoVolume3 = INT_MAX;
		l->levelMeterInit = 1;
		l->levelMeterPeakLast = -1;
		l->levelMeterNowLast = -1;
	}
}

void StatusWatch::ForgetLastValues() {
	for (int ch = 0; ch < 8; ch++) {
		tone_[ch].chReg = -1;
		for (int s = 0; s < 4; s++) {
			for (int g = 0; g < 6; g++) tone_[ch].op[s][g] = -1;
		}
	}
	for (int i = 0; i < 6; i++) toneGlobal_[i] = -1;
	const uint32_t now = nowTimeMs_;
	Reset();
	nowTimeMs_ = now;
}

const MXWORK_CH *StatusWatch::ChannelWork(int ch) const {
	if (ch <= 8) return &fm_[ch];
	return &pcm_[ch - 9];
}

bool StatusWatch::terminated() const {
	return g_ != 0 && g_->L001e13 != 0;
}

void StatusWatch::OnOpmInt() {
	if (g_ == 0) return;

	nowTimeMs_ = (uint32_t)((uint64_t)g_->PLAYTIME * 1024 / 4000);

	// ポーリング間隔をまたぐ「同じ音程の打ち直し」を取りこぼさない。
	// 旧 mxv の MXCALLBACK_OPMINTfunc と同じ判定。
	for (int ch = 0; ch < 16; ch++) {
		const MXWORK_CH *p = ChannelWork(ch);
		ChannelState *l = &ch_[ch];
		if (l->gateBack < p->S001b) l->hit = 1;
		l->gateBack = p->S001b;
	}
}

void StatusWatch::Poll(uint64_t frame, DispQueue *q) {
	if (g_ == 0) return;

	const bool isTerminate = (g_->L001e13 != 0);

	// ---- FM 8ch -------------------------------------------------------
	for (int ch = 0; ch < 8; ch++) {
		const MXWORK_CH *p = &fm_[ch];
		ChannelState *l = &ch_[ch];
		long sts;
		int keyon = 0;

		const bool audible =
		    (p->S0016 & (1 << 3)) != 0 && (g_->L001e06 & (1 << ch)) != 0 && !isTerminate;

		// 音色番号（@v 相当）。S0004 はメモリプール内オフセット。
		const uint8_t voiceColor =
		    (uint8_t)(((p->S0004 - g_->L002228) / 0x1b) % 13);

		// key
		sts = (long)(uint8_t)((p->S0012 + 27) >> 6);
		if (audible) {
			if ((long)l->noteOnKey != sts || l->hit) {
				q->Push(frame, DISP_KEYOFF, (uint8_t)ch, l->noteOnKey, 0);
				q->Push(frame, DISP_KEYON, (uint8_t)ch, (uint8_t)sts, voiceColor);
				l->noteOnKey = (uint8_t)sts;
				l->hit = 0;
				keyon = 1;
			}
		} else {
			if (l->noteOnKey != (uint8_t)-1) {
				q->Push(frame, DISP_KEYOFF, (uint8_t)ch, l->noteOnKey, 0);
				l->noteOnKey = (uint8_t)-1;
			}
		}

		// bend
		sts = (long)p->S0014 - (long)p->S0012;
		if (sts < 0) {
			sts = -(long)(uint8_t)((-sts) >> 6);
		} else {
			sts = (long)(uint8_t)(sts >> 6);
		}
		sts += (long)(uint8_t)((p->S0012 + 27) >> 6);
		sts = Clamp((int)sts, 0, 95);
		if (audible) {
			if ((long)l->bendKey != sts) {
				q->Push(frame, DISP_KEYOFF, (uint8_t)ch, l->bendKey, 0);
				q->Push(frame, DISP_KEYBEND, (uint8_t)ch, (uint8_t)sts, voiceColor);
				l->bendKey = (uint8_t)sts;
				q->Push(frame, DISP_KEYON, (uint8_t)ch, l->noteOnKey, voiceColor);
			}
		} else {
			if (l->bendKey != (uint8_t)-1) {
				q->Push(frame, DISP_KEYOFF, (uint8_t)ch, l->bendKey, 0);
				l->bendKey = (uint8_t)-1;
			}
		}

		// Volume
		sts = (long)p->S0022;
		if (l->volume != (int)sts) {
			q->Push(frame, DISP_VOLUME, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->volume = (int)sts;
		}

		// Panpot
		sts = (long)((p->S001c & 0xc0) >> 6);
		if (l->panpot != (int)sts) {
			q->Push(frame, DISP_PANPOT, (uint8_t)ch, (uint8_t)sts, 0);
			l->panpot = (int)sts;
		}

		// Detune
		sts = (long)(int16_t)p->S0010;
		if (l->detune != sts) {
			q->Push(frame, DISP_DETUNE, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->detune = sts;
		}

		// Voice
		{
			const uint8_t *voice = MxdrvOfsToPtr(context_, p->S0004);
			sts = (voice == 0) ? 0 : (long)voice[-1];
			if (l->voice != (int)sts) {
				q->Push(frame, DISP_VOICE, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
				l->voice = (int)sts;
			}
		}

		// Q
		sts = (long)p->S001e;
		if (l->q != (int)sts) {
			q->Push(frame, DISP_Q, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->q = (int)sts;
		}

		// Ptr
		sts = (p->S0000 == 0) ? 0 : (long)(p->S0000 - g_->L001e34);
		if (l->ptr != (uint32_t)sts) {
			q->Push(frame, DISP_PTR, (uint8_t)(ch | ((sts >> (16 - 4)) & 0xf0)),
			        (uint8_t)(sts >> 8), (uint8_t)sts);
			l->ptr = (uint32_t)sts;
		}

		// LFO Pitch
		sts = (long)(int16_t)(p->S0036 >> 16);
		if (l->lfoPitch != sts) {
			q->Push(frame, DISP_LFOPITCH, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->lfoPitch = sts;
		}
		sts = (long)p->S0026;
		if (l->lfoPitch1 != (int)sts) {
			q->Push(frame, DISP_LFOPITCH1, (uint8_t)ch, (uint8_t)sts, 0);
			l->lfoPitch1 = (int)sts;
		}
		sts = (long)((uint16_t)p->S003c / 2);
		if (l->lfoPitch2 != (int)sts) {
			q->Push(frame, DISP_LFOPITCH2, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->lfoPitch2 = (int)sts;
		}
		sts = (long)(uint16_t)p->S003c * (long)(((int32_t)p->S002e) >> 8);
		sts += (sts > 0) ? 128 : -128;
		sts /= 256 * 2;
		if (l->lfoPitch3 != (int)sts) {
			q->Push(frame, DISP_LFOPITCH3, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->lfoPitch3 = (int)sts;
		}
		sts = (long)p->S0024;
		if (l->lfoPitch4 != (int)sts) {
			q->Push(frame, DISP_LFOPITCH4, (uint8_t)ch, (uint8_t)sts, 0);
			l->lfoPitch4 = (int)sts;
		}

		// LFO Volume
		sts = (long)(int8_t)(p->S004a >> 8);
		if (l->lfoVolume != sts) {
			q->Push(frame, DISP_LFOVOLUME, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->lfoVolume = sts;
		}
		sts = (long)p->S0040;
		if (l->lfoVolume1 != (int)sts) {
			q->Push(frame, DISP_LFOVOLUME1, (uint8_t)ch, (uint8_t)sts, 0);
			l->lfoVolume1 = (int)sts;
		}
		sts = (long)((uint16_t)p->S004c / 2);
		if (l->lfoVolume2 != (int)sts) {
			q->Push(frame, DISP_LFOVOLUME2, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->lfoVolume2 = (int)sts;
		}
		sts = (long)(uint16_t)p->S004c * (long)(int16_t)p->S0044;
		sts += (sts > 0) ? 128 : -128;
		sts /= 256;
		if (l->lfoVolume3 != (int)sts) {
			q->Push(frame, DISP_LFOVOLUME3, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->lfoVolume3 = (int)sts;
		}

		// Level Meter
		{
			int levelMeterPut = 0;
			l->levelMeterNowLast = l->levelMeterNow;
			l->levelMeterPeakLast = l->levelMeterPeak;
			if (l->levelMeterNow > 0) {
				l->levelMeterNow =
				    (int)sqrt((double)l->levelMeterNow * l->levelMeterNow * 120.0 / 128);
			} else if (l->levelMeterNow < 0) {
				l->levelMeterNow = 0;
			}
			if (l->levelMeterPeakLate >= 50) {
				l->levelMeterPeak -= 256;
				if (l->levelMeterPeakDecCt >= 4) {
					l->levelMeterPeakDecCt = 0;
					l->levelMeterPeak -= 256;
				}
			} else {
				l->levelMeterPeakLate++;
			}
			if (l->levelMeterPeak < 0) l->levelMeterPeak = 0;
			if (keyon) {
				int vol = p->S0022;
				if (vol >= 128) {
					vol = 127 - (vol & 127);
				} else {
					vol = 127 - kVolTable[vol & 0x0f];
				}
				l->levelMeterNow = vol * 256;
				l->levelMeterPeak = vol * 256;
				l->levelMeterPeakLate = 0;
				l->levelMeterPeakDecCt = 0;
			}
			if (l->levelMeterInit) {
				l->levelMeterInit = 0;
				levelMeterPut = 1;
			}
			if (l->levelMeterPeak / 2 != l->levelMeterPeakLast / 2) levelMeterPut = 1;
			if (l->levelMeterNow / 2 != l->levelMeterNowLast / 2) levelMeterPut = 1;
			if (levelMeterPut) {
				q->Push(frame, DISP_LEVELMETER, (uint8_t)ch,
				        (uint8_t)(l->levelMeterPeak / 256 / 2),
				        (uint8_t)(l->levelMeterNow / 256 / 2));
			}
		}
	}

	// ---- PCM 8ch (鍵盤は 1 段にまとめて表示する) -----------------------
	for (int ch = 8; ch < 16; ch++) {
		const MXWORK_CH *p = ChannelWork(ch);
		ChannelState *l = &ch_[ch];
		long sts;

		const bool audible =
		    (p->S0016 & (1 << 3)) != 0 && (g_->L001e06 & (1 << ch)) != 0 && !isTerminate;

		// key
		sts = (long)(uint8_t)((p->S0012 + 27) >> 6);
		if (audible) {
			if ((long)l->noteOnKey != sts || l->hit) {
				q->Push(frame, DISP_KEYOFF, 8, l->noteOnKey, 0);
				RedrawNeighborBlackKeys(frame, ch, q);
				q->Push(frame, DISP_KEYON, 8, (uint8_t)sts, kKeyboardColor[ch - 8]);
				l->noteOnKey = (uint8_t)sts;
				l->noteOnKeyCount = 5;
				l->hit = 0;
			} else if (l->noteOnKeyCount) {
				l->noteOnKeyCount--;
				if (l->noteOnKeyCount == 0) {
					q->Push(frame, DISP_KEYOFF, 8, l->noteOnKey, 0);
					RedrawNeighborBlackKeys(frame, ch, q);
					q->Push(frame, DISP_KEYBEND, 8, l->noteOnKey, kKeyboardColor[ch - 8]);
				}
			}
		} else {
			if (l->noteOnKey != (uint8_t)-1) {
				q->Push(frame, DISP_KEYOFF, 8, l->noteOnKey, 0);
				RedrawNeighborBlackKeys(frame, ch, q);
				l->noteOnKey = (uint8_t)-1;
				l->noteOnKeyCount = 0;
			}
		}

		// Volume
		sts = (long)p->S0022;
		if (l->volume != (int)sts) {
			q->Push(frame, DISP_PCMVOLUME, (uint8_t)ch, (uint8_t)(sts >> 8), (uint8_t)sts);
			l->volume = (int)sts;
		}

		// Ptr
		{
			uint32_t ptr = 0;
			if (g_->L001e06 & (1 << ch)) ptr = p->S0000;
			sts = (ptr == 0) ? 0 : (long)(ptr - g_->L001e34);
			if (l->ptr != (uint32_t)sts) {
				q->Push(frame, DISP_PCMPTR, (uint8_t)(ch | ((sts >> (16 - 4)) & 0xf0)),
				        (uint8_t)(sts >> 8), (uint8_t)sts);
				l->ptr = (uint32_t)sts;
			}
		}
	}

	PollTone(frame, q);
}

// ---- 音色データ表示（tonedata.md） -----------------------------------------
// 描画側が今どちらのモードかに関わらず、変化した値を積む。切り替えたときは
// Player::RequestStatusRefresh → ForgetLastValues で全部積み直される。
void StatusWatch::PollTone(uint64_t frame, DispQueue *q) {
	// FM 8ch: $20+ch と、スロットごとの 5 レジスタ + 音色データの TL。
	for (int ch = 0; ch < 8; ch++) {
		const MXWORK_CH *p = &fm_[ch];
		ToneState *t = &tone_[ch];

		const int chReg = OpmReg(0x20 + ch) & 0x3f;
		if (t->chReg != chReg) {
			q->Push(frame, DISP_OPMCH, (uint8_t)ch, (uint8_t)chReg, 0);
			t->chReg = chReg;
		}

		// 音色データ（S0004 は音色番号の次のバイトを指す。+6〜+9 が TL）。
		const uint8_t *voice = MxdrvOfsToPtr(context_, p->S0004);
		for (int s = 0; s < 4; s++) {
			const int slot = ch + s * 8;
			int v[kNumOpmOpGroups];
			v[kOpmOpDT1MUL] = OpmReg(0x40 + slot);
			v[kOpmOpKSAR] = OpmReg(0x80 + slot);
			v[kOpmOpAMED1R] = OpmReg(0xa0 + slot);
			v[kOpmOpDT2D2R] = OpmReg(0xc0 + slot);
			v[kOpmOpD1LRR] = OpmReg(0xe0 + slot);
			v[kOpmOpTL] = (voice == 0) ? 0 : (voice[6 + s] & 0x7f);
			for (int g = 0; g < kNumOpmOpGroups; g++) {
				if (t->op[s][g] == v[g]) continue;
				q->Push(frame, DISP_OPMOP, (uint8_t)(ch | (s << 4)), (uint8_t)g, (uint8_t)v[g]);
				t->op[s][g] = v[g];
			}
		}
	}

	// OPM 全体の値（PCM の段に出す）。値 | (有効 << 8) で前回値と比べる。
	int gv[kNumOpmGlobalKinds];
	{
		const int noise = OpmReg(0x0f);
		gv[kOpmGlobalNoise] = (noise & 0x80) ? ((noise & 0x1f) | 0x100) : 0;
		gv[kOpmGlobalClockB] = OpmReg(0x12) | 0x100;
		gv[kOpmGlobalLFOFreq] = OpmReg(0x18) | 0x100;
		int pmd = opmPmd_, amd = opmAmd_;
		if (!opmCallbackActive_) {
			// 通知が無いときは $19 の最後の書き込みしか分からない。
			const int r = OpmReg(0x19);
			pmd = (r & 0x80) ? (r & 0x7f) : -1;
			amd = (r & 0x80) ? -1 : (r & 0x7f);
		}
		gv[kOpmGlobalLFOPMD] = (pmd < 0) ? 0 : (pmd | 0x100);
		gv[kOpmGlobalLFOAMD] = (amd < 0) ? 0 : (amd | 0x100);
		gv[kOpmGlobalLFOWave] = (OpmReg(0x1b) & 0x03) | 0x100;
	}
	for (int k = 0; k < kNumOpmGlobalKinds; k++) {
		if (toneGlobal_[k] == gv[k]) continue;
		q->Push(frame, DISP_OPMGLOBAL, (uint8_t)k, (uint8_t)(gv[k] & 0xff),
		        (uint8_t)((gv[k] & 0x100) ? 1 : 0));
		toneGlobal_[k] = gv[k];
	}
}

// PCM の鍵盤は 8ch を 1 段に重ねて描くため、ある鍵を消すと隣接する黒鍵の
// 描画が欠ける。旧 mxv と同じく、消した鍵の隣に鳴っている黒鍵があれば
// 描き直しイベントを積む。
void StatusWatch::RedrawNeighborBlackKeys(uint64_t frame, int ch, DispQueue *q) {
	const ChannelState *l = &ch_[ch];
	for (int i = 8; i < 16; i++) {
		if (i == ch) continue;
		ChannelState *l2 = &ch_[i];
		if (l2->noteOnKey >= 12 * 8) continue;
		if (!kIsBlack[l2->noteOnKey % 12]) continue;
		if (l2->noteOnKey + 1 != l->noteOnKey && l2->noteOnKey - 1 != l->noteOnKey) continue;
		if (!l2->noteOnKeyCount) {
			q->Push(frame, DISP_KEYOFF, 8, l2->noteOnKey, 0);
		}
		q->Push(frame, (uint8_t)(l2->noteOnKeyCount ? DISP_KEYON : DISP_KEYBEND), 8,
		        l2->noteOnKey, kKeyboardColor[i - 8]);
	}
}

}  // namespace mxv2
