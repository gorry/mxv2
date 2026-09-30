// mxv2 - 設定 UI: [ファイルシステムの設定] ダイアログ (F3)・SAF・渡されたファイル
//
// マウントする顔ぶれと並び順を決める。実体は Vfs のマウント一覧で、変えたら
// [FileSystem] へ書き戻す。Android の SAF（フォルダの許可を取る OS のピッカー）
// と、外から渡された MDX（openintent.h）の「どちらで開くか」の確認も、同じ
// マウントの話なのでここ。実装は settingsui_filesystems.cpp。
// クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_FILESYSTEMS_H
#define MXV2_SETTINGSUI_FILESYSTEMS_H

#include <string>

#include "settingsui_dialog.h"

namespace mxv2 {

class Filer;
class SettingsUi;

class FileSystemsDialog : public ModalDialog {
public:
	FileSystemsDialog() : selected_(0) {}

	void Open() {
		show_ = true;
		selected_ = 0;
	}
	// 同じダイアログを、mountRef のファイルシステムを選んだ状態で開く。
	// アクセス許可が失われた SAF をファイラーで開こうとしたときに使う
	// （[許可を取り直す…] へ導く）。
	void OpenFor(SettingsUi &ui, const std::string &mountRef);
	// SettingsUi::CloseDialog() 用。入れ子の確認を先に閉じる。
	bool CloseTop() {
		if (remove_.popup.CloseTop()) return true;
		if (addFs_.popup.CloseTop()) return true;
		if (handed_.popup.CloseTop()) return true;  // 単独で開くが、閉じ方は同じ
		return CloseSelf();
	}
	void Build(SettingsUi &ui, Filer *filer);

	// ---- ファイルシステムの追加と、OS の「フォルダを探す」 ---------------------
	// 追加 (dir:) の場所は OS ネイティブのパスなので、打ち込みと OS のダイアログで
	// 決める。ダイアログを出すのは main（開いている間はアプリが止まるので）。
	bool browsePending() const { return addFs_.browsePending; }
	void ClearBrowsePending() { addFs_.browsePending = false; }
	const std::string &browseStart() const { return addFs_.browseStart; }
	// 選ばれた **OS ネイティブのパス** を入力欄へ入れる。
	void SetBrowsedPath(const std::string &path);

	// ---- SAF（Android のフォルダの許可） ------------------------------------
	// アクセス許可が失われた SAF (mountRef) の許可を取り直す。OS のピッカーを
	// 元のフォルダで出し、同じフォルダを選び直せばそのマウントが戻る。
	// ピッカーを出せないとき・取り消したとき・別のフォルダを選んだときは
	// このダイアログをその行と注記つきで開く（fromFiler）。
	void RegrantAccess(SettingsUi &ui, const std::string &mountRef);
	// 同じことを、ダイアログの [許可を取り直す…] から。ピッカーを出せたら true。
	bool StartSafRegrant(SettingsUi &ui, const std::string &mountRef, bool fromFiler);
	bool safPicking() const { return saf_.picking; }
	// ピッカーの結果を取り込む（SettingsUi::Build から毎フレーム）。
	void PollSafPicked(SettingsUi &ui, Filer *filer);

	// ---- 外から渡された MDX ------------------------------------------------
	// 許可のあるフォルダの外にあった MDX の「どちらで開くか」を尋ねる
	// （SettingsUi::OpenHandedChoice を見ること）。
	void AskHanded(const std::string &uri, const std::string &name) {
		handed_.uri = uri;
		handed_.name = name;
		handed_.popup.ask = true;
	}
	// 尋ねている最中（ピッカーの結果待ちを含む）。
	bool handedBusy() const {
		return handed_.popup.ask || handed_.popup.open || !saf_.handedUri.empty();
	}
	bool handedOpen() const { return handed_.popup.open; }
	// Request::OpenHanded の開くもの (ref)。
	const std::string &handedRef() const { return handed_.ref; }
	void BuildHandedWindow(SettingsUi &ui, Filer *filer);

private:
	// 削除の確認。このダイアログの中に入れ子で開く。
	void BuildRemoveWindow(SettingsUi &ui, Filer *filer);
	void BuildAddWindow(SettingsUi &ui, Filer *filer);
	// Vfs の一覧 (all_) を触る前に、それを読んでいるスレッドの手を離させる。
	// 追加 (Vfs::Add) も削除 (RemoveMounted) も、必ずこれを通してから。
	void QuiesceVfsReaders(SettingsUi &ui, Filer *filer);
	// 選び終わったツリーをマウントに反映する（PollSafPicked の本体）。
	void ApplyPickedTree(SettingsUi &ui, Filer *filer, const std::string &uri,
	                     const std::string &regrant, bool fromFiler, bool handed);
	// OS の許可は残っているのに一覧から外れているツリーなら、一覧へ戻して
	// ref を作る（尋ねない）。戻せなければ false。
	bool MountGrantedTree(SettingsUi &ui, Filer *filer, const std::string &uri, std::string *ref);
	// ピッカーから戻ったあとの始末（許可が取れていれば saf: で、
	// 取り消されたらもう一度尋ねる）。
	void FinishHandedAfterPick(SettingsUi &ui, bool cancelled);
	// 写して開く（[このまま演奏する] と、許可を取っても見つからなかったとき）。
	void OpenHandedByCopy(SettingsUi &ui);

	int selected_;       // 一覧で選んでいる行
	std::string error_;  // 「カレントは削除できない」などの文言
	// 削除の確認（ダイアログの中に入れ子）。「OS のアクセス許可も取り消す」は
	// SAF のときだけ出す。[削除] はマウントを外すだけで許可は残るので、残すと
	// 「一覧に無いのに許可はある」状態になる（openintent.h の自動マウント）。
	struct Remove {
		NestedPopup popup;
		bool revoke;
		Remove() : revoke(true) {}
	} remove_;
	// ファイルシステムの追加 (dir:)。
	struct AddFs {
		NestedPopup popup;
		char pathBuf[512];
		std::string error;
		bool browsePending;
		std::string browseStart;
		AddFs() : browsePending(false) { pathBuf[0] = '\0'; }
	} addFs_;
	// 外から渡された MDX の「どちらで開くか」。
	struct Handed {
		NestedPopup popup;
		std::string uri;   // 尋ねている相手（渡された URI）
		std::string name;  // 画面に出す名前
		std::string ref;   // Request::OpenHanded の開くもの
	} handed_;
	// SAF のピッカー（Android）。
	struct Saf {
		bool picking;  // 選択画面を出していて、結果を待っている
		// 結果を待っている渡された URI（空なら [追加…] などの普通の選択）。
		std::string handedUri;
		// [許可を取り直す…] で出した選択画面なら、取り直す相手の mountRef。
		// 空なら [追加…]（新しくマウントする）。
		std::string regrantRef;
		// ファイラー（RegrantAccess）から出したピッカーか。うまくいかなかった
		// ときにこのダイアログへ落とすかどうかの印。
		bool regrantFromFiler;
		Saf() : picking(false), regrantFromFiler(false) {}
	} saf_;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_FILESYSTEMS_H
