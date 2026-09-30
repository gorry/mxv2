package net.gorry.mxv2;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;

import java.util.Locale;

/**
 * 小窓（ピクチャー・イン・ピクチャー）の中身。仕様は memo/pip.md。
 *
 * <pre>
 * +----------------------------------+
 * | ARCTAN-X  Field Theme            |  曲名（1 行。収まらなければスクロール）
 * | 演奏中  CONT                      |  状態の行（通知と同じ文字列）
 * | 01:23 / 03:45                    |  経過 / 演奏時間
 * | [==========------------------]  |  進み具合
 * +----------------------------------+
 * </pre>
 *
 * 出す内容は {@link PlaybackBridge#snapshot()} から毎回読む。ネイティブが
 * 位置を送ってくるのは状態が変わったときと飛んだときだけなので、演奏中は
 * 受け取った時刻からの経過を足して進める（通知の PlaybackState と同じ）。
 *
 * タッチはシステムの操作パネルに取られてここへは届かないので、入力は扱わない。
 */
final class PipView extends View {
	private static final String TAG = "mxv2";

	/** 同梱のフォント（apk の assets の中の置き場所）。 */
	private static final String kFontAsset = "assets/MPLUS1p-Regular.ttf";

	// 曲名のスクロール。本体の画面下の曲名欄（drawscreen.cpp の
	// ScrollOffsetAt / kTitleScrollHoldMs / kScrollBaseHeightsPerSec /
	// kTitleScrollSlack）と揃える。先頭で止まる → 一定の速さで末尾まで
	// 送る → 末尾で止まる → 先頭へ戻る、の繰り返し。
	private static final long kScrollHoldMs = 3000;
	private static final float kScrollHeightsPerSec = 40.0f / 24.0f;
	private static final float kScrollSlackPx = 2.0f;

	// 字の大きさ (sp)。端末の文字サイズの設定で伸び縮みする。曲名は
	// 通知の題名、状態と時刻は本文くらい。
	private static final float kTitleSp = 16.0f;
	private static final float kSubSp = 14.0f;

	/** 止まっている間の描き直しの間隔 (ms)。時刻の表示に足りればよい。 */
	private static final long kIdleTickMs = 500;

	// 色。暗い地に明るい字の固定（スキンの配色には追従しない）。
	private static final int kBgColor = 0xff101418;
	private static final int kTitleColor = 0xffffffff;
	private static final int kSubColor = 0xffb0b8c0;
	private static final int kDimColor = 0xff707880;
	private static final int kBarBgColor = 0xff333a40;
	private static final int kBarFgColor = 0xff37ff2c;  // 操作ボタンの LED の点灯色

	private final Paint mTitlePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mSubPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint mBarPaint = new Paint();

	/** スクロールの起点。曲名が変わったら数え直す。 */
	private String mScrollTitle;
	private long mScrollBaseMs;

	private boolean mRunning;

	private final Runnable mTick = new Runnable() {
		@Override
		public void run() {
			invalidate();
		}
	};

	PipView(Context context) {
		super(context);
		Typeface tf = Typeface.DEFAULT;
		try {
			tf = Typeface.createFromAsset(context.getAssets(), kFontAsset);
		} catch (Exception e) {
			// 無くても端末のフォントで描ける。
			Log.w(TAG, "cannot load the bundled font for the PiP view", e);
		}
		mTitlePaint.setTypeface(tf);
		mSubPaint.setTypeface(tf);
		setBackgroundColor(kBgColor);
	}

	/** 描き始める（小窓に入った）。 */
	void start() {
		mRunning = true;
		mScrollTitle = null;
		invalidate();
	}

	/** 描くのをやめる（小窓から戻った）。 */
	void stop() {
		mRunning = false;
		removeCallbacks(mTick);
	}

	@Override
	protected void onDraw(Canvas c) {
		super.onDraw(c);
		final int w = getWidth();
		final int h = getHeight();
		if (w <= 0 || h <= 0) return;

		final PlaybackBridge.Snapshot s = PlaybackBridge.snapshot();
		final long now = SystemClock.elapsedRealtime();

		// 余白・行の間隔・バーはウィンドウの高さから決める（小窓は利用者が広げ
		// 縮めできる）。
		final float pad = h * 0.08f;
		final float gap = h * 0.04f;
		final float barH = Math.max(2.0f, h * 0.05f);
		final float left = pad;
		final float right = w - pad;
		final float inner = right - left;

		// 字の大きさは**端末の文字サイズの設定に従う**（sp）。
		// applyDimension は Android 14 以降の非線形の拡大
		// （大きい字ほど伸び率が小さい）も反映する。
		// ただし 3 行がバーの上に収まらないときは、収まるまで同じ割合で
		// 縮める（小窓は小さく、設定を大きくしているとあふれるため）。
		final DisplayMetrics dm = getResources().getDisplayMetrics();
		float titleSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, kTitleSp, dm);
		float subSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, kSubSp, dm);
		mTitlePaint.setTextSize(titleSize);
		mSubPaint.setTextSize(subSize);
		{
			// 行の高さは字の大きさに比例するので、1 回測れば縮める割合が出る。
			final Paint.FontMetrics t0 = mTitlePaint.getFontMetrics();
			final Paint.FontMetrics s0 = mSubPaint.getFontMetrics();
			final float lines = (t0.descent - t0.ascent) + (s0.descent - s0.ascent) * 2.0f;
			final float room = h - pad * 2.0f - barH - gap * 2.5f;
			if (lines > room && room > 0.0f) {
				final float f = room / lines;
				titleSize *= f;
				subSize *= f;
				mTitlePaint.setTextSize(titleSize);
				mSubPaint.setTextSize(subSize);
			}
		}
		final Paint.FontMetrics tm = mTitlePaint.getFontMetrics();
		final Paint.FontMetrics sm = mSubPaint.getFontMetrics();

		boolean scrolling = false;

		// ---- 曲名 -------------------------------------------------------
		float y = pad;
		final String title = (s.title != null) ? s.title : "";
		if (!title.equals(mScrollTitle)) {
			mScrollTitle = title;
			mScrollBaseMs = now;
		}
		mTitlePaint.setColor(s.active ? kTitleColor : kDimColor);
		final float titleW = mTitlePaint.measureText(title);
		float offset = 0.0f;
		if (titleW > inner + kScrollSlackPx) {
			final float max = titleW - inner;
			offset = scrollOffsetAt(now - mScrollBaseMs, max, kScrollHoldMs,
			                        titleSize * kScrollHeightsPerSec);
			scrolling = true;
		}
		final float titleBottom = y + (tm.descent - tm.ascent);
		c.save();
		c.clipRect(left, y, right, titleBottom);
		c.drawText(title, left - offset, y - tm.ascent, mTitlePaint);
		c.restore();
		y = titleBottom + gap;

		// ---- 状態の行と時刻 -------------------------------------------------
		if (s.active) {
			mSubPaint.setColor(kSubColor);
			drawClipped(c, (s.text != null) ? s.text : "", left, right, y, sm);
			y += (sm.descent - sm.ascent) + gap * 0.5f;

			long pos = s.posMs;
			if (s.playing) pos += now - s.stampMs;
			if (pos < 0) pos = 0;
			if (s.durMs > 0 && pos > s.durMs) pos = s.durMs;
			final String time = (s.durMs > 0) ? formatTime(pos) + " / " + formatTime(s.durMs)
			                                  : formatTime(pos);
			drawClipped(c, time, left, right, y, sm);

			// ---- 進み具合 ------------------------------------------------
			final float barTop = h - pad - barH;
			mBarPaint.setColor(kBarBgColor);
			c.drawRect(left, barTop, right, barTop + barH, mBarPaint);
			if (s.durMs > 0) {
				final float r = (float)pos / (float)s.durMs;
				mBarPaint.setColor(kBarFgColor);
				c.drawRect(left, barTop, left + inner * r, barTop + barH, mBarPaint);
			}
		}

		if (mRunning) {
			removeCallbacks(mTick);
			if (scrolling) {
				postInvalidateOnAnimation();
			} else {
				postDelayed(mTick, kIdleTickMs);
			}
		}
	}

	private void drawClipped(Canvas c, String text, float left, float right, float top,
	                         Paint.FontMetrics fm) {
		c.save();
		c.clipRect(left, top, right, top + (fm.descent - fm.ascent));
		c.drawText(text, left, top - fm.ascent, mSubPaint);
		c.restore();
	}

	/** drawscreen.cpp の ScrollOffsetAt と同じ式。 */
	private static float scrollOffsetAt(long t, float maxPx, long holdMs, float pxPerSec) {
		if (maxPx <= 0.0f || pxPerSec <= 0.0f) return 0.0f;
		final long moveMs = (long)(maxPx * 1000.0f / pxPerSec + 0.5f);
		final long cycleMs = holdMs * 2 + moveMs;
		final long u = t % cycleMs;
		if (u < holdMs) return 0.0f;
		if (u >= holdMs + moveMs) return maxPx;
		final float off = (u - holdMs) * pxPerSec / 1000.0f;
		return (off > maxPx) ? maxPx : off;
	}

	private static String formatTime(long ms) {
		final long sec = ms / 1000;
		return String.format(Locale.ROOT, "%02d:%02d", sec / 60, sec % 60);
	}
}
