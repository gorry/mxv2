// mxv2 - マウス操作

#include "mouse.h"

#include <cmath>

#include "drawscreen.h"
#include "filer.h"
#include "player.h"

namespace mxv2 {

namespace {

// 慣性の減衰。1 フレームごとに v *= exp(-dt / kFlingTau) する。
// 0.25 秒で 1/e まで落ちるので、滑る距離はおよそ「離した速度 * 0.25 秒」。
const float kFlingTau = 0.25f;

// フレームが飛んだときに一気に進まないよう、1 回の刻みはここまで。
const float kFlingMaxStepSec = 0.1f;

// 指の速度の平滑化。1 に近いほど直近の動きを重く見る。
const float kVelocityBlend = 0.4f;

}  // namespace

MouseInput::MouseInput(DrawScreen *draw, Filer *filer, Player *player)
    : draw_(draw),
      filer_(filer),
      player_(player),
      captured_(kCapturedNone),
      capturedHit_(0),
      pressMask_(0),
      lastX_(0),
      lastY_(0),
      dragOriginX_(0),
      dragOriginY_(0),
      dragOriginThumb_(0),
      nextRepeatMs_(0),
      dragOriginTopPx_(0),
      pendingCursor_(-1),
      pendingOpen_(false),
      swipeArmed_(false),
      dragMoved_(false),
      seekDragging_(false),
      seekWasPaused_(false),
      seekDragMs_(0),
      lastMoveX_(0),
      lastMoveY_(0),
      lastMoveMs_(0),
      dragVelocity_(0.0f),
      dragVelocityX_(0.0f),
      flingActive_(false),
      flingVelocity_(0.0f),
      flingPos_(0.0f),
      flingAppliedPx_(0),
      flingLastMs_(0) {}

MouseRequest MouseInput::Handle(const SDL_Event &ev) {
	switch (ev.type) {
		case SDL_MOUSEBUTTONDOWN:
			if (ev.button.button != SDL_BUTTON_LEFT) break;
			lastX_ = ev.button.x;
			lastY_ = ev.button.y;
			return OnButtonDown(ev.button.x, ev.button.y, ev.button.clicks);

		case SDL_MOUSEBUTTONUP:
			if (ev.button.button != SDL_BUTTON_LEFT) break;
			lastX_ = ev.button.x;
			lastY_ = ev.button.y;
			return OnButtonUp(ev.button.x, ev.button.y);

		case SDL_MOUSEMOTION:
			lastX_ = ev.motion.x;
			lastY_ = ev.motion.y;
			OnMotion(ev.motion.x, ev.motion.y);
			break;

		case SDL_MOUSEWHEEL: {
			int dy = ev.wheel.y;
			if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
			OnWheel(dy);
			break;
		}

		default:
			break;
	}
	return kMouseRequestNone;
}

// ---------------------------------------------------------------------------

MouseRequest MouseInput::OnButtonDown(int x, int y, int clicks) {
	// 慣性で滑っている最中に触ったらブレーキ。止めるための操作なので、
	// この押下では項目を選ばない（スマートフォンの作法に合わせる）。
	const bool braking = flingActive_;
	StopFling();

	if (captured_ != kCapturedNone) return kMouseRequestNone;

	// スクロールバー
	//
	// ファイラーより先に見ること。スキンによっては [FileList] の矩形が
	// [ScrollBar] の矩形を含んでいることがあり（Phone がそうだった）、
	// 順番が逆だとスクロールバーのクリックがファイラーに食われて
	// 「触っても反応しない」状態になる。描画はスクロールバーが上に来るので、
	// 当たり判定もそれに合わせる。
	{
		const int hit = draw_->HitCheckScrollBar(x, y);
		if (hit != DrawScreen::kHitScrollBarNone) {
			captured_ = kCapturedScrollBar;
			capturedHit_ = hit;
			dragOriginY_ = y;
			dragOriginThumb_ = draw_->scrollBarThumb();
			nextRepeatMs_ = SDL_GetTicks() + kRepeatDelayMs;
			PressScrollBar(hit);
			return kMouseRequestNone;
		}
	}

	// ファイラー
	{
		const int row = draw_->HitCheckFileList(x, y);
		if (row >= 0) {
			const int index = filer_->top() + row;
			captured_ = kCapturedFileList;
			dragOriginX_ = x;
			dragOriginY_ = y;
			dragOriginTopPx_ = filer_->topPx();
			dragMoved_ = false;
			lastMoveX_ = x;
			lastMoveY_ = y;
			lastMoveMs_ = SDL_GetTicks();
			dragVelocity_ = 0.0f;
			dragVelocityX_ = 0.0f;
			// 空行 (項目より下) からでも掴めるようにする。指で送るときに
			// 「下の余白は掴めない」となると使いにくい。選ぶものは無い。
			// ブレーキで触ったときは選ばない。
			pendingCursor_ =
			    (!braking && index < filer_->itemCount()) ? index : -1;
			// 滑っているのを止めるための押下では、横スワイプも見ない
			// （止めるつもりの操作で開いたり親へ上がったりしないように）。
			swipeArmed_ = !braking;

			// W クリックは**2 回目を離したとき**に開く（印だけ立てる）。
			// 押した時点で開くと、その直後に届く「離した」がその場所の
			// 選択として効いてしまう。
			pendingOpen_ = (clicks >= 2 && pendingCursor_ >= 0);
			return kMouseRequestNone;
		}
	}

	// 操作ボタン
	{
		const int hit = draw_->HitCheckPlayKey(x, y);
		if (hit != DrawScreen::kHitPlayKeyNone) {
			captured_ = kCapturedPlayKey;
			capturedHit_ = hit;
			pressMask_ = 1u << (hit - 1);
			// FASTPLAY だけは押している間だけ効く。
			if (hit == DrawScreen::kHitPlayKeyFastPlay) player_->SetFastPlay(true);
			return kMouseRequestNone;
		}
	}

	// 音量バー
	{
		int volume = 0;
		if (draw_->HitCheckTotalVolBar(x, y, &volume)) {
			captured_ = kCapturedTotalVolBar;
			player_->SetMainVolume(volume);
			return kMouseRequestNone;
		}
	}

	// バナー。右クリックできない環境（スマートフォンなど）のために、
	// ここを押すとコンテキストメニューを出す。実際に出すのは離したときで、
	// 押した時点で出すと、その直後に届く「離した」がメニューの外を
	// クリックした扱いになって即閉じてしまう。
	if (draw_->HitCheckBanner(x, y)) {
		captured_ = kCapturedBanner;
		return kMouseRequestNone;
	}

	// プログレスバー
	{
		const int hit = draw_->HitCheckProgressBar(x, y);
		if (hit >= 0) {
			captured_ = kCapturedProgressBar;
			// 総演奏時間が分かっていなければバーの位置に意味がないので、
			// 掴んだだけで何もしない。
			if (player_->playTimeMs() != 0) {
				// 掴んだ時点で演奏を止める。シークは重いので指には追従
				// させず、離すまではバーと PLAY TIME の表示だけを動かす。
				// もともと一時停止していたなら、離したあともそのまま。
				seekWasPaused_ = player_->paused();
				player_->Pause();
				seekDragging_ = true;
				UpdateSeekDrag(x);
			}
			return kMouseRequestNone;
		}
	}

	// 鍵盤。押した段（PCM は 8 等分した区画）のチャンネルのマスクを
	// 切り替える。実行するのは離したときなので、ここでは覚えるだけ。
	// 面積が大きいので、当たり判定は一番最後に見る（小さい部品を隠さない）。
	{
		const int ch = draw_->HitCheckKeyboard(x, y);
		if (ch >= 0) {
			captured_ = kCapturedKeyboard;
			capturedHit_ = ch;
			return kMouseRequestNone;
		}
	}

	// ステータス欄。こちらは 1 チャンネルではなく、FM か PCM の一括切り替え
	// （キーの 0 / Shift+0 と同じ）。どの段を押しても同じなので、覚えるのは
	// 「FM 側か PCM 側か」だけでよい。
	{
		const int row = draw_->HitCheckStatus(x, y);
		if (row >= 0) {
			captured_ = kCapturedStatus;
			capturedHit_ = (row < 8) ? 0 : 1;
			return kMouseRequestNone;
		}
	}

	return kMouseRequestNone;
}

void MouseInput::OnMotion(int x, int y) {
	switch (captured_) {
		case kCapturedScrollBar:
			if (capturedHit_ == DrawScreen::kHitScrollBarThumb) {
				const int thumb = dragOriginThumb_ + (y - dragOriginY_);
				draw_->SetScrollBarThumb(thumb);
				const int movement = draw_->scrollBarMovement();
				if (movement > 0) {
					// つまみも画素単位で送る。行に丸めるとつまみの動きと
					// 一覧の動きがずれて見える。
					filer_->SetTopPx((int)((int64_t)filer_->maxTopPx() *
					                       draw_->scrollBarThumb() / movement));
				}
			}
			// 矢印・ページ送りは Poll() 側で位置を見て打ち直す。
			break;

		case kCapturedFileList: {
			// 指の速度を控える。離したあとの滑りに使う。
			{
				const uint32_t now = SDL_GetTicks();
				const uint32_t dtMs = now - lastMoveMs_;
				if (dtMs > 0) {
					const float v = (float)(y - lastMoveY_) * 1000.0f / (float)dtMs;
					dragVelocity_ = dragVelocity_ * (1.0f - kVelocityBlend) +
					                v * kVelocityBlend;
					// 横も同じように控える（はじいたかの判定に使う）。
					const float vx = (float)(x - lastMoveX_) * 1000.0f / (float)dtMs;
					dragVelocityX_ = dragVelocityX_ * (1.0f - kVelocityBlend) +
					                 vx * kVelocityBlend;
					lastMoveX_ = x;
					lastMoveY_ = y;
					lastMoveMs_ = now;
				}
			}

			// 掴んだ場所からの移動量をそのまま画素で送る（指に追従する）。
			const int dy = y - dragOriginY_;
			// 少し動かした程度ではドラッグ扱いにしない。ここを 0 にすると、
			// 押したときに 1px ぶれただけで選択できなくなる。
			if (!dragMoved_ && (dy > -kDragSlopPx && dy < kDragSlopPx)) break;
			dragMoved_ = true;
			filer_->SetTopPx(dragOriginTopPx_ - dy);
			break;
		}

		case kCapturedPlayKey:
			// ボタンから外れたら押下表示を戻す。
			if (draw_->HitCheckPlayKey(x, y) == capturedHit_) {
				pressMask_ = 1u << (capturedHit_ - 1);
			} else {
				pressMask_ = 0;
			}
			break;

		case kCapturedTotalVolBar:
			player_->SetMainVolume(draw_->VolumeFromX(x));
			break;

		case kCapturedProgressBar:
			// 表示だけを指に追従させる。実際に飛ぶのは離したとき。
			UpdateSeekDrag(x);
			break;

		default:
			break;
	}
}

MouseRequest MouseInput::OnButtonUp(int x, int y) {
	// 離した位置も拾っておく。動かさずに離したときは MOUSEMOTION が
	// 届いていないことがある。
	if (seekDragging_) UpdateSeekDrag(x);

	const int captured = captured_;
	const int hit = capturedHit_;
	const int pending = pendingCursor_;
	const bool open = pendingOpen_;
	const bool moved = dragMoved_;

	// ファイラーを横へ**はじいた**か。速さ・動いた距離・向きの 3 つが
	// そろったときだけ。ゆっくり横へずらしただけ、縦に振ったついでに
	// 横がぶれただけ、指を止めてから離した、のどれでも効かない。
	//   右 … ENTER（カーソルの項目を開く）
	//   左 … BACKSPACE（親フォルダへ）
	int swipe = 0;
	if (captured_ == kCapturedFileList && swipeArmed_ &&
	    SDL_GetTicks() - lastMoveMs_ <= kVelocityStaleMs) {
		const int dx = x - dragOriginX_;
		const int dy = y - dragOriginY_;
		const int absDx = (dx < 0) ? -dx : dx;
		const int absDy = (dy < 0) ? -dy : dy;
		if (absDx >= kSwipeMinPx && absDx > absDy) {
			if (dx > 0 && dragVelocityX_ >= (float)kSwipeVelocityPxPerSec) swipe = 1;
			if (dx < 0 && dragVelocityX_ <= -(float)kSwipeVelocityPxPerSec) swipe = -1;
		}
	}
	const bool seeking = seekDragging_;
	const bool wasPaused = seekWasPaused_;
	const uint32_t seekMs = seekDragMs_;
	ReleaseAll();

	// シークバーは離した位置へ飛ぶ。SeekMs は飛ぶ前の一時停止を引き継ぐので、
	// 掴んだ時点で止めたぶんはここで戻す（もともと一時停止していたなら
	// そのまま止まっている）。
	if (captured == kCapturedProgressBar) {
		if (seeking) {
			player_->SeekMs(seekMs);
			if (!wasPaused) player_->Resume();
		}
		return kMouseRequestNone;
	}

	// ファイラーは、ドラッグせずに離したときだけカーソルを合わせる。
	// W クリックの 2 回目なら、合わせたうえで開く。
	if (captured == kCapturedFileList) {
		// 横へはじいたときは、縦の慣性も選択も行わない。
		// **カーソルは動かさない**——キーボードの ENTER / BACKSPACE を
		// 指で行うための操作なので、指を置いた行ではなく今のカーソルが
		// 対象になる（触った拍子にカーソルが飛ばない）。
		if (swipe > 0) return kMouseRequestOpenCursor;
		if (swipe < 0) return kMouseRequestGoParent;

		if (!moved) {
			if (pending >= 0) filer_->SetCursor(pending);
			if (open && pending >= 0) return kMouseRequestOpenCursor;
			return kMouseRequestNone;
		}
		// 振り切った勢いで滑らせる。離す前に指が止まっていた
		// （最後の動きから間が空いている）ときは、置いただけとみなす。
		if (SDL_GetTicks() - lastMoveMs_ <= kVelocityStaleMs) {
			// 中身は指に付いて動くので、topPx の向きは指と逆。
			StartFling(-dragVelocity_);
		}
		return kMouseRequestNone;
	}

	// バナーは、押した場所で離したときだけメニューを出す。
	if (captured == kCapturedBanner) {
		return draw_->HitCheckBanner(x, y) ? kMouseRequestContextMenu : kMouseRequestNone;
	}

	// 鍵盤も、押したチャンネルの上で離したときだけ切り替える
	// （操作ボタンと同じ作法）。
	if (captured == kCapturedKeyboard) {
		if (draw_->HitCheckKeyboard(x, y) == hit) player_->ToggleChannel(hit);
		return kMouseRequestNone;
	}

	// ステータス欄は FM / PCM の一括切り替え。同じ側の段で離したときだけ。
	if (captured == kCapturedStatus) {
		const int row = draw_->HitCheckStatus(x, y);
		if (row >= 0 && ((row < 8) ? 0 : 1) == hit) {
			player_->ToggleChannelGroup(hit == 0 ? Player::kChannelMaskFm
			                                    : Player::kChannelMaskPcm);
		}
		return kMouseRequestNone;
	}

	if (captured != kCapturedPlayKey) return kMouseRequestNone;

	// FASTPLAY は離した時点で必ず解除する。
	if (hit == DrawScreen::kHitPlayKeyFastPlay) {
		player_->SetFastPlay(false);
		return kMouseRequestNone;
	}

	// 旧 mxv はボタンから外れた場所で離しても動作していたが、押下表示は
	// 外れた時点で戻るので、見た目に合わせてここでは実行しない。
	if (draw_->HitCheckPlayKey(x, y) != hit) return kMouseRequestNone;

	switch (hit) {
		case DrawScreen::kHitPlayKeyPrev:
			return kMouseRequestPrev;
		case DrawScreen::kHitPlayKeyNext:
			return kMouseRequestNext;

		case DrawScreen::kHitPlayKeyStop:
			player_->Stop();
			return kMouseRequestNone;

		case DrawScreen::kHitPlayKeyPlay:
			if (player_->paused()) {
				player_->Resume();
				return kMouseRequestNone;
			}
			// カーソルが MDX ならそれを、そうでなければ今の曲を掛け直す。
			if (filer_->itemCount() > 0 &&
			    (filer_->item(filer_->cursor()).type & kFileItemMdx) != 0) {
				return kMouseRequestOpenCursor;
			}
			return kMouseRequestReplay;

		case DrawScreen::kHitPlayKeyPause:
			if (player_->paused()) {
				player_->Resume();
			} else {
				player_->Pause();
			}
			return kMouseRequestNone;

		case DrawScreen::kHitPlayKeyCont:
			return kMouseRequestToggleCont;
		case DrawScreen::kHitPlayKeyRepeat:
			return kMouseRequestToggleRepeat;

		default:
			return kMouseRequestNone;
	}
}

void MouseInput::OnWheel(int dy) {
	filer_->SetTop(filer_->top() - dy * kWheelLines);
}

// ---------------------------------------------------------------------------

void MouseInput::PressScrollBar(int hit) {
	const int rows = filer_->visibleRows();
	int flags = draw_->scrollBarFlags();

	switch (hit) {
		case DrawScreen::kHitScrollBarUpArrow:
			filer_->SetTop(filer_->top() - 1);
			flags |= DrawScreen::kScrollBarUpArrowDown;
			break;
		case DrawScreen::kHitScrollBarDownArrow:
			filer_->SetTop(filer_->top() + 1);
			flags |= DrawScreen::kScrollBarDownArrowDown;
			break;
		case DrawScreen::kHitScrollBarUpPage:
			filer_->SetTop(filer_->top() - rows);
			break;
		case DrawScreen::kHitScrollBarDownPage:
			filer_->SetTop(filer_->top() + rows);
			break;
		case DrawScreen::kHitScrollBarThumb:
			flags |= DrawScreen::kScrollBarDrag;
			break;
		default:
			break;
	}
	draw_->SetScrollBarFlags(flags);
}

// ---------------------------------------------------------------------------

void MouseInput::StartFling(float velocity) {
	if (velocity < (float)kFlingStartPxPerSec && velocity > -(float)kFlingStartPxPerSec) {
		StopFling();
		return;
	}
	flingVelocity_ = velocity;
	flingPos_ = (float)filer_->topPx();
	flingAppliedPx_ = filer_->topPx();
	flingLastMs_ = SDL_GetTicks();
	flingActive_ = true;
}

void MouseInput::UpdateFling(uint32_t nowMs) {
	if (!flingActive_) return;

	// キー操作やフォルダ移動など、こちら以外がスクロール位置を動かしたら
	// 手を引く。そのまま続けると、動かされた先から引き戻してしまう。
	if (filer_->topPx() != flingAppliedPx_) {
		StopFling();
		return;
	}

	const uint32_t dtMs = nowMs - flingLastMs_;
	if (dtMs == 0) return;
	flingLastMs_ = nowMs;

	float dt = (float)dtMs / 1000.0f;
	if (dt > kFlingMaxStepSec) dt = kFlingMaxStepSec;

	flingPos_ += flingVelocity_ * dt;
	const int want = (int)((flingPos_ >= 0.0f) ? (flingPos_ + 0.5f) : (flingPos_ - 0.5f));
	filer_->SetTopPx(want);
	flingAppliedPx_ = filer_->topPx();
	// 端に着いたら止める（SetTopPx が丸めたかどうかで分かる）。
	if (flingAppliedPx_ != want) {
		StopFling();
		return;
	}

	flingVelocity_ *= expf(-dt / kFlingTau);
	if (flingVelocity_ < (float)kFlingStopPxPerSec &&
	    flingVelocity_ > -(float)kFlingStopPxPerSec) {
		StopFling();
	}
}

void MouseInput::Poll(uint32_t nowMs) {
	UpdateFling(nowMs);

	if (captured_ != kCapturedScrollBar) return;
	if (capturedHit_ == DrawScreen::kHitScrollBarThumb) return;
	if (nowMs < nextRepeatMs_) return;

	nextRepeatMs_ = nowMs + kRepeatIntervalMs;
	// 押した部品の上から外れている間は止める（旧 mxv と同じ）。
	if (draw_->HitCheckScrollBar(lastX_, lastY_) == capturedHit_) {
		PressScrollBar(capturedHit_);
	}
}

void MouseInput::ReleaseAll() {
	captured_ = kCapturedNone;
	capturedHit_ = 0;
	pressMask_ = 0;
	pendingCursor_ = -1;
	pendingOpen_ = false;
	swipeArmed_ = false;
	dragMoved_ = false;
	seekDragging_ = false;
	draw_->SetScrollBarFlags(0);
}

// ---------------------------------------------------------------------------

void MouseInput::UpdateSeekDrag(int x) {
	const uint32_t total = player_->playTimeMs();
	const int width = draw_->progressBarWidth();
	if (total == 0 || width <= 0) {
		seekDragMs_ = 0;
		return;
	}
	seekDragMs_ =
	    (uint32_t)((uint64_t)total * draw_->ProgressPosFromX(x) / width);
}

}  // namespace mxv2
