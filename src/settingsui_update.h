// mxv2 - 設定 UI: 更新チェック（updatecheck.h）の知らせと、[mxv2 の設定] の [通信]
//
// 通信と「いつ確かめるか」は main が持ち、ここは見せるだけ。結果のダイアログは、
// ほかのダイアログがすべて閉じてから単独で開くか、[今すぐ…] の結果なら
// [mxv2 の設定] の中に入れ子で開く。実装は settingsui_update.cpp。
// クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_UPDATE_H
#define MXV2_SETTINGSUI_UPDATE_H

#include "settingsui_dialog.h"
#include "updatecheck.h"

namespace mxv2 {

class SettingsUi;
struct Settings;

class UpdateDialog {
public:
	UpdateDialog()
	    : available_(false), running_(false), checkNow_(false), pending_(false), nested_(false) {}

	bool isOpen() const { return popup_.open; }
	bool CloseTop() { return popup_.CloseTop(); }
	// [mxv2 の設定] の中に入れ子で開いたか。ImGui のポップアップの id は
	// 開いた場所の id スタックで決まるので、開いた場所と同じところで組む
	// （SettingsUi::Build と SettingsWindow::Build が見る）。
	bool nested() const { return nested_; }

	// [mxv2 の設定] の [通信] グループ（SettingsWindow から）。
	void BuildNetworkGroup(SettingsUi &ui, Settings *settings);
	// 預かった結果をどこで開くか決める（SettingsUi::Build の頭から毎フレーム）。
	void Schedule(SettingsUi &ui);
	// 結果のダイアログ。単独で開くときは SettingsUi::Build から、入れ子のときは
	// [mxv2 の設定] の中から呼ぶ。
	void BuildWindow(SettingsUi &ui);

private:
	friend class SettingsUi;

	bool available_;  // [通信] を出すか（main が毎フレーム教える）
	bool running_;    // 確かめている最中（[今すぐ…] を押せなくする）
	bool checkNow_;   // [今すぐ…] が押された（SettingsUi::TakeUpdateCheckNow が読む）
	UpdateResult result_;
	bool pending_;    // 見せる結果を預かっている
	NestedPopup popup_;
	bool nested_;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_UPDATE_H
