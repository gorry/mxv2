// mxv2 - 設定 UI: ダイアログのクラスが共通に使う小さな型
//
// 各ダイアログのクラス（settingsui_*.h）が使う。SettingsUi 本体（settingsui.h）
// はそれらのヘッダを通して見る。実装は settingsui_dialog.cpp。
//
// 11 個のダイアログで繰り返していた定型を 3 つ、ここに括り出してある
// （memo/settingsui-split.md の第 3 段）:
//   ModalDialog   … show_ / isOpen / CloseTop の 3 行
//   NestedPopup   … 入れ子の確認の開き方・閉じ方（Begin / CancelButton）
//   モーダルの開き方（SyncModal + 置き場所 + 大きさ + BeginDialog）は
//   SettingsUi::BeginModal。

#ifndef MXV2_SETTINGSUI_DIALOG_H
#define MXV2_SETTINGSUI_DIALOG_H

namespace mxv2 {

class SettingsUi;

// 「開いているか」を bool 1 つで表せるモーダルのダイアログの土台。仮想関数は
// 持たない（SettingsUi は具象型で持つ）。入れ子の確認を持つクラスは CloseTop を
// 自分で定義し直して、先に入れ子を閉じる（隠すだけ。仮想ではない）。
class ModalDialog {
public:
	ModalDialog() : show_(false) {}
	bool isOpen() const { return show_; }
	// 中身（選んでいる行など）を保ったまま開き直す。往復から戻ってきたとき用
	// （SettingsUi::Build）。初めて開くときは各クラスの Open を使う。
	void Reopen() { show_ = true; }
	// SettingsUi::CloseDialog() 用。開いていれば閉じて true。
	bool CloseTop() {
		if (!show_) return false;
		show_ = false;
		return true;
	}

protected:
	bool show_;
};

// 入れ子で開く確認ダイアログ（上書き・削除・終了・渡されたファイル・更新の
// 知らせなど）。ImGui のポップアップは組み立ての途中でしか開閉できないので、
// 「次のフレームで開いて」「いま開いている」「ESC で閉じてほしい」の 3 つで
// 受け渡す。
//
// 組み立ての定型:
//   if (!popup.Begin(ui, kTitle)) return;
//   ... 中身 ...（決めたら ImGui::CloseCurrentPopup()）
//   if (popup.CancelButton(Msg("Button.Cancel"))) {}   // [キャンセル] と ESC
//   ImGui::EndPopup();
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
	// ask なら開き、open を更新して、開いていれば BeginPopupModal まで済ませる
	// （画面の中央、大きさは中身任せ）。true を返したら中身を組んで
	// ImGui::EndPopup() すること。false なら何もせず return。
	bool Begin(SettingsUi &ui, const char *title);
	// [キャンセル] のボタン。押されるか ESC（close）で CloseCurrentPopup して true。
	bool CancelButton(const char *label);
	// ボタンを出さずに、close（ESC）だけを見る版。[閉じる] のように別の条件と
	// まとめて閉じたいとき。true を返したら CloseCurrentPopup 済み。
	bool TakeClose();
};

// 設定ウィンドウなどから開き、閉じたら呼び出し元へ戻る往復。モーダルは
// 入れ子にしないので、呼び出し元が閉じきってから開き、閉じたら開き直す
// （配線は SettingsUi::Build）。
struct Handoff {
	bool openPending;     // 呼び出し元が閉じきったら開く
	bool returnToCaller;  // 閉じたら呼び出し元を開き直す
	Handoff() : openPending(false), returnToCaller(false) {}
	void Start() { openPending = returnToCaller = true; }
	// 呼び出し元（題名 callerTitle のポップアップ）が閉じきっていたら *show を
	// 立てて開く。毎フレーム呼ぶ。
	void OpenWhenCallerClosed(bool *show, const char *callerTitle);
	// 自分（題名 title）が閉じきったら 1 度だけ true を返す。呼ばれた側は
	// 呼び出し元を開き直すこと。blocked の間（自分がさらに別のダイアログへ
	// 出ている間）は返さない。
	bool TakeReturn(bool show, const char *title, bool blocked);
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_DIALOG_H
