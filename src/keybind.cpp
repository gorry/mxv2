// mxv2 - キーとマウスの割り当て（main.cpp から切り出し）

#include "keybind.h"

#include <string>

#include "drawscreen.h"
#include "filer.h"
#include "mouse.h"
#include "player.h"
#include "playctl.h"
#include "settingsui.h"

namespace mxv2 {
namespace app {

namespace {

// 演奏位置の移動幅。, / . が普通、Shift 付きの < / > が高速。
const uint32_t kSeekStepMs = 3 * 1000;

const uint32_t kSeekFastStepMs = 30 * 1000;

// - / + (;) キー 1 回で動かす音量。音量は -100..+100 なので、この幅だと端から端まで
// 40 回。旧 mxv はバー 1 画素ぶん (64 段) 動かしていたので、それに近い刻み。
const int kVolumeKeyStep = 5;

}  // namespace

// マウス・タッチの操作 (MouseInput::Handle) が返した要求を実行する。
void HandleMouseRequest(int request, const InputTargets &t) {
	switch (request) {
		case mxv2::kMouseRequestOpenCursor:
			OpenCursor(*t.ctx, t.filer, t.ui);
			break;
		case mxv2::kMouseRequestPrev: {
			std::string path;
			if (t.filer->PrevMdx(&path)) {
				StartPlay(*t.ctx, path);
			}
			break;
		}
		case mxv2::kMouseRequestNext: {
			std::string path;
			if (t.filer->NextMdx(&path)) {
				StartPlay(*t.ctx, path);
			}
			break;
		}
		case mxv2::kMouseRequestReplay:
			if (!t.currentPath->empty()) {
				StartPlay(*t.ctx, *t.currentPath);
			}
			break;
		case mxv2::kMouseRequestToggleCont:
			*t.autoNext = !*t.autoNext;
			*t.chromeRefresh = true;
			break;
		case mxv2::kMouseRequestToggleRepeat:
			*t.autoRepeat = !*t.autoRepeat;
			*t.chromeRefresh = true;
			break;
		case mxv2::kMouseRequestGoParent:
			// ファイラーで左へはじいた。BACKSPACE と同じ。
			t.filer->GoParent();
			*t.fileListRefresh = true;
			break;
		case mxv2::kMouseRequestContextMenu:
			t.ui->OpenContextMenu();
			break;
		default:
			break;
	}
}

// キーが押された。ダイアログが開いている間は来ない（main が先に ImGui へ
// 渡している）。ESC / 戻るキーのダイアログ閉じも main 側で済んでいる。
void HandleKeyDown(const SDL_KeyboardEvent &ev, const InputTargets &t) {
	const SDL_Keycode key = ev.keysym.sym;
	switch (key) {
		// 終了は必ず確認してから。押し間違いで演奏が止まるのを防ぐ
		// （ウィンドウの × とコンテキストメニューの [終了] は、
		// 意図してそこを選んでいるので確認しない）。
		case SDLK_ESCAPE:
		case SDLK_q:
			t.ui->OpenQuitConfirm();
			break;

		case SDLK_F1:
			t.ui->OpenSettings();
			break;
		case SDLK_F2:
			t.ui->OpenColors();
			break;
		case SDLK_F3:
			t.ui->OpenFileSystems();
			break;
		case SDLK_F4:
			t.ui->OpenBookmarks();
			break;
		// 表示の切り替え（メニューの [表示] と同じ。長押しの代わり）。
		case SDLK_F7:
			t.draw->ToggleStatusMode();
			t.player->RequestStatusRefresh();
			break;
		case SDLK_F8:
			t.draw->ToggleRegMap();
			break;
		case SDLK_F11:
		case SDLK_h:
			t.ui->OpenHelp();
			break;
		case SDLK_F12:
		case SDLK_a:
			t.ui->OpenAbout();
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
			                   (ev.keysym.mod & KMOD_SHIFT) != 0);
			const uint32_t step = fast ? kSeekFastStepMs : kSeekStepMs;
			const uint32_t now = t.player->nowTimeMs();
			uint32_t want = 0;
			if (back) {
				want = (now > step) ? (now - step) : 0;
			} else {
				want = now + step;
				const uint32_t total = t.player->playTimeMs();
				if (total != 0 && want > total) want = total;
			}
			t.player->SeekMs(want);
			break;
		}

		case SDLK_SPACE:
			if (t.player->paused()) {
				t.player->Resume();
			} else {
				t.player->Pause();
			}
			break;
		case SDLK_f:
			t.player->Fadeout();
			break;

		case SDLK_UP:
			t.filer->MoveCursor(-1);
			break;
		case SDLK_DOWN:
			t.filer->MoveCursor(1);
			break;
		case SDLK_PAGEUP:
			t.filer->MoveCursor(-t.filer->visibleRows());
			break;
		case SDLK_PAGEDOWN:
			t.filer->MoveCursor(t.filer->visibleRows());
			break;
		case SDLK_HOME:
			t.filer->SetCursor(0);
			break;
		case SDLK_END:
			t.filer->SetCursor(t.filer->itemCount() - 1);
			break;

		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			OpenCursor(*t.ctx, t.filer, t.ui);
			break;
		case SDLK_BACKSPACE:
			t.filer->GoParent();
			*t.fileListRefresh = true;
			break;

		// Android の戻るキー。ダイアログ（上で処理済み）→ 親フォルダ
		// → 終了の確認、の順に効く。ESC のようにいきなり閉じない。
		case SDLK_AC_BACK: {
			const std::string before = t.filer->currentRef();
			t.filer->GoParent();
			if (t.filer->currentRef() == before) {
				t.ui->OpenQuitConfirm();
			} else {
				*t.fileListRefresh = true;
			}
			break;
		}
		case SDLK_BACKSLASH:
			t.filer->GoRoot();
			*t.fileListRefresh = true;
			break;
		case SDLK_l:
			// フォルダを選ぶダイアログ。今の場所から出す。
			t.ui->OpenFolder(t.filer->currentRef());
			break;
		case SDLK_m:
			// Shift 付きはカレントフォルダの控え / 控え外し（確認あり）。
			// 素の M はファイラーの "Bookmarks>"（ジャンプ専用の一覧）。
			// 設定ダイアログは F4 だけ。
			if (ev.keysym.mod & KMOD_SHIFT) {
				t.ui->OpenBookmarkToggle();
			} else {
				t.ui->OpenBookmarkList();
			}
			break;

		case SDLK_n: {
			std::string path;
			if (t.filer->NextMdx(&path)) {
				StartPlay(*t.ctx, path);
			}
			break;
		}
		case SDLK_b: {
			std::string path;
			if (t.filer->PrevMdx(&path)) {
				StartPlay(*t.ctx, path);
			}
			break;
		}

		case SDLK_c:
			*t.autoNext = !*t.autoNext;
			*t.chromeRefresh = true;
			break;
		case SDLK_r:
			*t.autoRepeat = !*t.autoRepeat;
			*t.chromeRefresh = true;
			break;

		case SDLK_TAB:
			ToggleFileListFontSize(t.draw, t.filer, t.fileListRefresh);
			break;

		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			t.player->SetMainVolume(t.player->mainVolume() - kVolumeKeyStep);
			break;
		case SDLK_EQUALS:
		case SDLK_PLUS:
		case SDLK_KP_PLUS:
		// JP 配列の「+」は Shift+「;」なので、SDL には SDLK_SEMICOLON で
		// 届く（US 配列の「+」は Shift+「=」で SDLK_EQUALS）。
		// 「;」そのものは他に割り当てが無いので、修飾なしでも受ける。
		case SDLK_SEMICOLON:
			t.player->SetMainVolume(t.player->mainVolume() + kVolumeKeyStep);
			break;

		// チャンネルの一括マスク。旧 mxv は Ctrl+0 / Alt+0 / Ctrl+Alt+0。
		case SDLK_0:
			if (ev.keysym.mod & KMOD_CTRL) {
				t.player->ToggleChannelGroup(mxv2::Player::kChannelMaskAll);
			} else if (ev.keysym.mod & KMOD_SHIFT) {
				t.player->ToggleChannelGroup(mxv2::Player::kChannelMaskPcm);
			} else {
				t.player->ToggleChannelGroup(mxv2::Player::kChannelMaskFm);
			}
			break;

		default:
			// 1-8 で FM の ch.1-8、Shift を足すと PCM の ch.P-W。
			// 旧 mxv は Ctrl+1-8 / Alt+1-8 だったが、mxv2 は修飾無しの
			// 1-8 を先に FM へ割り当ててあるので、PCM を Shift 側にした。
			if (key >= SDLK_1 && key <= SDLK_8) {
				const int base = (ev.keysym.mod & KMOD_SHIFT) ? 8 : 0;
				t.player->ToggleChannel(base + (int)(key - SDLK_1));
			}
			break;
	}
}

}  // namespace app
}  // namespace mxv2
