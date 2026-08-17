// mxv2 - ビジュアライズイベントの消費側

#include "visualizer.h"

#include <cstring>

namespace mxv2 {

Visualizer::Visualizer(DrawScreen *draw) : draw_(draw) {
	Reset();
}

void Visualizer::Reset() {
	memset(levelMeter_, 0, sizeof(levelMeter_));
}

void Visualizer::Consume(DispQueue *queue, uint64_t visualFrame) {
	if (draw_ == 0) return;

	DispWork w;
	while (queue->Peek(&w)) {
		// まだ鳴っていないイベントはここで止める。
		if (w.frame > visualFrame) break;
		queue->Pop();

		const int v16 = (w.param2 << 8) | w.param3;
		const int ptr = ((w.param1 & 0xf0) * (65536 >> 4)) | v16;
		const int row = w.param1 & 0x0f;

		switch (w.cmd) {
			case DISP_KEYOFF:
				draw_->PutNoteOff(w.param2, w.param1);
				break;
			case DISP_KEYON:
				draw_->PutNoteOn(w.param2, w.param1, w.param3, 0);
				break;
			case DISP_KEYBEND:
				draw_->PutNoteOn(w.param2, w.param1, w.param3, 1);
				break;

			case DISP_VOLUME:
				draw_->PutVolume(v16, w.param1);
				break;
			case DISP_PANPOT:
				draw_->PutPanpot(w.param2, w.param1);
				break;
			case DISP_DETUNE:
				draw_->PutDetune(v16, w.param1);
				break;
			case DISP_VOICE:
				draw_->PutVoice(v16, w.param1);
				break;
			case DISP_Q:
				draw_->PutQ(v16, w.param1);
				break;
			case DISP_PTR:
				draw_->PutPtr(ptr, row);
				break;

			case DISP_PCMVOLUME:
				draw_->PutPCMVolume(v16, w.param1);
				break;
			case DISP_PCMPTR:
				draw_->PutPCMPtr(ptr, row);
				break;

			case DISP_LFOPITCH:
				draw_->PutLFOPitch(v16, w.param1);
				break;
			case DISP_LFOPITCH1:
				draw_->PutLFOPitch1(w.param2, w.param1);
				break;
			case DISP_LFOPITCH2:
				draw_->PutLFOPitch2(v16, w.param1);
				break;
			case DISP_LFOPITCH3:
				draw_->PutLFOPitch3((int16_t)v16, w.param1);
				break;
			case DISP_LFOPITCH4:
				draw_->PutLFOPitch4(w.param2, w.param1);
				break;

			case DISP_LFOVOLUME:
				draw_->PutLFOVolume(v16, w.param1);
				break;
			case DISP_LFOVOLUME1:
				draw_->PutLFOVolume1(w.param2, w.param1);
				break;
			case DISP_LFOVOLUME2:
				draw_->PutLFOVolume2(v16, w.param1);
				break;
			case DISP_LFOVOLUME3:
				draw_->PutLFOVolume3((int16_t)v16, w.param1);
				break;

			case DISP_LEVELMETER: {
				// param3 までを点灯、param2 (ピーク) を 1 セルだけ点灯。
				const int ch = w.param1 & 0x0f;
				char *m = levelMeter_[ch];
				int i = 0;
				for (; i <= w.param3 && i < kLevelMeterWidth; i++) m[i] = 1;
				for (; i < kLevelMeterWidth; i++) m[i] = 0;
				if (w.param2 < kLevelMeterWidth) m[w.param2] = 1;
				draw_->PutLevelMeter(m, ch);
				break;
			}

			default:
				break;
		}
	}
}

void Visualizer::UpdateChrome(const Player &player, bool refresh, bool autoNext,
                              bool autoRepeat, uint32_t pressMask) {
	if (draw_ == 0) return;

	draw_->PutProgressBar(player.nowTimeMs(), player.playTimeMs(), refresh);

	uint32_t status = pressMask;
	if (player.playing()) {
		status |= DrawScreen::kPlayKeyPlayLed;
		if (player.paused()) status |= DrawScreen::kPlayKeyPauseLed;
	}
	if (autoNext) status |= DrawScreen::kPlayKeyContLed;
	if (autoRepeat) status |= DrawScreen::kPlayKeyRepeatLed;
	draw_->PutPlayKey(status, refresh);

	draw_->PutTotalVolBar(player.volumeBarPos(), refresh);
}

}  // namespace mxv2
