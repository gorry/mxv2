package net.gorry.mxv2;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.util.Log;

/**
 * 端末の向きの固定の窓口。
 *
 * ネイティブ側（src/orientlock.cpp）から JNI で呼ばれる。
 *
 * <p>SDL の {@code SDL_HINT_ORIENTATIONS} はウィンドウを作るときにしか読まれない
 * ため、実行中に向きの許可を変えられない。ここで
 * {@link Activity#setRequestedOrientation(int)} を直に叩く。
 *
 * <p>縦・横の固定に {@code SENSOR_*} を使うのは意図的で、**上下反転（180 度）は
 * 許したい**ため（screen_orientation.md）。{@code SCREEN_ORIENTATION_PORTRAIT} だと
 * 反転しても画面が回らない。
 *
 * <p>{@code setRequestedOrientation} は UI スレッドから呼ぶ必要があるので、
 * Activity へ post する。
 */
public class OrientationBridge {
	private static final String TAG = "mxv2";

	// ネイティブの mxv2::orientlock::Request と同じ並び。
	public static final int ORIENT_FREE = 0;
	public static final int ORIENT_PORTRAIT = 1;
	public static final int ORIENT_LANDSCAPE = 2;

	private static Activity sActivity;

	/** MainActivity.onCreate から渡してもらう。 */
	public static synchronized void setActivity(Activity activity) {
		sActivity = activity;
	}

	public static synchronized boolean available() {
		return sActivity != null;
	}

	/** ネイティブから呼ばれる。値は ORIENT_* のどれか。 */
	public static void setOrientation(int orientation) {
		final Activity activity;
		synchronized (OrientationBridge.class) {
			activity = sActivity;
		}
		if (activity == null) return;

		final int req;
		switch (orientation) {
			case ORIENT_PORTRAIT:
				req = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
				break;
			case ORIENT_LANDSCAPE:
				req = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
				break;
			default:
				// 端末の回転ロックの設定には従う（SDL が既定で使う値と同じ）。
				req = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
				break;
		}
		activity.runOnUiThread(new Runnable() {
			@Override
			public void run() {
				try {
					activity.setRequestedOrientation(req);
				} catch (Exception e) {
					Log.w(TAG, "setRequestedOrientation failed", e);
				}
			}
		});
	}
}
