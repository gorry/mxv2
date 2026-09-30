package net.gorry.mxv2;

import android.app.PendingIntent;
import android.app.PictureInPictureParams;
import android.app.RemoteAction;
import android.content.pm.PackageManager;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.util.Log;
import android.util.Rational;

import java.util.ArrayList;

/**
 * ピクチャー・イン・ピクチャー（小窓）の窓口。仕様は memo/pip.md。
 *
 * 小窓に出すのは「通知の mxv2」相当（曲名・状態・時刻と、前の曲 /
 * 一時停止・再開 / 次の曲）で、中身は {@link PipView} が
 * {@link PlaybackBridge} の持っている状態から描く。スキンで描いた画面は
 * 出さない。
 *
 * **小窓の間は SDL の面 (SurfaceView) を隠す。** 面が消えると SDL は
 * PAUSED に移り、ネイティブには SDL_APP_WILLENTERBACKGROUND が届くので、
 * mxv2 は今のバックグラウンドの経路（描かない・演奏と曲送りと通知は続ける）に
 * そのまま入る。小さなウィンドウでキャンバスを作り直したり、横長のウィンドウを見て横向き用の
 * スキンへ切り替えたりさせないため。
 *
 * ネイティブ（src/pip.cpp）からは SDL のメインスレッドで呼ばれ、Activity
 * からは UI スレッドで呼ばれる。PiP の API を触るのは UI スレッドだけ。
 */
public class PipBridge {
	private static final String TAG = "mxv2";

	// ネイティブの mxv2::Settings::PipMode と同じ並び。
	public static final int MODE_OFF = 0;
	public static final int MODE_PLAYING = 1;  // 演奏中だけ（既定）
	public static final int MODE_ALWAYS = 2;

	/** 小窓の縦横比。曲名・状態・時刻・バーの 4 段が収まる形。 */
	private static final Rational kAspect = new Rational(2, 1);

	/** 小窓から戻ったあと、ネイティブにキャンバスを合わせ直させるまでの間 (ms)。 */
	private static final long kSettleMs = 250;

	private static MainActivity sActivity;

	/** ホームへ戻ったときのふるまい（[mxv2 の設定] の [画面]）。 */
	private static volatile int sMode = MODE_PLAYING;

	/**
	 * 小窓の中身を出している（SDL の面を隠している）。ネイティブが
	 * フレームごとに読む。戻るときは面を出してから少し待って下ろす。
	 */
	private static volatile boolean sInPip;

	/** 小窓の中身に切り替え済みか。UI スレッドだけが触る。 */
	private static boolean sShowing;

	/**
	 * 自分で別の画面（SAF のフォルダ選択・ブラウザなど）を開いたところ。
	 * そのときも onUserLeaveHint が呼ばれるので、小窓には入らない。
	 * 戻ってきた onResume で下ろす。UI スレッドだけが触る。
	 */
	private static boolean sLaunchingOwn;

	/** 端末が PiP を持っているか。最初に尋ねたときに決める。 */
	private static Boolean sSupported;

	/**
	 * MainActivity.onCreate から。**Activity が作り直されたときも来る**ので、
	 * 前の Activity の小窓の状態をここで捨てる。static はプロセスが続く限り
	 * 残り、SDL は同じプロセスで SDL_main を最初から走らせ直すため、残して
	 * おくとネイティブの見張り (inPip) が立ったままになり、キャンバスがウィンドウに
	 * 合わなくなる（2026-09-25、ユーザーの報告）。
	 */
	static void setActivity(MainActivity a) {
		sActivity = a;
		sShowing = false;
		sInPip = false;
		sLaunchingOwn = false;
	}

	// -------------------------------------------------------------------
	// ネイティブから
	// -------------------------------------------------------------------

	/** この端末で小窓が使えるか。Android 8.0 以降で、機能を持っている端末だけ。 */
	public static synchronized boolean available() {
		if (sSupported != null) return sSupported.booleanValue();
		final MainActivity a = sActivity;
		if (a == null) return false;
		boolean ok = false;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			try {
				ok = a.getPackageManager().hasSystemFeature(
				    PackageManager.FEATURE_PICTURE_IN_PICTURE);
			} catch (Exception e) {
				Log.w(TAG, "cannot query the picture-in-picture feature", e);
			}
		}
		sSupported = Boolean.valueOf(ok);
		return ok;
	}

	/** ホームへ戻ったときのふるまいを変える。値は MODE_* のどれか。 */
	public static void setMode(int mode) {
		sMode = mode;
		postUpdateParams();
	}

	/** メニューの [小窓で表示]。設定が「しない」でも入る。 */
	public static void enter() {
		final MainActivity a = sActivity;
		if (a == null || !available()) return;
		a.runOnUiThread(new Runnable() {
			@Override
			public void run() {
				enterNow();
			}
		});
	}

	/** 小窓の中身を出しているか。 */
	public static boolean inPip() {
		return sInPip;
	}

	// -------------------------------------------------------------------
	// PlaybackBridge から（演奏状態が変わった）
	// -------------------------------------------------------------------

	/** ボタン（一時停止 / 再開）と自動で入るかどうかを出し直す。 */
	static void onStateChanged() {
		postUpdateParams();
	}

	// -------------------------------------------------------------------
	// MainActivity から（UI スレッド）
	// -------------------------------------------------------------------

	/**
	 * ホームへ戻る・履歴を開くなど、利用者が離れようとしている。
	 * Android 12 以降は setAutoEnterEnabled に任せるので、ここは 11 まで。
	 */
	static void onUserLeaveHint() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) return;
		if (sLaunchingOwn || !shouldAutoEnter()) return;
		enterNow();
	}

	/**
	 * 小窓へ移るとき、Activity は一時停止する。**Android 12 以降の自動の
	 * 入り方では onPictureInPictureModeChanged より先にここが来る**ので、
	 * ここで面を隠して、小さなウィンドウで一度でも描かれるのを避ける。
	 */
	static void onPause() {
		final MainActivity a = sActivity;
		if (a == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
		if (a.isInPictureInPictureMode()) showPipContent();
	}

	static void onResume() {
		if (sLaunchingOwn) {
			sLaunchingOwn = false;
			updateParams();
		}
	}

	/** 自分で別の画面を開く直前（MainActivity.startActivityForResult）。 */
	static void onLaunchingOwn() {
		sLaunchingOwn = true;
		// Android 12 以降は、自動で入るフラグを先に下ろしておく。
		updateParams();
	}

	static void onModeChanged(boolean inPip) {
		if (inPip) {
			showPipContent();
		} else {
			hidePipContent();
		}
	}

	// -------------------------------------------------------------------

	private static void enterNow() {
		final MainActivity a = sActivity;
		if (a == null || !available()) return;
		// 表示するものが無いときは入らない。
		if (!PlaybackBridge.snapshot().active) return;
		// ウィンドウが小さくなる前から、ネイティブにはキャンバスを触らせない。
		sInPip = true;
		boolean ok = false;
		try {
			ok = a.enterPictureInPictureMode(buildParams());
		} catch (Exception e) {
			Log.w(TAG, "enterPictureInPictureMode failed", e);
		}
		if (!ok && !sShowing) sInPip = false;
	}

	private static void showPipContent() {
		final MainActivity a = sActivity;
		if (a == null || sShowing) return;
		sShowing = true;
		sInPip = true;
		a.showPipContent(true);
	}

	/**
	 * 小窓から戻った（広げた、または閉じた）。面を出すのは**ウィンドウが元の大きさに
	 * なってから**にしたいので、レイアウトを 1 回待つ。そのあと少し置いてから
	 * ネイティブの見張りを解く（出したばかりの面の大きさが落ち着くまで）。
	 */
	private static void hidePipContent() {
		final MainActivity a = sActivity;
		if (a == null || !sShowing) {
			sInPip = false;
			return;
		}
		sShowing = false;
		a.postToLayout(new Runnable() {
			@Override
			public void run() {
				// その間にまた入っていたら何もしない。
				if (sShowing) return;
				a.showPipContent(false);
				a.postToLayoutDelayed(new Runnable() {
					@Override
					public void run() {
						if (!sShowing) sInPip = false;
					}
				}, kSettleMs);
			}
		});
	}

	/** 自動で入ってよい状態か（ホームへ戻ったときに入るか）。 */
	private static boolean shouldAutoEnter() {
		if (sLaunchingOwn) return false;
		final PlaybackBridge.Snapshot s = PlaybackBridge.snapshot();
		if (!s.active) return false;
		switch (sMode) {
			case MODE_ALWAYS:
				return true;
			case MODE_PLAYING:
				return s.playing;
			default:
				return false;
		}
	}

	private static void postUpdateParams() {
		final MainActivity a = sActivity;
		if (a == null || !available()) return;
		a.runOnUiThread(new Runnable() {
			@Override
			public void run() {
				updateParams();
			}
		});
	}

	private static void updateParams() {
		final MainActivity a = sActivity;
		if (a == null || !available()) return;
		try {
			a.setPictureInPictureParams(buildParams());
		} catch (Exception e) {
			Log.w(TAG, "setPictureInPictureParams failed", e);
		}
	}

	private static PictureInPictureParams buildParams() {
		final MainActivity a = sActivity;
		final PlaybackBridge.Snapshot s = PlaybackBridge.snapshot();

		PictureInPictureParams.Builder b = new PictureInPictureParams.Builder();
		b.setAspectRatio(kAspect);

		// 小窓の中はタッチがシステムの操作パネルに取られるので、操作は
		// PiP のボタンで出す。行き先は通知のボタンと同じ PlaybackService。
		ArrayList<RemoteAction> actions = new ArrayList<RemoteAction>();
		actions.add(action(a, android.R.drawable.ic_media_previous, s.prevLabel,
		                   PlaybackService.ACTION_PREV));
		if (s.playing) {
			actions.add(action(a, android.R.drawable.ic_media_pause, s.pauseLabel,
			                   PlaybackService.ACTION_PAUSE));
		} else {
			actions.add(action(a, android.R.drawable.ic_media_play, s.playLabel,
			                   PlaybackService.ACTION_PLAY));
		}
		actions.add(action(a, android.R.drawable.ic_media_next, s.nextLabel,
		                   PlaybackService.ACTION_NEXT));
		b.setActions(actions);

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
			// ホームへ戻る動きと一緒に小窓になる。
			b.setAutoEnterEnabled(shouldAutoEnter());
			// 動画ではないので、大きさを変えるときに中身を引き伸ばして
			// 見せる必要は無い（描き直したものを出す）。
			b.setSeamlessResizeEnabled(false);
		}
		return b.build();
	}

	private static RemoteAction action(MainActivity a, int icon, String label, String intentAction) {
		final PendingIntent pi = PlaybackService.actionIntent(a, intentAction);
		return new RemoteAction(Icon.createWithResource(a, icon), label, label, pi);
	}
}
