// mxv2 - エントリポイント
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

#ifdef _WIN32
#include <windows.h>
#endif

#include "assetpath.h"
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
#include "vfs.h"
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

// - / + (;) キー 1 回で動かす音量。音量は -100..+100 なので、この幅だと端から端まで
// 40 回。旧 mxv はバー 1 画素ぶん (64 段) 動かしていたので、それに近い刻み。
const int kVolumeKeyStep = 5;

// 設定を書き戻すまでの待ち時間 (ms)。音量のドラッグやウィンドウ移動は毎フレーム
// 値が変わるので、手が止まってからまとめて 1 回書く。
const uint32_t kSettingsSaveDelayMs = 400;

// 標準出力の行き先を用意する。
//
// Windows では GUI アプリとしてリンクしてあるので、既定ではコンソールが無く
// printf は捨てられる（黒いウィンドウを出さないため）。
//   ・出力がすでにファイル等へ繋がっているなら何もしない（リダイレクト）
//   ・端末から起動されたならその端末へ出す（新しい窓は開かない）
//   ・それも無く wantConsole なら、新しくコンソールを開く（-console / -h）
// Windows 以外は元から標準出力があるので何もしない。
void SetupConsole(bool wantConsole) {
#ifdef _WIN32
	{
		const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		if (h != NULL && h != INVALID_HANDLE_VALUE) return;
	}
	if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
		if (!wantConsole) return;
		if (!AllocConsole()) return;
	}
	FILE *f = 0;
	freopen_s(&f, "CONOUT$", "w", stdout);
	freopen_s(&f, "CONOUT$", "w", stderr);
	// 日本語が化けないよう、コンソール側も UTF-8 にする。
	SetConsoleOutputCP(CP_UTF8);
#else
	(void)wantConsole;
#endif
}

// コンソールを出す指定があるか。設定を読む前に見たいので、ここだけ先に走らせる。
bool WantsConsole(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-console") == 0) return true;
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0) return true;
	}
	return false;
}

// ユーザーフォルダの名前。Windows なら %APPDATA%\mxv2\ になる。
// 設定 (mxv2.ini) と、ユーザーが足したスキンの置き場所。
const char *kUserDirName = "mxv2";

// コマンドライン専用の指定。永続化する設定は Settings が持つ。
struct Options {
	std::string target;  // MDX ファイルかディレクトリ。空ならカレント（ref 可）
	std::vector<std::string> pdxSearchDirs;  // -pdxpath (複数指定可)
	std::string assetsDir;
	std::string userDir;
	// 表示を遅らせる時間 (ms)。指定が無ければ音の遅れに自動で合わせる。
	int latencyMs;
	bool latencySet;
	bool quitOnEnd;
	// 出力サンプリングレート。0 なら設定 (ini) の値を使う。
	int sampleRate;

	Options()
	    : latencyMs(0), latencySet(false), quitOnEnd(true), sampleRate(0) {}
};

// 演奏位置の移動幅。, / . が普通、Shift 付きの < / > が高速。
const uint32_t kSeekStepMs = 3 * 1000;
const uint32_t kSeekFastStepMs = 30 * 1000;

// キーとマウスの操作一覧。-h の出力と [操作方法] ダイアログで同じものを
// 使うので、文面はここだけに置く。書式指定 (% や %s) は入れないこと。
const char *kKeyHelpText =
	    "キー操作:\n"
	    "  ESC / Q         終了\n"
	    "  SPACE           一時停止 / 再開\n"
	    "  F               フェードアウト\n"
	    "  ENTER           ファイラーの項目を開く\n"
	    "  BACKSPACE       親フォルダへ（ルートではファイルシステムの選択へ）\n"
	    "  \\               ファイルシステムのルートへ\n"
	    "  L               フォルダを選んで移動\n"
	    "  UP/DOWN         カーソル移動\n"
	    "  PGUP/PGDN       カーソル移動（ページ単位）\n"
	    "  HOME/END        カーソル移動（最初/最後）\n"
	    "  N / B           次 / 前の MDX を演奏\n"
	    "  C               自動で次の曲へ (CONT)\n"
	    "  R               自動で繰り返す (REPEAT)\n"
	    "  TAB             ファイラーの文字サイズ\n"
	    "  1-8             FM チャンネル (ch.1-8) のマスク切り替え\n"
	    "  0               FM チャンネルの一括マスク\n"
	    "  Shift+1-8       PCM チャンネル (ch.P-W) のマスク切り替え\n"
	    "  Shift+0         PCM チャンネルの一括マスク\n"
	    "  Ctrl+0          全チャンネルの一括マスク\n"
	    "  - / +(;)        音量を変更 （マスター音量は[mxv の設定]で）\n"
	    "  , / .           演奏位置を移動\n"
	    "  < / >           演奏位置を高速移動\n"
	    "  F1              [mxv の設定]ダイアログを開く\n"
	    "  F2              [配色設定]ダイアログを開く\n"
	    "  F3              [ファイルシステムの設定]ダイアログを開く\n"
	    "  F11 / H         [操作方法]ダイアログを開く\n"
	    "  F12 / A         [バージョン情報]ダイアログを開く\n"
	    "マウス操作:\n"
	    "  バナー          メニュー表示（右クリックでも出る）\n"
	    "  ファイラー      ファイル/ディレクトリを選択、ダブルクリックで開く\n"
	    "  鍵盤            そのチャンネルのマスクを切り替え"
	    "（PCM は横 8 等分で ch.P-W）\n"
	    "  ステータス欄    FM / PCM のマスクを一括で切り替え\n"
	    "  PREV            前の曲へ移動\n"
	    "  STOP            演奏を停止\n"
	    "  PLAY            演奏を開始\n"
	    "  FAST            演奏を早送り\n"
	    "  PAUSE           演奏を一時停止/解除\n"
	    "  NEXT            次の曲へ移動\n"
	    "  CONT            自動で次の曲へ\n"
	    "  REPEAT          自動で繰り返す\n"
	    "  音量バー        音量を変更 （マスター音量は[mxv の設定]で）\n"
	    "  プログレスバー  演奏位置を移動\n"
	    "  ドロップ        MDX を落とすとその場所へ移って演奏"
	    "（フォルダなら移動だけ）\n";

void PrintUsage(const char *argv0) {
	printf("%s", AppHeader().c_str());
	printf(
	    "usage:\n"
	    "  %s [options] [<mdxfile> | <dir>]\n"
	    "options:\n"
	    "  -zoom <percent> 表示倍率 %% (100 でドット等倍。既定はシステムの拡大率)\n"
	    "  -loops <n>      自動フェードアウトまでのループ数 (既定 2)\n"
	    "  -nofade         自動フェードアウトしない\n"
	    "  -rate <hz>      出力サンプリングレート (44100 / 48000%s。既定 %d)\n"
	    "  -latency <ms>   表示を遅らせる時間 ms (この起動だけ。ふつうは設定で)\n"
	    "  -pdxpath <dir>  PDX の追加探索先\n"
	    "  -assets <dir>   同梱素材の場所 (既定: 実行ファイルの隣の assets)\n"
	    "  -userdir <dir>  設定とユーザー素材の場所 (既定: OS のユーザーフォルダ)\n"
	    "  -skin <name>    スキン名 (assets:<name> で同梱ぶんを名指し。既定: Default)\n"
	    "  -folderfirst    ファイラーでフォルダを先に並べる\n"
	    "  -noquit         演奏終了後も閉じない\n"
	    "  -console        ログを出すコンソールを開く (既定は開かない)\n",
	    argv0, mxv2::Player::kSupports96kHz ? " / 96000" : "",
	    mxv2::Player::kDefaultSampleRate);
	printf("%s", kKeyHelpText);
}

// 素材と設定の置き場所を決めるオプションだけ先に見る。mxv2.ini はここで
// 決まったユーザーフォルダから読むので、ParseArgs より前に要る
// （-console と同じ理由）。
void PrescanDirs(int argc, char **argv, Options *opt) {
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "-assets") == 0) {
			opt->assetsDir = argv[++i];
		} else if (strcmp(argv[i], "-userdir") == 0) {
			opt->userDir = argv[++i];
		}
	}
}

// ini に書かれた順でファイルシステムをマウントする。仕様 (filesystem.md) の
// とおり、知らないものは警告して捨て、削除できないものが抜けていれば足す。
// 直したところがあれば true を返す（読み終えてから書き戻すため）。
bool LoadFileSystems(mxv2::Vfs *vfs, const std::vector<std::string> &refs) {
	bool fixed = false;
	vfs->ClearMounts();
	for (size_t i = 0; i < refs.size(); i++) {
		mxv2::FileSystem *fs = 0;
		std::string rel;
		if (!vfs->Parse(refs[i], &fs, &rel) || fs == 0) {
			printf("warning  : 知らないファイルシステムなので削除しました: %s\n",
			       refs[i].c_str());
			fixed = true;
			continue;
		}
		if (!vfs->Mount(fs)) fixed = true;  // 同じものが二重に書かれていた
	}
	if (vfs->EnsureRequired()) fixed = true;
	return fixed;
}

// 今のマウント順を ini に書く形へ。
std::vector<std::string> SaveFileSystems(const mxv2::Vfs &vfs) {
	std::vector<std::string> out;
	for (int i = 0; i < vfs.count(); i++) {
		out.push_back(std::string(vfs.at(i)->id()) + ":");
	}
	return out;
}

// 音まわりのログ 1 行。起動時と、出力レートを変えて開き直したときに出す。
void PrintAudioInfo(const mxv2::Player &player, bool latencyAuto) {
	printf("audio    : %d Hz / buffer %d frames / 表示の遅らせ %d frames (%.1f ms)%s\n",
	       player.sampleRate(), player.audioBufferFrames(), player.displayLatencyFrames(),
	       player.displayLatencyFrames() * 1000.0f / player.sampleRate(),
	       latencyAuto ? " [自動]" : "");
	fflush(stdout);
}

// 旧い版は実行ファイルの隣に mxv2.ini を置いていた。ユーザーフォルダ側が
// まだ無ければ、そこから 1 度だけ引き取る（元は残す）。
void MigrateLegacySettings(const std::string &newPath) {
	if (mxv2::FileExists(newPath)) return;

	const std::string oldPath = mxv2::JoinPath(mxv2::ExecutableDir(), "mxv2.ini");
	std::vector<uint8_t> data;
	if (!mxv2::FileExists(oldPath) || !mxv2::ReadWholeFile(oldPath, &data)) return;
	if (!mxv2::WriteWholeFile(newPath, data)) return;
	printf("settings : %s を引き継ぎました\n", oldPath.c_str());
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
		} else if (strcmp(a, "-rate") == 0 && i + 1 < argc) {
			// 出力サンプリングレート。x68sound が持っているフィルタ表で
			// 決まるので、対応していない値はここで弾く（そのまま渡すと
			// 黙って 22050 に落とされる）。ini には残さない。
			opt->sampleRate = atoi(argv[++i]);
			if (!mxv2::Player::IsSupportedSampleRate(opt->sampleRate)) {
				printf("ERROR: 対応していないサンプリングレートです: %d\n",
				       opt->sampleRate);
				return false;
			}
		} else if (strcmp(a, "-latency") == 0 && i + 1 < argc) {
			// 桁を間違えても画面が止まったきりにならないよう、常識的な幅で頭打ち。
			opt->latencyMs = atoi(argv[++i]);
			if (opt->latencyMs > 10000) opt->latencyMs = 10000;
			if (opt->latencyMs < -10000) opt->latencyMs = -10000;
			opt->latencySet = true;
		} else if (strcmp(a, "-pdxpath") == 0 && i + 1 < argc) {
			opt->pdxSearchDirs.push_back(argv[++i]);
		} else if (strcmp(a, "-assets") == 0 && i + 1 < argc) {
			// 実際の処理は PrescanDirs（設定を読む前に要る）。ここでは受け流す。
			i++;
		} else if (strcmp(a, "-userdir") == 0 && i + 1 < argc) {
			i++;  // 同上
		} else if (strcmp(a, "-skin") == 0 && i + 1 < argc) {
			st->skinName = argv[++i];
		} else if (strcmp(a, "-console") == 0) {
			// 実際の処理は main の先頭 (SetupConsole)。ここでは受け流すだけ。
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
	const mxv2::Vfs *vfs;
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
// 入力は裸のパスでも ref でもよいので、ここで ref へ揃える。
std::vector<std::string> PdxSearchDirs(const mxv2::Vfs &vfs, const Options &opt,
                                       const mxv2::Settings &st) {
	std::vector<std::string> in = opt.pdxSearchDirs;
	if (!st.pdxPath.empty()) in.push_back(st.pdxPath);

	std::vector<std::string> dirs;
	for (size_t i = 0; i < in.size(); i++) {
		std::string ref;
		if (!vfs.Resolve(in[i], std::string(), &ref) || ref.empty()) {
			printf("warning  : PDX の探索先を読めません: %s\n", in[i].c_str());
			continue;
		}
		dirs.push_back(ref);
	}
	return dirs;
}

// 1 曲読み込んで演奏を始める。path は ref。
bool PlayPath(const std::string &path, const mxv2::Vfs &vfs, const Options &opt,
              const mxv2::Settings &st, mxv2::Player *player, mxv2::DrawScreen *draw,
              mxv2::Visualizer *visualizer, mxv2::Screen *screen) {
	mxv2::MdxSong song;
	std::string err;
	if (!mxv2::LoadMdxSong(vfs, path, PdxSearchDirs(vfs, opt, st), &song, &err)) {
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
	const bool ok = PlayPath(path, *ctx.vfs, *ctx.opt, *ctx.settings, ctx.player, ctx.draw,
	                         ctx.visualizer, ctx.screen);
	*ctx.playing = ok;
	if (ok) *ctx.currentPath = path;
	*ctx.endSeen = false;
	*ctx.chromeRefresh = true;
	*ctx.fileListRefresh = true;
}

// ファイラーのカーソルを開く。曲なら演奏、フォルダやファイルシステムなら移動、
// "[Setting]" ならファイルシステムの設定ダイアログ。
// キー (ENTER)・マウス・コンテキストメニューの 3 か所から同じ手順を通す。
void OpenCursor(const PlayContext &ctx, mxv2::Filer *filer, mxv2::SettingsUi *ui) {
	std::string path;
	switch (filer->Open(&path)) {
		case mxv2::kFilerOpenPlay:
			StartPlay(ctx, path);
			break;
		case mxv2::kFilerOpenMoved:
			*ctx.fileListRefresh = true;
			break;
		case mxv2::kFilerOpenSettings:
			ui->OpenFileSystems();
			break;
		default:
			break;
	}
}

// ドラッグ＆ドロップで落とされたものを開く。落とし物はネイティブのパスなので、
// 行き先は必ずローカルファイルシステムになる。
//   MDX      … そのファイルのあるフォルダへ移ってから演奏（コマンドラインで
//               MDX を渡したときと同じ）
//   フォルダ … そこへ移動するだけ
//   それ以外 … 何もしない
void OpenDropped(const PlayContext &ctx, mxv2::Filer *filer, const std::string &nativePath) {
	std::string ref;
	if (!ctx.vfs->Resolve(nativePath, std::string(), &ref) || ref.empty()) {
		printf("warning  : 場所を読み取れません: %s\n", nativePath.c_str());
		return;
	}

	if (ctx.vfs->IsDir(ref)) {
		filer->SetCurrentRef(ref);
		*ctx.fileListRefresh = true;
		return;
	}

	// 拡張子だけでなく中身も見る。拡張子を付け替えただけのファイルを
	// 落とされても演奏を始めないため。
	if (!mxv2::IsMdxFile(*ctx.vfs, ref)) {
		printf("warning  : MDX ファイルではありません: %s\n", nativePath.c_str());
		return;
	}

	filer->SetCurrentRef(ctx.vfs->Parent(ref));
	filer->SelectByPath(ref);
	StartPlay(ctx, ref);
}

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

	mxv2::AssetPaths paths;
	paths.bundledDir = opt.assetsDir.empty()
	                       ? mxv2::JoinPath(mxv2::ExecutableDir(), "assets")
	                       : opt.assetsDir;
	paths.userDir = opt.userDir.empty() ? mxv2::UserDataDir(kUserDirName) : opt.userDir;
	if (!mxv2::MakeDirectories(paths.userDir)) {
		printf("warning  : ユーザーフォルダを作れません: %s\n", paths.userDir.c_str());
	}
	// 設定 -> コマンドラインの順に読む（後勝ち）。
	mxv2::Settings settings;
	const std::string settingsPath = mxv2::Settings::PathIn(paths.userDir);
	MigrateLegacySettings(settingsPath);
	settings.Load(settingsPath);
	// ini に倍率が無かったかどうかを覚えておく（終了時に書き残すため）。
	const int legacyScale = settings.legacyScale;

	if (!ParseArgs(argc, argv, &opt, &settings)) {
		PrintUsage(argc > 0 ? argv[0] : "mxv2");
		return EXIT_FAILURE;
	}

	// 使い方を出すだけのときに邪魔をしないよう、ここまで来てから出す。
	printf("assets   : %s\n", paths.bundledDir.c_str());
	printf("userdir  : %s\n", paths.userDir.c_str());

	// ファイルシステム。同梱アセット・ユーザーフォルダ・ローカルの 3 つを
	// 用意する。場所の指定はここから先すべて ref（vfs.h）。
	mxv2::Vfs vfs;
	vfs.Configure(paths.bundledDir, paths.userDir);
	// ファイラーのルートに並べる順は ini から。読めなかったぶんや足りない
	// ぶんは LoadFileSystems が補うので、そのときは書き戻す。
	bool dirtyFileSystems = LoadFileSystems(&vfs, settings.fileSystems);
	settings.fileSystems = SaveFileSystems(vfs);
	// ユーザーフォルダ側の mdx/ は無ければ作る（曲の置き場所として見せる）。
	{
		const mxv2::FileSystem *userFs = vfs.FindById("userdir");
		if (userFs != 0 && !userFs->nativeRoot().empty() &&
		    !mxv2::MakeDirectories(userFs->nativeRoot())) {
			printf("warning  : %s を作れません\n", userFs->nativeRoot().c_str());
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
			printf("ERROR: 場所を読み取れません: %s\n", opt.target.c_str());
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
			for (int i = 0; i < 64 && !ref.empty(); i++) {
				if (vfs.IsDir(ref)) break;
				ref = vfs.Parent(ref);
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

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		printf("ERROR: SDL_Init に失敗しました: %s\n", SDL_GetError());
		return EXIT_FAILURE;
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

	// うまくいかないときの逃げ場。同梱ぶんは必ずあるはずなので名指しする。
	const std::string kFallbackSkin = mxv2::MakeBundledSkinRef("Default");

	// スキンが画面サイズを決めるので、ウィンドウより先に読む。
	// 指定のスキンが無ければ同梱の Default へ落ちる（ini に書かれたスキンの
	// フォルダをユーザーが消しても起動できるように）。
	mxv2::Skin skin;
	{
		std::string err;
		if (!skin.Load(paths, settings.skinName, &err)) {
			printf("warning  : %s\n", err.c_str());
			settings.skinName = kFallbackSkin;
			dirtyFields |= mxv2::Settings::kFieldSkin;
			if (!skin.Load(paths, settings.skinName, &err)) {
				printf("ERROR: %s\n", err.c_str());
				printf("       -assets <dir> で同梱素材の場所を指定してください。\n");
				SDL_Quit();
				return EXIT_FAILURE;
			}
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

	// ファイラーと曲名の文字は、キャンバスとは別に出力解像度で描いて重ねる。
	mxv2::TextLayer textLayer;
	{
		std::string err;
		if (!textLayer.Init(&screen, mxv2::FontSearchDirs(skin, paths), &err)) {
			printf("warning  : %s\n", err.c_str());
		} else if (!textLayer.available()) {
			printf("warning  : 日本語フォントが読めません。assets/ に "
			       "MPLUS1p-Regular.ttf があるか確認してください"
			       "（font.ttf をユーザーフォルダかスキンに置けば"
			       "そちらが使われます）。\n");
		}
	}

	mxv2::DrawScreen draw;
	{
		std::string err;
		draw.SetTextLayer(&textLayer);
		if (!draw.Init(&skin, &err)) {
			// 素材の足りないスキンでも起動できなくならないよう、同梱の
			// Default へ逃がす（layout.ini を書かずに theme.mxv だけ置いた
			// ユーザースキンなど）。
			printf("warning  : スキン %s を使えません: %s\n", settings.skinName.c_str(),
			       err.c_str());
			const bool retry = (settings.skinName != kFallbackSkin) &&
			                   skin.Load(paths, kFallbackSkin, &err);
			if (retry) {
				settings.skinName = kFallbackSkin;
				dirtyFields |= mxv2::Settings::kFieldSkin;
				screen.Resize(skin.screenW, skin.screenH, &err);
				textLayer.SetFontDirs(mxv2::FontSearchDirs(skin, paths));
				textLayer.Rebuild(&screen, &err);
			}
			if (!retry || !draw.Init(&skin, &err)) {
				printf("ERROR: %s\n", err.c_str());
				printf("       -assets <dir> で同梱素材の場所を、-skin <name> でスキンを"
				       "指定してください。\n");
				screen.Close();
				SDL_Quit();
				return EXIT_FAILURE;
			}
		}
		draw.SetFileListFontSize(settings.fileListFontSize);
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
			printf("warning  : %s\n", err.c_str());
		}
		ui.SetVfs(&vfs);
		ui.SetAboutHeader(AppHeader());
		ui.SetHelpText(kKeyHelpText);
	}

	mxv2::Filer filer;
	filer.SetVfs(&vfs);
	filer.SetFolderFirst(settings.folderFirst);
	filer.SetViewMetrics(draw.fileListRows(), draw.fileListItemH());
	filer.SetCurrentRef(startDir);
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
	ctx.playing = &playing;
	ctx.currentPath = &currentPath;
	ctx.endSeen = &endSeen;
	ctx.chromeRefresh = &chromeRefresh;
	ctx.fileListRefresh = &fileListRefresh;

	if (!startFile.empty()) StartPlay(ctx, startFile);

	const bool quitWhenDone = opt.quitOnEnd && !startFile.empty();

	// ドラッグ＆ドロップ。SDL は落とされたもの 1 つにつき 1 イベント送って
	// くるので、まとめて落とされたときは最初の 1 つだけを覚えておき、
	// 一区切り (DROPCOMPLETE) してから開く。
	std::string dropPath;
	bool dropSeen = false;
	bool dropGroup = false;

	while (!quit) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			ui.ProcessEvent(ev);

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
				case mxv2::kMouseRequestOpenCursor:
					OpenCursor(ctx, &filer, &ui);
					break;
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
					ui.OpenColors();
					break;
				case SDLK_F3:
					ui.OpenFileSystems();
					break;
				case SDLK_F11:
				case SDLK_h:
					ui.OpenHelp();
					break;
				case SDLK_F12:
				case SDLK_a:
					ui.OpenAbout();
					break;

				// 演奏位置の移動。Shift 付き (< >) は大きく飛ぶ。
				// 「,」「.」と「<」「>」は配列によって同じキーコードで届いたり
				// 別のキーコードで届いたりするので、両方を受ける。
				case SDLK_COMMA:
				case SDLK_PERIOD:
				case SDLK_LESS:
				case SDLK_GREATER: {
					const bool back = (key == SDLK_COMMA || key == SDLK_LESS);
					const bool fast = (key == SDLK_LESS || key == SDLK_GREATER ||
					                   (ev.key.keysym.mod & KMOD_SHIFT) != 0);
					const uint32_t step = fast ? kSeekFastStepMs : kSeekStepMs;
					const uint32_t now = player.nowTimeMs();
					uint32_t want = 0;
					if (back) {
						want = (now > step) ? (now - step) : 0;
					} else {
						want = now + step;
						const uint32_t total = player.playTimeMs();
						if (total != 0 && want > total) want = total;
					}
					player.SeekMs(want);
					break;
				}

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
				case SDLK_KP_ENTER:
					OpenCursor(ctx, &filer, &ui);
					break;
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
					ui.OpenFolder(filer.currentRef());
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
				// JP 配列の「+」は Shift+「;」なので、SDL には SDLK_SEMICOLON で
				// 届く（US 配列の「+」は Shift+「=」で SDLK_EQUALS）。
				// 「;」そのものは他に割り当てが無いので、修飾なしでも受ける。
				case SDLK_SEMICOLON:
					player.SetMainVolume(player.mainVolume() + kVolumeKeyStep);
					break;

				// チャンネルの一括マスク。旧 mxv は Ctrl+0 / Alt+0 / Ctrl+Alt+0。
				case SDLK_0:
					if (ev.key.keysym.mod & KMOD_CTRL) {
						player.ToggleChannelGroup(mxv2::Player::kChannelMaskAll);
					} else if (ev.key.keysym.mod & KMOD_SHIFT) {
						player.ToggleChannelGroup(mxv2::Player::kChannelMaskPcm);
					} else {
						player.ToggleChannelGroup(mxv2::Player::kChannelMaskFm);
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

		// 落とされたものを開く。イベントを汲み終えてからにするのは、
		// まとめて落とされたときに最初のアイテムで決めるため。
		if (dropSeen && !dropGroup) {
			const std::string path = dropPath;
			dropSeen = false;
			dropPath.clear();
			OpenDropped(ctx, &filer, path);
		}

		// 設定ウィンドウでスキンが選ばれていたら、ここで作り直す。
		// 画面サイズごと変わりうるので、UI の中ではやらない。
		if (!ui.pendingSkin().empty()) {
			const std::string name = ui.pendingSkin();
			ui.ClearPendingSkin();

			mxv2::Skin next;
			std::string err;
			if (!next.Load(paths, name, &err)) {
				printf("warning  : スキン %s を読めません: %s\n", name.c_str(), err.c_str());
			} else {
				const mxv2::Skin prev = skin;
				skin = next;

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
					printf("warning  : スキン %s に切り替えられません: %s\n", name.c_str(),
					       err.c_str());
					skin = prev;
					screen.Resize(skin.screenW, skin.screenH, &err);
					textLayer.SetFontDirs(mxv2::FontSearchDirs(skin, paths));
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
					printf("warning  : %d Hz で開けません: %s\n", want, err.c_str());
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
						StartPlay(ctx, keep);
						if (atMs != 0) player.SeekMs(atMs);
						if (wasPaused) player.Pause();
						// 曲の掛け直しと空回しでマスクが消えるので入れ直す。
						player.SetChannelMask(mask);
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

		mouse.Poll(SDL_GetTicks());

		// 設定 UI はここで組み立てる。配色を変えると 640x480 の
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
		if (settings.lastDir != filer.currentRef()) {
			settings.lastDir = filer.currentRef();
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
		// [ファイルシステムの設定] は Vfs のマウント一覧を直に触るので、
		// 書き戻す前にそこから拾い直す。
		if (newDirt & mxv2::Settings::kFieldFileSystems) {
			settings.fileSystems = SaveFileSystems(vfs);
		}

		// コンテキストメニューからの要求。演奏の開始・曲送り・終了は
		// メインループが状態を持っているのでここで実行する。
		switch (ui.TakeRequest()) {
			case mxv2::SettingsUi::kRequestOpenCursor:
				OpenCursor(ctx, &filer, &ui);
				break;
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
		if (settings.lastDir != filer.currentRef()) {
			settings.lastDir = filer.currentRef();
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
