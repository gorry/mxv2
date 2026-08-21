// mxv2 - ビジュアライズイベントの消費側
//
// DispQueue に積まれたイベントを「再生済みサンプル位置に追いついた分だけ」
// 取り出し、DrawScreen の Put* を呼ぶ。旧 mxv の MX_UpdateStatusDisplay()
// 後半 (DISPWORKBUF から取り出して Screen_Put* を呼ぶループ) にあたる。

#ifndef MXV2_VISUALIZER_H
#define MXV2_VISUALIZER_H

#include <cstdint>

#include "dispqueue.h"
#include "drawscreen.h"
#include "player.h"

namespace mxv2 {

class Visualizer {
public:
	explicit Visualizer(DrawScreen *draw);

	void Reset();

	// 再生位置 visualFrame に追いついたイベントを描画する。
	void Consume(DispQueue *queue, uint64_t visualFrame);

	// 進捗バー・操作ボタン・音量バーの更新。
	//   autoNext   = CONT LED (演奏終了で次の曲へ)
	//   autoRepeat = REPEAT LED
	//   pressMask  = マウスで押されている操作ボタンのビット (MouseInput から)
	void UpdateChrome(const Player &player, bool refresh, bool autoNext, bool autoRepeat,
	                  uint32_t pressMask);

private:
	DrawScreen *draw_;

	// 旧 mxv の LastStatus[].LevelMeter に相当。点灯セルの表。
	char levelMeter_[16][kLevelMeterWidth];

	Visualizer(const Visualizer &);
	Visualizer &operator=(const Visualizer &);
};

}  // namespace mxv2

#endif  // MXV2_VISUALIZER_H
