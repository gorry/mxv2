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

	// 描いてある鍵盤とレベルメーターを消す。
	// **演奏位置が飛んだとき（曲の切り替え・シーク）と止めたときに呼ぶ。**
	// 頼むのは Player 側 (Player::TakeDisplayReset)、呼ぶのはメインループ。
	//
	// 消す位置は**描いた側で覚えておく**しかない。イベントは「デコード位置」で
	// 打刻され、画面に出るのは「再生位置」まで追いついた分だけなので、両者は
	// リングバッファのぶんだけずれている。**キューを捨てると、画面に出ている
	// 鍵盤を消す指示まで一緒に消える**し、生成側の StatusWatch が覚えているのは
	// デコード位置の状態なので、そちらからも作れない。
	//
	// **新しいイベントを描く前に呼ぶこと。**あとから呼ぶと、飛んだ先で
	// 鳴り始めた鍵盤まで消してしまう。
	void AllOff();

private:
	// 鍵盤の段。FM 8ch がそれぞれ 1 段、PCM 8ch は重ねて 1 段。
	static const int kNoteRows = 9;
	// 鍵盤の数。実際に使うのは 0..95 だが、余裕を見て 2 のべき乗で持つ。
	static const int kNoteKeys = 128;

	DrawScreen *draw_;

	// 旧 mxv の LastStatus[].LevelMeter に相当。点灯セルの表。
	char levelMeter_[16][kLevelMeterWidth];

	// いま押されている（＝画面に描いてある）鍵盤。AllOff で使う。
	bool noteOn_[kNoteRows][kNoteKeys];

	Visualizer(const Visualizer &);
	Visualizer &operator=(const Visualizer &);
};

}  // namespace mxv2

#endif  // MXV2_VISUALIZER_H
