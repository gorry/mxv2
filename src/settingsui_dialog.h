// mxv2 - 設定 UI: ダイアログの状態に使う共通の小さな型
//
// 各ダイアログのクラス（settingsui_*.h）が使う。SettingsUi 本体（settingsui.h）
// はそれらのヘッダを通して見る。ここには ImGui にも SettingsUi にも依らない
// ものだけを置く。

#ifndef MXV2_SETTINGSUI_DIALOG_H
#define MXV2_SETTINGSUI_DIALOG_H

namespace mxv2 {

// 入れ子で開く確認ダイアログ（上書き・削除・終了・渡されたファイル・更新の
// 知らせなど）。ImGui のポップアップは組み立ての途中でしか開閉できないので、
// 「次のフレームで開いて」「いま開いている」「ESC で閉じてほしい」の 3 つで
// 受け渡す。
struct NestedPopup {
	bool ask;    // 次のフレームで開く
	bool open;   // いま開いている（ESC の判断に使う）
	bool close;  // ESC で閉じてほしい
	NestedPopup() : ask(false), open(false), close(false) {}
	// SettingsUi::CloseDialog() 用。開いていれば閉じる印を付けて true。
	bool CloseTop() {
		if (!open) return false;
		close = true;
		return true;
	}
};

// 設定ウィンドウなどから開き、閉じたら呼び出し元へ戻る往復。モーダルは
// 入れ子にしないので、呼び出し元が閉じきってから開き、閉じたら開き直す
// （配線は SettingsUi::Build）。
struct Handoff {
	bool openPending;     // 呼び出し元が閉じきったら開く
	bool returnToCaller;  // 閉じたら呼び出し元を開き直す
	Handoff() : openPending(false), returnToCaller(false) {}
	void Start() { openPending = returnToCaller = true; }
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_DIALOG_H
