// mxv2 - 設定 UI: コンテキストメニュー（右クリック / バナー長押し）と終了の確認
//
// 旧 mxv の TrackPopupMenu 相当。設定ウィンドウが閉じていても出す。
// 指で操作するとき（SettingsUi::touchUi_）、[表示] と [その他] は ImGui の
// サブメニューではなく、同じポップアップの中身を入れ替える「ページ」にする。
// 携帯の縦画面ではメニューの左右どちらにもサブメニューを置く幅が無く、ImGui は
// 親に重ねて出すが、重ねた子は親の下に描かれて見えない（2026-09-16、
// Pixel 7a でユーザーの報告）。ページなら幅は要らない。
// 実装は settingsui_menu.cpp。クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_MENU_H
#define MXV2_SETTINGSUI_MENU_H

#include <SDL.h>

#include "imgui.h"

#include "settingsui_dialog.h"

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
class SettingsUi;
struct Settings;

class ContextMenu {
public:
	struct Page {
		enum Type {
			Main = 0,
			View,   // [表示] の中身
			Other,  // [その他] の中身
			Count,
		};
	};

	ContextMenu()
	    : openRequested_(false), open_(false), closeWanted_(false), page_(Page::Main),
	      switchTo_(-1), animFrom_(-1), animForward_(true), animStartMs_(0), noShortcut_(false) {
		for (int i = 0; i < Page::Count; i++) {
			pageSize_[i] = ImVec2(0.0f, 0.0f);
			childWarm_[i] = false;
		}
	}

	// 次のフレームで開く。
	void Open() { openRequested_ = true; }
	// 直前のフレームで開いていたか（ESC を食う判断に使う）。
	bool isOpen() const { return open_; }
	// 開いていれば閉じる（実際に閉じるのは次の Build）。
	void RequestClose() {
		if (open_) closeWanted_ = true;
	}
	// 終了の確認を次のフレームで開く。[終了] で SettingsUi の request_ に
	// Request::Quit が積まれる。
	void AskQuit() { quit_.ask = true; }
	bool quitOpen() const { return quit_.open; }
	// メニューは ImGui のポップアップの中からしか閉じられないので、印だけ
	// 付けて実際の始末は Build に任せる。
	bool CloseTop() {
		if (open_) {
			closeWanted_ = true;
			return true;
		}
		return quit_.CloseTop();
	}
	void Build(SettingsUi &ui, Settings *settings, DrawScreen *draw, Player *player,
	           Filer *filer);
	void BuildQuitWindow(SettingsUi &ui);

private:
	// ページへ入る / 戻る項目。サブメニューの見出しと同じ見た目（右端に ▶、
	// 戻るは ◀）で、押してもポップアップを閉じない。押されたら true。
	bool PageItem(const char *label, bool back);
	// MenuItem。子ウィンドウの中では ImGui が自分でポップアップを閉じないので、
	// 押されたらここで閉じる。
	bool MenuItem(const char *label, const char *shortcut, bool selected, bool enabled);
	// 1 ページぶんの項目。paged でなければ [表示] [その他] は ImGui のサブメニュー。
	void BuildPageItems(SettingsUi &ui, int page, bool paged, Settings *settings,
	                    DrawScreen *draw, Player *player, Filer *filer);
	// ページを子ウィンドウに描き、中身の大きさを pageSize_ に測る。
	// 子が丸ごと切り取られて何も描けなかったら false。
	bool BuildPageChild(SettingsUi &ui, int page, const ImVec2 &pos, Settings *settings,
	                    DrawScreen *draw, Player *player, Filer *filer);
	// その項目（ポップアップの中から）。
	void BuildItems(SettingsUi &ui, Settings *settings, DrawScreen *draw, Player *player,
	                Filer *filer);

	bool openRequested_;
	bool open_;
	bool closeWanted_;  // ESC で閉じてほしい
	int page_;          // Page。メニューを開くたびに Page::Main へ戻す
	// ページの切り替えは横スクロールで見せる（2026-09-16、ユーザーの要望。
	// 下るときは右から、戻るときは左から入ってくる）。各ページは自分の
	// 子ウィンドウに描く（MenuItem の列幅はウィンドウごとなので、2 ページを
	// 同じウィンドウに描くと列が揃ってしまう）。遷移中は 2 つの子を
	// 横にずらして描き、ポップアップの大きさは前→次へ補間する。
	int switchTo_;      // このフレームで押された行き先（無ければ -1）
	int animFrom_;      // 滑り出ていく前のページ（遷移中でなければ -1）
	bool animForward_;  // 下る（次が右から入る）か、戻る（左から）か
	Uint32 animStartMs_;
	// 各ページの中身の大きさ。描くたびに測り直す（未測は 0）。
	ImVec2 pageSize_[Page::Count];
	// 子ウィンドウの列幅が決まっているか。ImGui はウィンドウが出直すたびに
	// 列幅を測り直すので、最初の 1 フレームはラベルと短縮キーが重なる。
	// 決まるまではポップアップごと隠す（ポップアップ自身が最初のフレームで
	// やっているのと同じ）。
	bool childWarm_[Page::Count];
	// 短縮キーの列を出さない。どれかのページが画面の横に入りきらないと
	// 分かったら立て、メニューを閉じるまで保つ（毎フレーム決め直すと、
	// 列を消して幅が縮んだ途端に「入る」に戻ってちらつく）。指で操作する
	// 端末はキーボードが無いことが多いので、消しても失うものは少ない
	// （2026-09-24、320px 幅の XS17 で "Shift+M" が切れていた）。
	bool noShortcut_;
	NestedPopup quit_;  // 終了の確認
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_MENU_H
