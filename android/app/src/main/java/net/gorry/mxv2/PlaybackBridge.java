package net.gorry.mxv2;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

import java.util.ArrayDeque;

/**
 * 演奏状態の通知の窓口。
 *
 * ネイティブ側（src/nowplaying.cpp）から JNI で呼ばれる。通知そのものを出すのは
 * 前面サービス {@link PlaybackService} で、ここは
 *
 *   ・ネイティブ → サービス … 出す内容（曲名・状態・位置）を預かって出し直させる
 *   ・サービス → ネイティブ … 通知のボタンや他アプリの都合で起きたことを
 *                              待ち行列に積む（ネイティブが takeRequest で取る）
 *
 * という受け渡しだけを持つ。**文言は message.ini からネイティブ経由で渡ってくる**
 * ので、ここにも strings.xml にも日本語は置かない。
 *
 * **呼ばれるスレッドが違う**（ネイティブは SDL のメインスレッド、サービスは
 * UI スレッド）ので、状態を触るところは synchronized にしてある。
 */
public class PlaybackBridge {
	private static final String TAG = "mxv2";

	// ネイティブの mxv2::nowplaying::Request と同じ並び。
	public static final int REQ_NONE = 0;
	public static final int REQ_PLAY = 1;
	public static final int REQ_PAUSE = 2;
	public static final int REQ_PREV = 3;
	public static final int REQ_NEXT = 4;
	public static final int REQ_STOP = 5;
	public static final int REQ_FOCUS_LOST = 6;
	public static final int REQ_FOCUS_GAINED = 7;

	private static Activity sActivity;

	/** 起きているサービス。onCreate/onDestroy で自分を入れ替える。 */
	private static PlaybackService sService;

	/** startService まで済んでいるか。 */
	private static boolean sStarted;

	// 通知に出す文言（message.ini 由来）。
	private static String sChannelName = "Playback";
	private static String sChannelDesc = "";
	private static String sPrevLabel = "Prev";
	private static String sPlayLabel = "Play";
	private static String sPauseLabel = "Pause";
	private static String sNextLabel = "Next";
	private static String sStopLabel = "Stop";

	// いま出す内容。
	private static String sTitle = "";
	private static String sText = "";
	private static boolean sPlaying;
	private static long sPosMs;
	private static long sDurMs;

	private static final ArrayDeque<Integer> sRequests = new ArrayDeque<Integer>();

	public static void setActivity(Activity a) {
		sActivity = a;
	}

	public static boolean available() {
		return sActivity != null;
	}

	// -------------------------------------------------------------------
	// ネイティブから
	// -------------------------------------------------------------------

	public static synchronized void setLabels(String channel, String channelDesc, String prev,
	                                          String play, String pause, String next,
	                                          String stop) {
		sChannelName = channel;
		sChannelDesc = channelDesc;
		sPrevLabel = prev;
		sPlayLabel = play;
		sPauseLabel = pause;
		sNextLabel = next;
		sStopLabel = stop;
	}

	/**
	 * 出す内容を差し替える。まだサービスが動いていなければ起こす。
	 *
	 * **前面サービスはアプリが前面にいるあいだしか起こせない**（Android 12 以降）。
	 * 演奏を始めるのは画面を見ているときなので、ふつうはここで起きる。曲が
	 * 変わるだけならサービスは動いたままなので、バックグラウンドでの自動送り
	 * (CONT/REPEAT) でも起こし直しは要らない。
	 */
	public static void update(String title, String text, boolean playing, long posMs,
	                          long durMs) {
		final Activity a = sActivity;
		if (a == null) return;

		synchronized (PlaybackBridge.class) {
			sTitle = title;
			sText = text;
			sPlaying = playing;
			sPosMs = posMs;
			sDurMs = durMs;
			if (!sStarted) {
				sStarted = true;
				try {
					Intent i = new Intent(a, PlaybackService.class);
					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
						a.startForegroundService(i);
					} else {
						a.startService(i);
					}
				} catch (Exception e) {
					// バックグラウンドからは起こせないことがある。演奏そのものは
					// 続くので、通知だけ諦める（次に前面へ戻ったときに出る）。
					Log.w(TAG, "cannot start the playback service", e);
					sStarted = false;
				}
				return;  // 起動時に onStartCommand が出す
			}
		}
		refresh();
	}

	/** 通知を消してサービスを止める。 */
	public static void shutdown() {
		final Activity a = sActivity;
		synchronized (PlaybackBridge.class) {
			if (!sStarted) return;
			sStarted = false;
		}
		if (a == null) return;
		try {
			a.stopService(new Intent(a, PlaybackService.class));
		} catch (Exception e) {
			Log.w(TAG, "cannot stop the playback service", e);
		}
	}

	public static synchronized int takeRequest() {
		if (sRequests.isEmpty()) return REQ_NONE;
		return sRequests.poll().intValue();
	}

	// -------------------------------------------------------------------
	// サービスから
	// -------------------------------------------------------------------

	static synchronized void setService(PlaybackService s) {
		sService = s;
	}

	/** サービスが止まった（自分から止まった場合も含む）。 */
	static synchronized void serviceGone(PlaybackService s) {
		if (sService == s) sService = null;
		sStarted = false;
	}

	/** 通知を出し直させる。UI スレッドへ渡すのはサービス側。 */
	static void refresh() {
		final PlaybackService s;
		synchronized (PlaybackBridge.class) {
			s = sService;
		}
		if (s != null) s.postRefresh();
	}

	static synchronized void postRequest(int req) {
		// 際限なく溜めない（ネイティブが取りに来ないことは無いはずだが、
		// バックグラウンドで詰まったときの保険）。
		if (sRequests.size() > 32) sRequests.clear();
		sRequests.add(Integer.valueOf(req));
	}

	// 通知を組み立てるための読み出し。まとめて 1 つの箱で渡す。
	static synchronized Snapshot snapshot() {
		Snapshot s = new Snapshot();
		s.channelName = sChannelName;
		s.channelDesc = sChannelDesc;
		s.prevLabel = sPrevLabel;
		s.playLabel = sPlayLabel;
		s.pauseLabel = sPauseLabel;
		s.nextLabel = sNextLabel;
		s.stopLabel = sStopLabel;
		s.title = sTitle;
		s.text = sText;
		s.playing = sPlaying;
		s.posMs = sPosMs;
		s.durMs = sDurMs;
		return s;
	}

	static class Snapshot {
		String channelName;
		String channelDesc;
		String prevLabel;
		String playLabel;
		String pauseLabel;
		String nextLabel;
		String stopLabel;
		String title;
		String text;
		boolean playing;
		long posMs;
		long durMs;
	}
}
