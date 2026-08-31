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
	bool quitOnEnd;
	// 出力サンプリングレート。0 なら設定 (ini) の値を使う。
	int sampleRate;

	Options()
	    : latencyMs(0), latencySet(false), quitOnEnd(true), sampleRate(0) {}
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
		// ファイルシステムの選択そのもの（空の ref）は控えられない。
		if (fs == 0) {
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

	// 文言はここから先すべてカタログ (assets/locale/<ロケール>/message.ini)
	// から引く。知らないロケールを渡されたときは英語で代用する。
	// 1 つも読めなければキー名が出る（動きはする）。
	{
		bool usedFallback = false;
		if (!mxv2::LoadMessages(paths, opt.locale, &usedFallback)) {
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
	if (!mxv2::MakeDirectories(paths.userDir)) {
		Warn(&warnings, mxv2::MsgF("Log.UserDirFailed", paths.userDir));
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
	bool dirtyFileSystems = LoadFileSystems(&vfs, settings.fileSystems, &warnings);
	settings.fileSystems = SaveFileSystems(vfs);
	// ブックマークも同じく、読めない指定を捨てたら書き戻す。
	const bool dirtyBookmarks = LoadBookmarks(vfs, &settings.bookmarks, &warnings);
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

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
		printf("ERROR: %s\n", mxv2::MsgF("Error.SdlInit", SDL_GetError()).c_str());
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

	// スキンが画面サイズを決めるので、ウィンドウより先に読む。
	// 指定のスキンが無ければ同梱の Default へ落ちる（ini に書かれたスキンの
	// フォルダをユーザーが消しても起動できるように）。
	mxv2::Skin skin;
	{
		std::string err;
		if (!skin.Load(paths, settings.skinName, &err)) {
			Warn(&warnings, err);
			settings.skinName = kFallbackSkin;
			dirtyFields |= mxv2::Settings::kFieldSkin;
			if (!skin.Load(paths, settings.skinName, &err)) {
				printf("ERROR: %s\n", err.c_str());
				printf("       %s\n", mxv2::Msg("Error.HintAssets"));
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

	// 演奏終了時のふるまい
	bool autoNext = false;    // CONT
	bool autoRepeat = false;  // REPEAT

	bool quit = false;
	// 端末がバックグラウンドへ回した (Android)。描くのも音も止める。
	bool inBackground = false;
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

			// 端末のライフサイクル (Android)。バックグラウンドでは描かない
			// ——描き続けると OS に止められる——ので、印を立てて音も止める。
			if (ev.type == SDL_APP_WILLENTERBACKGROUND) {
				inBackground = true;
				player.SetAudioSuspended(true);
				continue;
			}
			if (ev.type == SDL_APP_DIDENTERFOREGROUND) {
				inBackground = false;
				player.SetAudioSuspended(false);
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
					if (ev.key.keysym.mod & KMOD_SHIFT) {
						ui.OpenBookmarkToggle();
					} else {
						ui.OpenBookmarks();
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

		// 設定ウィンドウでスキンが選ばれていたら、ここで作り直す。
		// 画面サイズごと変わりうるので、UI の中ではやらない。
		if (!ui.pendingSkin().empty()) {
			const std::string name = ui.pendingSkin();
			ui.ClearPendingSkin();

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

		// バックグラウンドでは 1 フレームも描かない。イベントが来るまで
		// 寝て待つ（描かないまま回すと、ただ CPU を焼くだけになる）。
		if (inBackground) {
			SDL_WaitEventTimeout(NULL, 200);
			continue;
		}

		mouse.Poll(SDL_GetTicks());

		// フォルダの中身も MDX のタイトルも曲そのものも別スレッドで
		// 読んでいる。届いたぶんをここで取り込む。
		if (filer.PollDir()) fileListRefresh = true;
		if (filer.PollTitles()) fileListRefresh = true;
		PollSong(ctx);

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
					printf("warning  : %s\n",
					       mxv2::MsgF("Log.SettingsSaveFailed", settingsPath).c_str());
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

		// 読み込み中は前の曲が鳴り続けているので、その終わりで次へ送らない。
		if (playing && !songLoad.active && player.playTerminated()) {
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
