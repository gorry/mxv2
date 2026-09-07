package net.gorry.mxv2;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

/**
 * 演奏中の通知を出す前面サービス。
 *
 * mxv2 はバックグラウンドへ回っても演奏を続ける（SDL の BLOCK_ON_PAUSE を
 * 切ってある）。**前面サービスが立っていないと、画面が消えたあとに OS が
 * プロセスを止めてしまう**ので、演奏しているあいだはこれを走らせる。
 *
 * 出す内容はネイティブ側が決めて {@link PlaybackBridge} に預ける。ここは
 * それを Notification と MediaSession の形にするだけで、演奏そのものには
 * 触らない（ボタンが押されたら PlaybackBridge へ要求を積み、ネイティブの
 * メインループが実行する）。
 */
public class PlaybackService extends Service {
	private static final String TAG = "mxv2";

	private static final String CHANNEL_ID = "mxv2.playback";
	private static final int NOTIFY_ID = 1;

	private static final String ACTION_PREV = "net.gorry.mxv2.action.PREV";
	private static final String ACTION_PLAY = "net.gorry.mxv2.action.PLAY";
	private static final String ACTION_PAUSE = "net.gorry.mxv2.action.PAUSE";
	private static final String ACTION_NEXT = "net.gorry.mxv2.action.NEXT";
	private static final String ACTION_STOP = "net.gorry.mxv2.action.STOP";

	private Handler mHandler;
	private MediaSession mSession;
	private PowerManager.WakeLock mWakeLock;
	private AudioManager mAudioManager;
	private boolean mHasFocus;
	private boolean mForeground;

	/** 他のアプリが音を使い始めた / 返してきた。 */
	private final AudioManager.OnAudioFocusChangeListener mFocusListener =
	    new AudioManager.OnAudioFocusChangeListener() {
		    public void onAudioFocusChange(int change) {
			    switch (change) {
				    case AudioManager.AUDIOFOCUS_LOSS:
				    case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
				    case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
					    // 小さくして鳴らし続ける（ダッキング）作りにはしていない。
					    // MDX は音量を絞ると聴き取れないので、どれも止める。
					    PlaybackBridge.postRequest(PlaybackBridge.REQ_FOCUS_LOST);
					    break;
				    case AudioManager.AUDIOFOCUS_GAIN:
					    PlaybackBridge.postRequest(PlaybackBridge.REQ_FOCUS_GAINED);
					    break;
				    default:
					    break;
			    }
		    }
	    };

	/** ヘッドホンが抜けた。スピーカーで鳴り出さないように止める。 */
	private final BroadcastReceiver mNoisyReceiver = new BroadcastReceiver() {
		public void onReceive(Context context, Intent intent) {
			if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) {
				PlaybackBridge.postRequest(PlaybackBridge.REQ_PAUSE);
			}
		}
	};

	@Override
	public void onCreate() {
		super.onCreate();
		mHandler = new Handler(Looper.getMainLooper());
		PlaybackBridge.setService(this);

		createChannel();

		mSession = new MediaSession(this, "mxv2");
		mSession.setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS |
		                  MediaSession.FLAG_HANDLES_TRANSPORT_CONTROLS);
		mSession.setCallback(new MediaSession.Callback() {
			@Override
			public void onPlay() {
				PlaybackBridge.postRequest(PlaybackBridge.REQ_PLAY);
			}

			@Override
			public void onPause() {
				PlaybackBridge.postRequest(PlaybackBridge.REQ_PAUSE);
			}

			@Override
			public void onStop() {
				PlaybackBridge.postRequest(PlaybackBridge.REQ_STOP);
			}

			@Override
			public void onSkipToNext() {
				PlaybackBridge.postRequest(PlaybackBridge.REQ_NEXT);
			}

			@Override
			public void onSkipToPrevious() {
				PlaybackBridge.postRequest(PlaybackBridge.REQ_PREV);
			}
		});
		mSession.setActive(true);

		PowerManager pm = (PowerManager)getSystemService(Context.POWER_SERVICE);
		if (pm != null) {
			mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "mxv2:playback");
			mWakeLock.setReferenceCounted(false);
		}

		mAudioManager = (AudioManager)getSystemService(Context.AUDIO_SERVICE);
		requestFocus();

		registerReceiver(mNoisyReceiver,
		                 new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
	}

	@Override
	public int onStartCommand(Intent intent, int flags, int startId) {
		final String action = (intent != null) ? intent.getAction() : null;
		if (ACTION_PREV.equals(action)) {
			PlaybackBridge.postRequest(PlaybackBridge.REQ_PREV);
		} else if (ACTION_PLAY.equals(action)) {
			PlaybackBridge.postRequest(PlaybackBridge.REQ_PLAY);
		} else if (ACTION_PAUSE.equals(action)) {
			PlaybackBridge.postRequest(PlaybackBridge.REQ_PAUSE);
		} else if (ACTION_NEXT.equals(action)) {
			PlaybackBridge.postRequest(PlaybackBridge.REQ_NEXT);
		} else if (ACTION_STOP.equals(action)) {
			PlaybackBridge.postRequest(PlaybackBridge.REQ_STOP);
		}

		// **startForegroundService から 5 秒以内に startForeground を呼ぶこと。**
		// 遅れると落とされるので、要求の処理を待たずにここで出す。
		refresh();
		return START_NOT_STICKY;
	}

	@Override
	public IBinder onBind(Intent intent) {
		return null;
	}

	/** 履歴からアプリを消された。演奏も終わるので通知も消す。 */
	@Override
	public void onTaskRemoved(Intent rootIntent) {
		stopSelf();
		super.onTaskRemoved(rootIntent);
	}

	@Override
	public void onDestroy() {
		PlaybackBridge.serviceGone(this);
		try {
			unregisterReceiver(mNoisyReceiver);
		} catch (Exception ignored) {
			// 登録前に落ちたときだけ来る。
		}
		abandonFocus();
		releaseWakeLock();
		if (mSession != null) {
			mSession.setActive(false);
			mSession.release();
			mSession = null;
		}
		stopForeground(true);
		mForeground = false;
		super.onDestroy();
	}

	/** ネイティブ（SDL のメインスレッド）から呼ばれる。UI スレッドへ渡す。 */
	void postRefresh() {
		final Handler h = mHandler;
		if (h == null) return;
		h.post(new Runnable() {
			public void run() {
				refresh();
			}
		});
	}

	// -------------------------------------------------------------------

	private void refresh() {
		final PlaybackBridge.Snapshot s = PlaybackBridge.snapshot();

		updateSession(s);

		final Notification n = buildNotification(s);
		if (!mForeground) {
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
				startForeground(NOTIFY_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
			} else {
				startForeground(NOTIFY_ID, n);
			}
			mForeground = true;
		} else {
			NotificationManager nm =
			    (NotificationManager)getSystemService(Context.NOTIFICATION_SERVICE);
			if (nm != null) nm.notify(NOTIFY_ID, n);
		}

		// 鳴っているあいだだけ CPU を起こしておく。画面が消えたあとも
		// デコードスレッドを回し続けるために要る。
		if (s.playing) {
			acquireWakeLock();
			requestFocus();
		} else {
			releaseWakeLock();
		}
	}

	private void updateSession(PlaybackBridge.Snapshot s) {
		if (mSession == null) return;

		MediaMetadata.Builder md = new MediaMetadata.Builder();
		md.putString(MediaMetadata.METADATA_KEY_TITLE, s.title);
		md.putString(MediaMetadata.METADATA_KEY_ARTIST, s.text);
		md.putLong(MediaMetadata.METADATA_KEY_DURATION, s.durMs);
		mSession.setMetadata(md.build());

		PlaybackState.Builder ps = new PlaybackState.Builder();
		ps.setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE |
		              PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_STOP |
		              PlaybackState.ACTION_SKIP_TO_NEXT |
		              PlaybackState.ACTION_SKIP_TO_PREVIOUS);
		ps.setState(s.playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED,
		            s.posMs, s.playing ? 1.0f : 0.0f);
		mSession.setPlaybackState(ps.build());
	}

	private Notification buildNotification(PlaybackBridge.Snapshot s) {
		Notification.Builder b;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			b = new Notification.Builder(this, CHANNEL_ID);
		} else {
			b = new Notification.Builder(this);
		}

		b.setContentTitle(s.title);
		b.setContentText(s.text);
		// 通知バーに出る小さいアイコンは白抜きの単色で描かれるので、
		// アプリのアイコン（色付き）ではなく OS のものを使う。
		b.setSmallIcon(s.playing ? android.R.drawable.ic_media_play
		                         : android.R.drawable.ic_media_pause);
		b.setContentIntent(openAppIntent());
		b.setVisibility(Notification.VISIBILITY_PUBLIC);
		b.setOngoing(s.playing);
		b.setShowWhen(false);

		b.addAction(action(android.R.drawable.ic_media_previous, s.prevLabel, ACTION_PREV));
		if (s.playing) {
			b.addAction(action(android.R.drawable.ic_media_pause, s.pauseLabel, ACTION_PAUSE));
		} else {
			b.addAction(action(android.R.drawable.ic_media_play, s.playLabel, ACTION_PLAY));
		}
		b.addAction(action(android.R.drawable.ic_media_next, s.nextLabel, ACTION_NEXT));
		b.addAction(action(android.R.drawable.ic_menu_close_clear_cancel, s.stopLabel,
		                   ACTION_STOP));

		if (mSession != null) {
			Notification.MediaStyle style = new Notification.MediaStyle();
			style.setMediaSession(mSession.getSessionToken());
			// 畳んだときに見えるのは 3 つまで。前の曲 / 一時停止 / 次の曲。
			style.setShowActionsInCompactView(0, 1, 2);
			b.setStyle(style);
		}
		return b.build();
	}

	private Notification.Action action(int icon, String label, String intentAction) {
		Intent i = new Intent(this, PlaybackService.class);
		i.setAction(intentAction);
		PendingIntent pi =
		    PendingIntent.getService(this, intentAction.hashCode(), i, pendingIntentFlags());
		return new Notification.Action.Builder(
		           android.graphics.drawable.Icon.createWithResource(this, icon), label, pi)
		    .build();
	}

	/** 通知をタップしたときの行き先。動いているアプリの画面を前へ出す。 */
	private PendingIntent openAppIntent() {
		Intent i = new Intent(this, MainActivity.class);
		i.setAction(Intent.ACTION_MAIN);
		i.addCategory(Intent.CATEGORY_LAUNCHER);
		i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
		return PendingIntent.getActivity(this, 0, i, pendingIntentFlags());
	}

	private int pendingIntentFlags() {
		int flags = PendingIntent.FLAG_UPDATE_CURRENT;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			// Android 12 以降は可変・不変のどちらかを必ず指定する。
			flags |= PendingIntent.FLAG_IMMUTABLE;
		}
		return flags;
	}

	private void createChannel() {
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
		NotificationManager nm =
		    (NotificationManager)getSystemService(Context.NOTIFICATION_SERVICE);
		if (nm == null) return;

		final PlaybackBridge.Snapshot s = PlaybackBridge.snapshot();
		NotificationChannel ch = new NotificationChannel(CHANNEL_ID, s.channelName,
		                                                NotificationManager.IMPORTANCE_LOW);
		ch.setDescription(s.channelDesc);
		ch.setShowBadge(false);
		ch.setSound(null, null);
		ch.enableVibration(false);
		nm.createNotificationChannel(ch);
	}

	private void requestFocus() {
		if (mHasFocus || mAudioManager == null) return;
		try {
			// AudioFocusRequest (API 26 以降) を使う版もあるが、こちらは
			// すべての版で動く。非推奨だが置き換えられてはいない。
			final int r = mAudioManager.requestAudioFocus(
			    mFocusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
			mHasFocus = (r == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
		} catch (Exception e) {
			Log.w(TAG, "requestAudioFocus failed", e);
		}
	}

	private void abandonFocus() {
		if (!mHasFocus || mAudioManager == null) return;
		mHasFocus = false;
		try {
			mAudioManager.abandonAudioFocus(mFocusListener);
		} catch (Exception e) {
			Log.w(TAG, "abandonAudioFocus failed", e);
		}
	}

	private void acquireWakeLock() {
		if (mWakeLock != null && !mWakeLock.isHeld()) mWakeLock.acquire();
	}

	private void releaseWakeLock() {
		if (mWakeLock != null && mWakeLock.isHeld()) mWakeLock.release();
	}
}
