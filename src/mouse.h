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
	kMouseRequestGoParent,     // ファイラーで左へはじいた (BACKSPACE と同じ)
	kMouseRequestToggleFontSize,  // ファイラーを長押しした (TAB と同じ)
};

class MouseInput {
public:
	MouseInput(DrawScreen *draw, Filer *filer, Player *player);

	// SDL のマウスイベントを 1 つ処理する。マウス以外のイベントは無視する。
	MouseRequest Handle(const SDL_Event &ev);

	// 矢印・ページ送りの押しっぱなしオートリピートと、長押しの判定。
	// 毎フレーム 1 回呼ぶ。長押しが成立したときはその要求を返す。
	MouseRequest Poll(uint32_t nowMs);

	// 操作ボタンの押下表示ビット。DrawScreen::PutPlayKey の status へ足す。
	uint32_t playKeyPressMask() const { return pressMask_; }

	// シークバーを掴んでいる間は true。この間の演奏位置の表示（進捗バーと
	// PLAY TIME）は、実際の演奏位置ではなく seekDragMs() を使う。
	// 実際にシークするのは離したときで、それまで演奏は止めてある。
	// 掴む前が一時停止なら、離したあとも一時停止のまま。
	bool seekDragging() const { return seekDragging_; }
	uint32_t seekDragMs() const { return seekDragMs_; }

	// 旧 mxv のオートリピート。20ms タイマで「10 回待って以降 2 回ごと」
	// だったので、200ms 待って以降 40ms ごとにする。
	static const uint32_t kRepeatDelayMs = 200;
	static const uint32_t kRepeatIntervalMs = 40;

	// ホイール 1 段で送る行数 (旧 mxv は SPI_GETWHEELSCROLLLINES)。
	static const int kWheelLines = 3;

	// これ以上動かしたらドラッグとみなす閾値 (論理 px)。
	static const int kDragSlopPx = 3;

	// 長押し。指（マウス）を動かさずにこれだけ押し続けたら成立する。
	// 500ms（ユーザーの指定）。Android の「押し続ける時間」の設定で選べる
	// **最短**が 400ms なので、それより短くはしない。
	// キーボードの無い端末のための導線で、今あるのは 2 つ:
	//   STOP      … フェードアウト（F キー）
	//   ファイラー … 文字サイズの切り替え（TAB キー）
	// 成立した押下は、離しても何もしない（停止も選択も慣性も起こさない）。
	static const uint32_t kLongPressMs = 500;

	// 慣性スクロール（スマートフォンのスワイプに合わせたもの）。
	// 離したときの速度で滑り続け、指数的に減速する。
	// 速度は論理 px / 秒。
	static const int kFlingStartPxPerSec = 80;  // これ未満なら滑らせない
	static const int kFlingStopPxPerSec = 20;   // これ未満まで落ちたら止める
	// 指が止まってからこれだけ経って離したら、滑らせない（置いただけ）。
	static const uint32_t kVelocityStaleMs = 80;

	// ファイラーの横スワイプ。**はじく**操作にだけ反応させたいので、
	// 速さと動いた距離の両方で見る（ゆっくり横へずらしただけでは効かない）。
	// 右へはじくと ENTER（カーソルの項目を開く）、左へはじくと BACKSPACE
	// （親フォルダへ）。速度は論理 px / 秒、距離は論理 px。
	static const int kSwipeVelocityPxPerSec = 400;
	static const int kSwipeMinPx = 40;

private:
	enum Captured {
		kCapturedNone = 0,
		kCapturedFileList,
		kCapturedScrollBar,
		kCapturedPlayKey,
		kCapturedTotalVolBar,
		kCapturedProgressBar,
		kCapturedBanner,
		kCapturedKeyboard,
		kCapturedStatus,
	};

	MouseRequest OnButtonDown(int x, int y, int clicks);
	MouseRequest OnButtonUp(int x, int y);
	void OnMotion(int x, int y);
	void OnWheel(int dy);

	// スクロールバーの押下 1 回分。旧 mxv の WM_USER_SCROLLBAR_PRESS。
	void PressScrollBar(int hit);
	void ReleaseAll();

	// シークバーを掴んでいる間、マウスの x から seekDragMs_ を作り直す。
	void UpdateSeekDrag(int x);

	// 慣性スクロール。velocity は Filer::topPx() の毎秒変化量。
	void StartFling(float velocity);
	void UpdateFling(uint32_t nowMs);
	void StopFling() { flingActive_ = false; }

	DrawScreen *draw_;
	Filer *filer_;
	Player *player_;

	int captured_;
	int capturedHit_;   // 掴んだ部品のヒットコード
	uint32_t pressMask_;

	int lastX_, lastY_;      // 直近のマウス位置（オートリピート用）
	int dragOriginX_;        // 掴んだ時のマウス x（横スワイプの判定用）
	int dragOriginY_;        // 掴んだ時のマウス y（つまみ / ファイラー共用）
	int dragOriginThumb_;    // つまみを掴んだ時のつまみ位置
	uint32_t nextRepeatMs_;  // 次のオートリピート時刻

	// ファイラーのドラッグスクロール。指に追従するよう画素単位で送る。
	// カーソルは押した時点では動かさず、ドラッグせずに離したときだけ動かす
	// （フォルダを開くダイアログと同じ作法。ドラッグしたつもりが選択に
	// なってしまうのを避ける）。
	int dragOriginTopPx_;  // 掴んだ時の Filer::topPx()
	int pendingCursor_;    // 離したときに合わせる項目。-1 なら合わせない
	bool pendingOpen_;     // W クリックの 2 回目。離したときに開く印
	bool swipeArmed_;      // 横スワイプを見てよい押下か（ブレーキでは見ない）
	bool dragMoved_;       // ドラッグ扱いになったか

	// 長押し。押した時刻と、この押下で長押しが成立したか（以後は離すまで
	// 何もしない）。操作ボタンとファイラーで使う。
	uint32_t pressStartMs_;
	bool longPressDone_;

	// シークバーのドラッグ。シーク (MXDRV_PlayAt) は曲の頭から空回しする
	// 重い処理なので、指に追従して掛けるわけにはいかない。掴んでいる間は
	// 演奏を止めて表示だけを動かし、離したときに一度だけシークする。
	bool seekDragging_;
	bool seekWasPaused_;   // 掴む前が一時停止だったか（離したあとに戻す）
	uint32_t seekDragMs_;  // 指の位置が指す演奏位置

	// 指の速度。直近の動きを平滑化したもの (論理 px / 秒、画面座標)。
	// 縦は慣性スクロール、横はスワイプ（ENTER / BACKSPACE）の判定に使う。
	int lastMoveX_;
	int lastMoveY_;
	uint32_t lastMoveMs_;
	float dragVelocity_;
	float dragVelocityX_;

	// 慣性で滑っている間の状態。
	bool flingActive_;
	float flingVelocity_;  // topPx の毎秒変化量
	float flingPos_;       // 端数を持ち越すための位置
	int flingAppliedPx_;   // 前のフレームで自分が入れた値。
	                       // 食い違っていたら他所が動かしたので手を引く
	uint32_t flingLastMs_;

	MouseInput(const MouseInput &);
	MouseInput &operator=(const MouseInput &);
};

}  // namespace mxv2

#endif  // MXV2_MOUSE_H
