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
      dragOriginY_(0),
      dragOriginThumb_(0),
      nextRepeatMs_(0),
      dragOriginTopPx_(0),
      pendingCursor_(-1),
      dragMoved_(false),
      lastMoveY_(0),
      lastMoveMs_(0),
      dragVelocity_(0.0f),
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
	// ファイルリストより先に見ること。スキンによっては [FileList] の矩形が
	// [ScrollBar] の矩形を含んでいることがあり（Phone がそうだった）、
	// 順番が逆だとスクロールバーのクリックがファイルリストに食われて
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

	// ファイルリスト
	{
		const int row = draw_->HitCheckFileList(x, y);
		if (row >= 0) {
			const int index = filer_->top() + row;
			captured_ = kCapturedFileList;
			dragOriginY_ = y;
			dragOriginTopPx_ = filer_->topPx();
			dragMoved_ = false;
			lastMoveY_ = y;
			lastMoveMs_ = SDL_GetTicks();
			dragVelocity_ = 0.0f;
			// 空行 (項目より下) からでも掴めるようにする。指で送るときに
			// 「下の余白は掴めない」となると使いにくい。選ぶものは無い。
			// ブレーキで触ったときは選ばない。
			pendingCursor_ =
			    (!braking && index < filer_->itemCount()) ? index : -1;

			// W クリックはその場で開く。カーソルは離したときに合わせるので、
			// ここでは先に合わせておく。
			if (clicks >= 2 && pendingCursor_ >= 0) {
				filer_->SetCursor(pendingCursor_);
				return kMouseRequestOpenCursor;
			}
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
			const uint32_t total = player_->playTimeMs();
			if (total != 0) {
				player_->SeekMs((uint32_t)((uint64_t)total * hit /
				                           draw_->progressBarWidth()));
			}
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
			// 旧 mxv と同じくドラッグ追従はしない (シークが重いため)。
			break;

		default:
			break;
	}
}

MouseRequest MouseInput::OnButtonUp(int x, int y) {
	const int captured = captured_;
	const int hit = capturedHit_;
	const int pending = pendingCursor_;
	const bool moved = dragMoved_;
	ReleaseAll();

	// ファイルリストは、ドラッグせずに離したときだけカーソルを合わせる。
	if (captured == kCapturedFileList) {
		if (!moved) {
			if (pending >= 0) filer_->SetCursor(pending);
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
	dragMoved_ = false;
	draw_->SetScrollBarFlags(0);
}

}  // namespace mxv2
