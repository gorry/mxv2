// mxv2 - 設定の保存と復元
//
// 旧 mxv の mxv.ini にあたる。ユーザーフォルダの mxv2.ini を読み書きする。
// 旧 mxv にあった [Switch] の SampleRate / Between / PCMBuf / ROMEO は
// mxv2 では固定（48000 / SDL 任せ / 非対応）なので持たない。
//
// コマンドラインオプションは、読み込んだ設定を上書きする形で効く。

#ifndef MXV2_SETTINGS_H
#define MXV2_SETTINGS_H

#include <string>
#include <vector>

namespace mxv2 {

struct Settings {
	// [UI]
	// 画面とログの文言に使う言語（assets/locale/<名前>）。**空なら「自動」**で、
	// 動作環境の言語 (Screen::SystemLocale) に一番近い同梱ぶんを使う。
	// -locale はこれより優先されるが、ini には残さない。
	std::string locale;

	// 画面の向きでスキンを切り替えるときのふるまい
	// （screen_orientation.md）。UI では「縦画面のみ」「横画面のみ」
	// 「起動時の方向で切り替える」「常に切り替える」の 4 つに見せる。
	struct OrientationMode {
		enum Type {
			PortraitOnly = 0,
			LandscapeOnly,
			Startup,  // 起動時（と設定を閉じたとき）の向きで決めて、以後は固定
			Always,   // 向きが変わるたびに切り替える
			Count
	
		};
	};
	// ini に書く名前との変換。綴りは大文字小文字を問わない。
	static const char *OrientationModeName(int mode);
	static int OrientationModeFromName(const std::string &name, int fallback);

	// [Screen]
	// **スキンの設定は 2 系統ある**（screen_orientation.md）。
	//   skinName                      … 縦横切り替えが OFF のとき使う（従来どおり）
	//   skinPortrait / skinLandscape  … ON のとき、向きごとに使う
	//   orientationMode               … ON のときの切り替えかた
	// 機能の ON/OFF そのものは起動オプション (-orient) で決まり、保存しない。
	//
	// ini に書かれたスキンが無いときは、その系統の**既定のスキン**へ落とす
	// （プラットフォームごとに違う。settings.cpp の kDefaultSkin*）。
	// 落ちたことは ini に書き戻さない（2026-09-12、screen_orientation.md）。
	static const char *DefaultSkinName();       // OFF のとき（フォルダ名。assets: 無し）
	static const char *DefaultSkinPortrait();   // ON・縦（ref。assets: 付き）
	static const char *DefaultSkinLandscape();  // ON・横（ref。assets: 付き）
	std::string skinName;  // skin/<名前>（同梱ぶんとユーザーぶんの両方から探す）
	std::string skinPortrait;
	std::string skinLandscape;
	int orientationMode;  // OrientationMode
	// 表示倍率 (%)。100 でドット等倍。0 なら「まだ決まっていない」で、
	// 初回起動時にシステムの拡大率 (175% など) を拾って埋める。
	int zoomPercent;
	// 旧い ini の Scale=<整数倍>。0 なら無し。倍率を % へ移すための繋ぎで、
	// 読み込み時にしか使わない（保存はしない）。
	int legacyScale;
	// 拡大時の補間方法。"nearest" / "linear" / "sharp"。
	// 実際の値との変換は Screen::ScaleModeFromName / ScaleModeName。
	std::string scaleFilter;
	// フルスクリーン表示（デスクトップだけ。Screen::CanFullScreen）。
	// Alt+Enter でも設定ダイアログでも、最後の状態をここへ写して次の起動へ
	// 持ち越す。ini は [Screen] FullScreen。**-fullscreen は起動時の値を
	// 上書きするだけで、そのままでは ini に残らない**（-nofade などと同じ。
	// 書かれるのは kFieldFullScreen が立ったときだけ）。
	bool fullScreen;
	// 指で操作する端末向けに、ダイアログの押せるところを広げるか。
	// kTouchAuto なら Screen::TouchPreferred() に従う（Android は有効）。
	int touchUi;
	struct TouchUi {
		enum Type { Auto = 0, On = 1, Off = 2 };
	};
	// ホームへ戻ったときに小窓（ピクチャー・イン・ピクチャー）で出すか
	// （Android だけ。pip.h / memo/pip.md）。ini は [Screen] Pip。
	// 値は Java の PipBridge.MODE_* と同じ並び。
	int pipMode;
	struct PipMode {
		enum Type { Off = 0, Playing = 1, Always = 2, Count };
	};

	// [Filer]
	int fileListFontSize;  // 0 = 小 / 1 = 大 (旧 mxv の FontSize)
	bool folderFirst;
	// ファイラーの曲名が桁に収まらないときの横スクロール。
	int fileListScroll;
	struct FileListScroll {
		enum Type {
			None = 0,    // しない
			Cursor = 1,  // カーソル行だけ
			All = 2,     // 全て
	
		};
	};
	std::string lastDir;  // 最後に開いていたディレクトリ
	// 最後に演奏したファイル (ref)。対象を省略して起動したとき、その場所を開いて
	// このファイルにカーソルを合わせる（鳴らしはしない）。無くなっていれば
	// 記録が無いのと同じ扱いで lastDir から始める（2026-09-28、ユーザーの指示）。
	// ini は [Filer] LastFile。
	std::string lastFile;
	// lastFile を鳴らしていたか（一時停止中・止めたあとは false）。[Filer] LastPlaying。
	// resumePlayOnStart が入っていて、これが true なら次の起動で lastFile を鳴らす。
	bool lastPlaying;
	// [Behavior] ResumePlayOnStart。終了時に演奏していたファイルを次回起動時に
	// 演奏する（2026-09-28、ユーザーの指示）。対象を指定して起動したときは関係ない。
	bool resumePlayOnStart;

	// [Play]
	// 出力サンプリングレート。既定は 48000 で、x68sound が 96kHz に対応して
	// いる版（X68SOUND_SUPPORT_96KHZ）でだけ 96000 も選べる。対応していない
	// 値が書かれていても捨てはしない（別のビルドで書いた値かもしれないので、
	// そのときは Player 側が既定へ落として鳴らす）。
	int sampleRate;
	int loops;
	bool fadeout;
	// メイン画面の CONT / REPEAT ボタン。押した状態を次の起動へ持ち越す。
	bool autoNext;    // CONT   … 演奏が終わったら次の曲へ
	bool autoRepeat;  // REPEAT … 演奏が終わったら同じ曲をもう一度
	// マスター音量 -100..+100 (0 = 中央)。メイン画面の音量はその場かぎりの
	// 調整なので記録しない（起動時は必ず 0 から始まる）。
	int masterVolume;
	// 画面を音に合わせて遅らせる量。latencyAuto なら latencyMs は見ず、
	// オーディオ装置のバッファ長をそのまま使う（既定）。
	// 手動のときは ms で、正の値で表示が遅れ、負で先行する。
	bool latencyAuto;
	int latencyMs;
	// 手動指定できる幅。設定ウィンドウのスライダもこの範囲。
	static const int kLatencyMsMin = -200;
	// 上限は、[画面の遅れ] ダイアログの「秒」(0〜9) と「ミリ秒」(0〜999) の
	// 2 本のスライダーで作れる最大の 9999ms（2026-09-28。カーオーディオに 5 秒
	// 近い遅れがあった。AV アンプ TX-NR676E も電源を入れるたびに 1000ms 以上
	// 変わった）。
	static const int kLatencyMsMax = 9999;
	// 自動のとき、出力先が Bluetooth なら足す遅れ (ms)。**機器ごとに覚える**
	// （2026-09-28、ユーザーの指示）。測った遅れには受け側の中の遅れ（AV アンプの
	// 処理など）が入らず、Windows は Bluetooth の遅れそのものも入らない
	// （outputlatency.h）ので、その差を機器ごとに手で足す。
	// [Bluetooth] DefaultLatency … 初めてつないだ機器の値。Windows は BT525 FM で
	//   目で合わせた 80ms、Android は測れるので 0ms（memo/bluetooth.md）。
	//   Windows は以前の [Bluetooth] Latency・[Play] BluetoothLatency を引き継ぐ。
	// [Bluetooth] DeviceCount / Device<n> / Device<n>Latency … 機器ごとの値。
	//   名前は outputlatency::BluetoothName()。
	// Bluetooth の設定はどれも機器ごとに持つ（2026-09-28、ユーザーの指示。設定
	// ウィンドウの [Bluetooth（機器名）] は、つないでいる機器のものだけを出す）。
	// Device<n>Swap / Device<n>Resume も同じ並び。
	struct BtDevice {
		std::string name;
		int latencyMs;
		// 車などへ渡す曲の情報で、アルバム欄（ふだんはフォルダ名）とアーティスト欄
		// （ふだんはファイル名）を入れ替える。Android だけ。
		bool swapArtistAlbum;
		// Bluetooth が切れて一時停止したあと、この機器がつながり直したら一時停止を
		// 解く（車のエンジンを切って掛け直したとき）。Android だけ。
		bool resumeOnReconnect;
	};
	int btDefaultLatencyMs;
	std::vector<BtDevice> btDevices;
	// 受け側の中の遅れが 5 秒近い機器（カーオーディオ）があるので、[遅延時間]
	// ダイアログの 2 本のスライダー（秒 0〜9・ミリ秒 0〜999）で作れる 9999ms。
	// 電源を入れるたびに変わる機器は、利用者がそのつど合わせ直す（ユーザー判断）。
	static const int kBluetoothLatencyMsMax = 9999;
	static const int kMaxBtDevices = 32;
	// 初めてつないだ機器の swapArtistAlbum / resumeOnReconnect。設定ウィンドウには
	// 出さない。[Bluetooth] DefaultSwapArtistAlbum / DefaultResumeOnReconnect
	// （機器ごとにする前の SwapArtistAlbum / ResumeOnReconnect を引き継ぐ）。
	bool btDefaultSwapArtistAlbum;
	bool btDefaultResumeOnReconnect;
	// 機器の設定。覚えていなければ既定の値で埋めたもの。
	BtDevice BluetoothDeviceFor(const std::string &name) const;
	// 機器の遅延時間。覚えていなければ btDefaultLatencyMs。
	int BluetoothLatencyFor(const std::string &name) const;
	// 覚える。数が kMaxBtDevices を超えたら古いもの（先頭）から捨てる。
	void SetBluetoothDevice(const BtDevice &device);

	// [Path]
	// PDX の探索先。MDX と同じフォルダで見つからなかったときに、この並び順で
	// 探す（ref。ファイルシステムをまたいでよい）。ini は [Path] PdxCount と
	// Pdx<n>。以前の [Path] PDX（1 本だけ）は読み込み時に 1 件目として
	// 取り込み、次の保存で新しい形へ書き換える。2026-09-16、ユーザーの指示
	// （それまでは 1 本だけで、複数は -pdxpath で足していた）。
	std::vector<std::string> pdxPaths;
	static const int kMaxPdxPaths = 64;

	// [FileSystem]
	// ファイラーのルート（ファイルシステムの選択）に並べる順。中身は
	// "assets:" のような ref。書き出す本数の上限は kMaxFileSystems。
	std::vector<std::string> fileSystems;
	static const int kMaxFileSystems = 64;

	// [Bookmark]
	// よく開く場所の控え。中身はフォルダの ref で、並び順がそのまま
	// ダイアログとファイラーの "Bookmarks>" の並び。初回起動時（ini に
	// [Bookmark] Count が無いとき）は "assets:" が 1 つ入る（消してよい）。
	std::vector<std::string> bookmarks;
	static const int kMaxBookmarks = 64;
	// ini に [Bookmark] が無く、上の初期値を使ったか（書き戻す合図。
	// ini には書かない）。
	bool bookmarksDefaulted;

	// [Position] 復元用。**ウィンドウの位置はいつでも覚える**——2026-09-18 まであった
	// [Position] Save（記憶するかどうかの切り替え）は、UI が無く ini を手で
	// 書くしかなかったうえ、0 になっていると理由も分からず位置が戻らないので、
	// ユーザーの指示で廃止した（ini に残っている Save= は次の保存で消える）。
	// 0 未満なら「まだ覚えていない」で、その回は OS 任せの位置に出る。
	//
	// **覚えるのは位置だけで、大きさは覚えない**（2026-09-18、ユーザーの判断）。
	// 起動時のウィンドウは必ず「スキンの宣言サイズ x 表示倍率」にスナップする。
	// windowW/H は保存経路（SaveFields の kFieldWindowPos）が写さないので
	// ふつうは 0 のままで、ini に手で書いたときだけ「その大きさ以上で開く」
	// 指定として効く（main.cpp）。
	int windowX, windowY;
	int windowW, windowH;
	// 最小化したまま終えたか（[Position] Iconic）。立っていれば次の起動も
	// 最小化で始める。旧 mxv にあった作法で、2026-09-18 に移植した。
	bool windowIconic;
	// 最大化したまま終えたか（[Position] Maximized）。同じく次の起動へ
	// 持ち越す（2026-09-18、ユーザーの指示。最小化を覚えるなら最大化も、
	// という揃え）。**最小化中は更新しない**——最小化するとアプリケーション
	// ウィンドウ（SDL_Window）の最大化のフラグが落ちるので、そのまま写すと
	// 「最大化したまま最小化して終了」で最大化を忘れてしまう。
	bool windowMaximized;

	// [Network] 更新チェック（updatecheck.h）。UpdateCheck が立っていれば
	// 1 日 1 回、GitHub の最新リリースと今の版を比べる（既定は ON）。
	// NextUpdateCheck は次に確かめてよい時刻（UNIX 時間の秒。0 なら「まだ
	// 一度も確かめていない」で、使える状態になりしだい確かめる）。
	// 確かめ始めるたびに 24 時間後へ送る（成功・失敗を問わない）。
	// 2026-09-24、ユーザーの指示。
	bool updateCheck;
	long long nextUpdateCheck;

	// [Cast] Quality。送る品質の段（cast::Quality。0 最低 … 4 最高）。既定は
	// Android が 1（低: 854x480・30fps）、パソコンが 2（中: 1280x720・30fps）。
	int castQuality;
#ifdef __ANDROID__
	static const int kCastQualityDefault = 1;
#else
	static const int kCastQualityDefault = 2;
#endif
	static const int kCastQualityMax = 4;

	// [Cast] MuteLocal。Chromecast へ送っている間、手元の音を消す（既定は ON。
	// TV と数秒ずれて二重に聞こえるため。cast.h / memo/cast.md）。
	bool castMuteLocal;
	// [Cast] VideoAdvanceMs。送る映像の時刻をこれだけ早める（0..kCastAdvanceMsMax）。
	// 鍵盤は「次に送る絵」で初めて出るので、音より少し遅れて見える（Android で
	// 60f 換算 3f ほど）。見ながら合わせるための補正。
	int castVideoAdvanceMs;
	static const int kCastAdvanceMsMax = 200;
	// 既定値。ユーザーが TV（REGZA の Cast 受信部）で見て合わせた値
	// （2026-09-25）: パソコン 16ms、Android（Pixel 7a）48ms。Android は音声の
	// コールバックが粗い（2048 フレーム）ぶん大きい。
#ifdef __ANDROID__
	static const int kCastAdvanceMsDefault = 48;
#else
	static const int kCastAdvanceMsDefault = 16;
#endif

	// [Tutorial] Done。初回起動のチュートリアルを見終えた（またはスキップ
	// した）。無ければ次の起動で出す（tutorial.md）。
	bool tutorialDone;

	// 保存する項目。「変わった項目だけを書き戻す」ために使う。
	// 設定ウィンドウには保存ボタンが無く、触った時点で保存する作りなので、
	// 何を触ったかをこのビットで積んでいく。
	struct Field {
		enum Type {
			Skin = 1 << 0,
			Zoom = 1 << 1,
			Filter = 1 << 2,
			FontSize = 1 << 3,
			FolderFirst = 1 << 4,
			LastDir = 1 << 5,
			LastFile = 1 << 28,        // [Filer] LastFile / LastPlaying
			ResumePlay = 1 << 29,      // [Behavior] ResumePlayOnStart
			Loops = 1 << 6,
			Fadeout = 1 << 7,
			Volume = 1 << 8,
			PdxPaths = 1 << 9,
			WindowPos = 1 << 10,
			Latency = 1 << 11,
			FileSystems = 1 << 12,
			SampleRate = 1 << 13,
			Bookmarks = 1 << 14,
			TouchUi = 1 << 15,
			FileListScroll = 1 << 16,
			ContRepeat = 1 << 17,
			Locale = 1 << 18,
			// 縦横切り替えが ON のときのスキン 2 つと、切り替えかた。
			OrientSkin = 1 << 19,
			OrientMode = 1 << 20,
			Tutorial = 1 << 21,
			FullScreen = 1 << 22,
			UpdateCheck = 1 << 23,     // [Network] UpdateCheck
			UpdateSchedule = 1 << 24,  // [Network] NextUpdateCheck
			Pip = 1 << 25,             // [Screen] Pip
			Cast = 1 << 26,            // [Cast] Quality / MuteLocal / VideoAdvanceMs
			Bluetooth = 1 << 27,       // [Bluetooth] すべて（機器ごとの値と既定の値）
	
		};
	};

	Settings();

	// dir に置く mxv2.ini のパス。dir はふつうユーザーフォルダ
	// （UserDataDir()。実行ファイルの隣は書けないことがある）。
	static std::string PathIn(const std::string &dir);

	bool Load(const std::string &path);
	bool Save(const std::string &path) const;

	// ファイルを読み直してから fields の項目だけを差し替えて書く。
	// こうしないと -nofade のようなコマンドラインの一時指定まで
	// residue として ini に焼き付いてしまう。
	bool SaveFields(const std::string &path, unsigned fields) const;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGS_H
