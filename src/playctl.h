// mxv2 - 曲の進行（main.cpp から切り出し）
//
// 曲の読み込みの依頼と受け取り、演奏終了時の送り、通知（Android）との
// やりとり。キーボード・マウス・自動送りのどれからでも同じ手順を通すため、
// 触るもの一式を PlayContext にまとめてある。

#ifndef MXV2_PLAYCTL_H
#define MXV2_PLAYCTL_H

#include <cstdint>
#include <string>
#include <vector>

#include "cmdline.h"

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
class Screen;
class SettingsUi;
class SongLoader;
class Vfs;
class Visualizer;
struct Settings;

namespace app {

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

// 1 曲読み込んで演奏を始める。path は ref。読むのは別スレッドなので、
// 実際に音が変わるのは PollSong が結果を受け取ったとき。
void StartPlay(const PlayContext &ctx, const std::string &path);
// 同じく。読み終わってから演奏位置・一時停止・チャンネルマスクを掛け直す
// （出力レートを変えたときの掛け直し用）。
void StartPlayResume(const PlayContext &ctx, const std::string &path, int seekMs, bool pause,
                     uint32_t channelMask, bool hasChannelMask);
// 読み終わった曲を演奏へ渡す。毎フレーム呼ぶこと。何か起きたら true。
bool PollSong(const PlayContext &ctx);

// ファイラーの文字サイズ（小 / 大）を切り替える。
void ToggleFileListFontSize(mxv2::DrawScreen *draw, mxv2::Filer *filer, bool *fileListRefresh);
// ファイラーのカーソルを開く（曲なら演奏、フォルダなら移動、など）。
void OpenCursor(const PlayContext &ctx, mxv2::Filer *filer, mxv2::SettingsUi *ui);
// ドラッグ＆ドロップで落とされたものを開く。
void OpenDropped(const PlayContext &ctx, mxv2::Filer *filer, const std::string &nativePath);

// 演奏が終わったら CONT / REPEAT に従って次の曲へ送る（バックグラウンドでも呼ぶ）。
void PollSongEnd(const PlayContext &ctx, mxv2::Filer *filer, uint64_t frame,
                 uint64_t lingerFrames, bool autoNext, bool autoRepeat, bool quitWhenDone,
                 uint64_t *endFrame, bool *quit);
// 演奏状態の通知（Android）を更新する。
void UpdateNowPlaying(const mxv2::Player &player, const std::string &currentPath, bool playing,
                      bool autoNext, bool autoRepeat);
// 通知のボタンや、他のアプリ・ヘッドホンの都合で届いた要求を処理する。
void PollNotifyRequests(const PlayContext &ctx, mxv2::Filer *filer, bool *pausedByFocus);
// 音が途切れたことをときどきログに出す。
void PollUnderruns(const mxv2::Player &player, uint32_t *last, uint32_t *nextMs);
// 通知に出す文言をカタログから渡す（起動時と、言語を替えたとき）。
void SetNotifyLabels();

}  // namespace app
}  // namespace mxv2

#endif  // MXV2_PLAYCTL_H
