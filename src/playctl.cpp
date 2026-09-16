// mxv2 - 曲の進行（main.cpp から切り出し）

#include "playctl.h"

#include <cstdio>

#include <SDL.h>

#include "drawscreen.h"
#include "filer.h"
#include "fileutil.h"
#include "mdxsong.h"
#include "message.h"
#include "nowplaying.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "settingsui.h"
#include "songloader.h"
#include "vfs.h"
#include "visualizer.h"

namespace mxv2 {
namespace app {

namespace {

// アンダーラン（音の途切れ）を知らせる間隔 (ms)。まとめて 1 行にする。
const uint32_t kUnderrunReportMs = 5000;

// PDX の探索先。-pdxpath の指定と設定の一覧を合わせたもの（この順）。
// 入力は裸のパスでも ref でもよいので、ここで ref へ揃える。
std::vector<std::string> PdxSearchDirs(const mxv2::Vfs &vfs, const Options &opt,
                                       const mxv2::Settings &st) {
	std::vector<std::string> in = opt.pdxSearchDirs;
	in.insert(in.end(), st.pdxPaths.begin(), st.pdxPaths.end());

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

}  // namespace

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

// ファイラーの文字サイズ（小 / 大）を切り替える。TAB キーと、ファイラーの
// 長押し（キーボードの無い端末向け）の 2 か所から同じ手順を通す。
void ToggleFileListFontSize(mxv2::DrawScreen *draw, mxv2::Filer *filer, bool *fileListRefresh) {
	draw->SetFileListFontSize(draw->fileListFontSize() ^ 1);
	filer->SetViewMetrics(draw->fileListRows(), draw->fileListItemH());
	*fileListRefresh = true;
}

// ファイラーのカーソルを開く。曲なら演奏、フォルダやファイルシステムなら移動、
// "[Setting]" ならファイルシステムの設定ダイアログ（"Bookmarks>" の中なら
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
		case mxv2::kFilerOpenNeedsAccess:
			// SAF の許可が失われている。OS のピッカーを直に出して取り直させる
			// （出せなければ設定ダイアログをその行で開く）。
			ui->RegrantAccess(path);
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

}  // namespace app
}  // namespace mxv2
