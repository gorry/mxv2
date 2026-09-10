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

#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>

#include "androidassets.h"
#endif

#include "assetpath.h"
#include "drawscreen.h"
#include "fileutil.h"
#include "filer.h"
#include "mdxsong.h"
#include "message.h"
#include "mouse.h"
#include "nowplaying.h"
#include "orientlock.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "settingsui.h"
#include "skin.h"
#include "songloader.h"
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

// アンダーラン（音の途切れ）を知らせる間隔 (ms)。まとめて 1 行にする。
const uint32_t kUnderrunReportMs = 5000;

// バックグラウンド（描かないとき）に回る間隔 (ms)。演奏そのものはオーディオ
// 装置とデコードスレッドが進めるので、ここでやるのは曲送りと通知の更新だけ。
// 曲の終わりに気付くのがこの間隔ぶん遅れうるので、あまり長くはしない。
const int kBackgroundTickMs = 100;

#ifdef __ANDROID__
// 標準出力を logcat へ流す番人。Android のアプリは標準出力がどこにも
// 繋がっていないので、そのままでは printf が消えてしまう。パイプに
// 差し替えて、こちらの端を読んだぶんだけ logcat へ渡す。
//
// **SDL_Log は使わないこと。** SDL の既定のログ出力は logcat へ書いたあと
// stderr にも同じものを書くので、stderr までパイプに差し替えていると
// 「読んだものをまた書く」の輪ができて延々と回り続ける。ここでは
// stdout だけを差し替え、書き出しも __android_log_write を直に呼ぶ。
int LogcatPumpThread(void *data) {
	const int fd = (int)(intptr_t)data;
	std::string line;
	char buf[512];
	for (;;) {
		const ssize_t n = read(fd, buf, sizeof(buf));
		if (n <= 0) break;
		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] == '\n') {
				__android_log_write(ANDROID_LOG_INFO, "mxv2", line.c_str());
				line.clear();
			} else if (buf[i] != '\r') {
				line += buf[i];
			}
		}
		// 行の途中で溜め込みすぎないよう、長すぎるものはそこで出す。
		if (line.size() >= 1024) {
			__android_log_write(ANDROID_LOG_INFO, "mxv2", line.c_str());
			line.clear();
		}
	}
	if (!line.empty()) __android_log_write(ANDROID_LOG_INFO, "mxv2", line.c_str());
	return 0;
}
#endif

// 標準出力の行き先を用意する。
//
// Windows では GUI アプリとしてリンクしてあるので、既定ではコンソールが無く
// printf は捨てられる（黒いウィンドウを出さないため）。
//   ・出力がすでにファイル等へ繋がっているなら何もしない（リダイレクト）
//   ・端末から起動されたならその端末へ出す（新しい窓は開かない）
//   ・それも無く wantConsole なら、新しくコンソールを開く（-console / -h）
// Android は標準出力が捨てられるので、パイプ経由で logcat へ流す
// （logcat -s mxv2 で読める。SDL 自身のログは SDL/APP など別のタグに出る）。
// それ以外は元から標準出力があるので何もしない。
void SetupConsole(bool wantConsole) {
#if defined(__ANDROID__)
	(void)wantConsole;
	int fds[2];
	if (pipe(fds) != 0) return;
	// stderr は差し替えない（SDL のログがそこへ二重に出るため。上の注記）。
	if (dup2(fds[1], STDOUT_FILENO) < 0) return;
	close(fds[1]);
	SDL_Thread *th = SDL_CreateThread(LogcatPumpThread, "logcat", (void *)(intptr_t)fds[0]);
	// スレッドが作れなくても動きはする（ログが出ないだけ）。
	if (th != 0) SDL_DetachThread(th);
#elif defined(_WIN32)
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

// 起動時の警告。ウィンドウが開く前に起きたことは、ログに出しても
// 気付かれないので、ためておいて最初のフレームでダイアログに出す。
// 演奏中に出る警告（PDX が無い、など）はログだけ。あちらは操作の結果として
// その場で出るものなので、起動時の箱には入れない。
typedef std::vector<std::string> Warnings;

void Warn(Warnings *box, const std::string &text) {
	printf("warning  : %s\n", text.c_str());
	fflush(stdout);
	if (box != 0) box->push_back(text);
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
	std::string locale;  // 文言の言語。空なら既定 (ja-JP)
	// 表示を遅らせる時間 (ms)。指定が無ければ音の遅れに自動で合わせる。
	int latencyMs;
	bool latencySet;
	// 演奏し終えたら mxv2 ごと終わる（-quit）。**デバッグ用**で、既定は false。
	// ふつうに MDX を渡して起動したときは、鳴らし終えても閉じずに、その
	// ファイルのあるフォルダを開いたまま残る。
	bool quitOnEnd;
	// 出力サンプリングレート。0 なら設定 (ini) の値を使う。
	int sampleRate;

	// ---- 画面の向きでスキンを切り替える（screen_orientation.md） --------
	// -orient <0|1>。-1 なら指定なしで、プラットフォームごとの既定
	// （Android は ON、それ以外は OFF）になる。
	int orient;
	// -orientlock <0|1|2>。スキンを選ぶための向きを固定する**デバッグ用**。
	// 0 なら実物（デスクトップではダミー）を見る。回転の制御には効かない。
	int orientLock;
	// -skin が指定されたか。指定されていたら縦横切り替えは OFF にする
	// （名指しされたスキンを勝手に差し替えない）。
	bool skinSet;

	Options()
	    : latencyMs(0),
	      latencySet(false),
	      quitOnEnd(false),
	      sampleRate(0),
	      orient(-1),
	      orientLock(0),
	      skinSet(false) {}
};

// 演奏位置の移動幅。, / . が普通、Shift 付きの < / > が高速。
const uint32_t kSeekStepMs = 3 * 1000;
const uint32_t kSeekFastStepMs = 30 * 1000;

// 「名前」と「説明」の 2 段組を 1 行出す。桁は全角を 2 と数えて揃える
// （同梱フォントの都合で、ダイアログ側も同じ数え方をしている）。
void PrintRow(const std::string &name, const std::string &desc, int width) {
	std::string pad;
	for (int i = mxv2::MsgDisplayWidth(name); i < width; i++) pad += ' ';
	printf("  %s%s %s\n", name.c_str(), pad.c_str(), desc.c_str());
}

// 一覧（[UsageOptions] [HelpKeys] [HelpMouse]）を 2 段組で出す。
// 名前の桁は一番広いものに合わせる。
void PrintRows(const char *section, int width, const std::string &a0 = std::string(),
               const std::string &a1 = std::string()) {
	const std::vector<mxv2::MsgRow> &rows = mxv2::MsgList(section);
	for (size_t i = 0; i < rows.size(); i++) {
		PrintRow(rows[i].key, mxv2::MsgFill(rows[i].value, a0, a1), width);
	}
}

// 一覧の中で一番広い名前（桁揃えの幅）。
int RowsWidth(const char *section) {
	const std::vector<mxv2::MsgRow> &rows = mxv2::MsgList(section);
	int w = 0;
	for (size_t i = 0; i < rows.size(); i++) {
		const int n = mxv2::MsgDisplayWidth(rows[i].key);
		if (n > w) w = n;
	}
	return w;
}

void PrintUsage(const char *argv0) {
	printf("%s", AppHeader().c_str());
	printf("usage:\n  %s [options] [<mdxfile> | <dir>]\noptions:\n", argv0);
	PrintRows("UsageOptions", RowsWidth("UsageOptions"),
	          mxv2::Player::kSupports96kHz ? " / 96000" : "",
	          mxv2::MsgNum("%d", mxv2::Player::kDefaultSampleRate));

	// キー・マウスの一覧。ダイアログ ([操作方法]) と同じものを出す。
	// 桁は両方まとめて揃える（std::max は windows.h の max マクロと
	// ぶつかるので使わない）。
	int width = RowsWidth("HelpKeys");
	const int mouseWidth = RowsWidth("HelpMouse");
	if (mouseWidth > width) width = mouseWidth;
	printf("%s\n", mxv2::Msg("Help.Keys"));
	PrintRows("HelpKeys", width);
	printf("%s\n", mxv2::Msg("Help.Mouse"));
	PrintRows("HelpMouse", width);
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
		} else if (strcmp(argv[i], "-locale") == 0) {
			opt->locale = argv[++i];
		}
	}
}

// ini に書かれた順でファイルシステムをマウントする。仕様 (filesystem.md) の
// とおり、知らないものは警告して捨て、削除できないものが抜けていれば足す。
// 直したところがあれば true を返す（読み終えてから書き戻すため）。
bool LoadFileSystems(mxv2::Vfs *vfs, const std::vector<std::string> &refs,
                     Warnings *box) {
	bool fixed = false;
	vfs->ClearMounts();
	for (size_t i = 0; i < refs.size(); i++) {
		mxv2::FileSystem *fs = 0;

		// 場所を持つもの（フォルダマウントや外部 FS）は、ここで作って預ける。
		// 同じ場所が二重に書かれていたら、先に作ったほうを使う。
		mxv2::FileSystem *made = vfs->CreateFromMountRef(refs[i]);
		if (made != 0) {
			if (vfs->Add(made)) {
				fs = made;
			} else {
				fs = vfs->FindByMountRef(made->mountRef());
				delete made;
				fixed = true;
			}
		} else {
			std::string rel;
			if (!vfs->Parse(refs[i], &fs, &rel)) fs = 0;
		}

		if (fs == 0) {
			Warn(box, mxv2::MsgF("Log.UnknownFileSystem", refs[i]));
			fixed = true;
			continue;
		}
		if (!vfs->Mount(fs)) fixed = true;  // 同じものが二重に書かれていた
	}
	if (vfs->EnsureRequired()) fixed = true;
	return fixed;
}

// ini に書かれたブックマークを ref へ揃える。仕様 (bookmark.md) のとおり、
// 知らないファイルシステムは警告して捨てる。到達できるかどうかはここでは
// 見ない（時間が掛かるし、外付けが外れているだけかもしれない）。
// 直したところがあれば true を返す（読み終えてから書き戻すため）。
bool LoadBookmarks(const mxv2::Vfs &vfs, std::vector<std::string> *refs,
                   Warnings *box) {
	bool fixed = false;
	std::vector<std::string> out;
	for (size_t i = 0; i < refs->size(); i++) {
		mxv2::FileSystem *fs = 0;
		std::string rel;
		if (!vfs.Parse((*refs)[i], &fs, &rel)) {
			Warn(box, mxv2::MsgF("Log.BookmarkUnknownFs", (*refs)[i]));
			fixed = true;
			continue;
		}
		// ファイルシステムの選択そのもの（空の ref）と、ブックマークの
		// 一覧 (bookmark:) 自身は控えられない。
		if (fs == 0 || fs->isJumpList()) {
			fixed = true;
			continue;
		}
		const std::string ref = mxv2::Vfs::MakeRef(fs, rel);
		if (ref != (*refs)[i]) fixed = true;  // 書き方を揃えた
		out.push_back(ref);
	}
	if ((int)out.size() > mxv2::Settings::kMaxBookmarks) {
		out.resize(mxv2::Settings::kMaxBookmarks);
		fixed = true;
	}
	*refs = out;
	return fixed;
}

// 今のマウント順を ini に書く形へ。
std::vector<std::string> SaveFileSystems(const mxv2::Vfs &vfs) {
	std::vector<std::string> out;
	for (int i = 0; i < vfs.count(); i++) {
		// 場所を持つ外部ファイルシステムは "<id>:<場所>" を返す。
		out.push_back(vfs.at(i)->mountRef());
	}
	return out;
}

// 音まわりのログ 1 行。起動時と、出力レートを変えて開き直したときに出す。
void PrintAudioInfo(const mxv2::Player &player, bool latencyAuto) {
	// カタログの値は前後の空白が落ちるので、区切りはこちらで足す。
	std::string line =
	    mxv2::MsgF("Log.Audio", mxv2::MsgNum("%d", player.sampleRate()),
	               mxv2::MsgNum("%d", player.audioBufferFrames()),
	               mxv2::MsgNum("%d", player.displayLatencyFrames()),
	               mxv2::MsgNum("%.1f", player.displayLatencyFrames() * 1000.0 /
	                                        player.sampleRate()));
	if (latencyAuto) line += std::string(" ") + mxv2::Msg("Log.AudioAuto");
	printf("audio    : %s\n", line.c_str());
	// どの口で鳴らしているか。Android は AAudio と OpenSL ES で音の
	// 途切れやすさが変わるので、切り分けに要る。
	{
		const char *driver = SDL_GetCurrentAudioDriver();
		if (driver != 0) printf("audiodrv : %s\n", driver);
	}
	fflush(stdout);
}

// 前の版が mxv2.ini を置いていた場所から、1 度だけ引き取る（元は残す）。
// 引き取り先が既にあれば何もしない。心当たりは 2 つ:
//   ・実行ファイルの隣（デスクトップの旧い版）
//   ・Android の内部ストレージ（外から見えないので、ユーザーフォルダを
//     外部へ移した 2026-08-31 より前の版）
void MigrateLegacySettings(const std::string &newPath) {
	if (mxv2::FileExists(newPath)) return;

	std::vector<std::string> olds;
	olds.push_back(mxv2::JoinPath(mxv2::ExecutableDir(), "mxv2.ini"));
	{
		const std::string legacy = mxv2::LegacyUserDataDir(kUserDirName);
		if (!legacy.empty()) olds.push_back(mxv2::JoinPath(legacy, "mxv2.ini"));
	}

	for (size_t i = 0; i < olds.size(); i++) {
		const std::string &oldPath = olds[i];
		if (mxv2::DirNameOf(oldPath) == mxv2::DirNameOf(newPath)) continue;
		std::vector<uint8_t> data;
		if (!mxv2::FileExists(oldPath) || !mxv2::ReadWholeFile(oldPath, &data)) continue;
		if (!mxv2::WriteWholeFile(newPath, data)) continue;
		printf("settings : %s\n", mxv2::MsgF("Log.SettingsMigrated", oldPath).c_str());
		return;
	}
}

// mxv2.ini から読んだ設定を、コマンドラインで上書きする。
bool ParseArgs(int argc, char **argv, Options *opt, mxv2::Settings *st) {
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-') {
			if (!opt->target.empty()) {
				printf("ERROR: %s\n", mxv2::MsgF("Error.TargetTwice", a).c_str());
				return false;
			}
			opt->target = a;
			continue;
		}
		if (strcmp(a, "-nofade") == 0) {
			st->fadeout = false;
		} else if (strcmp(a, "-quit") == 0) {
			opt->quitOnEnd = true;
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
				printf("ERROR: %s\n",
				       mxv2::MsgF("Error.BadSampleRate",
				                  mxv2::MsgNum("%d", opt->sampleRate))
				           .c_str());
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
		} else if (strcmp(a, "-locale") == 0 && i + 1 < argc) {
			i++;  // 同上
		} else if (strcmp(a, "-skin") == 0 && i + 1 < argc) {
			st->skinName = argv[++i];
			opt->skinSet = true;
		} else if (strcmp(a, "-orient") == 0 && i + 1 < argc) {
			opt->orient = atoi(argv[++i]);
			if (opt->orient != 0 && opt->orient != 1) {
				printf("ERROR: %s\n",
				       mxv2::MsgF("Error.BadOption", a, argv[i]).c_str());
				return false;
			}
		} else if (strcmp(a, "-orientlock") == 0 && i + 1 < argc) {
			opt->orientLock = atoi(argv[++i]);
			if (opt->orientLock < 0 || opt->orientLock > 2) {
				printf("ERROR: %s\n",
				       mxv2::MsgF("Error.BadOption", a, argv[i]).c_str());
				return false;
			}
		} else if (strcmp(a, "-console") == 0) {
			// 実際の処理は main の先頭 (SetupConsole)。ここでは受け流すだけ。
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "-help") == 0) {
			return false;
		} else {
			printf("ERROR: %s\n", mxv2::MsgF("Error.UnknownOption", a).c_str());
			return false;
		}
	}
	if (st->loops < 1) st->loops = 1;
	return true;
}

// 読み込み中の曲。**読み終わるまで前の曲はそのまま鳴っている**ので、
// ここには「届いたら何をするか」だけを置く。
struct SongLoad {
	bool active;
	std::string path;        // 読んでいる曲の ref
	uint32_t startTicks;     // 頼んだ時刻 (SDL_GetTicks)
	bool noticeShown;        // 「読み込み中」を曲名の位置に出したか
	std::string prevTitle;   // 出す前の曲名（読めなかったときに戻す）

	// 読み終わってから掛け直すもの。出力レートを変えたときだけ使う
	// （同期だった頃は StartPlay の直後に呼んでいた）。
	int seekMs;
	bool pause;
	uint32_t channelMask;
	bool hasChannelMask;

	SongLoad()
	    : active(false),
	      startTicks(0),
	      noticeShown(false),
	      seekMs(0),
	      pause(false),
	      channelMask(0),
	      hasChannelMask(false) {}
};

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
	mxv2::SongLoader *loader;

	bool *playing;
	std::string *currentPath;
	bool *endSeen;
	bool *chromeRefresh;
	bool *fileListRefresh;
	SongLoad *load;
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
			printf("warning  : %s\n",
			       mxv2::MsgF("Log.PdxPathUnreadable", in[i]).c_str());
			continue;
		}
		dirs.push_back(ref);
	}
	return dirs;
}

// 「読み込み中」を曲名の位置に出すまでの待ち時間 (ms)。ローカルの曲は
// 一瞬で届くので、すぐ出すとちらつくだけになる。
const uint32_t kSongNoticeDelayMs = 250;

// 1 曲読み込んで演奏を始める。path は ref。
// **読むのは別スレッド**なので、ここは依頼を出すだけ。実際に音が変わるのは
// PollSong が結果を受け取ったとき。それまでは前の曲がそのまま鳴っている。
void StartPlayResume(const PlayContext &ctx, const std::string &path,
                     int seekMs, bool pause, uint32_t channelMask, bool hasChannelMask) {
	SongLoad *ld = ctx.load;
	ld->active = true;
	ld->path = path;
	ld->startTicks = SDL_GetTicks();
	ld->noticeShown = false;
	ld->prevTitle.clear();
	ld->seekMs = seekMs;
	ld->pause = pause;
	ld->channelMask = channelMask;
	ld->hasChannelMask = hasChannelMask;

	ctx.loader->Start(ctx.vfs, path, PdxSearchDirs(*ctx.vfs, *ctx.opt, *ctx.settings));
	*ctx.endSeen = false;
}

void StartPlay(const PlayContext &ctx, const std::string &path) {
	StartPlayResume(ctx, path, 0, false, 0, false);
}

// 読み終わった曲を演奏へ渡す。毎フレーム呼ぶこと。何か起きたら true。
bool PollSong(const PlayContext &ctx) {
	SongLoad *ld = ctx.load;

	mxv2::SongLoader::Result r;
	if (ctx.loader->Take(&r)) {
		// 世代番号で古いものは捨てられているが、念のため行き先も見る。
		if (ld->active && r.ref == ld->path) {
			ld->active = false;
			const std::string prevTitle = ld->prevTitle;
			const bool restoreTitle = ld->noticeShown;
			ld->noticeShown = false;

			std::string err;
			if (!r.ok) {
				printf("ERROR: %s\n", r.err.c_str());
				fflush(stdout);
				if (restoreTitle) ctx.draw->PutMDXTitle(prevTitle);
				*ctx.playing = false;
				*ctx.chromeRefresh = true;
				*ctx.fileListRefresh = true;
				return true;
			}
			if (!ctx.player->PlaySong(r.song, &err)) {
				printf("ERROR: %s\n", err.c_str());
				fflush(stdout);
				if (restoreTitle) ctx.draw->PutMDXTitle(prevTitle);
				*ctx.playing = false;
				*ctx.chromeRefresh = true;
				*ctx.fileListRefresh = true;
				return true;
			}

			ctx.visualizer->Reset();
			ctx.draw->Reload();
			ctx.draw->PutMDXTitle(r.song.title);
			if (ctx.screen != 0) {
				ctx.screen->SetTitle(r.song.title.empty()
				                         ? std::string("mxv2")
				                         : ("mxv2 - " + r.song.title));
			}

			// 出力レートを変えたときの掛け直し。曲を掛け直すとマスクが
			// 消えるので、ここで入れ直す。
			if (ld->seekMs != 0) ctx.player->SeekMs(ld->seekMs);
			if (ld->pause) ctx.player->Pause();
			if (ld->hasChannelMask) ctx.player->SetChannelMask(ld->channelMask);

			printf("play     : %s\n", r.song.path.c_str());
			printf("title    : %s\n", r.song.title.c_str());
			if (r.song.requiresPdx && !r.song.hasPdx) {
				printf("warning  : %s\n",
				       mxv2::MsgF("Log.PdxNotFound", r.song.pdxFileName).c_str());
			}
			printf("duration : %.1f sec\n", ctx.player->playTimeMs() / 1000.0f);
			// MSVC の setvbuf は _IOLBF を全バッファ扱いにするので、明示的に流す。
			fflush(stdout);

			*ctx.playing = true;
			*ctx.currentPath = r.ref;
			*ctx.endSeen = false;
			*ctx.chromeRefresh = true;
			*ctx.fileListRefresh = true;
			return true;
		}
	}

	// 手間取っているときだけ、曲名の位置で知らせる。
	if (ld->active && !ld->noticeShown &&
	    (SDL_GetTicks() - ld->startTicks) >= kSongNoticeDelayMs) {
		ld->prevTitle = ctx.draw->mdxTitle();
		ctx.draw->PutMDXTitle(mxv2::Msg("Player.Loading"));
		ld->noticeShown = true;
		return true;
	}
	return false;
}

// ファイラーのカーソルを開く。曲なら演奏、フォルダやファイルシステムなら移動、
// "[Setting]" ならファイルシステムの設定ダイアログ（"BookMark>" の中なら
// ブックマークの設定）、ブックマークの行ならその場所へ移る。
// キー (ENTER)・マウス・右へのスワイプから同じ手順を通す。
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
		case mxv2::kFilerOpenBookmark:
			// 控え直し（ファイルを指していたとき）が要るので UI 側に任せる。
			ui->OpenBookmarkRef(path);
			break;
		case mxv2::kFilerOpenBookmarkSettings:
			ui->OpenBookmarks();
			break;
		default:
			break;
	}
}

// 画面をまるごと描き直させる。GL コンテキストが失われたあと
// (SDL_RENDER_DEVICE_RESET / TARGETS_RESET) と、バックグラウンドから戻った
// ときに呼ぶ。テクスチャは中身だけでなく**器ごと**無効になっているので
// 作り直し、mxv2 は差分更新なので**「もう描いた」印まで戻す**。
void ForceRedrawAll(mxv2::Screen *screen, mxv2::TextLayer *textLayer, mxv2::DrawScreen *draw,
                    mxv2::Player *player, mxv2::SettingsUi *ui, bool *chromeRefresh,
                    bool *fileListRefresh) {
	std::string err;
	if (!screen->ResetTextures(&err)) printf("warning  : %s\n", err.c_str());
	if (!textLayer->Rebuild(screen, &err)) printf("warning  : %s\n", err.c_str());
	ui->HandleDeviceReset();
	draw->Reload();
	player->RequestStatusRefresh();
	*chromeRefresh = true;
	*fileListRefresh = true;
}

// ---- 画面の向きでスキンを切り替える（screen_orientation.md） --------------

// 切り替えかたから、いま使うべき向きを決める。「起動時の方向で切り替える」と
// 「常に切り替える」は、渡された今の向きをそのまま使う。
mxv2::Screen::Orientation OrientationForMode(int mode, mxv2::Screen::Orientation now) {
	if (mode == mxv2::Settings::kOrientPortraitOnly) return mxv2::Screen::kPortrait;
	if (mode == mxv2::Settings::kOrientLandscapeOnly) return mxv2::Screen::kLandscape;
	return now;
}

// 端末そのものの向きを、切り替えかたに合わせて固定する（Android だけ。
// それ以外では orientlock が何もしない）。
//
// **固定しても上下反転は許す**（SENSOR_PORTRAIT / SENSOR_LANDSCAPE）。
// 「常に切り替える」なら自由に回してよい。
void ApplyOrientationMode(int mode, mxv2::Screen::Orientation now) {
	switch (mode) {
		case mxv2::Settings::kOrientPortraitOnly:
			mxv2::orientlock::Set(mxv2::orientlock::kPortrait);
			break;
		case mxv2::Settings::kOrientLandscapeOnly:
			mxv2::orientlock::Set(mxv2::orientlock::kLandscape);
			break;
		case mxv2::Settings::kOrientStartup:
			// 起動した（か、設定を閉じた）ときの向きで固定する。
			mxv2::orientlock::Set((now == mxv2::Screen::kPortrait)
			                          ? mxv2::orientlock::kPortrait
			                          : mxv2::orientlock::kLandscape);
			break;
		default:
			mxv2::orientlock::Set(mxv2::orientlock::kFree);
			break;
	}
}

// 窓の大きさに合わせてキャンバスを作り直す（fullscreen.md）。
//
// スキンは「宣言サイズ」でレイアウトを持っているが、窓の縦横比がそれと
// 違うときは、ファイラーを置いた向きだけ伸ばして窓に寄せる。伸ばせる量は
// 背景ビットマップの大きさで頭打ちになり、あふれたぶんは今までどおり
// レターボックスになる。
//
// 呼ぶのは**フレームの頭で 1 回だけ**。窓のリサイズはイベントで印を立てる
// だけにして、ここでまとめて作り直す。これがそのままリサイズ中のデバウンスに
// なる（ドラッグ中に何十回イベントが来ても、作り直すのは 1 フレームに 1 回）。
// 大きさが変わっていなければ何もしない。作り直したら true。
bool SyncCanvasToWindow(mxv2::Screen *screen, mxv2::TextLayer *textLayer,
                        mxv2::DrawScreen *draw, mxv2::Filer *filer, const mxv2::Skin &skin) {
	int outW = 0, outH = 0;
	screen->GetOutputSize(&outW, &outH);
	int cw = 0, ch = 0;
	skin.CanvasSizeFor(outW, outH, draw->stretchLimit(), &cw, &ch);
	if (cw == screen->width() && ch == screen->height()) return false;

	// 順番が大事: 画面 -> 文字レイヤー -> DrawScreen。
	// DrawScreen::Resize は最後に Reload() まで済ませて曲名を描き直すので、
	// その前にレイヤーを作り直しておく。
	std::string err;
	if (!screen->SetCanvasSize(cw, ch, &err)) {
		printf("warning  : %s\n", err.c_str());
		return false;
	}
	textLayer->SyncToScreen(screen);
	if (!draw->Resize(cw, ch, &err)) {
		printf("warning  : %s\n", err.c_str());
		return false;
	}
	filer->SetViewMetrics(draw->fileListRows(), draw->fileListItemH());
	return true;
}

// 演奏が終わったら CONT / REPEAT に従って次の曲へ送る。
//
// **バックグラウンド（描かないとき）でも呼ぶ**ので、描画とは切り離してある。
// frame は表示位置 (Player::visualFrame)。終わってすぐには送らず、余韻
// (lingerFrames) のぶん鳴らしきってから次へ行く。
void PollSongEnd(const PlayContext &ctx, mxv2::Filer *filer, uint64_t frame,
                 uint64_t lingerFrames, bool autoNext, bool autoRepeat, bool quitWhenDone,
                 uint64_t *endFrame, bool *quit) {
	// 読み込み中は前の曲が鳴り続けているので、その終わりで次へ送らない。
	if (!*ctx.playing || ctx.load->active || !ctx.player->playTerminated()) return;

	if (!*ctx.endSeen) {
		*ctx.endSeen = true;
		*endFrame = frame;
		return;
	}
	if (frame <= *endFrame + lingerFrames) return;

	*ctx.endSeen = false;
	std::string path;
	if (autoRepeat && !ctx.currentPath->empty()) {
		StartPlay(ctx, *ctx.currentPath);
	} else if (autoNext && filer->NextMdx(&path)) {
		StartPlay(ctx, path);
	} else if (quitWhenDone) {
		*quit = true;
	} else {
		// 終わったら停止状態にする。[■] を押したときとまったく同じ扱いで、
		// PLAY の LED が消え、鍵盤も消え、PLAY TIME は 00:00 へ戻る。
		//
		// **Player も止めること。** ここの印 (*ctx.playing) を下ろすだけだと
		// `player.playing()` が true のまま残り、画面はいつまでも鳴っている
		// ように見える（Visualizer::UpdateChrome が LED を点け続ける）。
		// 通知 (UpdateNowPlaying) も同じ理由で消えなくなる。
		*ctx.playing = false;
		ctx.player->Stop();
	}
}

// 演奏状態の通知（Android）。画面を見ていないときの唯一の窓口になるので、
// バックグラウンドでも毎回呼ぶ。中身が変わらなければ何も起きない。
void UpdateNowPlaying(const mxv2::Player &player, const std::string &currentPath, bool playing,
                      bool autoNext, bool autoRepeat) {
	// Windows などでは何もしない。文言を組み立てる前に抜ける。
	if (!mxv2::nowplaying::Available()) return;

	mxv2::nowplaying::State st;
	// player.playing() は「曲を持っている」（[■] で止めると false）、
	// playing は「終わりまで行っていない」。どちらか欠けたら通知は消す。
	st.active = playing && player.playing();
	if (st.active) {
		st.playing = !player.paused();
		st.title = player.song().title;
		if (st.title.empty()) st.title = mxv2::BaseNameOf(currentPath);
		if (st.title.empty()) st.title = mxv2::Msg("Notify.NoTitle");

		st.text = mxv2::Msg(st.playing ? "Notify.Playing" : "Notify.Paused");
		// CONT / REPEAT は画面が見えないところでも効くので、通知に出す。
		if (autoNext) {
			st.text += "  ";
			st.text += mxv2::Msg("Notify.Cont");
		}
		if (autoRepeat) {
			st.text += "  ";
			st.text += mxv2::Msg("Notify.Repeat");
		}

		st.posMs = player.nowTimeMs();
		st.durMs = player.playTimeMs();
	}
	mxv2::nowplaying::Update(st);
}

// 音が途切れた（デコードが間に合わず無音を差し込んだ）ことを知らせる。
// **終了時のまとめだけでは、長く鳴らしっぱなしにするとき——バックグラウンド
// 演奏——に気付けない**ので、増えたぶんをときどき出す。
void PollUnderruns(const mxv2::Player &player, uint32_t *last, uint32_t *nextMs) {
	const uint32_t now = player.underruns();
	// 曲が変わると数え直しになる。
	if (now < *last) *last = 0;
	if (now == *last) return;

	const uint32_t ticks = SDL_GetTicks();
	if (*nextMs != 0 && (int32_t)(ticks - *nextMs) < 0) return;
	printf("warning  : audio underrun x%u\n", now - *last);
	fflush(stdout);
	*last = now;
	*nextMs = ticks + kUnderrunReportMs;
}

// 通知（Android）のボタンや、他のアプリ・ヘッドホンの都合で届いた要求。
// 画面を見ていないときの唯一の操作手段なので、**バックグラウンドでも回す**。
//
// pausedByFocus は「他のアプリに音を譲って止めた」印。返してもらったときに
// 自動で再開するのはこの印が立っているときだけで、自分で止めていた曲を
// 勝手に鳴らし始めることはない。
void PollNotifyRequests(const PlayContext &ctx, mxv2::Filer *filer, bool *pausedByFocus) {
	mxv2::Player *player = ctx.player;
	for (;;) {
		const mxv2::nowplaying::Request req = mxv2::nowplaying::TakeRequest();
		if (req == mxv2::nowplaying::kRequestNone) break;

		std::string path;
		switch (req) {
			case mxv2::nowplaying::kRequestPlay:
				*pausedByFocus = false;
				if (player->paused()) player->Resume();
				break;
			case mxv2::nowplaying::kRequestPause:
				*pausedByFocus = false;
				if (!player->paused()) player->Pause();
				break;
			case mxv2::nowplaying::kRequestPrev:
				if (filer->PrevMdx(&path)) StartPlay(ctx, path);
				break;
			case mxv2::nowplaying::kRequestNext:
				if (filer->NextMdx(&path)) StartPlay(ctx, path);
				break;
			case mxv2::nowplaying::kRequestStop:
				player->Stop();
				break;
			case mxv2::nowplaying::kRequestFocusLost:
				// 鳴っていたときだけ印を付ける。
				if (player->playing() && !player->paused()) {
					player->Pause();
					*pausedByFocus = true;
				}
				break;
			case mxv2::nowplaying::kRequestFocusGained:
				if (*pausedByFocus) {
					*pausedByFocus = false;
					player->Resume();
				}
				break;
			default:
				break;
		}
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
		printf("warning  : %s\n", mxv2::MsgF("Log.DropUnreadable", nativePath).c_str());
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
		printf("warning  : %s\n", mxv2::MsgF("Log.DropNotMdx", nativePath).c_str());
		return;
	}

	filer->SetCurrentRef(ctx.vfs->Parent(ref));
	filer->SelectByPath(ref);
	StartPlay(ctx, ref);
}

}  // namespace

// 演奏状態の通知（Android）に出す文言をカタログから渡す。起動時と、
// 設定ウィンドウで言語を替えたときに呼ぶ（Java 側は文言を持っていない）。
void SetNotifyLabels() {
	mxv2::nowplaying::Labels labels;
	labels.channel = mxv2::Msg("Notify.Channel");
	labels.channelDesc = mxv2::Msg("Notify.ChannelDesc");
	labels.prev = mxv2::Msg("Notify.Prev");
	labels.play = mxv2::Msg("Notify.Play");
	labels.pause = mxv2::Msg("Notify.Pause");
	labels.next = mxv2::Msg("Notify.Next");
	labels.stop = mxv2::Msg("Notify.Stop");
	mxv2::nowplaying::SetLabels(labels);
}

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
	mxv2::ExtractBundledAssets(mxv2::ExecutableDir(), &warnings);
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
	// ファイラーの "BookMark>" に並ぶのは Settings::bookmarks そのもの。
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

	// うまくいかないときの逃げ場。同梱ぶんは必ずあるはずなので名指しする。
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
	// 指定のスキンが無ければ同梱の Default へ落ちる（ini に書かれたスキンの
	// フォルダをユーザーが消しても起動できるように）。
	// **落ちたことは ini に書き戻さない**（縦横切り替えのときは特に、
	// スキンを置き直せば次の起動で元の名前へ戻ってほしいため）。
	mxv2::Skin skin;
	{
		std::string err;
		if (!skin.Load(paths, wantSkin, &err)) {
			Warn(&warnings, err);
			if (!orientEnabled) {
				settings.skinName = kFallbackSkin;
				dirtyFields |= mxv2::Settings::kFieldSkin;
			}
			if (!skin.Load(paths, kFallbackSkin, &err)) {
				printf("ERROR: %s\n", err.c_str());
				printf("       %s\n", mxv2::Msg("Error.HintAssets"));
				SDL_Quit();
				return EXIT_FAILURE;
			}
		}
	}

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
		// 前回の大きさに戻す。**縮める方向には戻さない**（表示倍率が
		// 100% を割るとキャンバスが潰れるので、宣言サイズ x 表示倍率を
		// 下限にする）。伸びたぶんはファイラーの行数として戻ってくる。
		// **窓の大きさを決められるプラットフォームだけ**（Android では窓＝画面で、
		// SDL_SetWindowSize を呼ぶと SDL 側の記録だけがずれる。Screen の
		// CanResizeWindow のコメント）。
		if (mxv2::Screen::CanResizeWindow() && settings.savePosition &&
		    settings.windowW > 0 && settings.windowH > 0) {
			const int minW = skin.screenW * settings.zoomPercent / 100;
			const int minH = skin.screenH * settings.zoomPercent / 100;
			const int w = (settings.windowW > minW) ? settings.windowW : minW;
			const int h = (settings.windowH > minH) ? settings.windowH : minH;
			SDL_SetWindowSize(screen.window(), w, h);
		}
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
			// 素材の足りないスキンでも起動できなくならないよう、同梱の
			// Default へ逃がす（layout.ini を書かずに theme.mxv だけ置いた
			// ユーザースキンなど）。
			Warn(&warnings, mxv2::MsgF("Log.SkinUnusable", settings.skinName, err));
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
				printf("       %s\n", mxv2::Msg("Error.HintAssetsSkin"));
				screen.Close();
				SDL_Quit();
				return EXIT_FAILURE;
			}
		}
		draw.SetFileListFontSize(settings.fileListFontSize);
		draw.SetFileListScroll(settings.fileListScroll);
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

	// 演奏状態の通知（Android）に出す文言。Java 側には文言を持たせず、
	// message.ini から引いたものを渡す。
	{
		mxv2::nowplaying::Labels labels;
		labels.channel = mxv2::Msg("Notify.Channel");
		labels.channelDesc = mxv2::Msg("Notify.ChannelDesc");
		labels.prev = mxv2::Msg("Notify.Prev");
		labels.play = mxv2::Msg("Notify.Play");
		labels.pause = mxv2::Msg("Notify.Pause");
		labels.next = mxv2::Msg("Notify.Next");
		labels.stop = mxv2::Msg("Notify.Stop");
		mxv2::nowplaying::SetLabels(labels);
	}

	if (!startFile.empty()) StartPlay(ctx, startFile);

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

			mxv2::Skin next;
			std::string err;
			if (!next.Load(paths, name, &err)) {
				printf("warning  : %s\n",
				       mxv2::MsgF("Log.SkinUnreadable", name, err).c_str());
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
					printf("warning  : %s\n",
					       mxv2::MsgF("Log.SkinSwitchFailed", name, err).c_str());
					skin = prev;
					screen.Resize(skin.screenW, skin.screenH, &err);
					textLayer.SetFontDirs(mxv2::FontSearchDirs(skin, paths));
					textLayer.Rebuild(&screen, &err);
					draw.Init(&skin, &err);
				} else if (!orientEnabled) {
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
			if (isMouse && ui.wantCaptureMouse()) continue;
			// ダイアログが開いている間はアプリ側でキーを扱わない。
			// F1/F2 は開くだけなので通す必要はなく、閉じるのは上の ESC。
			if (isKey && ui.wantCaptureKeyboard()) continue;

			// マウス。イベントは窓の画素で届くので、キャンバスの論理座標へ
			// 直してから配る（当たり判定はすべて論理座標）。
			SDL_Event mev = ev;
			screen.WindowEventToCanvas(&mev);
			switch (mouse.Handle(mev)) {
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
				case mxv2::kMouseRequestGoParent:
					// ファイラーで左へはじいた。BACKSPACE と同じ。
					filer.GoParent();
					fileListRefresh = true;
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
				// 終了は必ず確認してから。押し間違いで演奏が止まるのを防ぐ
				// （ウィンドウの × とコンテキストメニューの [終了] は、
				// 意図してそこを選んでいるので確認しない）。
				case SDLK_ESCAPE:
				case SDLK_q:
					ui.OpenQuitConfirm();
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
				case SDLK_F4:
					ui.OpenBookmarks();
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

				// Android の戻るキー。ダイアログ（上で処理済み）→ 親フォルダ
				// → 終了の確認、の順に効く。ESC のようにいきなり閉じない。
				case SDLK_AC_BACK: {
					const std::string before = filer.currentRef();
					filer.GoParent();
					if (filer.currentRef() == before) {
						ui.OpenQuitConfirm();
					} else {
						fileListRefresh = true;
					}
					break;
				}
				case SDLK_BACKSLASH:
					filer.GoRoot();
					fileListRefresh = true;
					break;
				case SDLK_l:
					// フォルダを選ぶダイアログ。今の場所から出す。
					ui.OpenFolder(filer.currentRef());
					break;
				case SDLK_m:
					// Shift 付きはカレントフォルダの控え / 控え外し（確認あり）。
					// 素の M はファイラーの "BookMark>"（ジャンプ専用の一覧）。
					// 設定ダイアログは F4 だけ。
					if (ev.key.keysym.mod & KMOD_SHIFT) {
						ui.OpenBookmarkToggle();
					} else {
						ui.OpenBookmarkList();
					}
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

		mouse.Poll(SDL_GetTicks());

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
		// CONT / REPEAT はキー・マウス・メニューのどこからでも変わるので、
		// 切り替えた場所ごとに書くのではなく、ここで見比べて拾う
		// （文字の大きさや音量と同じ扱い）。
		if (settings.autoNext != autoNext || settings.autoRepeat != autoRepeat) {
			settings.autoNext = autoNext;
			settings.autoRepeat = autoRepeat;
			newDirt |= mxv2::Settings::kFieldContRepeat;
		}
		if (settings.savePosition) {
			int wx = 0, wy = 0, ww = 0, wh = 0;
			screen.GetWindowRect(&wx, &wy, &ww, &wh);
			if (wx != settings.windowX || wy != settings.windowY ||
			    ww != settings.windowW || wh != settings.windowH) {
				settings.windowX = wx;
				settings.windowY = wy;
				settings.windowW = ww;
				settings.windowH = wh;
				newDirt |= mxv2::Settings::kFieldWindowPos;
			}
		}
		ui.SetOrientationState(orientEnabled, orientNow);
		ui.Build(&settings, &draw, &player, &filer, &screen);
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
		// ブックマークが変わったら、"BookMark>" を開いていれば並べ直す。
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
		if (settings.autoNext != autoNext || settings.autoRepeat != autoRepeat) {
			settings.autoNext = autoNext;
			settings.autoRepeat = autoRepeat;
			dirtyFields |= mxv2::Settings::kFieldContRepeat;
		}
		if (settings.savePosition) {
			int wx = 0, wy = 0, ww = 0, wh = 0;
			screen.GetWindowRect(&wx, &wy, &ww, &wh);
			if (wx != settings.windowX || wy != settings.windowY ||
			    ww != settings.windowW || wh != settings.windowH) {
				settings.windowX = wx;
				settings.windowY = wy;
				settings.windowW = ww;
				settings.windowH = wh;
				dirtyFields |= mxv2::Settings::kFieldWindowPos;
			}
		}
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
