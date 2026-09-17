// mxv2 - コマンドラインと標準出力の支度（main.cpp から切り出し）
//
// 永続化する設定は Settings が持つ。ここにあるのは、その場かぎりの指定
// (Options) と、使い方の表示、コンソールの用意。

#ifndef MXV2_CMDLINE_H
#define MXV2_CMDLINE_H

#include <string>
#include <vector>

namespace mxv2 {

struct Settings;

namespace app {

// ユーザーフォルダの名前。Windows なら %APPDATA%\mxv2\ になる。
// 設定 (mxv2.ini) と、ユーザーが足したスキンの置き場所。
extern const char *const kUserDirName;

// アプリの名前・バージョン・ビルド日付・著作権表示（Usage の先頭と
// バージョン情報）。
std::string AppHeader();

// 標準出力の行き先を用意する（Windows のコンソール、Android の logcat）。
void SetupConsole(bool wantConsole);
// -console / -h があるか。設定を読む前に見るので ParseArgs とは別。
bool WantsConsole(int argc, char **argv);

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
	// -tutorial。見終えていてもチュートリアルを出す（**デバッグ用**。
	// 終わっても ini の Done は触らない）。
	bool tutorial;
	// -multi。多重起動の抑止をしない（**デバッグ用**）。既定では、すでに
	// 動いている mxv2 があれば開くものをそちらへ渡して終わる（旧 mxv と
	// 同じ。singleinstance.h）。2 つ並べて見比べたいときだけ外す。
	bool multiInstance;

	Options()
	    : latencyMs(0),
	      latencySet(false),
	      quitOnEnd(false),
	      sampleRate(0),
	      orient(-1),
	      orientLock(0),
	      skinSet(false),
	      tutorial(false),
	      multiInstance(false) {}
};

void PrintUsage(const char *argv0);
// 素材と設定の置き場所を決めるオプションだけ先に見る。
void PrescanDirs(int argc, char **argv, Options *opt);
// mxv2.ini から読んだ設定を、コマンドラインで上書きする。誤りや -h なら false。
bool ParseArgs(int argc, char **argv, Options *opt, mxv2::Settings *st);

}  // namespace app
}  // namespace mxv2

#endif  // MXV2_CMDLINE_H
