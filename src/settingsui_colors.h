// mxv2 - 設定 UI: [配色設定] ダイアログ (F2)
//
// スキンの配色 (colors.ini) を編集して保存する。設定ウィンドウとは独立に
// 開閉する。実装は settingsui_colors.cpp。クラスの作法は settingsui_cast.h の
// 頭を見ること。

#ifndef MXV2_SETTINGSUI_COLORS_H
#define MXV2_SETTINGSUI_COLORS_H

#include <string>

#include "settingsui_dialog.h"

namespace mxv2 {

class DrawScreen;
class Player;
class SettingsUi;
struct Settings;

class ColorsDialog {
public:
	ColorsDialog() : show_(false), skinNameReset_(true), saveErrorFresh_(false) {
		skinNameBuf_[0] = '\0';
	}

	void Open() {
		show_ = true;
		// 名前の欄は、開いたときに今のスキン名で埋め直す。
		skinNameReset_ = true;
	}
	bool isOpen() const { return show_; }
	bool CloseTop() {
		if (overwrite_.CloseTop()) return true;
		if (!show_) return false;
		show_ = false;
		return true;
	}
	void Build(SettingsUi &ui, Settings *settings, DrawScreen *draw, Player *player);

private:
	friend class SettingsUi;

	// その先頭。スキン名・保存・読み直す・参照元のスキン。
	void BuildSkinSaveRow(SettingsUi &ui, Settings *settings, DrawScreen *draw);
	// 上書き確認。配色設定の中に入れ子で開く。
	void BuildOverwriteWindow(SettingsUi &ui, Settings *settings, DrawScreen *draw);
	// 今の配色を skin/<名前>/colors.ini として保存する。書き込み先は
	// ユーザーフォルダ側（同梱ぶんは読み取り専用なので触らない）。
	// 名前が今のスキンと違えば、今のスキンを土台にした新しいスキンを作り、
	// 保存したあとそのスキンへ切り替える。
	void SaveAs(SettingsUi &ui, const std::string &name, Settings *settings, DrawScreen *draw);
	// 画面を作り直す。ステータス欄は変化があったときしか描かないので、
	// 作り直したあとは Player に積み直しを頼む。
	void Rebuild(DrawScreen *draw, Player *player);

	bool show_;
	// 保存先のスキン名。開いたときに今のスキン名で埋め直す。
	char skinNameBuf_[128];
	bool skinNameReset_;
	std::string saveError_;  // 保存できなかった理由（ダイアログに出す）
	// 文言は一番下に出るので、出したフレームだけそこまでスクロールする
	// （ダイアログは縦がいっぱいで、足すと画面の外へ出てしまう）。
	bool saveErrorFresh_;
	NestedPopup overwrite_;      // 上書き確認
	std::string overwriteName_;  // 確認中の名前
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_COLORS_H
