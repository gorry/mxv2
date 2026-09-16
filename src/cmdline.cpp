// mxv2 - コマンドラインと標準出力の支度（main.cpp から切り出し）

#include "cmdline.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>
#endif

#include "appprofile.h"  // CMake が Profile.ini から生成する
#include "message.h"
#include "player.h"
#include "settings.h"

namespace mxv2 {
namespace app {

// ユーザーフォルダの名前。Windows なら %APPDATA%\mxv2\ になる。
// 設定 (mxv2.ini) と、ユーザーが足したスキンの置き場所。
const char *const kUserDirName = "mxv2";

namespace {

// アプリの名前・バージョン・著作権表示。Usage の先頭とバージョン情報。
// 値は mxv2/Profile.ini（著作者専用）からコピーします。CMake が configure の
// たびに src/appprofile.h.in → appprofile.h を生成し、ここはそれを写すだけ
// （ソースに値を刻まない。Windows の VERSIONINFO と Android の versionName も
// 同じ Profile.ini から出る）。
const char *kAppName = MXV2_APP_NAME;

const char *kAppVersion = MXV2_APP_VERSION;

const char *kAppCopyright = MXV2_APP_COPYRIGHT;

// 「名前」と「説明」の 2 段組を 1 行出す。桁は全角を 2 と数えて揃える
// （同梱フォントの都合で、ダイアログ側も同じ数え方をしている）。
void PrintRow(const std::string &name, const std::string &desc, int width) {
	std::string pad;
	for (int i = mxv2::MsgDisplayWidth(name); i < width; i++) pad += ' ';
	printf("  %s%s %s\n", name.c_str(), pad.c_str(), desc.c_str());
}

// 一覧（[UsageOptions] [HelpKeys] [HelpMouse] [HelpPad]）を 2 段組で出す。
// 名前の桁は一番広いものに合わせる。
void PrintRows(const char *section, int width, const std::string &a0 = std::string(),
               const std::string &a1 = std::string()) {
	const std::vector<mxv2::MsgRow> &rows = mxv2::MsgList(section);
	for (size_t i = 0; i < rows.size(); i++) {
		PrintRow(rows[i].key, mxv2::MsgFill(rows[i].value, a0, a1), width);
	}
}

}  // namespace

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

// ビルド日付はこのファイルをコンパイルした日付 (__DATE__)。
std::string AppHeader() {
	char buf[256];
	snprintf(buf, sizeof(buf), "%s  Version %s  (build %s)\n%s\n", kAppName, kAppVersion,
	         __DATE__, kAppCopyright);
	return buf;
}

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

void PrintUsage(const char *argv0) {
	printf("%s", AppHeader().c_str());
	printf("usage:\n  %s [options] [<mdxfile> | <dir>]\noptions:\n", argv0);
	PrintRows("UsageOptions", RowsWidth("UsageOptions"),
	          mxv2::Player::kSupports96kHz ? " / 96000" : "",
	          mxv2::MsgNum("%d", mxv2::Player::kDefaultSampleRate));

	// キー・マウス・ゲームパッドの一覧。ダイアログ ([操作方法]) と同じものを
	// 出す。桁は 3 つまとめて揃える（std::max は windows.h の max マクロと
	// ぶつかるので使わない）。
	int width = RowsWidth("HelpKeys");
	const int mouseWidth = RowsWidth("HelpMouse");
	const int padWidth = RowsWidth("HelpPad");
	if (mouseWidth > width) width = mouseWidth;
	if (padWidth > width) width = padWidth;
	printf("%s\n", mxv2::Msg("Help.Keys"));
	PrintRows("HelpKeys", width);
	printf("%s\n", mxv2::Msg("Help.Mouse"));
	PrintRows("HelpMouse", width);
	printf("%s\n", mxv2::Msg("Help.Pad"));
	PrintRows("HelpPad", width);
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
		} else if (strcmp(a, "-tutorial") == 0) {
			opt->tutorial = true;
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

}  // namespace app
}  // namespace mxv2
