// mxv2 - 曲の進行（main.cpp から切り出し）

#include "playctl.h"

#include <cstdio>

#include <SDL.h>

#include "cast.h"
#include "drawscreen.h"
#include "filer.h"
#include "fileutil.h"
#include "mdxsong.h"
#include "message.h"
#include "nowplaying.h"
#include "outputlatency.h"
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

// Bluetooth 側などの早送り・巻き戻しで飛ぶ幅 (ms)。長押しの間は繰り返し届く
// 機器もあるので、キーボードの , . (3 秒) より大きめ、< > (30 秒) より小さめ。
const uint32_t kRemoteSeekStepMs = 10 * 1000;

// アルバム欄に載せるフォルダの長さの上限（文字数）。
const size_t kAlbumMaxChars = 32;

// 通知に出す曲番号の控え。曲が替わったときに一覧から引き、見つかったら
// その曲の間は持ち続ける（ファイラーで別のフォルダへ移っても消さない。
// 車の画面の表示が途中で変わると、曲が替わったように見える相手がいる）。
struct TrackNumberMemo {
	std::string path;
	int number;
	int count;
};
TrackNumberMemo g_trackMemo = { std::string(), 0, 0 };

// UTF-8 の文字数。
size_t Utf8Length(const std::string &s) {
	size_t n = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (((unsigned char)s[i] & 0xc0) != 0x80) n++;
	}
	return n;
}

// MediaSession のアルバム欄（車や AV アンプの画面）に出す名前。
// 曲のあるフォルダから、kAlbumMaxChars 文字を超えない範囲で親フォルダを
// 遡って "/" でつなぐ（2026-09-27、ユーザーの指示）。"a/b/c/d.mdx" なら
// "a/b/c" が収まれば全部、超えれば a、b の順に削る。c だけで超えるときは
// c だけをそのまま使う。
// ファイルシステムの頭（"assets:" など）、ドライブ（"C:"）、SAF のツリーの
// URI の部分（":" や "%" を含む）は遡る先に含めない。
std::string FolderNameOf(const std::string &ref) {
	std::string path = ref;
	const size_t scheme = path.find(':');
	if (scheme != std::string::npos && scheme >= 2) path = path.substr(scheme + 1);

	std::vector<std::string> parts;
	std::string cur;
	for (size_t i = 0; i < path.size(); i++) {
		const char c = path[i];
		if (c == '/' || c == '\\') {
			parts.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	// cur は曲のファイル名なので捨てる。parts の最後が曲のあるフォルダ。

	std::string out;
	for (size_t k = parts.size(); k > 0; k--) {
		const std::string &name = parts[k - 1];
		if (name.empty() || name.find(':') != std::string::npos ||
		    name.find('%') != std::string::npos) {
			break;
		}
		if (out.empty()) {
			out = name;  // 曲のあるフォルダは長くてもそのまま
			continue;
		}
		const std::string next = name + "/" + out;
		if (Utf8Length(next) > kAlbumMaxChars) break;
		out = next;
	}
	return out;
}

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
#ifdef __APPLE__
				// Finder 起動ではログが見えないので、読めなかった理由を表示する。
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Msg("Error.MdxPlaybackTitle"),
				                         r.err.c_str(), ctx.screen ? ctx.screen->window() : 0);
#endif
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
				ctx.screen->SetTitle(WindowTitle(r.song.title));
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
//
// **優先順位は CONT > REPEAT**（旧 mxv の mxv.cpp、WM_TIMER / IDT_DISPLAY と
// 同じ。2026-09-18 にユーザーの指摘で直した。それまでは REPEAT を先に見ていて、
// 両方点けると CONT が効かなかった）。旧 mxv での 4 通りは:
//
//   CONT  REPEAT  動作
//   ----  ------  --------------------------------------------------
//   OFF   OFF     止まる
//   OFF   ON      同じ曲をもう一度
//   ON    OFF     次の曲へ。一覧の終わりまで来たら止まる
//   ON    ON      次の曲へ。**終わりまで来たら先頭へ戻ってもう一周**
//
// つまり REPEAT は「CONT が点いているときは一覧全体の繰り返し」を意味する。
// 先頭へ戻るところは旧 mxv と同じく「カーソルを 0（".." の行）に置いてから
// 次の MDX を探す」——つまり一覧の最初の曲から。
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
	if (autoNext) {
		// CONT が優先。まず次の曲を探す。
		if (filer->NextMdx(&path)) {
			StartPlay(ctx, path);
			return;
		}
		// 一覧の終わりまで来た。REPEAT も点いていれば先頭へ戻ってもう一周する。
		if (autoRepeat) {
			filer->SetCursor(0);
			if (filer->NextMdx(&path)) {
				StartPlay(ctx, path);
				return;
			}
		}
	} else if (autoRepeat && !ctx.currentPath->empty()) {
		// CONT が消えていれば、同じ曲をもう一度。
		StartPlay(ctx, *ctx.currentPath);
		return;
	}

	if (quitWhenDone) {
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
void UpdateNowPlaying(const mxv2::Player &player, const mxv2::Filer &filer,
                      const std::string &currentPath, bool playing, bool autoNext,
                      bool autoRepeat, const mxv2::Settings &settings) {
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

		st.artist = mxv2::BaseNameOf(currentPath);
		st.album = FolderNameOf(currentPath);
		// 設定 [Bluetooth（機器名）] の「アルバム名とアーティスト名を入れ替える」。
		// アルバム欄を出さない相手でもフォルダ名を見せたいとき。機器ごとに覚えて
		// いるので、いまつないでいる機器のものを使う（Bluetooth でなければ入れ替えない）。
		const std::string btName = mxv2::outputlatency::BluetoothName();
		if (!btName.empty() && settings.BluetoothDeviceFor(btName).swapArtistAlbum) {
			st.artist.swap(st.album);
		}

		if (g_trackMemo.path != currentPath) {
			g_trackMemo.path = currentPath;
			g_trackMemo.number = 0;
			g_trackMemo.count = 0;
		}
		// 読み込み中などで引けなければ、次のフレームで引き直す。
		if (g_trackMemo.number == 0) {
			filer.MdxNumberOf(currentPath, &g_trackMemo.number, &g_trackMemo.count);
		}
		st.trackNumber = g_trackMemo.number;
		st.trackCount = g_trackMemo.count;

		st.posMs = player.nowTimeMs();
		st.durMs = player.playTimeMs();
	}
	mxv2::nowplaying::Update(st);
}

// 通知（Android）のボタンや、他のアプリ・ヘッドホンの都合で届いた要求。
// 画面を見ていないときの唯一の操作手段なので、**バックグラウンドでも回す**。
//
// autoPause は「他のアプリに音を譲って止めた」「出力先が外れて止めた」の印。
// 都合が解けたときに自動で再開するのはこの印が立っているときだけで、自分で
// 止めていた曲を勝手に鳴らし始めることはない。
void PollNotifyRequests(const PlayContext &ctx, mxv2::Filer *filer, AutoPause *autoPause,
                        const mxv2::Settings &settings) {
	mxv2::Player *player = ctx.player;
	for (;;) {
		const mxv2::nowplaying::Request req = mxv2::nowplaying::TakeRequest();
		if (req == mxv2::nowplaying::kRequestNone) break;

		std::string path;
		switch (req) {
			case mxv2::nowplaying::kRequestPlay:
				*autoPause = kAutoPauseNone;
				if (player->playing()) {
					if (player->paused()) player->Resume();
					break;
				}
				// 止まっている（[■] のあと・起動してまだ演奏していない）。
				// Bluetooth 側の再生ボタン（車のハンドルなど）で始められるよう、
				// 最後の曲を掛け直す。無ければカーソルの MDX、それも無ければ
				// 一覧の次の MDX。画面の [▶] と違ってカーソルより最後の曲を
				// 先にするのは、画面を見ずに押すボタンだから。
				if (!ctx.currentPath->empty()) {
					StartPlay(ctx, *ctx.currentPath);
				} else if (filer->itemCount() > 0 &&
				           (filer->item(filer->cursor()).type & mxv2::kFileItemMdx) != 0 &&
				           filer->Open(&path) == mxv2::kFilerOpenPlay) {
					StartPlay(ctx, path);
				} else if (filer->NextMdx(&path)) {
					StartPlay(ctx, path);
				}
				break;
			case mxv2::nowplaying::kRequestPause:
				*autoPause = kAutoPauseNone;
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
			case mxv2::nowplaying::kRequestSeekForward:
			case mxv2::nowplaying::kRequestSeekBack: {
				if (!player->playing()) break;
				const uint32_t now = player->nowTimeMs();
				uint32_t want = 0;
				if (req == mxv2::nowplaying::kRequestSeekBack) {
					want = (now > kRemoteSeekStepMs) ? (now - kRemoteSeekStepMs) : 0;
				} else {
					want = now + kRemoteSeekStepMs;
					const uint32_t total = player->playTimeMs();
					if (total != 0 && want > total) want = total;
				}
				player->SeekMs(want);
				break;
			}
			case mxv2::nowplaying::kRequestSeekTo: {
				if (!player->playing()) break;
				uint32_t want = mxv2::nowplaying::TakeSeekMs();
				const uint32_t total = player->playTimeMs();
				if (total != 0 && want > total) want = total;
				player->SeekMs(want);
				break;
			}
			case mxv2::nowplaying::kRequestFocusLost:
			case mxv2::nowplaying::kRequestRouteLost:
				// 鳴っていたときだけ印を付ける。
				if (player->playing() && !player->paused()) {
					player->Pause();
					*autoPause = (req == mxv2::nowplaying::kRequestFocusLost) ? kAutoPauseFocus
					                                                         : kAutoPauseRoute;
				}
				break;
			case mxv2::nowplaying::kRequestFocusGained:
				if (*autoPause == kAutoPauseFocus) {
					*autoPause = kAutoPauseNone;
					if (player->playing() && player->paused()) player->Resume();
				}
				break;
			// 車のエンジンを掛け直したときなど（memo/bluetooth.md）。止めたあとで
			// 画面から鳴らし直していれば、一時停止していないので何もしない。
			// 再開するかは、つながった機器の設定で決める（機器ごと）。1 秒ごとに
			// 引き直している機器名は古いかもしれないので、ここで引き直す。
			case mxv2::nowplaying::kRequestBluetoothConnected: {
				if (*autoPause != kAutoPauseRoute) break;
				mxv2::outputlatency::RefreshBluetoothName();
				const std::string name = mxv2::outputlatency::BluetoothName();
				if (name.empty() || !settings.BluetoothDeviceFor(name).resumeOnReconnect) break;
				*autoPause = kAutoPauseNone;
				if (player->playing() && player->paused()) player->Resume();
				break;
			}
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

// 出力先の遅れを測り、自動の表示の遅らせへ足す（outputlatency.h）。
//
// 測るのは、演奏中（一時停止していない）で「画面と音を自動的に合わせる」が
// 入っているときだけ。手で決めた値には足さない。
// Chromecast へ送っている間は足さない。TV へ送る絵の時刻は visualFrame から
// 出しているので、手元の出力先の遅れを足すと絵が音より遅れて届き、
// 書き出しで捨てられる（memo/bluetooth.md の 3。手元の音はふつう消している）。
//
// 出力先が Bluetooth のときは、その機器の遅延時間（設定の [Bluetooth]。
// Settings::BluetoothLatencyFor）を足す。測った遅れには受け側の中の遅れが
// 入らず、Windows は Bluetooth の遅れそのものも入らないので。
void PollOutputLatency(mxv2::Player *player, const mxv2::Settings &settings, int *lastLoggedMs) {
	const bool casting = mxv2::cast::GetState() != mxv2::cast::kIdle;
	const bool want =
	    player->playing() && !player->paused() && player->displayLatencyAuto() && !casting;
	mxv2::outputlatency::SetActive(want, player->sampleRate());

	int ms = mxv2::outputlatency::LatencyMs();
	if (casting || !player->displayLatencyAuto()) ms = 0;
	if (ms < 0) return;  // まだ測れていない。前の値のまま
	if (ms > 0 && mxv2::outputlatency::OutputIsBluetooth()) {
		const std::string name = mxv2::outputlatency::BluetoothName();
		const int btMs = settings.BluetoothLatencyFor(name);
		ms += btMs;
		// どの機器の値を足したかは、つなぎ替えたときだけログに出す。
		static std::string lastName;
		if (name != lastName) {
			printf("audio    : %s\n",
			       mxv2::MsgF("Log.AudioBluetoothDevice", name, mxv2::MsgNum("%d", btMs)).c_str());
			fflush(stdout);
			lastName = name;
		}
	}
	// 測るたびに 1ms 前後揺れるので、5ms 以上変わったときだけ替える
	// （替えるたびに表示が飛ぶ）。
	const int rate = player->sampleRate();
	const int nowMs = (int)((int64_t)player->outputLatencyFrames() * 1000 / rate);
	if (ms == 0 ? nowMs != 0 : (ms - nowMs >= 5 || nowMs - ms >= 5)) {
		player->SetOutputLatency((int)((int64_t)ms * rate / 1000));
	}
	// ログは 20ms 以上変わったときだけ（Bluetooth のつなぎ外しが分かれば十分）。
	if (casting || !player->displayLatencyAuto()) return;
	if (*lastLoggedMs < 0 || ms - *lastLoggedMs >= 20 || *lastLoggedMs - ms >= 20) {
		printf("audio    : %s\n",
		       mxv2::MsgF("Log.AudioOutputLatency", mxv2::MsgNum("%d", ms),
		                  mxv2::MsgNum("%.1f", player->displayLatencyFrames() * 1000.0 / rate))
		           .c_str());
		fflush(stdout);
		*lastLoggedMs = ms;
	}
}

// 演奏状態の通知（Android）に出す文言をカタログから渡す。起動時と、
// 設定ウィンドウで言語を替えたときに呼ぶ（Java 側は文言を持っていない）。
std::string WindowTitle(const std::string &songTitle) {
	std::string title = mxv2::ExecutableBaseName();
	if (!songTitle.empty()) title += " - " + songTitle;
	return title;
}

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
