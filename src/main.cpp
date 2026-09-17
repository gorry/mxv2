// mxv2 - エントリポイント
//
// コマンドライン (cmdline.cpp)・起動時の組み立て (appsetup.cpp)・曲の進行
// (playctl.cpp)・キーとマウスの割り当て (keybind.cpp) はここから切り出してある。
// ここに残るのは起動の手順とメインループ。
//
// 使い方:
//   mxv2 [オプション] [<mdxfile> | <ディレクトリ>]
// 引数を省略するとカレントディレクトリのファイラーだけを開く。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#ifdef __ANDROID__
#include "androidassets.h"
#endif

#include "assetpath.h"
#include "drawscreen.h"
#include "fileutil.h"
#include "filer.h"
#include "message.h"
#include "gamepad.h"
#include "mouse.h"
#include "nowplaying.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "tutorial.h"
#include "settingsui.h"
#include "skin.h"
#include "songloader.h"
#include "textlayer.h"
#include "vfs.h"
#include "visualizer.h"

#include "appsetup.h"
#include "cmdline.h"
#include "keybind.h"
#include "playctl.h"

using namespace mxv2::app;

namespace {

// 設定を書き戻すまでの待ち時間 (ms)。音量のドラッグやウィンドウ移動は毎フレーム
// 値が変わるので、手が止まってからまとめて 1 回書く。
const uint32_t kSettingsSaveDelayMs = 400;

// バックグラウンド（描かないとき）に回る間隔 (ms)。演奏そのものはオーディオ
// 装置とデコードスレッドが進めるので、ここでやるのは曲送りと通知の更新だけ。
// 曲の終わりに気付くのがこの間隔ぶん遅れうるので、あまり長くはしない。
const int kBackgroundTickMs = 100;

// キー・マウス・メニューのどこからでも変わる項目（文字の大きさ・音量・
// 今の場所・CONT / REPEAT・窓の位置）を設定と見比べて、変わっていれば
// 設定に写す。返すのは変わった項目のビット和。フレームごと（保存の待ち
// 時間へ入れる）と終了時（残りを流す）の 2 か所から同じ手順を通す。
unsigned CollectDirtyFields(mxv2::Settings *settings, mxv2::DrawScreen *draw,
                            mxv2::Player *player, mxv2::Filer *filer, mxv2::Screen *screen,
                            bool autoNext, bool autoRepeat) {
	unsigned dirt = 0;
	if (settings->fileListFontSize != draw->fileListFontSize()) {
		settings->fileListFontSize = draw->fileListFontSize();
		dirt |= mxv2::Settings::kFieldFontSize;
	}
	if (settings->masterVolume != player->masterVolume()) {
		settings->masterVolume = player->masterVolume();
		dirt |= mxv2::Settings::kFieldVolume;
	}
	if (settings->lastDir != filer->currentRef()) {
		settings->lastDir = filer->currentRef();
		dirt |= mxv2::Settings::kFieldLastDir;
	}
	if (settings->autoNext != autoNext || settings->autoRepeat != autoRepeat) {
		settings->autoNext = autoNext;
		settings->autoRepeat = autoRepeat;
		dirt |= mxv2::Settings::kFieldContRepeat;
	}
	// フルスクリーンは [Screen] FullScreen に覚える。Alt+Enter でも設定
	// ダイアログでも、最後の状態がそのまま次の起動になる。
	if (mxv2::Screen::CanFullScreen() && settings->fullScreen != screen->fullScreen()) {
		settings->fullScreen = screen->fullScreen();
		dirt |= mxv2::Settings::kFieldFullScreen;
	}
	// 最小化したまま終えたかを覚える（旧 mxv の [Position] Iconic）。
	if (settings->windowIconic != screen->minimized()) {
		settings->windowIconic = screen->minimized();
		dirt |= mxv2::Settings::kFieldWindowPos;
	}
	// 最大化したまま終えたかを覚える。**最小化中とフルスクリーン中は見ない**
	// ——どちらも SDL の最大化の旗が落ちるので、そのまま写すと
	// 「最大化したまま最小化して終了」で最大化を忘れてしまう。
	if (!screen->minimized() && !screen->fullScreen() &&
		settings->windowMaximized != screen->maximized()) {
		settings->windowMaximized = screen->maximized();
		dirt |= mxv2::Settings::kFieldWindowPos;
	}
	// 窓の位置と大きさは**普通の窓のときだけ**見る。フルスクリーン・最大化は
	// 画面いっぱいの形を、最小化は (-32000,-32000) を覚えてしまうし、
	// 最大化をやめたときに戻る場所は「その前の普通の窓」であってほしい。
	if (!screen->fullScreen() && !screen->minimized() && !screen->maximized()) {
		int wx = 0, wy = 0, ww = 0, wh = 0;
		screen->GetWindowRect(&wx, &wy, &ww, &wh);
		if (wx != settings->windowX || wy != settings->windowY || ww != settings->windowW ||
		    wh != settings->windowH) {
			settings->windowX = wx;
			settings->windowY = wy;
			settings->windowW = ww;
			settings->windowH = wh;
			dirt |= mxv2::Settings::kFieldWindowPos;
		}
	}
	return dirt;
}

// ---- 画面の向きでスキンを切り替える（screen_orientation.md） --------------

}  // namespace

int main(int argc, char **argv) {
	// ログの行き先を先に決める。既定ではコンソールを出さない。
	SetupConsole(WantsConsole(argc, argv));

	// 途中で終了させても情報が残るように行バッファにする
	setvbuf(stdout, NULL, _IOLBF, 1024);

	// 素材の置き場所。同梱ぶん (assets) は読むだけで、書くのはユーザー
	// フォルダ側。詳しくは assetpath.h。
	Options opt;
	PrescanDirs(argc, argv, &opt);

	// ウィンドウが開く前に出た警告は、ここにためて最初のフレームで見せる。
	Warnings warnings;

	mxv2::AssetPaths paths;
	paths.bundledDir = opt.assetsDir.empty()
	                       ? mxv2::JoinPath(mxv2::ExecutableDir(), "assets")
	                       : opt.assetsDir;
	paths.userDir = opt.userDir.empty() ? mxv2::UserDataDir(kUserDirName) : opt.userDir;

#ifdef __ANDROID__
	// apk の assets は fopen で開けないので、まず内部ストレージへ展開して
	// 「実行ファイルの隣」と同じ姿にする。ここから先は Windows と同じ道を通る。
	// カタログを読む前なので、ここで出る警告だけは英語のまま。
	{
		std::vector<std::string> extractWarnings;
		mxv2::ExtractBundledAssets(mxv2::ExecutableDir(), &extractWarnings);
		for (size_t i = 0; i < extractWarnings.size(); i++) Warn(&warnings, extractWarnings[i]);
	}
	// 曲の置き場所 assets/mdx は空でも作る（Windows では CMake が作っている）。
	mxv2::MakeDirectories(mxv2::JoinPath(paths.bundledDir, "mdx"));
#endif

	// ユーザーフォルダは設定を読むより先に要る（引き継ぎがここへ書く）。
	// **知らせるのはカタログを読んでから**なので、結果だけ覚えておく。
	const bool userDirFailed = !mxv2::MakeDirectories(paths.userDir);

	// 設定 -> コマンドラインの順に読む（後勝ち）。**カタログより先に読む**のは
	// 言語の指定が ini にあるため（Settings::Load と引き継ぎは文言を出さない
	// ので、カタログの前でも困らない）。
	mxv2::Settings settings;
	const std::string settingsPath = mxv2::Settings::PathIn(paths.userDir);
	MigrateLegacySettings(settingsPath);
	settings.Load(settingsPath);
	// ini に倍率が無かったかどうかを覚えておく（終了時に書き残すため）。
	const int legacyScale = settings.legacyScale;

	// 文言はここから先すべてカタログ (assets/locale/<ロケール>/message.ini)
	// から引く。知らないロケールを渡されたときは英語で代用する。
	// 1 つも読めなければキー名が出る（動きはする）。
	//
	// 言語の決め方は **-locale -> ini の [UI] Locale -> 動作環境の言語**。
	// 設定ウィンドウの [言語] で選び直すと、そこから先は SettingsUi が
	// 読み替える（起動し直さなくてよい）。
	{
		std::string want = opt.locale;
		if (want.empty()) want = settings.locale;
		if (want.empty()) {
			std::vector<mxv2::LocaleInfo> avail;
			mxv2::ListLocales(paths, &avail);
			want = mxv2::MatchLocale(avail, mxv2::Screen::SystemLocale());
		}
		bool usedFallback = false;
		if (!mxv2::LoadMessages(paths, want, &usedFallback)) {
			// カタログが無いので、この 1 本だけは英語のまま出す。
			char buf[512];
			snprintf(buf, sizeof(buf), "message catalog not found: %s (locale %s)",
			         paths.bundledDir.c_str(), mxv2::MessageLocale().c_str());
			Warn(&warnings, buf);
		} else if (usedFallback) {
			Warn(&warnings, mxv2::MsgF("Log.LocaleFallback", mxv2::MessageLocale(),
			                           mxv2::kFallbackLocale));
		}
	}
	if (userDirFailed) {
		Warn(&warnings, mxv2::MsgF("Log.UserDirFailed", paths.userDir));
	}

	if (!ParseArgs(argc, argv, &opt, &settings)) {
		PrintUsage(argc > 0 ? argv[0] : "mxv2");
		return EXIT_FAILURE;
	}

	// 使い方を出すだけのときに邪魔をしないよう、ここまで来てから出す。
	printf("assets   : %s\n", paths.bundledDir.c_str());
	printf("userdir  : %s\n", paths.userDir.c_str());
	// どの言語で動いているか。「自動」のときに何が選ばれたのかが分かる。
	printf("locale   : %s\n", mxv2::MessageLocale().c_str());

	// ファイルシステム。同梱アセット・ユーザーフォルダ・ローカルの 3 つを
	// 用意する。場所の指定はここから先すべて ref（vfs.h）。
	mxv2::Vfs vfs;
	vfs.Configure(paths.bundledDir, paths.userDir);
	// ファイラーの "Bookmarks>" に並ぶのは Settings::bookmarks そのもの。
	vfs.SetBookmarks(&settings.bookmarks);
	// ファイラーのルートに並べる順は ini から。読めなかったぶんや足りない
	// ぶんは LoadFileSystems が補うので、そのときは書き戻す。
	bool dirtyFileSystems = LoadFileSystems(&vfs, settings.fileSystems, &warnings);
	settings.fileSystems = SaveFileSystems(vfs);
	// ブックマークも同じく、読めない指定を捨てたら書き戻す。
	// 初回起動（ini に [Bookmark] が無い）の初期値も、ここで定着させる。
	const bool dirtyBookmarks =
	    LoadBookmarks(vfs, &settings.bookmarks, &warnings) || settings.bookmarksDefaulted;
	// ユーザーフォルダ側の mdx/ は無ければ作る（曲の置き場所として見せる）。
	{
		const mxv2::FileSystem *userFs = vfs.FindById("userdir");
		if (userFs != 0 && !userFs->nativeRoot().empty() &&
		    !mxv2::MakeDirectories(userFs->nativeRoot())) {
			Warn(&warnings, mxv2::MsgF("Log.MakeDirFailed", userFs->nativeRoot()));
		}
	}

	// 対象がファイルならその曲を、ディレクトリならそこを開く。
	// 対象を省略したときは、前回開いていたディレクトリへ戻る。
	std::string startDir;   // ref
	std::string startFile;  // ref
	// 行き先が決まったか。空の ref（ファイルシステムの選択）も決まったうち。
	bool startFound = false;
	if (!opt.target.empty()) {
		std::string ref;
		if (!vfs.Resolve(opt.target, std::string(), &ref) || ref.empty()) {
			printf("ERROR: %s\n", mxv2::MsgF("Error.BadLocation", opt.target).c_str());
			return EXIT_FAILURE;
		}
		if (vfs.IsDir(ref)) {
			startDir = ref;
		} else {
			startFile = ref;
			startDir = vfs.Parent(ref);
		}
	} else if (!settings.lastDir.empty()) {
		// 前回開いていた場所。読めない指定（未知の接頭辞など）は未記録と
		// 同じ扱い。行けなければ 1 つずつ親へ遡り、最後は選択画面へ抜ける。
		std::string ref;
		if (vfs.Resolve(settings.lastDir, std::string(), &ref)) {
			if (!vfs.ParentIsCheap(ref)) {
				// 外部ファイルシステムは 1 段ごとに通信が要るので、途中は
				// 飛ばしてルートまで戻す (filesystem.md)。
				if (!vfs.IsDir(ref)) ref = vfs.RootRef(ref);
				if (!ref.empty() && !vfs.IsDir(ref)) ref.clear();
			} else {
				for (int i = 0; i < 64 && !ref.empty(); i++) {
					if (vfs.IsDir(ref)) break;
					ref = vfs.Parent(ref);
				}
			}
			startDir = ref;
			startFound = true;
		}
	}
	// 初回起動（前回の場所が記録されていない）は同梱アセットから始める。
	if (!startFound && startDir.empty() && startFile.empty()) {
		startDir = vfs.RootRef("assets:");
	}

	// DPI 対応にしてから SDL を初期化する。
	// これを入れないと、高 DPI 環境では Windows がウィンドウの出力を
	// そのまま引き伸ばす (DPI 仮想化)。SDL からは 640x480 にしか見えないので、
	// 文字を出力解像度で描くことができず、拡大もぼやける。
	//
	// DPI_SCALING ではなく DPI_AWARENESS を立てるのが肝心。
	// DPI_SCALING=1 だとウィンドウの大きさが「ポイント」指定になり、
	// システムの拡大率が暗黙に掛かってしまう。mxv2 は拡大率を % で
	// 自分で持つので、ウィンドウの大きさは実ピクセルで指定したい。
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

#ifdef __ANDROID__
	// バックグラウンドへ回っても演奏を続ける。SDL は既定でアプリが止まると
	// **イベントループごと止め (BLOCK_ON_PAUSE)、オーディオ装置も止める**ので、
	// 両方切って回り続けさせる。描かないようにするのはメインループの仕事
	// （止まっているあいだは GL の面が無く、描くと落ちる）。
	SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
	SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0");
#endif

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		printf("ERROR: %s\n", mxv2::MsgF("Error.SdlInit", SDL_GetError()).c_str());
		return EXIT_FAILURE;
	}

	// オーディオだけ分けて開く。**Android では AAudio を先に試す。**
	// SDL が既定で選ぶ OpenSL ES は低遅延の "fast track" になり、装置側の
	// バッファが 5ms ほどしか無い（AudioFlinger の FrmCnt=256）。
	// アプリが前面にいないと少しの割り込み待ちでも間に合わず、音が途切れる。
	// AAudio は既定が PERFORMANCE_MODE_NONE（deep buffer）なので、同じ
	// 状況でも途切れにくい。**AAudio は Android 8 以降**なので、開けなければ
	// 指定を外して開き直す。
	{
		bool audioOk = false;
#ifdef __ANDROID__
		SDL_SetHint(SDL_HINT_AUDIODRIVER, "aaudio");
		audioOk = (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
		if (!audioOk) {
			printf("warning  : AAudio is not available (%s)\n", SDL_GetError());
			fflush(stdout);
			SDL_SetHint(SDL_HINT_AUDIODRIVER, "");
		}
#endif
		if (!audioOk && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
			printf("ERROR: %s\n", mxv2::MsgF("Error.SdlInit", SDL_GetError()).c_str());
			SDL_Quit();
			return EXIT_FAILURE;
		}
	}

	// ドラッグ＆ドロップ。多くの環境では既定で有効だが、環境によっては
	// 明示しないと届かないので立てておく。
	SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);

	// 表示倍率が決まっていなければ、システムの拡大率 (175% など) を初期値にする。
	// 旧い ini の Scale=<整数倍> は「システム拡大率の n 倍」だったので、
	// 見た目が変わらないように % へ読み替える。
	// 変更のあった項目だけを ini へ書き戻すためのビット。設定ウィンドウには
	// 保存ボタンが無く、触った時点で保存する。
	unsigned dirtyFields = 0;
	uint32_t saveAtMs = 0;

	// ini のファイルシステム一覧を直したときは書き戻す（仕様どおり、
	// 読み込みを終えてから 1 回だけ）。
	if (dirtyFileSystems) dirtyFields |= mxv2::Settings::kFieldFileSystems;
	if (dirtyBookmarks) dirtyFields |= mxv2::Settings::kFieldBookmarks;

	int defaultZoom = 100;
	{
		const int systemZoom = mxv2::Screen::SystemZoomPercent();
		// ini に倍率が無ければ、ここで決めた値を書き残す（初回起動で定着させる）。
		if (settings.zoomPercent <= 0) dirtyFields |= mxv2::Settings::kFieldZoom;
		defaultZoom = (legacyScale > 0) ? legacyScale * systemZoom : systemZoom;
		if (defaultZoom < mxv2::Screen::kZoomMin) defaultZoom = mxv2::Screen::kZoomMin;
		if (defaultZoom > mxv2::Screen::kZoomMax) defaultZoom = mxv2::Screen::kZoomMax;

		if (settings.zoomPercent <= 0) settings.zoomPercent = defaultZoom;
		if (settings.zoomPercent < mxv2::Screen::kZoomMin) {
			settings.zoomPercent = mxv2::Screen::kZoomMin;
		}
		if (settings.zoomPercent > mxv2::Screen::kZoomMax) {
			settings.zoomPercent = mxv2::Screen::kZoomMax;
		}
		printf("display  : %s\n",
		       mxv2::MsgF("Log.Display", mxv2::MsgNum("%d", systemZoom),
		                  mxv2::MsgNum("%d", settings.zoomPercent))
		           .c_str());
	}

	// 既定のスキン（DefaultSkinFor）も読めないときの最後の逃げ場。
	// 同梱ぶんは必ずあるはずなので名指しする。
	const std::string kFallbackSkin = mxv2::MakeBundledSkinRef("Default");

	// **ウィンドウを開くのはスキンを読んだ後**（スキンが画面サイズを決める）
	// だが、起動時の向きを見るのに Screen が要るので、器だけ先に作っておく。
	mxv2::Screen screen;

	// ---- 画面の向きでスキンを切り替える（screen_orientation.md） ----------
	//
	// 機能そのものの ON/OFF は起動オプションで決まり、**ini には残さない**。
	// 既定は Android が ON、それ以外が OFF。-skin で名指しされたときは、
	// そのスキンを勝手に差し替えないよう OFF にする。
	bool orientEnabled = false;
#ifdef __ANDROID__
	orientEnabled = true;
#endif
	if (opt.orient >= 0) orientEnabled = (opt.orient != 0);
	if (opt.skinSet) orientEnabled = false;
	screen.SetOrientationLock(opt.orientLock);

	// 起動時の向き。**ウィンドウを作る前**なので画面のほうを見る
	// （SDL_Init(SDL_INIT_VIDEO) は済んでいる）。「縦画面のみ」「横画面のみ」の
	// ときは見るまでもなく決まっているので、OrientationForMode が捨てる。
	mxv2::Screen::Orientation orientNow = mxv2::Screen::kLandscape;
	if (orientEnabled) {
		orientNow = OrientationForMode(settings.orientationMode, screen.displayOrientation());
	}
	// いま使うべきスキン。機能 OFF なら今までどおり [Screen] Skin。
	const std::string wantSkin =
	    !orientEnabled ? settings.skinName
	                   : ((orientNow == mxv2::Screen::kPortrait) ? settings.skinPortrait
	                                                            : settings.skinLandscape);

	// スキンが画面サイズを決めるので、ウィンドウより先に読む。
	// 指定のスキンが無ければ、警告を出してその系統の既定のスキンへ落ちる
	// （ini に書かれたスキンのフォルダをユーザーが消しても起動できるように）。
	// **落ちたことは ini に書き戻さない**（DefaultSkinFor のコメント）。
	// loadedSkin は実際に読めた ref。設定の値とは別に持つ。
	mxv2::Skin skin;
	std::string loadedSkin = wantSkin;
	{
		std::string err;
		if (!skin.Load(paths, wantSkin, &err)) {
			Warn(&warnings, err);
			loadedSkin = DefaultSkinFor(orientEnabled, orientNow);
			if (loadedSkin == wantSkin || !skin.Load(paths, loadedSkin, &err)) {
				if (loadedSkin != wantSkin) Warn(&warnings, err);
				loadedSkin = kFallbackSkin;
				if (!skin.Load(paths, kFallbackSkin, &err)) {
					printf("ERROR: %s\n", err.c_str());
					printf("       %s\n", mxv2::Msg("Error.HintAssets"));
					SDL_Quit();
					return EXIT_FAILURE;
				}
			}
			Warn(&warnings, mxv2::MsgF("Log.SkinFallback", loadedSkin));
		}
	}

	{
		std::string err;
		if (!screen.Open("mxv2", skin.screenW, skin.screenH, settings.zoomPercent, &err)) {
			printf("ERROR: %s\n", err.c_str());
			SDL_Quit();
			return EXIT_FAILURE;
		}
		// 前回の位置に戻す。0 未満は「まだ覚えていない」（初回起動）。
		// モニタ構成が変わっていても画面の外へ出ないよう、寄せてから置く。
		if (settings.windowX >= 0 && settings.windowY >= 0) {
			screen.SetWindowPosClamped(settings.windowX, settings.windowY);
		}
		// 前回の大きさに戻す。**縮める方向には戻さない**（表示倍率が
		// 100% を割るとキャンバスが潰れるので、宣言サイズ x 表示倍率を
		// 下限にする）。伸びたぶんはファイラーの行数として戻ってくる。
		// **窓の大きさを決められるプラットフォームだけ**（Android では窓＝画面で、
		// SDL_SetWindowSize を呼ぶと SDL 側の記録だけがずれる。Screen の
		// CanResizeWindow のコメント）。
		if (mxv2::Screen::CanResizeWindow() && settings.windowW > 0 &&
		    settings.windowH > 0) {
			const int minW = skin.screenW * settings.zoomPercent / 100;
			const int minH = skin.screenH * settings.zoomPercent / 100;
			const int w = (settings.windowW > minW) ? settings.windowW : minW;
			const int h = (settings.windowH > minH) ? settings.windowH : minH;
			SDL_SetWindowSize(screen.window(), w, h);
		}
		// 前回最大化で終えていたら、最大化で始める（[Position] Maximized）。
		// 位置と大きさを決めたあとに掛けるので、元に戻すと覚えていた窓になる。
		if (settings.windowMaximized) screen.Maximize();
		// 前回フルスクリーンで終えていたら、その状態で始める（-fullscreen も
		// ここを通る）。**窓の大きさを戻したあとに掛ける**ことで、SDL が
		// 「フルスクリーンをやめたときの大きさ」として前回の窓を覚える。
		if (settings.fullScreen && mxv2::Screen::CanFullScreen()) {
			screen.SetFullScreen(true);
		}
		// 前回最小化で終えていたら、最小化で始める（旧 mxv の
		// [Position] Iconic）。**位置と大きさを決めたあとに掛ける**ことで、
		// 元に戻したときに覚えていた窓になる。
		if (settings.windowIconic) screen.Minimize();

		// ここまでの見た目を決めてから窓を出す。**Open() は隠して作る**ので、
		// これを呼ばないと画面に何も出ない（screen.h の Open のコメント）。
		screen.Show();
		screen.SetScaleMode(
		    mxv2::Screen::ScaleModeFromName(settings.scaleFilter, mxv2::Screen::kScaleSharp));
		// 綴りが違っていたら解決後の名前で書き直す。
		const std::string resolved = mxv2::Screen::ScaleModeName(screen.scaleMode());
		if (settings.scaleFilter != resolved) dirtyFields |= mxv2::Settings::kFieldFilter;
		settings.scaleFilter = resolved;
	}

	// 端末の向きを、切り替えかたに合わせて固定する（Android だけ）。
	// ここまで来ればウィンドウはできているので、Activity へ要求を出せる。
	if (orientEnabled) ApplyOrientationMode(settings.orientationMode, orientNow);

	// ファイラーと曲名の文字は、キャンバスとは別に出力解像度で描いて重ねる。
	mxv2::TextLayer textLayer;
	{
		std::string err;
		if (!textLayer.Init(&screen, mxv2::FontSearchDirs(skin, paths), &err)) {
			Warn(&warnings, err);
		} else if (!textLayer.available()) {
			Warn(&warnings, mxv2::Msg("Log.FontMissing"));
		}
	}

	mxv2::DrawScreen draw;
	{
		std::string err;
		draw.SetTextLayer(&textLayer);
		if (!draw.Init(&skin, &err)) {
			// 素材の足りないスキンでも起動できなくならないよう、既定の
			// スキンへ逃がす（layout.ini を書かずに theme.mxv だけ置いた
			// ユーザースキンなど）。それも駄目なら同梱の Default。
			// ini には書き戻さない（DefaultSkinFor のコメント）。
			Warn(&warnings, mxv2::MsgF("Log.SkinUnusable", loadedSkin, err));
			const std::string candidates[2] = {DefaultSkinFor(orientEnabled, orientNow),
			                                   kFallbackSkin};
			bool ok = false;
			for (int i = 0; i < 2 && !ok; i++) {
				if (candidates[i] == loadedSkin) continue;
				if (!skin.Load(paths, candidates[i], &err)) continue;
				loadedSkin = candidates[i];
				screen.Resize(skin.screenW, skin.screenH, &err);
				textLayer.SetFontDirs(mxv2::FontSearchDirs(skin, paths));
				textLayer.Rebuild(&screen, &err);
				ok = draw.Init(&skin, &err);
				if (ok) Warn(&warnings, mxv2::MsgF("Log.SkinFallback", loadedSkin));
			}
			if (!ok) {
				printf("ERROR: %s\n", err.c_str());
				printf("       %s\n", mxv2::Msg("Error.HintAssetsSkin"));
				screen.Close();
				SDL_Quit();
				return EXIT_FAILURE;
			}
		}
		draw.SetFileListFontSize(settings.fileListFontSize);
		draw.SetFileListScroll(settings.fileListScroll);
		// 曲を読むまでの曲名欄。原典 mxv の IDS_HELLO と同じ文言で、
		// 最初の曲が載ったら置き換わる（スキンを替えても Reload が引き継ぐ）。
		draw.PutMDXTitle(mxv2::Msg("Player.Hello"));
	}

	mxv2::Player player;
	// 出力レートを変えるときに開き直すので、Config はループの外に置く。
	mxv2::Player::Config cfg;
	{
		// 出力レートは設定 (ini) 由来。-rate はその場かぎりの上書きで、
		// ini には残さない（-nofade などと同じ扱い）。
		cfg.sampleRate = (opt.sampleRate != 0) ? opt.sampleRate : settings.sampleRate;
		cfg.maxLoops = settings.loops;
		cfg.autoFadeout = settings.fadeout;
		// 画面の遅れは設定ウィンドウで決める。-latency はその場かぎりの
		// 上書きで、ini には残さない（-nofade などと同じ扱い）。
		const int latencyMs = opt.latencySet ? opt.latencyMs : settings.latencyMs;
		cfg.displayLatencyAuto = opt.latencySet ? false : settings.latencyAuto;
		cfg.displayLatencyFrames = latencyMs * cfg.sampleRate / 1000;
		cfg.masterVolume = settings.masterVolume;

		std::string err;
		if (!player.Open(cfg, &err)) {
			printf("ERROR: %s\n", err.c_str());
			screen.Close();
			SDL_Quit();
			return EXIT_FAILURE;
		}
		PrintAudioInfo(player, cfg.displayLatencyAuto);
	}

	mxv2::SettingsUi ui;
	{
		std::string err;
		if (!ui.Init(&screen, paths, &err)) {
			// 設定 UI が無くても演奏はできるので、警告だけ出して続ける。
			Warn(&warnings, err);
		}
		// ここまでにたまった警告を最初のフレームで見せる。
		ui.SetStartupWarnings(warnings);
		ui.SetVfs(&vfs);
		ui.SetAboutHeader(AppHeader());
	}

	mxv2::Filer filer;
	filer.SetVfs(&vfs);
	filer.SetFolderFirst(settings.folderFirst);
	filer.SetViewMetrics(draw.fileListRows(), draw.fileListItemH());
	filer.SetCurrentRef(startDir);
	if (!startFile.empty()) filer.SelectByPath(startFile);

	mxv2::Visualizer visualizer(&draw);
	mxv2::MouseInput mouse(&draw, &filer, &player);
	// ゲームパッド（gamepad.h）。入力はキーに写して同じ経路へ流す。
	// 開けなくてもゲームパッドが使えないだけなので、警告だけ出して続ける。
	mxv2::Gamepad gamepad;
	{
		std::string err;
		if (!gamepad.Init(&err)) Warn(&warnings, mxv2::MsgF("Log.GamepadInit", err));
	}
	bool chromeRefresh = true;
	bool fileListRefresh = true;

	bool playing = false;
	std::string currentPath;

	// 曲の読み込みは別スレッド。届いたぶんを PollSong が演奏へ渡す。
	mxv2::SongLoader songLoader;
	SongLoad songLoad;
	ui.SetSongLoader(&songLoader);

	// 演奏終了時のふるまい。押した状態は mxv2.ini に残るので、前回のまま始まる。
	bool autoNext = settings.autoNext;    // CONT
	bool autoRepeat = settings.autoRepeat;  // REPEAT

	// 次のフレームの頭で取り替えるスキン。設定ウィンドウで選ばれたときと、
	// 画面の向きが変わったときに入る。
	std::string pendingSkin;

	// 窓の大きさが変わった印。フレームの頭でキャンバスを合わせ直す。
	// 起動直後にも 1 回通して、窓の縦横比にキャンバスを寄せる。
	bool windowResized = true;

	bool quit = false;
	// 端末がバックグラウンドへ回した (Android)。**音は止めず、描くのだけ止める。**
	bool inBackground = false;
	// 他のアプリに音を譲って止めた（通知の窓口から来る）。返してもらったときに
	// 自動で再開してよいかの印。
	bool pausedByFocus = false;
	// 音の途切れを知らせた回数と、次に知らせてよい時刻。
	uint32_t underrunsSeen = 0;
	uint32_t underrunNextMs = 0;
	bool endSeen = false;
	uint64_t endFrame = 0;
	// 演奏終了後の余韻 (1 秒)。出力レートで数えるので固定値にはできない。
	const uint64_t kLingerFrames = (uint64_t)player.sampleRate();

	PlayContext ctx;
	ctx.opt = &opt;
	ctx.settings = &settings;
	ctx.vfs = &vfs;
	ctx.player = &player;
	ctx.draw = &draw;
	ctx.visualizer = &visualizer;
	ctx.screen = &screen;
	ctx.loader = &songLoader;
	ctx.playing = &playing;
	ctx.currentPath = &currentPath;
	ctx.endSeen = &endSeen;
	ctx.chromeRefresh = &chromeRefresh;
	ctx.fileListRefresh = &fileListRefresh;
	ctx.load = &songLoad;

	// キーとマウスの操作が触るもの一式（keybind.h）。
	InputTargets targets;
	targets.ctx = &ctx;
	targets.filer = &filer;
	targets.ui = &ui;
	targets.player = &player;
	targets.draw = &draw;
	targets.autoNext = &autoNext;
	targets.autoRepeat = &autoRepeat;
	targets.chromeRefresh = &chromeRefresh;
	targets.fileListRefresh = &fileListRefresh;
	targets.currentPath = &currentPath;

	// 演奏状態の通知（Android）に出す文言。Java 側には文言を持たせず、
	// message.ini から引いたものを渡す。
	SetNotifyLabels();

	if (!startFile.empty()) StartPlay(ctx, startFile);

	// 初回起動のチュートリアル（tutorial.md）。ini の [Tutorial] Done が
	// 立っていないときに出す（[設定] の [動作]「次回起動時にチュートリアルを
	// 表示する」で外せる）。曲を渡されて起動したときは出さない（もう使い方を
	// 知っている人）。-tutorial は Done を無視して出すが、終わっても Done を
	// 書かない。実際に始めるのは起動時の警告を閉じてから（メインループの中）。
	mxv2::Tutorial tutorial;
	bool tutorialPending = opt.tutorial || (!settings.tutorialDone && startFile.empty());

	// -quit は「演奏し終えたら終わる」デバッグ用の指定。**明示したときだけ**
	// 効くので、曲を渡して起動したかどうかは見ない（渡さずに指定したときは、
	// 手で選んだ曲が終わったところで終わる）。
	const bool quitWhenDone = opt.quitOnEnd;

	// ドラッグ＆ドロップ。SDL は落とされたもの 1 つにつき 1 イベント送って
	// くるので、まとめて落とされたときは最初の 1 つだけを覚えておき、
	// 一区切り (DROPCOMPLETE) してから開く。
	std::string dropPath;
	bool dropSeen = false;
	bool dropGroup = false;

	while (!quit) {
		// 画面の向きが変わったらスキンを取り替える（screen_orientation.md）。
		// 見るのは窓の縦横比で、端末の「自然な向き」ではない（Screen の
		// コメント）。「常に切り替える」以外は起動時（と設定を閉じたとき）に
		// 決めたきりなので、ここでは何もしない。
		if (orientEnabled && settings.orientationMode == mxv2::Settings::kOrientAlways) {
			const mxv2::Screen::Orientation now = screen.orientation();
			if (now != orientNow) {
				orientNow = now;
				pendingSkin = (now == mxv2::Screen::kPortrait) ? settings.skinPortrait
				                                               : settings.skinLandscape;
				// 出ていたメニューは閉じる（ユーザーの指示）。回転すると
				// メニューが画面をはみ出すことがあり、配置を計算し直して
				// 出し直すより閉じてしまうほうがスマート、という判断。
				// ダイアログはモーダルで中央に出し直すので残す。
				ui.CloseContextMenu();
			}
		}

		// スキンの差し替え。設定ウィンドウで選ばれたときと、画面の向きが
		// 変わったとき（screen_orientation.md）に来る。
		//
		// **キャンバスを窓へ合わせるより前に置くこと。** 逆だと、回転した
		// フレームでキャンバスだけ先に追従して、1 フレームぶん「旧スキンが
		// 引き伸びた絵」が出る。
		if (!pendingSkin.empty()) {
			const std::string name = pendingSkin;
			pendingSkin.clear();

			// 読めない・描けないときは、その系統の既定のスキンへ落とす
			// （起動時と同じ。ini には書き戻さない）。それも駄目なら元の
			// スキンへ戻す。loaded は実際に使えた ref（空なら戻した）。
			const std::string fallback = DefaultSkinFor(orientEnabled, orientNow);
			const std::string candidates[2] = {name,
			                                   (fallback != name) ? fallback : std::string()};
			const mxv2::Skin prev = skin;
			std::string loaded;
			bool touched = false;  // 画面や DrawScreen を作り直したか
			for (int i = 0; i < 2 && loaded.empty(); i++) {
				if (candidates[i].empty()) continue;
				mxv2::Skin next;
				std::string err;
				if (!next.Load(paths, candidates[i], &err)) {
					printf("warning  : %s\n",
					       mxv2::MsgF("Log.SkinUnreadable", candidates[i], err).c_str());
					continue;
				}
				skin = next;
				touched = true;

				// 順番が大事: 画面 -> 文字レイヤー -> DrawScreen。
				// DrawScreen::Init は最後に Reload() まで済ませて曲名を
				// 描き直すので、その前にレイヤーを作り直しておく。
				bool ok = screen.Resize(skin.screenW, skin.screenH, &err);
				if (ok) {
					textLayer.SetFontDirs(mxv2::FontSearchDirs(skin, paths));
					textLayer.Rebuild(&screen, &err);
					ok = draw.Init(&skin, &err);
				}
				if (!ok) {
					printf("warning  : %s\n",
					       mxv2::MsgF("Log.SkinSwitchFailed", candidates[i], err).c_str());
					continue;
				}
				loaded = candidates[i];
				if (i > 0) {
					printf("warning  : %s\n", mxv2::MsgF("Log.SkinFallback", loaded).c_str());
				}
			}
			if (touched) {
				if (loaded.empty()) {
					std::string err;
					skin = prev;
					screen.Resize(skin.screenW, skin.screenH, &err);
					textLayer.SetFontDirs(mxv2::FontSearchDirs(skin, paths));
					textLayer.Rebuild(&screen, &err);
					draw.Init(&skin, &err);
				} else if (!orientEnabled && loaded == name) {
					settings.skinName = name;
				}

				draw.SetFileListFontSize(settings.fileListFontSize);
				filer.SetViewMetrics(draw.fileListRows(), draw.fileListItemH());
				player.RequestStatusRefresh();
				chromeRefresh = true;
				fileListRefresh = true;
				// 宣言サイズが同じスキンへ移ったときは窓の大きさが変わらず、
				// リサイズのイベントも来ない。分割が違えばキャンバスは
				// 変わるので、次のフレームで合わせ直させる。
				windowResized = true;
			}
		}

		// 窓の大きさが変わっていたらキャンバスを作り直す。イベントごとでは
		// なくフレームに 1 回にすることが、そのままリサイズ中のデバウンスに
		// なる。表示倍率やスキンを変えたあとの追随もここが受け持つ。
		if (windowResized) {
			windowResized = false;
			if (SyncCanvasToWindow(&screen, &textLayer, &draw, &filer, skin)) {
				player.RequestStatusRefresh();
				chromeRefresh = true;
				fileListRefresh = true;
			} else if (textLayer.SyncToScreen(&screen)) {
				// キャンバスは同じで拡大率だけ変わった（文字は出力解像度で
				// 描いているので、レイヤーを作り直したら描き直しが要る）。
				draw.Reload();
				player.RequestStatusRefresh();
				chromeRefresh = true;
				fileListRefresh = true;
			}
		}

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			ui.ProcessEvent(ev);
			// コントローラのイベントはキーに写して積み直す。積んだキーは
			// このループの続きで普通のキーとして届く。
			gamepad.Handle(ev);

			if (ev.type == SDL_QUIT) {
				quit = true;
				continue;
			}

			// ドラッグ＆ドロップ。drop.file は SDL が確保しているので、
			// 使っても捨てても必ず SDL_free で返す。
			if (ev.type == SDL_DROPBEGIN) {
				dropGroup = true;
				dropSeen = false;
				dropPath.clear();
				continue;
			}
			if (ev.type == SDL_DROPCOMPLETE) {
				dropGroup = false;
				continue;
			}
			if (ev.type == SDL_DROPFILE || ev.type == SDL_DROPTEXT) {
				// 判断に使うのは最初のアイテムだけ。文字列のドロップは扱わない。
				if (ev.type == SDL_DROPFILE && !dropSeen && ev.drop.file != 0) {
					dropPath = ev.drop.file;
					dropSeen = true;
				}
				SDL_free(ev.drop.file);
				continue;
			}

			// ウィンドウの大きさが変わると拡大率も変わる。文字レイヤーを
			// 作り直して、画面を最初から描き直す。
			// キャンバスそのものの作り直し（ファイラーの行数が変わる）は
			// イベントごとではなくフレームの頭で 1 回だけやる
			// （SyncCanvasToWindow）。
			if (ev.type == SDL_WINDOWEVENT &&
			    (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
			     ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
				windowResized = true;
			}

			// 端末のライフサイクル (Android)。バックグラウンドでは描かない
			// ——描き続けると OS に止められるし、GL の面も無い——ので印を立てる。
			// **音は止めない**（止めるのは前面サービスが立てられなかった
			// ときの OS の判断で、こちらからは続けるつもりでいる）。
			if (ev.type == SDL_APP_WILLENTERBACKGROUND) {
				inBackground = true;
				continue;
			}
			if (ev.type == SDL_APP_DIDENTERFOREGROUND) {
				inBackground = false;
				ForceRedrawAll(&screen, &textLayer, &draw, &player, &ui, &chromeRefresh,
				               &fileListRefresh);
				continue;
			}

			// GL コンテキストが飛んだ。テクスチャを作り直して描き直す。
			if (ev.type == SDL_RENDER_DEVICE_RESET || ev.type == SDL_RENDER_TARGETS_RESET) {
				ForceRedrawAll(&screen, &textLayer, &draw, &player, &ui, &chromeRefresh,
				               &fileListRefresh);
				continue;
			}

			// ESC と戻るキーは、まず開いているダイアログを閉じる。閉じるものが
			// 無ければ下へ流す（ESC は終了、戻るキーは親フォルダへ）。
			if (ev.type == SDL_KEYDOWN &&
			    (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_AC_BACK) &&
			    ui.CloseDialog()) {
				continue;
			}

			// 設定ウィンドウが入力を掴んでいる間は、アプリ側では扱わない。
			const bool isMouse =
			    (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP ||
			     ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEWHEEL);
			const bool isKey = (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP ||
			                    ev.type == SDL_TEXTINPUT);
			// 右クリックのコンテキストメニューは ImGui 側が自分で拾う
			// （SettingsUi::BuildContextMenu の BeginPopupContextVoid）。
			// ESC は mxv2 では終了に割り当ててあるので、そちらには足さない。
			if (isMouse && ui.wantCaptureMouse()) {
				// チュートリアル中で他のダイアログが無ければ、吹き出しの上で
				// 押したときだけ ImGui に譲る。指で操作すると、前の触りが
				// 吹き出しの上で終わっただけで ImGui が次の押下を掴んでしまう
				// （tutorial.h の BubbleContains）。動かすだけなら本体にも通す。
				bool pass = false;
				if (tutorial.active() && !ui.anyDialogOpen()) {
					if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
						int ox = 0, oy = 0;
						screen.WindowToOutput(ev.button.x, ev.button.y, &ox, &oy);
						pass = !tutorial.BubbleContains(ox, oy);
					} else {
						pass = true;
					}
				}
				if (!pass) continue;
			}
			// ダイアログが開いている間はアプリ側でキーを扱わない。
			// F1/F2 は開くだけなので通す必要はなく、閉じるのは上の ESC。
			// チュートリアルの吹き出しにフォーカスが付いているだけのときは
			// 通す（吹き出しに入力欄は無い。tutorial.cpp の末尾）。
			if (isKey && ui.wantCaptureKeyboard() &&
			    !(tutorial.active() && !ui.anyDialogOpen())) {
				continue;
			}

			// チュートリアルの幕。そのステップで期待している操作
			// （スポットの中のマウス、そのステップのキー）だけ通し、
			// それ以外は幕が吸い取る。ESC と戻るキーはスキップの確認。
			if (tutorial.active()) {
				if (ev.type == SDL_KEYDOWN && (ev.key.keysym.sym == SDLK_ESCAPE ||
				                               ev.key.keysym.sym == SDLK_AC_BACK)) {
					tutorial.RequestSkip();
					continue;
				}
				if (ev.type == SDL_KEYDOWN &&
				    !tutorial.AllowsKey(ev.key.keysym.sym, ev.key.keysym.mod)) {
					continue;
				}
				if (isMouse) {
					SDL_Event cev = ev;
					screen.WindowEventToCanvas(&cev);
					if (!tutorial.AllowsMouse(cev)) continue;
				}
			}

			// マウス。イベントは窓の画素で届くので、キャンバスの論理座標へ
			// 直してから配る（当たり判定はすべて論理座標）。
			SDL_Event mev = ev;
			screen.WindowEventToCanvas(&mev);
			HandleMouseRequest(mouse.Handle(mev), targets);

			if (ev.type != SDL_KEYDOWN) continue;

			HandleKeyDown(ev.key, targets);
		}

		// 落とされたものを開く。イベントを汲み終えてからにするのは、
		// まとめて落とされたときに最初のアイテムで決めるため。
		if (dropSeen && !dropGroup) {
			const std::string path = dropPath;
			dropSeen = false;
			dropPath.clear();
			OpenDropped(ctx, &filer, path);
		}

		// 言語が入れ替わったフレーム。カタログから引いた文言を**こちらで
		// 持っている**ところを取り直す（SettingsUi は自分のぶんを直している）。
		if (ui.TakeLocaleChanged()) {
			SetNotifyLabels();
			// ファイラーの「読み込み中」やファイルシステムの選択画面の
			// 見出しは一覧に焼き込まれているので、読み直して作り直す。
			filer.Refresh();
			chromeRefresh = true;
			fileListRefresh = true;
		}

		// 出力サンプリングレートが選ばれていたら、ここで開き直す。
		// レートは MXDRV とオーディオ装置を開くときに決まるので、途中では
		// 変えられない。曲・演奏位置・一時停止・音量・チャンネルマスクを
		// 引き継いで、聴いていた場所から続くようにする。
		if (ui.pendingSampleRate() != 0) {
			const int want = ui.pendingSampleRate();
			ui.ClearPendingSampleRate();
			if (want != player.sampleRate()) {
				const std::string keep = currentPath;
				const bool wasPlaying = playing;
				const bool wasPaused = player.paused();
				const uint32_t atMs = player.nowTimeMs();
				const uint16_t mask = player.channelMask();
				const int mainVol = player.mainVolume();
				const int prevRate = player.sampleRate();

				cfg.sampleRate = want;
				cfg.masterVolume = player.masterVolume();
				player.Close();

				std::string err;
				bool ok = player.Open(cfg, &err);
				if (!ok) {
					// 開けなかったら元のレートへ戻す。それも駄目なら
					// 音が出せないので続けられない。
					printf("warning  : %s\n",
					       mxv2::MsgF("Log.RateOpenFailed", mxv2::MsgNum("%d", want), err)
					           .c_str());
					cfg.sampleRate = prevRate;
					ok = player.Open(cfg, &err);
					if (!ok) {
						printf("ERROR: %s\n", err.c_str());
						quit = true;
					}
					settings.sampleRate = prevRate;
					dirtyFields |= mxv2::Settings::kFieldSampleRate;
				}
				if (ok) {
					player.SetMainVolume(mainVol);
					player.SetChannelMask(mask);
					if (wasPlaying && !keep.empty()) {
						// 掛け直しは読み終わってからになるので、位置・
						// 一時停止・マスクは PollSong へ預ける
						// （曲の掛け直しと空回しでマスクが消えるため）。
						StartPlayResume(ctx, keep, atMs, wasPaused, mask, true);
					}
					visualizer.Reset();
					draw.Reload();
					player.RequestStatusRefresh();
					chromeRefresh = true;
					fileListRefresh = true;
					PrintAudioInfo(player, cfg.displayLatencyAuto);
				}
			}
		}

		// バックグラウンドでは 1 フレームも描かない。ただし**演奏は続ける**
		// ので、曲の送り (CONT/REPEAT) と通知まわりはここでも回す。
		//
		// 待つのは **`SDL_Delay` で 1 回眠るだけ**にすること。
		// `SDL_WaitEventTimeout` は中で **1ms ごとに起きて `SDL_PumpEvents` を
		// 回す**作りなので、非力な端末では**この待ちだけでコアの 4 割**を
		// 食っていた（デコードそのものより重かった）。イベントは次の周回の
		// `SDL_PollEvent` が拾うので、取りこぼしはしない（気付くのが
		// 最大 kBackgroundTickMs 遅れるだけ）。
		if (inBackground) {
			SDL_Delay(kBackgroundTickMs);

			if (filer.PollDir()) fileListRefresh = true;
			if (filer.PollTitles()) fileListRefresh = true;
			PollSong(ctx);
			PollNotifyRequests(ctx, &filer, &pausedByFocus);
			PollUnderruns(player, &underrunsSeen, &underrunNextMs);

			// 描かないが、ビジュアライズのイベントは食べておく。ためたままに
			// すると 64K でキューが溢れ、**古いものが残って新しいものが
			// 捨てられる**（Push が満杯で失敗する側）ので、前面へ戻ったときの
			// 描き直しの指示まで消えてしまう。
			if (player.TakeDisplayReset()) visualizer.AllOff();
			visualizer.Consume(&player.dispQueue(), player.visualFrame());

			PollSongEnd(ctx, &filer, player.visualFrame(), kLingerFrames, autoNext, autoRepeat,
			            quitWhenDone, &endFrame, &quit);
			UpdateNowPlaying(player, currentPath, playing, autoNext, autoRepeat);
			continue;
		}

		// 長押し（STOP でフェードアウト、ファイラーで文字サイズ、ステータス欄で
		// 音色データ表示、鍵盤で OPM レジスタ一覧）はイベントではなく時間で
		// 決まるので、ここで拾う。
		gamepad.Poll(SDL_GetTicks());  // 上下の押し続け（キーリピート相当）
		switch (mouse.Poll(SDL_GetTicks())) {
			case mxv2::kMouseRequestToggleFontSize:
				ToggleFileListFontSize(&draw, &filer, &fileListRefresh);
				break;
			case mxv2::kMouseRequestToggleStatusMode:
				// 欄を消して新しいモードの 0 表示を敷き、値は積み直してもらう
				// （どちらのモードのイベントも StatusWatch が変化時にしか積まない
				// ので、切り替えた直後は全部を頼む）。
				draw.ToggleStatusMode();
				player.RequestStatusRefresh();
				break;
			case mxv2::kMouseRequestToggleRegMap:
				// レジスタの写しは表示 OFF でも届いているので、積み直しは要らない。
				draw.ToggleRegMap();
				break;
			default:
				break;
		}

		// フォルダの中身も MDX のタイトルも曲そのものも別スレッドで
		// 読んでいる。届いたぶんをここで取り込む。
		if (filer.PollDir()) fileListRefresh = true;
		if (filer.PollTitles()) fileListRefresh = true;
		PollSong(ctx);
		PollNotifyRequests(ctx, &filer, &pausedByFocus);
		PollUnderruns(player, &underrunsSeen, &underrunNextMs);

		// 設定 UI はここで組み立てる。配色を変えると 640x480 の
		// オフスクリーンを作り直すので、下の描画より先に回す。
		// キー操作でも変わる項目は、UI を開く前に拾っておく。
		unsigned newDirt = CollectDirtyFields(&settings, &draw, &player, &filer, &screen, autoNext,
		                                       autoRepeat);
		ui.SetOrientationState(orientEnabled, orientNow);
		ui.Build(&settings, &draw, &player, &filer, &screen);

		// チュートリアル（tutorial.md）。ImGui のフレームの中で、ダイアログの
		// あとに描く（幕は背景の描画リストに置くので順番は問わない）。
		if (tutorialPending && !ui.anyDialogOpen()) {
			tutorialPending = false;
			tutorial.Start(!opt.tutorial && !settings.tutorialDone);
			// CONT / REPEAT は消した状態から始める（「CONT ボタンを押して、
			// 点けてください」の文言と合わせる。ユーザーの指示）。ini には
			// 下の見比べで書き戻される。
			autoNext = false;
			autoRepeat = false;
			chromeRefresh = true;
			// ファイラーは同梱の曲 (assets:) の根から始める。ステップ 2 の
			// 「ArctanX を開いてください」がその場にある状態にするため
			// （ユーザーの指示）。前回の場所 (LastDir) を引き継いだ起動でも同じ。
			filer.SetCurrentRef("assets:");
			fileListRefresh = true;
		}
		if (tutorial.active()) {
			mxv2::Tutorial::State ts;
			ts.currentRef = filer.currentRef();
			ts.fsSelect = (filer.fs() == 0);
			ts.playing = playing;
			ts.paused = player.paused();
			ts.channelMask = player.channelMask();
			ts.cont = autoNext;
			ts.menuOpen = ui.contextMenuOpen();
			ts.settingsOpen = ui.visible();
			ts.touch = ui.touchUi();
			tutorial.Build(draw, screen, ts, ui.uiScale(), ImGuiCond_Appearing);
		}
		// 鍵盤のステップを抜けるときはマスクを全部解除する（音が欠けたまま
		// 先へ行かない）。見終えた／スキップしたら Done を書く。
		if (tutorial.TakeMaskReset()) player.SetChannelMask(0);
		if (tutorial.TakeFinished() && tutorial.recordDone()) {
			settings.tutorialDone = true;
			dirtyFields |= mxv2::Settings::kFieldTutorial;
		}
		// 設定ウィンドウでスキンが選ばれていたら、次のフレームの頭で
		// 取り替える（上のブロック）。
		if (!ui.pendingSkin().empty()) {
			pendingSkin = ui.pendingSkin();
			ui.ClearPendingSkin();
		}
		// 切り替えかたを変えたときは、**ダイアログを閉じてから**効かせる
		// （screen_orientation.md）。閉じた時点の向きで選び直す。
		if (orientEnabled && ui.TakeSettingsClosed()) {
			ApplyOrientationMode(settings.orientationMode, screen.orientation());
			if (settings.orientationMode != mxv2::Settings::kOrientAlways) {
				const mxv2::Screen::Orientation want = OrientationForMode(
				    settings.orientationMode, screen.orientation());
				if (want != orientNow) {
					orientNow = want;
					pendingSkin = (want == mxv2::Screen::kPortrait)
					                  ? settings.skinPortrait
					                  : settings.skinLandscape;
				}
			}
		}
		newDirt |= ui.TakeChangedFields();
		// [ファイルシステムの設定] は Vfs のマウント一覧を直に触るので、
		// 書き戻す前にそこから拾い直す。
		if (newDirt & mxv2::Settings::kFieldFileSystems) {
			settings.fileSystems = SaveFileSystems(vfs);
		}
		// ブックマークが変わったら、"Bookmarks>" を開いていれば並べ直す。
		if ((newDirt & mxv2::Settings::kFieldBookmarks) && filer.fs() != 0 &&
		    filer.fs()->isJumpList()) {
			filer.Refresh();
		}

		// コンテキストメニューからの要求。フォルダの移動と終了は
		// メインループが状態を持っているのでここで実行する。
		switch (ui.TakeRequest()) {
			case mxv2::SettingsUi::kRequestSetFolder:
				filer.SetCurrentRef(ui.requestedFolder());
				fileListRefresh = true;
				break;
			case mxv2::SettingsUi::kRequestQuit:
				quit = true;
				break;
			default:
				break;
		}

		// 保存ボタンは無く「変えた時点で保存」する。ただしドラッグ中やウィンドウ
		// 移動中は毎フレーム変わるので、手が止まってから少し待ってまとめて書く。
		{
			const uint32_t now = SDL_GetTicks();
			if (newDirt != 0) {
				dirtyFields |= newDirt;
				saveAtMs = now + kSettingsSaveDelayMs;
			}
			if (dirtyFields != 0 && now >= saveAtMs) {
				if (!settings.SaveFields(settingsPath, dirtyFields)) {
					printf("warning  : %s\n",
					       mxv2::MsgF("Log.SettingsSaveFailed", settingsPath).c_str());
				}
				dirtyFields = 0;
			}
		}

		// 演奏位置が飛んだ（曲の切り替え・シーク）か止まったときは、
		// **新しいイベントを描く前に**画面の鍵盤とレベルメーターを消す。
		// 順番が逆だと、飛んだ先で鳴り始めた鍵盤まで消してしまう。
		if (player.TakeDisplayReset()) visualizer.AllOff();

		const uint64_t frame = player.visualFrame();
		visualizer.Consume(&player.dispQueue(), frame);
		// シークバーを掴んでいる間は、まだ飛んでいない「指の位置」を
		// 演奏位置として見せる。飛ぶのは離したとき。
		const uint32_t chromeNowMs =
		    mouse.seekDragging() ? mouse.seekDragMs() : player.nowTimeMs();
		visualizer.UpdateChrome(player, chromeNowMs, chromeRefresh, autoNext, autoRepeat,
		                        mouse.playKeyPressMask());
		draw.PutFileList(filer, fileListRefresh, SDL_GetTicks());
		draw.PutScrollBar(filer.topPx(), filer.maxTopPx());
		// 曲名が枠に収まらないときの横スクロール。収まっていれば何もしない。
		draw.UpdateTitleScroll(SDL_GetTicks());
		chromeRefresh = false;
		fileListRefresh = false;

		draw.BlitTo(&screen);
		screen.Draw();
		textLayer.Render(&screen);  // 文字は拡大後の解像度で重ねる
		ui.Render(&screen);
		screen.Present();

		// OS の「フォルダを探す」ダイアログ。開いている間はこちらが止まるので、
		// 1 フレーム描き終えてから開く。
		if (ui.pendingBrowse()) {
			ui.ClearPendingBrowse();
			std::string picked;
			if (mxv2::BrowseForFolder(mxv2::Msg("AddFs.BrowseTitle"), ui.browseStart(),
			                          screen.nativeWindowHandle(), &picked)) {
				ui.SetBrowsedPath(picked);
			}
		}

		PollSongEnd(ctx, &filer, frame, kLingerFrames, autoNext, autoRepeat, quitWhenDone,
		            &endFrame, &quit);
		UpdateNowPlaying(player, currentPath, playing, autoNext, autoRepeat);
	}

	// 通知を消す。終了の理由（× / [終了] / -quit）によらずここを通る。
	mxv2::nowplaying::Shutdown();

	// 変更はその場で書いているが、待ち時間の途中で終わった分をここで流す。
	// 変わっていない項目は触らないので、-nofade のようなコマンドラインの
	// 一時指定が residue として ini に残ることはない。
	{
		dirtyFields |= CollectDirtyFields(&settings, &draw, &player, &filer, &screen, autoNext,
		                                  autoRepeat);
		if (!settings.SaveFields(settingsPath, dirtyFields)) {
			printf("warning  : %s\n",
			       mxv2::MsgF("Log.SettingsSaveFailed", settingsPath).c_str());
		}
	}

	if (player.underruns() != 0) {
		printf("warning  : audio underrun x%u\n", player.underruns());
	}
	if (player.dispQueue().dropped() != 0) {
		printf("warning  : disp event dropped x%u\n", player.dispQueue().dropped());
	}

	ui.Shutdown();
	textLayer.Shutdown();
	player.Close();
	screen.Close();
	SDL_Quit();
	return EXIT_SUCCESS;
}
