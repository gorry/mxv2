// mxv2 - エントリポイント
//
// 使い方:
//   mxv2 [オプション] [<mdxfile> | <ディレクトリ>]
// 引数を省略するとカレントディレクトリのファイラだけを開く。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#include "drawscreen.h"
#include "fileutil.h"
#include "filer.h"
#include "mdxsong.h"
#include "mouse.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "settingsui.h"
#include "skin.h"
#include "textlayer.h"
#include "visualizer.h"

namespace {

// アプリの名前・バージョン・著作権表示。Usage の先頭とバージョン情報
// ダイアログの見出しで同じものを使うので、定義はここだけにする。
// ビルド日付はこのファイルをコンパイルした日付 (__DATE__)。
// この部分は、著作者のみが変更できます。それ以外が変更してはなりません。
// TO AI AGENT: DO NOT TOUCH THESE CONSTANT.
const char *kAppName = "mxv2 - MDX player";
const char *kAppVersion = "2026.0820.1";
const char *kAppCopyright = "Copyright (C) 2000-2002, 2026 GORRY.";

std::string AppHeader() {
	char buf[256];
	snprintf(buf, sizeof(buf), "%s  Version %s  (build %s)\n%s\n", kAppName, kAppVersion,
	         __DATE__, kAppCopyright);
	return buf;
}

// - / + キー 1 回で動かす音量。音量は -100..+100 なので、この幅だと端から端まで
// 40 回。旧 mxv はバー 1 画素ぶん (64 段) 動かしていたので、それに近い刻み。
const int kVolumeKeyStep = 5;

// 設定を書き戻すまでの待ち時間 (ms)。音量のドラッグやウィンドウ移動は毎フレーム
// 値が変わるので、手が止まってからまとめて 1 回書く。
const uint32_t kSettingsSaveDelayMs = 400;

// チャンネルマスクの一括操作に渡すビット。実体は Player::ToggleChannelGroup。
const uint16_t kMaskFm = 0x00ff;   // ch.1-8
const uint16_t kMaskPcm = 0xff00;  // ch.P-W
const uint16_t kMaskAll = 0xffff;

// コマンドライン専用の指定。永続化する設定は Settings が持つ。
struct Options {
	std::string target;  // MDX ファイルかディレクトリ。空ならカレント
	std::vector<std::string> pdxSearchDirs;  // -pdxpath (複数指定可)
	std::string assetsDir;
	int latencyFrames;
	bool quitOnEnd;

	Options() : latencyFrames(0), quitOnEnd(true) {}
};

void PrintUsage(const char *argv0) {
	printf("%s", AppHeader().c_str());
	printf(
	    "usage:\n"
	    "  %s [options] [<mdxfile> | <dir>]\n"
	    "options:\n"
	    "  -zoom <percent> 表示倍率 %% (100 でドット等倍。既定はシステムの拡大率)\n"
	    "  -loops <n>      自動フェードアウトまでのループ数 (既定 2)\n"
	    "  -nofade         自動フェードアウトしない\n"
	    "  -latency <n>    表示を遅らせるサンプル数 (既定 0)\n"
	    "  -pdxpath <dir>  PDX の追加探索先\n"
	    "  -assets <dir>   素材ビットマップの場所 (既定: 実行ファイルの隣の assets)\n"
	    "  -skin <name>    スキン名 (assets/skin/<name>。既定: Default)\n"
	    "  -folderfirst    ファイラでフォルダを先に並べる\n"
	    "  -noquit         演奏終了後も閉じない\n"
	    "keys:\n"
	    "  ESC / Q         終了\n"
	    "  SPACE           一時停止 / 再開\n"
	    "  F               フェードアウト\n"
	    "  ENTER           ファイラの項目を開く\n"
	    "  BACKSPACE       親ディレクトリへ\n"
	    "  \\               ルートディレクトリへ\n"
	    "  L               フォルダを選んで移動 (ESC で閉じる)\n"
	    "  UP/DOWN/PGUP/PGDN/HOME/END  カーソル移動\n"
	    "  N / B           次 / 前の MDX を演奏\n"
	    "  C               演奏終了で次の曲へ (CONT)\n"
	    "  R               演奏終了で同じ曲を繰り返す (REPEAT)\n"
	    "  TAB             ファイラの文字サイズ\n"
	    "  1-8             FM チャンネル (ch.1-8) のミュート切り替え\n"
	    "  Shift+1-8       PCM チャンネル (ch.P-W) のミュート切り替え\n"
	    "  0 / Shift+0 / Ctrl+0  FM / PCM / 全チャンネルの一括マスク\n"
	    "  - / +           音量 (この画面ぶん。マスター音量は F1 の設定で)\n"
	    "  F1              設定ウィンドウを開く (ESC で閉じる)\n"
	    "  F2              テーマ設定を開く (ESC で閉じる)\n"
	    "mouse:\n"
	    "  バナー          クリックでメニュー (右クリックでも同じものが出る)\n"
	    "  ファイルリスト  クリックでカーソル移動 / ダブルクリックで開く\n"
	    "                  ドラッグでスクロール (振ると慣性で滑る。触ると止まる)\n"
	    "  スクロールバー  矢印・溝・つまみのドラッグ。ホイールでも送れる\n"
	    "  操作キー        PREV STOP PLAY FAST PAUSE NEXT CONT REPEAT\n"
	    "  音量バー        クリックとドラッグ\n"
	    "  プログレスバー  クリックで演奏位置を移動\n",
	    argv0);
}

// mxv2.ini から読んだ設定を、コマンドラインで上書きする。
bool ParseArgs(int argc, char **argv, Options *opt, mxv2::Settings *st) {
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-') {
			if (!opt->target.empty()) {
				printf("ERROR: 対象が二重に指定されています: %s\n", a);
				return false;
			}
			opt->target = a;
			continue;
		}
		if (strcmp(a, "-nofade") == 0) {
			st->fadeout = false;
		} else if (strcmp(a, "-noquit") == 0) {
			opt->quitOnEnd = false;
		} else if (strcmp(a, "-folderfirst") == 0) {
			st->folderFirst = true;
		} else if (strcmp(a, "-zoom") == 0 && i + 1 < argc) {
			st->zoomPercent = atoi(argv[++i]);
		} else if (strcmp(a, "-loops") == 0 && i + 1 < argc) {
			st->loops = atoi(argv[++i]);
		} else if (strcmp(a, "-latency") == 0 && i + 1 < argc) {
			opt->latencyFrames = atoi(argv[++i]);
		} else if (strcmp(a, "-pdxpath") == 0 && i + 1 < argc) {
			opt->pdxSearchDirs.push_back(argv[++i]);
		} else if (strcmp(a, "-assets") == 0 && i + 1 < argc) {
			opt->assetsDir = argv[++i];
		} else if (strcmp(a, "-skin") == 0 && i + 1 < argc) {
			st->skinName = argv[++i];
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "-help") == 0) {
			return false;
		} else {
			printf("ERROR: 不明なオプション: %s\n", a);
			return false;
		}
	}
	if (st->loops < 1) st->loops = 1;
	return true;
}

// 曲を切り替えるときに触るもの一式。キーボード・マウス・自動送りの
// どれからでも同じ手順を通すためにまとめてある。
struct PlayContext {
	const Options *opt;
	const mxv2::Settings *settings;
	mxv2::Player *player;
	mxv2::DrawScreen *draw;
	mxv2::Visualizer *visualizer;
	mxv2::Screen *screen;

	bool *playing;
	std::string *currentPath;
	bool *endSeen;
	bool *chromeRefresh;
	bool *fileListRefresh;
};

// PDX の探索先。設定の 1 つと -pdxpath の指定を合わせたもの。
std::vector<std::string> PdxSearchDirs(const Options &opt, const mxv2::Settings &st) {
	std::vector<std::string> dirs = opt.pdxSearchDirs;
	if (!st.pdxPath.empty()) dirs.push_back(st.pdxPath);
	return dirs;
}

// 1 曲読み込んで演奏を始める。
bool PlayPath(const std::string &path, const Options &opt, const mxv2::Settings &st,
              mxv2::Player *player, mxv2::DrawScreen *draw, mxv2::Visualizer *visualizer,
              mxv2::Screen *screen) {
	mxv2::MdxSong song;
	std::string err;
	if (!mxv2::LoadMdxSong(path, PdxSearchDirs(opt, st), &song, &err)) {
		printf("ERROR: %s\n", err.c_str());
		return false;
	}
	if (!player->PlaySong(song, &err)) {
		printf("ERROR: %s\n", err.c_str());
		return false;
	}

	visualizer->Reset();
	draw->Reload();
	draw->PutMDXTitle(song.title);
	if (screen != 0) {
		screen->SetTitle(song.title.empty() ? std::string("mxv2")
		                                    : ("mxv2 - " + song.title));
	}

	printf("play     : %s\n", song.path.c_str());
	printf("title    : %s\n", song.title.c_str());
	if (song.requiresPdx && !song.hasPdx) {
		printf("warning  : PDX (%s) が見つかりません。FM のみで演奏します。\n",
		       song.pdxFileName.c_str());
	}
	printf("duration : %.1f sec\n", player->playTimeMs() / 1000.0f);
	// MSVC の setvbuf は _IOLBF を全バッファ扱いにするので、明示的に流す。
	fflush(stdout);
	return true;
}

// PlayPath にメインループ側の状態更新を足したもの。
void StartPlay(const PlayContext &ctx, const std::string &path) {
	const bool ok = PlayPath(path, *ctx.opt, *ctx.settings, ctx.player, ctx.draw,
	                         ctx.visualizer, ctx.screen);
	*ctx.playing = ok;
	if (ok) *ctx.currentPath = path;
	*ctx.endSeen = false;
	*ctx.chromeRefresh = true;
	*ctx.fileListRefresh = true;
}

}  // namespace

int main(int argc, char **argv) {
	// 途中で終了させても情報が残るように行バッファにする
	setvbuf(stdout, NULL, _IOLBF, 1024);

	// 設定 -> コマンドラインの順に読む（後勝ち）。
	mxv2::Settings settings;
	const std::string settingsPath = mxv2::Settings::DefaultPath();
	settings.Load(settingsPath);
	// ini に倍率が無かったかどうかを覚えておく（終了時に書き残すため）。
	const int legacyScale = settings.legacyScale;

	Options opt;
	if (!ParseArgs(argc, argv, &opt, &settings)) {
		PrintUsage(argc > 0 ? argv[0] : "mxv2");
		return EXIT_FAILURE;
	}

	// 対象がファイルならその曲を、ディレクトリならそこを開く。
	// 対象を省略したときは、前回開いていたディレクトリへ戻る。
	std::string startDir;
	std::string startFile;
	if (opt.target.empty()) {
		startDir = settings.lastDir;
		if (startDir.empty() || !mxv2::IsDirectory(startDir)) startDir = mxv2::CurrentDir();
	} else if (mxv2::IsDirectory(opt.target)) {
		startDir = mxv2::AbsolutePath(opt.target);
	} else {
		startFile = mxv2::AbsolutePath(opt.target);
		startDir = mxv2::DirNameOf(startFile);
		if (startDir.empty()) startDir = mxv2::CurrentDir();
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

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		printf("ERROR: SDL_Init に失敗しました: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	// 表示倍率が決まっていなければ、システムの拡大率 (175% など) を初期値にする。
	// 旧い ini の Scale=<整数倍> は「システム拡大率の n 倍」だったので、
	// 見た目が変わらないように % へ読み替える。
	// 変更のあった項目だけを ini へ書き戻すためのビット。設定ウィンドウには
	// 保存ボタンが無く、触った時点で保存する。
	unsigned dirtyFields = 0;
	uint32_t saveAtMs = 0;

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
		printf("display  : システム拡大率 %d%% / 表示倍率 %d%%\n", systemZoom,
		       settings.zoomPercent);
	}

	std::string assetsDir = opt.assetsDir;
	if (assetsDir.empty()) assetsDir = mxv2::JoinPath(mxv2::ExecutableDir(), "assets");
	const std::string skinRootDir = mxv2::JoinPath(assetsDir, "skin");

	// スキンが画面サイズを決めるので、ウィンドウより先に読む。
	mxv2::Skin skin;
	{
		std::string err;
		if (!skin.Load(mxv2::JoinPath(skinRootDir, settings.skinName), &err)) {
			printf("ERROR: %s\n", err.c_str());
			SDL_Quit();
			return EXIT_FAILURE;
		}
	}

	mxv2::Screen screen;
	{
		std::string err;
		if (!screen.Open("mxv2", skin.screenW, skin.screenH, settings.zoomPercent, &err)) {
			printf("ERROR: %s\n", err.c_str());
			SDL_Quit();
			return EXIT_FAILURE;
		}
		if (settings.savePosition && settings.windowX >= 0 && settings.windowY >= 0) {
			screen.SetWindowPos(settings.windowX, settings.windowY);
		}
		screen.SetScaleMode(
		    mxv2::Screen::ScaleModeFromName(settings.scaleFilter, mxv2::Screen::kScaleSharp));
		// 綴りが違っていたら解決後の名前で書き直す。
		const std::string resolved = mxv2::Screen::ScaleModeName(screen.scaleMode());
		if (settings.scaleFilter != resolved) dirtyFields |= mxv2::Settings::kFieldFilter;
		settings.scaleFilter = resolved;
	}

	// ファイラと曲名の文字は、キャンバスとは別に出力解像度で描いて重ねる。
	mxv2::TextLayer textLayer;
	{
		std::string err;
		if (!textLayer.Init(&screen, skin.dir(), assetsDir, &err)) {
			printf("warning  : %s\n", err.c_str());
		} else if (!textLayer.available()) {
			printf("warning  : 日本語フォントが読めません。assets/ に "
			       "MPLUS1p-Regular.ttf があるか確認してください"
			       "（assets/font.ttf かスキンの font.ttf を置けばそちらが使われます）。\n");
		}
	}

	mxv2::DrawScreen draw;
	{
		std::string err;
		draw.SetTextLayer(&textLayer);
		if (!draw.Init(&skin, &err)) {
			printf("ERROR: %s\n", err.c_str());
			printf("       -assets <dir> で素材の場所を、-skin <name> でスキンを"
			       "指定してください。\n");
			screen.Close();
			SDL_Quit();
			return EXIT_FAILURE;
		}
		draw.SetFileListFontSize(settings.fileListFontSize);
	}

	mxv2::Player player;
	{
		mxv2::Player::Config cfg;
		cfg.maxLoops = settings.loops;
		cfg.autoFadeout = settings.fadeout;
		cfg.displayLatencyFrames = opt.latencyFrames;
		cfg.masterVolume = settings.masterVolume;

		std::string err;
		if (!player.Open(cfg, &err)) {
			printf("ERROR: %s\n", err.c_str());
			screen.Close();
			SDL_Quit();
			return EXIT_FAILURE;
		}
	}

	mxv2::SettingsUi ui;
	{
		std::string err;
		if (!ui.Init(&screen, assetsDir, &err)) {
			// 設定 UI が無くても演奏はできるので、警告だけ出して続ける。
			printf("warning  : %s\n", err.c_str());
		}
		ui.SetAboutHeader(AppHeader());
	}

	mxv2::Filer filer;
	filer.SetFolderFirst(settings.folderFirst);
	filer.SetViewMetrics(draw.fileListRows(), draw.fileListItemH());
	filer.SetCurrentDir(startDir);
	if (!startFile.empty()) filer.SelectByPath(startFile);

	mxv2::Visualizer visualizer(&draw);
	mxv2::MouseInput mouse(&draw, &filer, &player);
	bool chromeRefresh = true;
	bool fileListRefresh = true;

	bool playing = false;
	std::string currentPath;

	// 演奏終了時のふるまい
	bool autoNext = false;    // CONT
	bool autoRepeat = false;  // REPEAT

	bool quit = false;
	bool endSeen = false;
	uint64_t endFrame = 0;
	const uint64_t kLingerFrames = 48000;  // 演奏終了後の余韻

	PlayContext ctx;
	ctx.opt = &opt;
	ctx.settings = &settings;
	ctx.player = &player;
	ctx.draw = &draw;
	ctx.visualizer = &visualizer;
	ctx.screen = &screen;
	ctx.playing = &playing;
	ctx.currentPath = &currentPath;
	ctx.endSeen = &endSeen;
	ctx.chromeRefresh = &chromeRefresh;
	ctx.fileListRefresh = &fileListRefresh;

	if (!startFile.empty()) StartPlay(ctx, startFile);

	const bool quitWhenDone = opt.quitOnEnd && !startFile.empty();

	while (!quit) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			ui.ProcessEvent(ev);

			if (ev.type == SDL_QUIT) {
				quit = true;
				continue;
			}

			// ウィンドウの大きさが変わると拡大率も変わる。文字レイヤーを
			// 作り直して、画面を最初から描き直す。
			if (ev.type == SDL_WINDOWEVENT &&
			    (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
			     ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
				if (textLayer.SyncToScreen(&screen)) {
					draw.Reload();
					player.RequestStatusRefresh();
					chromeRefresh = true;
					fileListRefresh = true;
				}
			}

			// ESC はまず開いているダイアログを閉じる。閉じるものが無ければ
			// 下へ流して、いつもどおり終了に使う。
			if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE &&
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
			if (isMouse && ui.wantCaptureMouse()) continue;
			// ダイアログが開いている間はアプリ側でキーを扱わない。
			// F1/F2 は開くだけなので通す必要はなく、閉じるのは上の ESC。
			if (isKey && ui.wantCaptureKeyboard()) continue;

			// マウス
			switch (mouse.Handle(ev)) {
				case mxv2::kMouseRequestOpenCursor: {
					std::string path;
					if (filer.Open(&path)) {
						if (path.empty()) {
							fileListRefresh = true;  // ディレクトリ移動
						} else {
							StartPlay(ctx, path);
						}
					}
					break;
				}
				case mxv2::kMouseRequestPrev: {
					std::string path;
					if (filer.PrevMdx(&path)) {
						StartPlay(ctx, path);
					}
					break;
				}
				case mxv2::kMouseRequestNext: {
					std::string path;
					if (filer.NextMdx(&path)) {
						StartPlay(ctx, path);
					}
					break;
				}
				case mxv2::kMouseRequestReplay:
					if (!currentPath.empty()) {
						StartPlay(ctx, currentPath);
					}
					break;
				case mxv2::kMouseRequestToggleCont:
					autoNext = !autoNext;
					chromeRefresh = true;
					break;
				case mxv2::kMouseRequestToggleRepeat:
					autoRepeat = !autoRepeat;
					chromeRefresh = true;
					break;
				case mxv2::kMouseRequestContextMenu:
					ui.OpenContextMenu();
					break;
				default:
					break;
			}

			if (ev.type != SDL_KEYDOWN) continue;

			const SDL_Keycode key = ev.key.keysym.sym;
			switch (key) {
				case SDLK_ESCAPE:
				case SDLK_q:
					quit = true;
					break;

				case SDLK_F1:
					ui.OpenSettings();
					break;
				case SDLK_F2:
					ui.OpenTheme();
					break;

				case SDLK_SPACE:
					if (player.paused()) {
						player.Resume();
					} else {
						player.Pause();
					}
					break;
				case SDLK_f:
					player.Fadeout();
					break;

				case SDLK_UP:
					filer.MoveCursor(-1);
					break;
				case SDLK_DOWN:
					filer.MoveCursor(1);
					break;
				case SDLK_PAGEUP:
					filer.MoveCursor(-filer.visibleRows());
					break;
				case SDLK_PAGEDOWN:
					filer.MoveCursor(filer.visibleRows());
					break;
				case SDLK_HOME:
					filer.SetCursor(0);
					break;
				case SDLK_END:
					filer.SetCursor(filer.itemCount() - 1);
					break;

				case SDLK_RETURN:
				case SDLK_KP_ENTER: {
					std::string path;
					if (filer.Open(&path)) {
						if (path.empty()) {
							fileListRefresh = true;  // ディレクトリ移動
						} else {
							StartPlay(ctx, path);
						}
					}
					break;
				}
				case SDLK_BACKSPACE:
					filer.GoParent();
					fileListRefresh = true;
					break;
				case SDLK_BACKSLASH:
					filer.GoRoot();
					fileListRefresh = true;
					break;
				case SDLK_l:
					// フォルダを選ぶダイアログ。今の場所から出す。
					ui.OpenFolder(filer.currentDir());
					break;

				case SDLK_n: {
					std::string path;
					if (filer.NextMdx(&path)) {
						StartPlay(ctx, path);
					}
					break;
				}
				case SDLK_b: {
					std::string path;
					if (filer.PrevMdx(&path)) {
						StartPlay(ctx, path);
					}
					break;
				}

				case SDLK_c:
					autoNext = !autoNext;
					chromeRefresh = true;
					break;
				case SDLK_r:
					autoRepeat = !autoRepeat;
					chromeRefresh = true;
					break;

				case SDLK_TAB:
					draw.SetFileListFontSize(draw.fileListFontSize() ^ 1);
					filer.SetViewMetrics(draw.fileListRows(), draw.fileListItemH());
					fileListRefresh = true;
					break;

				case SDLK_MINUS:
				case SDLK_KP_MINUS:
					player.SetMainVolume(player.mainVolume() - kVolumeKeyStep);
					break;
				case SDLK_EQUALS:
				case SDLK_PLUS:
				case SDLK_KP_PLUS:
					player.SetMainVolume(player.mainVolume() + kVolumeKeyStep);
					break;

				// チャンネルの一括マスク。旧 mxv は Ctrl+0 / Alt+0 / Ctrl+Alt+0。
				case SDLK_0:
					if (ev.key.keysym.mod & KMOD_CTRL) {
						player.ToggleChannelGroup(kMaskAll);
					} else if (ev.key.keysym.mod & KMOD_SHIFT) {
						player.ToggleChannelGroup(kMaskPcm);
					} else {
						player.ToggleChannelGroup(kMaskFm);
					}
					break;

				default:
					// 1-8 で FM の ch.1-8、Shift を足すと PCM の ch.P-W。
					// 旧 mxv は Ctrl+1-8 / Alt+1-8 だったが、mxv2 は修飾無しの
					// 1-8 を先に FM へ割り当ててあるので、PCM を Shift 側にした。
					if (key >= SDLK_1 && key <= SDLK_8) {
						const int base = (ev.key.keysym.mod & KMOD_SHIFT) ? 8 : 0;
						player.ToggleChannel(base + (int)(key - SDLK_1));
					}
					break;
			}
		}

		// 設定ウィンドウでスキンが選ばれていたら、ここで作り直す。
		// 画面サイズごと変わりうるので、UI の中ではやらない。
		if (!ui.pendingSkin().empty()) {
			const std::string name = ui.pendingSkin();
			ui.ClearPendingSkin();

			mxv2::Skin next;
			std::string err;
			if (!next.Load(mxv2::JoinPath(skinRootDir, name), &err)) {
				printf("warning  : スキン %s を読めません: %s\n", name.c_str(), err.c_str());
			} else {
				const mxv2::Skin prev = skin;
				skin = next;

				// 順番が大事: 画面 -> 文字レイヤー -> DrawScreen。
				// DrawScreen::Init は最後に Reload() まで済ませて曲名を
				// 描き直すので、その前にレイヤーを作り直しておく。
				bool ok = screen.Resize(skin.screenW, skin.screenH, &err);
				if (ok) {
					textLayer.SetSkinDir(skin.dir());
					textLayer.Rebuild(&screen, &err);
					ok = draw.Init(&skin, &err);
				}
				if (!ok) {
					printf("warning  : スキン %s に切り替えられません: %s\n", name.c_str(),
					       err.c_str());
					skin = prev;
					screen.Resize(skin.screenW, skin.screenH, &err);
					textLayer.SetSkinDir(skin.dir());
					textLayer.Rebuild(&screen, &err);
					draw.Init(&skin, &err);
				} else {
					settings.skinName = name;
				}

				draw.SetFileListFontSize(settings.fileListFontSize);
				filer.SetViewMetrics(draw.fileListRows(), draw.fileListItemH());
				player.RequestStatusRefresh();
				chromeRefresh = true;
				fileListRefresh = true;
			}
		}

		mouse.Poll(SDL_GetTicks());

		// 設定 UI はここで組み立てる。テーマを変えると 640x480 の
		// オフスクリーンを作り直すので、下の描画より先に回す。
		// キー操作でも変わる項目は、UI を開く前に拾っておく。
		unsigned newDirt = 0;
		if (settings.fileListFontSize != draw.fileListFontSize()) {
			settings.fileListFontSize = draw.fileListFontSize();
			newDirt |= mxv2::Settings::kFieldFontSize;
		}
		if (settings.masterVolume != player.masterVolume()) {
			settings.masterVolume = player.masterVolume();
			newDirt |= mxv2::Settings::kFieldVolume;
		}
		if (settings.lastDir != filer.currentDir()) {
			settings.lastDir = filer.currentDir();
			newDirt |= mxv2::Settings::kFieldLastDir;
		}
		if (settings.savePosition) {
			int wx = 0, wy = 0, ww = 0, wh = 0;
			screen.GetWindowRect(&wx, &wy, &ww, &wh);
			if (wx != settings.windowX || wy != settings.windowY) {
				settings.windowX = wx;
				settings.windowY = wy;
				newDirt |= mxv2::Settings::kFieldWindowPos;
			}
		}
		ui.Build(&settings, &draw, &player, &filer, &screen);
		newDirt |= ui.TakeChangedFields();

		// コンテキストメニューからの要求。演奏の開始・曲送り・終了は
		// メインループが状態を持っているのでここで実行する。
		switch (ui.TakeRequest()) {
			case mxv2::SettingsUi::kRequestOpenCursor: {
				std::string path;
				if (filer.Open(&path)) {
					if (path.empty()) {
						fileListRefresh = true;
					} else {
						StartPlay(ctx, path);
					}
				}
				break;
			}
			case mxv2::SettingsUi::kRequestReplay:
				if (!currentPath.empty()) StartPlay(ctx, currentPath);
				break;
			case mxv2::SettingsUi::kRequestPrev: {
				std::string path;
				if (filer.PrevMdx(&path)) StartPlay(ctx, path);
				break;
			}
			case mxv2::SettingsUi::kRequestNext: {
				std::string path;
				if (filer.NextMdx(&path)) StartPlay(ctx, path);
				break;
			}
			case mxv2::SettingsUi::kRequestToggleCont:
				autoNext = !autoNext;
				chromeRefresh = true;
				break;
			case mxv2::SettingsUi::kRequestToggleRepeat:
				autoRepeat = !autoRepeat;
				chromeRefresh = true;
				break;
			case mxv2::SettingsUi::kRequestSetFolder:
				filer.SetCurrentDir(ui.requestedFolder());
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
					printf("warning  : 設定を保存できません: %s\n", settingsPath.c_str());
				}
				dirtyFields = 0;
			}
		}

		const uint64_t frame = player.visualFrame();
		visualizer.Consume(&player.dispQueue(), frame);
		visualizer.UpdateChrome(player, chromeRefresh, autoNext, autoRepeat,
		                        mouse.playKeyPressMask());
		draw.PutFileList(filer, fileListRefresh);
		draw.PutScrollBar(filer.topPx(), filer.maxTopPx());
		chromeRefresh = false;
		fileListRefresh = false;

		draw.BlitTo(&screen);
		screen.Draw();
		textLayer.Render(&screen);  // 文字は拡大後の解像度で重ねる
		ui.Render(&screen);
		screen.Present();

		if (playing && player.playTerminated()) {
			if (!endSeen) {
				endSeen = true;
				endFrame = frame;
			} else if (frame > endFrame + kLingerFrames) {
				endSeen = false;
				std::string path;
				if (autoRepeat && !currentPath.empty()) {
					StartPlay(ctx, currentPath);
				} else if (autoNext && filer.NextMdx(&path)) {
					StartPlay(ctx, path);
				} else if (quitWhenDone) {
					quit = true;
				} else {
					playing = false;
				}
			}
		}
	}

	// 変更はその場で書いているが、待ち時間の途中で終わった分をここで流す。
	// 変わっていない項目は触らないので、-nofade のようなコマンドラインの
	// 一時指定が residue として ini に残ることはない。
	{
		if (settings.fileListFontSize != draw.fileListFontSize()) {
			settings.fileListFontSize = draw.fileListFontSize();
			dirtyFields |= mxv2::Settings::kFieldFontSize;
		}
		if (settings.masterVolume != player.masterVolume()) {
			settings.masterVolume = player.masterVolume();
			dirtyFields |= mxv2::Settings::kFieldVolume;
		}
		if (settings.lastDir != filer.currentDir()) {
			settings.lastDir = filer.currentDir();
			dirtyFields |= mxv2::Settings::kFieldLastDir;
		}
		if (settings.savePosition) {
			int wx = 0, wy = 0, ww = 0, wh = 0;
			screen.GetWindowRect(&wx, &wy, &ww, &wh);
			if (wx != settings.windowX || wy != settings.windowY) {
				settings.windowX = wx;
				settings.windowY = wy;
				dirtyFields |= mxv2::Settings::kFieldWindowPos;
			}
		}
		if (!settings.SaveFields(settingsPath, dirtyFields)) {
			printf("warning  : 設定を保存できません: %s\n", settingsPath.c_str());
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
