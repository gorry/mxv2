// mxv2 - 設定 UI: コンテキストメニュー（右クリック / バナーの長押し）と終了の確認
//
// settingsui.cpp から切り出した。指で操作するときの 2 段目は「ページ」で、
// 切り替えは横スクロール（settingsui.h の CtxPage）。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "imgui_internal.h"
#include "drawscreen.h"
#include "filer.h"
#include "player.h"
#include "screen.h"
#include "settings.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// コンテキストメニューのページが横に滑る時間 (ms)。
const Uint32 kCtxSlideMs = 180;

}  // namespace

// 旧 mxv の右クリックメニュー (mxv.cpp の CreateContextMenu) にあたる。
// 終了はメインループの持ち物なので request_ に積んで返す。
//
// **[操作] と [マスク] は 2026-09-10 に外した**（ユーザーの指示）。旧 mxv で
// まだ画面をクリックして操作できなかった頃の名残で、操作ボタン・鍵盤・
// ステータス欄を押せるようになった今はもう要らない。モバイルでは階層のある
// メニューが扱いづらく、横画面では項目数そのものに余裕が無い、という事情もある。
//
// **[表示] のサブメニューは 2026-09-15 に足した**（ユーザーの指示）。2 段目は
// 音色データ表示（tonedata.md）とレジスタ一覧（regmap.md）の ON/OFF で、
// ページへ入る / 戻る項目（settingsui.h の CtxPage）。
bool SettingsUi::CtxPageItem(const char *label, bool back) {
	// 矢印は BeginMenu が描くのと同じ場所（印の列 MenuColumns.OffsetMark）に、
	// 同じ RenderArrow で描く。列の位置は前のフレームの並びから決まるが、
	// 列が決まるまでは隠している（ctxChildWarm_）ので、ずれては見えない。
	ImGuiWindow *window = ImGui::GetCurrentWindow();
	const ImVec2 pos = window->DC.CursorPos;
	const float fontSize = ImGui::GetFontSize();
	const float minW = window->DC.MenuColumns.OffsetMark + IM_TRUNC(fontSize * 1.20f);
	const float extraW = ImMax(0.0f, ImGui::GetContentRegionAvail().x - minW);

	// MenuItem は押すとポップアップを閉じるので、その振る舞いだけ止める。
	ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
	const bool pressed = ImGui::MenuItem(label);
	ImGui::PopItemFlag();
	// ページを替えたら、指の位置を ImGui から忘れさせる。ImGui は最後に
	// 触れた位置をマウスとして覚えていて、入れ替わった中身のその位置にある
	// 項目（[その他] → [戻る] のあとの [表示] など）が触れてもいないのに
	// ハイライトされる（2026-09-16、ユーザーの指摘）。次に触れれば SDL が
	// 位置を送り直す（ProcessEvent も参照）。
	if (pressed) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

	ImGui::RenderArrow(window->DrawList,
	                   ImVec2(pos.x + window->DC.MenuColumns.OffsetMark + extraW + fontSize * 0.30f,
	                          pos.y),
	                   ImGui::GetColorU32(ImGuiCol_Text), back ? ImGuiDir_Left : ImGuiDir_Right);
	return pressed;
}

bool SettingsUi::CtxMenuItem(const char *label, const char *shortcut, bool selected,
                             bool enabled) {
	if (ctxNoShortcut_) shortcut = NULL;
	if (!ImGui::MenuItem(label, shortcut, selected, enabled)) return false;
	// ImGui の自動クローズは「自分がポップアップであるとき」だけ効く
	// （Selectable の window->Flags & ImGuiWindowFlags_Popup）。ページの
	// 子ウィンドウの中から押されたときはこちらで閉じる。
	if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_ChildWindow) ImGui::CloseCurrentPopup();
	return true;
}

// 1 ページぶんの項目。
void SettingsUi::BuildCtxPageItems(int page, bool paged, Settings *settings, DrawScreen *draw,
                                   Player *player, Filer *filer) {
	if (page == kCtxPageView) {
		// [表示] … 表示の ON/OFF。チェックが今の状態。
		const bool tone = (draw->statusMode() == DrawScreen::kStatusModeTone);
		if (CtxMenuItem(Msg("Menu.ToneData"), "F7", tone, true)) {
			// ステータス欄の長押しと同じ経路（値の積み直しまで）。
			draw->ToggleStatusMode();
			if (player != 0) player->RequestStatusRefresh();
		}
		if (CtxMenuItem(Msg("Menu.RegMap"), "F8", draw->regMapVisible(), true)) {
			draw->ToggleRegMap();
		}
		if (paged) {
			ImGui::Separator();
			if (CtxPageItem(Msg("Menu.Back"), true)) {
				ctxSwitchTo_ = kCtxPageMain;
				ctxAnimForward_ = false;
			}
		}
		return;
	}
	if (page == kCtxPageOther) {
		// [その他] … [操作方法…] と [バージョン情報…]（2026-09-16、ユーザーの
		// 指示）。横画面の携帯でメニューが縦に収まらず（ジェスチャー
		// ナビゲーションの帯ぶん短い Pixel 8 Pro でスクロールバーが出た）、
		// 項目を減らすために畳んだ。
		if (CtxMenuItem(Msg("Menu.Help"), "F11", false, true)) showHelp_ = true;
		if (CtxMenuItem(Msg("Menu.About"), "F12", false, true)) showAbout_ = true;
		if (paged) {
			ImGui::Separator();
			if (CtxPageItem(Msg("Menu.Back"), true)) {
				ctxSwitchTo_ = kCtxPageMain;
				ctxAnimForward_ = false;
			}
		}
		return;
	}

	// 並びは bookmark.md の「メインメニュー」のとおり:
	// 移動（フォルダを開く / ブックマークを開く / ブックマークに追加）→
	// 設定 4 つ → 操作方法・バージョン情報 →（デスクトップのみ）終了。
	// [フォルダを開く…] はモバイルには置かない（2026-09-15、ユーザーの指示）。
	if (!Screen::IsMobile() && CtxMenuItem(Msg("Menu.Folder"), "L", false, true)) {
		SetFolderDir(filer->currentRef());
		showFolder_ = true;
	}
	// ファイラーの "Bookmarks>"（ジャンプ専用）。設定ダイアログは下の段。
	if (CtxMenuItem(Msg("Menu.BookmarkList"), "M", false, true)) OpenBookmarkList();
	{
		// カレントを控える / 控えを外す。どちらも確認してから実行するので、
		// ここでは印を立てるだけ（メニューの中で OpenPopup すると入れ子の
		// ポップアップになってしまう）。項目は 1 つで、
		//   控えられる場所 … [ブックマークに追加…]
		//   控え済みの場所 … [ブックマークから削除…]
		//   どちらもできない場所（ルート・Bookmarks>）… [ブックマークに追加…] を無効で
		// （2026-09-16、ユーザーの指示。前日に削除を外したのは勘違いだったとのこと）。
		// 追加も削除もできる状態は無い（控え済みなら削除しかできない）。
		const std::string cur = filer->currentRef();
		const bool has = (FindBookmark(settings->bookmarks, cur) >= 0);
		if (CtxMenuItem(has ? Msg("Menu.BookmarkRemove") : Msg("Menu.BookmarkAdd"), "Shift+M",
		                false, CanBookmark(cur))) {
			bmOpenToggle_ = true;
		}
	}

	ImGui::Separator();
	// [表示] … 2 段目。指で操作するときはページ、それ以外はサブメニュー。
	if (paged) {
		if (CtxPageItem(Msg("Menu.View"), false)) {
			ctxSwitchTo_ = kCtxPageView;
			ctxAnimForward_ = true;
		}
	} else if (ImGui::BeginMenu(Msg("Menu.View"))) {
		BuildCtxPageItems(kCtxPageView, false, settings, draw, player, filer);
		ImGui::EndMenu();
	}

	ImGui::Separator();
	if (CtxMenuItem(Msg("Menu.Settings"), "F1", false, true)) visible_ = true;
	// F2 と同じ経路を通す（スキン名の欄を埋め直すため）。
	if (CtxMenuItem(Msg("Menu.Colors"), "F2", false, true)) OpenColors();
	if (CtxMenuItem(Msg("Menu.FileSystems"), "F3", false, true)) OpenFileSystems();
	if (CtxMenuItem(Msg("Menu.Bookmarks"), "F4", false, true)) OpenBookmarks();

	ImGui::Separator();
	// [その他] … 2 段目。
	if (paged) {
		if (CtxPageItem(Msg("Menu.Other"), false)) {
			ctxSwitchTo_ = kCtxPageOther;
			ctxAnimForward_ = true;
		}
	} else if (ImGui::BeginMenu(Msg("Menu.Other"))) {
		BuildCtxPageItems(kCtxPageOther, false, settings, draw, player, filer);
		ImGui::EndMenu();
	}
	// [終了] はモバイルには置かない（区切り線ごと）。
	if (Screen::CanQuitApp()) {
		ImGui::Separator();
		if (CtxMenuItem(Msg("Menu.Quit"), NULL, false, true)) request_ = kRequestQuit;
	}
}

// ページを子ウィンドウに描く。大きさは前に測ったもの（未測なら残り全部。
// 見えない状態で測るときだけ）。中身の大きさは MenuItem の列幅
// （MenuColumns.NextTotalWidth。このフレームの項目から即決まる）と、
// カーソルの進んだ高さから取る。
bool SettingsUi::BuildCtxPageChild(int page, const ImVec2 &pos, Settings *settings,
                                   DrawScreen *draw, Player *player, Filer *filer) {
	static const char *const kNames[kCtxPageCount] = {"##ctxpage0", "##ctxpage1", "##ctxpage2"};
	ImGui::SetCursorPos(pos);
	ImGui::BeginChild(kNames[page], ctxPageSize_[page], ImGuiChildFlags_None,
	                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGuiWindow *cw = ImGui::GetCurrentWindow();
	const bool drawn = !cw->SkipItems;
	if (drawn) {
		BuildCtxPageItems(page, true, settings, draw, player, filer);
		const ImVec2 measured((float)cw->DC.MenuColumns.NextTotalWidth,
		                      cw->DC.CursorMaxPos.y - cw->DC.CursorStartPos.y);
		if (measured.x > 0.0f && measured.y > 0.0f) ctxPageSize_[page] = measured;
		ctxChildWarm_[page] = true;
	}
	ImGui::EndChild();
	return drawn;
}

// コンテキストメニューの項目。BuildContextMenu がポップアップを開いた中から
// 呼ぶ。閉じるフレームには呼ばない（BuildContextMenu のコメント）。
void SettingsUi::BuildContextMenuItems(Settings *settings, DrawScreen *draw, Player *player,
                                       Filer *filer) {
	// デスクトップ（マウス）は ImGui のサブメニューをそのまま使う。
	const bool paged = touchUi_;
	if (!paged) {
		BuildCtxPageItems(kCtxPageMain, false, settings, draw, player, filer);
		return;
	}

	// 指で操作するときの 2 段目は「ページ」（settingsui.h の CtxPage）。
	// ページは子ウィンドウに描き、ポップアップの大きさは最後の Dummy で申告する
	// （遷移中の子は横にはみ出しているので、子の大きさをそのまま使えない）。
	ImGuiWindow *popup = ImGui::GetCurrentWindow();
	const ImVec2 start = ImGui::GetCursorPos();
	const ImVec2 startScreen = ImGui::GetCursorScreenPos();
	const ImVec2 savedMax = popup->DC.CursorMaxPos;
	const ImVec2 savedIdeal = popup->DC.IdealMaxPos;
	const Uint32 now = SDL_GetTicks();

	// 遷移の進み具合（ease-out）。
	float t = 1.0f;
	if (ctxAnimFrom_ >= 0) {
		const float p = (float)(now - ctxAnimStartMs_) / (float)kCtxSlideMs;
		if (p >= 1.0f) {
			ctxAnimFrom_ = -1;
		} else {
			const float q = 1.0f - p;
			t = 1.0f - q * q * q;
		}
	}

	// 列幅の決まっていない子を出すフレームはポップアップごと隠す
	// （settingsui.h の ctxChildWarm_）。隠れていても項目は組まれるので測れる。
	if (!ctxChildWarm_[ctxPage_] || (ctxAnimFrom_ >= 0 && !ctxChildWarm_[ctxAnimFrom_])) {
		popup->HiddenFramesCannotSkipItems = 1;
	}

	ImVec2 size;
	if (ctxAnimFrom_ < 0) {
		if (!BuildCtxPageChild(ctxPage_, start, settings, draw, player, filer)) {
			// 子が丸ごと切り取られた（開いた最初のフレームはポップアップに
			// 大きさが無い）。項目をポップアップへ直に組んで大きさだけ測る。
			// 隠れているフレームなので見えない。
			ImGui::SetCursorPos(start);
			BuildCtxPageItems(ctxPage_, true, settings, draw, player, filer);
			const ImVec2 measured((float)popup->DC.MenuColumns.NextTotalWidth,
			                      popup->DC.CursorMaxPos.y - popup->DC.CursorStartPos.y);
			if (measured.x > 0.0f && measured.y > 0.0f) ctxPageSize_[ctxPage_] = measured;
		}
		size = ctxPageSize_[ctxPage_];
		if (ctxSwitchTo_ >= 0) {
			// 行き先のページを見えない状態で 1 度描いて、大きさを測り、列幅を
			// 決めておく。遷移はこの次のフレームから。Alpha は 0 にしない
			// （ImGui は Alpha 0 で Begin したウィンドウを隠して項目を飛ばすので
			// 測れない）。
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f / 1024.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
			ImGui::BeginDisabled();
			BuildCtxPageChild(ctxSwitchTo_, start, settings, draw, player, filer);
			ImGui::EndDisabled();
			ImGui::PopStyleVar(2);
			ctxAnimFrom_ = ctxPage_;
			ctxPage_ = ctxSwitchTo_;
			ctxSwitchTo_ = -1;
			ctxAnimStartMs_ = now;
		}
	} else {
		// 遷移中。大きさは前→次へ補間し、その矩形の中に 2 ページを横に
		// ずらして描く。押せないように（見た目は変えずに）無効にしておく。
		const ImVec2 &a = ctxPageSize_[ctxAnimFrom_];
		const ImVec2 &b = ctxPageSize_[ctxPage_];
		size = ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
		const float dir = ctxAnimForward_ ? 1.0f : -1.0f;
		const float offFrom = -dir * t * size.x;
		const float offTo = dir * (1.0f - t) * size.x;
		// 2 ページは幅が違うので、ずらした矩形どうしが重なる。子ウィンドウに
		// 背景は無いので重なった所は両方の字が見えてしまう。境目（下るときは
		// 次のページの左端、戻るときは前のページの左端）で左右に分け、
		// 出ていくページと入ってくるページをそれぞれ自分の側だけに描く。
		const float boundary = ctxAnimForward_ ? offTo : offFrom;
		const ImVec2 clipMin = startScreen;
		const ImVec2 clipMid(startScreen.x + boundary, startScreen.y);
		const ImVec2 clipMax(startScreen.x + size.x, startScreen.y + size.y);
		ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
		ImGui::BeginDisabled();
		// 出ていくページ。下るときは左側、戻るときは右側。
		if (ctxAnimForward_) {
			ImGui::PushClipRect(clipMin, ImVec2(clipMid.x, clipMax.y), true);
		} else {
			ImGui::PushClipRect(clipMid, clipMax, true);
		}
		BuildCtxPageChild(ctxAnimFrom_, ImVec2(start.x + offFrom, start.y), settings, draw, player,
		                  filer);
		ImGui::PopClipRect();
		// 入ってくるページ。その反対側。
		if (ctxAnimForward_) {
			ImGui::PushClipRect(clipMid, clipMax, true);
		} else {
			ImGui::PushClipRect(clipMin, ImVec2(clipMid.x, clipMax.y), true);
		}
		BuildCtxPageChild(ctxPage_, ImVec2(start.x + offTo, start.y), settings, draw, player, filer);
		ImGui::PopClipRect();
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
	}

	// 短縮キーの列ごと画面の横に入りきらないページがあれば、列を外して
	// 測り直す（ctxNoShortcut_）。ImGui はポップアップを画面の幅で
	// 切り詰めるので、放っておくと右端の短縮キーが途中で切れる。測った幅は
	// 切り詰める前の中身の幅なので、ここで比べられる。
	if (!ctxNoShortcut_) {
		const ImGuiStyle &style = ImGui::GetStyle();
		const float availW = ImGui::GetIO().DisplaySize.x - style.DisplaySafeAreaPadding.x * 2.0f -
		                     style.WindowPadding.x * 2.0f;
		for (int i = 0; i < kCtxPageCount; i++) {
			if (ctxPageSize_[i].x > availW) {
				ctxNoShortcut_ = true;
				// 列幅を決め直すので、決まるまでまた隠す。
				for (int j = 0; j < kCtxPageCount; j++) ctxChildWarm_[j] = false;
				popup->HiddenFramesCannotSkipItems = 1;
				break;
			}
		}
	}

	// ポップアップの大きさを申告する。子が進めたぶんは捨てる。
	popup->DC.CursorMaxPos = savedMax;
	popup->DC.IdealMaxPos = savedIdeal;
	ImGui::SetCursorPos(start);
	ImGui::Dummy(size);
}

// 長押しの代わり。横幅 480px 程度までは短縮キーの列ごと収まる。それより
// 狭い画面では列を外す（ctxNoShortcut_）。縦は ApplyScale が行を詰めて収める。
void SettingsUi::BuildContextMenu(Settings *settings, DrawScreen *draw, Player *player,
                                  Filer *filer) {
	// バナーを押したときはこちらから開ける（右クリックできない環境向け）。
	// BeginPopupContextVoid と同じ id なので、下の Begin がそのまま拾う。
	if (openContextMenu_) {
		openContextMenu_ = false;
		ImGui::OpenPopup("##mxv2ctx");
	}
	// 開いていない間に 1 ページ目へ戻しておく（右クリックで開く経路は
	// BeginPopupContextVoid の中なので、開く前に見るのが確実）。
	if (!ImGui::IsPopupOpen("##mxv2ctx")) {
		ctxPage_ = kCtxPageMain;
		ctxSwitchTo_ = -1;
		ctxAnimFrom_ = -1;
		// 子ウィンドウはポップアップと一緒に出直すので、列幅も決め直し。
		for (int i = 0; i < kCtxPageCount; i++) ctxChildWarm_[i] = false;
		// 短縮キーの列を出すかも開くたびに決め直す（回転で幅が変わる）。
		ctxNoShortcut_ = false;
	}

	// どのウィンドウにも属さない場所での右クリック用の API を使う。
	// 素の OpenPopup + BeginPopup だと親ウィンドウが無い扱いになり、
	// 開いた次のフレームで ImGui に閉じられてしまう。
	if (ImGui::BeginPopupContextVoid("##mxv2ctx", ImGuiPopupFlags_MouseButtonRight)) {
		// ESC で閉じる。ImGui はキーボードナビを切ってあるとポップアップに
		// 対して WantCaptureKeyboard を立てないので、ESC はメインループ側へ
		// 素通りしてしまう（そのまま終了に使われていた）。main から
		// CloseDialog() 経由で来た印をここで始末する。CloseCurrentPopup は
		// ポップアップの中でしか呼べないので、この位置でないといけない。
		if (closeContextMenu_) {
			closeContextMenu_ = false;
			ImGui::CloseCurrentPopup();
			// 項目は出さずにここで抜ける。続けて BuildContextMenuItems を通すと、
			// [表示] などのサブメニューが開いていたときに、閉じたそのフレームで
			// BeginMenu がサブメニューを開き直してしまう（ImGui はタッチの最後の
			// 位置をマウスとして覚えていて、回転後もその項目に重なったままなら
			// hovered 扱い）。親を閉じたあとに開いたサブメニューは
			// OpenPopupStack の 0 段目に孤立し、以後どの Begin にも拾われず、
			// 窓を持たないので「外を押したら閉じる」にも掛からない。ImGui は
			// ポップアップがある限り WantCaptureMouse を立てるので、タッチが
			// 一切効かなくなる（2026-09-16、横画面でサブメニューを出したまま
			// 縦にすると操作不能、とユーザーの報告）。
		} else {
			BuildContextMenuItems(settings, draw, player, filer);
		}
		ImGui::EndPopup();
	}

	// メニューが開いているかを覚えておく。ESC を「メニューを閉じる」に
	// 使ってよいかの判断に要る。id は BeginPopupContextVoid が今のウィンドウの
	// id スタックから作るので、同じ場所で聞かないと食い違う。
	contextMenuOpen_ = ImGui::IsPopupOpen("##mxv2ctx");
	if (!contextMenuOpen_) closeContextMenu_ = false;
}

// 終了の確認。Android の戻るキーで、ダイアログも戻る先も無いときに出す。
// メイン画面から直に開くので、単独のモーダルとして扱う。
void SettingsUi::BuildQuitWindow() {
	if (quitAsk_) {
		quitAsk_ = false;
		ImGui::OpenPopup(kQuitTitle);
	}

	quitOpen_ = ImGui::IsPopupOpen(kQuitTitle);
	if (!quitOpen_) {
		quitClose_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kQuitTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ConfirmText(Msg("Dialog.QuitQuestion"));
	ImGui::Separator();
	// Enter でも終了できるようにする。ESC で開いて ESC で閉じられる一方、
	// 「はい」がマウスでしか押せないと、キーボードだけでは終われなくなる。
	const bool enter =
	    ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
	if (ImGui::Button(Msg("Button.Quit")) || enter) {
		request_ = kRequestQuit;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || quitClose_) {
		quitClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
