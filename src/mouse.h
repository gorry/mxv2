// mxv2 - マウス操作（旧 mxv/mxv.cpp の WM_LBUTTON* / WM_MOUSEMOVE /
//                    WM_MOUSEWHEEL / WM_USER_*_PRESS の移植）
//
// 旧 mxv は SetCapture/ReleaseCapture でボタンの押しっぱなしを追いかけていた。
// SDL はボタンを押した時点で暗黙にキャプチャするので、ここでは「今どの部品を
// 掴んでいるか」だけを持つ。
//
// 曲の差し替え (MDX の読み込み) はここではやらず、MouseRequest として
// メインループへ返す。演奏中の操作 (停止・一時停止・音量・シーク) は
// Player を直接叩く。

#ifndef MXV2_MOUSE_H
#define MXV2_MOUSE_H

#include <cstdint>

#include <SDL.h>

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;

// マウス操作の結果、メインループにやってもらうこと。
enum MouseRequest {
	kMouseRequestNone = 0,
	kMouseRequestOpenCursor,  // カーソル位置を開く (曲なら演奏 / ディレクトリなら移動)
	kMouseRequestPrev,        // 前の MDX へ
	kMouseRequestNext,        // 次の MDX へ
	kMouseRequestReplay,      // 今の曲を頭から
	kMouseRequestToggleCont,
	kMouseRequestToggleRepeat,
	kMouseRequestContextMenu,  // バナーを押した (右クリックの代わり)
};

class MouseInput {
public:
	MouseInput(DrawScreen *draw, Filer *filer, Player *player);

	// SDL のマウスイベントを 1 つ処理する。マウス以外のイベントは無視する。
	MouseRequest Handle(const SDL_Event &ev);

	// 矢印・ページ送りの押しっぱなしオートリピート。毎フレーム 1 回呼ぶ。
	void Poll(uint32_t nowMs);

	// 操作キーの押下表示ビット。DrawScreen::PutPlayKey の status へ足す。
	uint32_t playKeyPressMask() const { return pressMask_; }

	// 旧 mxv のオートリピート。20ms タイマで「10 回待って以降 2 回ごと」
	// だったので、200ms 待って以降 40ms ごとにする。
	static const uint32_t kRepeatDelayMs = 200;
	static const uint32_t kRepeatIntervalMs = 40;

	// ホイール 1 段で送る行数 (旧 mxv は SPI_GETWHEELSCROLLLINES)。
	static const int kWheelLines = 3;

private:
	enum Captured {
		kCapturedNone = 0,
		kCapturedFileList,
		kCapturedScrollBar,
		kCapturedPlayKey,
		kCapturedTotalVolBar,
		kCapturedProgressBar,
		kCapturedBanner,
	};

	MouseRequest OnButtonDown(int x, int y, int clicks);
	MouseRequest OnButtonUp(int x, int y);
	void OnMotion(int x, int y);
	void OnWheel(int dy);

	// スクロールバーの押下 1 回分。旧 mxv の WM_USER_SCROLLBAR_PRESS。
	void PressScrollBar(int hit);
	void ReleaseAll();

	DrawScreen *draw_;
	Filer *filer_;
	Player *player_;

	int captured_;
	int capturedHit_;   // 掴んだ部品のヒットコード
	uint32_t pressMask_;

	int lastX_, lastY_;      // 直近のマウス位置（オートリピート用）
	int dragOriginY_;        // つまみを掴んだ時のマウス y
	int dragOriginThumb_;    // つまみを掴んだ時のつまみ位置
	uint32_t nextRepeatMs_;  // 次のオートリピート時刻

	MouseInput(const MouseInput &);
	MouseInput &operator=(const MouseInput &);
};

}  // namespace mxv2

#endif  // MXV2_MOUSE_H
