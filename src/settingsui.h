// mxv2 - 設定 UI（Dear ImGui）
//
// 旧 mxv のプロパティシート (mxvprop.cpp) と、テーマ (.mxv) の編集にあたる。
// F1 で開閉する。開いている間だけ ImGui がキーとマウスを横取りする。
//
// ImGui は実解像度で描く。SDL_RenderSetLogicalSize が入っているせいで
// マウス座標だけは論理座標で届くので、拡大率を掛け戻して合わせている。
//
// スキンの切り替えは画面サイズごと変わりうるので、ここでは名前を
// pendingSkin() に置くだけにして、実際の作り直しはメインループに任せる。

#ifndef MXV2_SETTINGSUI_H
#define MXV2_SETTINGSUI_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "imgui.h"

#include "assetpath.h"

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
class Screen;
struct Settings;

class SettingsUi {
public:
	SettingsUi();
	~SettingsUi();

	// ImGui の初期化。スキンとフォントは paths から探す。
	bool Init(Screen *screen, const AssetPaths &paths, std::string *err);
	void Shutdown();

	// SDL イベントを ImGui へ渡す。
	void ProcessEvent(const SDL_Event &ev);

	// 直前のフレームで ImGui が入力を欲しがっているか。true の間、
	// アプリ側は同種のイベントを無視する。
	bool wantCaptureMouse() const;
	bool wantCaptureKeyboard() const;

	// ダイアログはすべてモーダルなので、一度に開けるのは 1 つだけ。
	// 他が開いている間はキーを効かせない（先にそれを閉じてもらう）。
	// ImGui のモーダルは入れ子が前提の作りで、別のモーダルへ直接
	// 掛け替えようとすると開き直しに失敗する。
	//
	// F1 は開くだけ。閉じるのは ESC か × ボタン。
	void OpenSettings() {
		if (busy()) return;
		visible_ = true;
	}
	bool visible() const { return visible_; }

	// テーマの色を編集するダイアログ (F2)。設定ウィンドウとは独立に開閉する。
	// F2 も開くだけ。閉じるのは ESC か × ボタン（設定ウィンドウと同じ）。
	void OpenTheme() {
		if (busy()) return;
		showTheme_ = true;
		// 名前の欄は、開いたときに今のスキン名で埋め直す。
		themeNameReset_ = true;
	}

	// フォルダを選ぶダイアログ (L)。旧 mxv の MX_GetNewDirFileList にあたる。
	// 原典は SHBrowseForFolder だったが Windows 専用なので、パスの打ち込みと
	// フォルダ一覧を持つ自前のダイアログにしてある。
	// dir は最初に見せるフォルダ（ふつうはファイラの今の場所）。
	void OpenFolder(const std::string &dir) {
		if (busy()) return;
		folderTarget_ = kFolderTargetFiler;
		folderReturnToSettings_ = false;
		SetFolderDir(dir);
		showFolder_ = true;
	}

	// 操作方法のダイアログ (F11 / H)。文面は main.cpp の -h と同じもの。
	void OpenHelp() {
		if (busy()) return;
		showHelp_ = true;
	}

	// バージョン情報のダイアログ (F12 / A)。
	void OpenAbout() {
		if (busy()) return;
		showAbout_ = true;
	}

	// 開いているダイアログを閉じる。閉じるものが無ければ false。
	// ESC 用。ImGui はキーボードナビを有効にしていないとポップアップを
	// ESC で閉じてくれないので、こちらで面倒を見る。
	bool CloseDialog() {
		// コンテキストメニューも ESC で閉じる。閉じるのは ImGui の
		// ポップアップの中からでないとできないので、ここでは印だけ付けて
		// 実際の始末は Build() に任せる。
		if (contextMenuOpen_) { closeContextMenu_ = true; return true; }
		// 上書き確認はテーマのダイアログの中に入れ子で開くので、先に閉じる。
		if (overwriteOpen_) { closeOverwrite_ = true; return true; }
		if (showAbout_) { showAbout_ = false; return true; }
		if (showHelp_) { showHelp_ = false; return true; }
		if (showFolder_) { showFolder_ = false; return true; }
		if (showTheme_) { showTheme_ = false; return true; }
		if (visible_) { visible_ = false; return true; }
		return false;
	}

	// 右クリックで開くコンテキストメニュー（旧 mxv の TrackPopupMenu 相当）。
	// 設定ウィンドウが閉じていても出す。
	void OpenContextMenu() { openContextMenu_ = true; }

	// バージョン情報の見出し（名前・版・ビルド日付・著作権表示）。
	// 文言は main.cpp が持っているので渡してもらう。
	void SetAboutHeader(const std::string &text) { aboutHeader_ = text; }

	// 操作方法の本文。-h で出すものと同じ文字列を main.cpp から渡してもらう。
	// 受け取った時点でキー名と説明に切り分ける（表示は桁を揃えて出すため）。
	void SetHelpText(const std::string &text);

	// メニューから出た「メインループにやってもらうこと」。読んだら消える。
	// 演奏の開始・曲送り・終了はメインループが状態を持っているので、
	// ここでは要求だけ返す。
	enum Request {
		kRequestNone = 0,
		kRequestOpenCursor,   // ファイラのカーソルを開く
		kRequestReplay,       // 今の曲を掛け直す
		kRequestPrev,
		kRequestNext,
		kRequestToggleCont,
		kRequestToggleRepeat,
		kRequestSetFolder,    // ファイラを requestedFolder() へ移す
		kRequestQuit,
	};
	Request TakeRequest() {
		const Request r = request_;
		request_ = kRequestNone;
		return r;
	}

	// kRequestSetFolder の行き先。
	const std::string &requestedFolder() const { return requestedFolder_; }

	// 1 フレーム分の UI を組み立てる。設定の変更はその場で反映する。
	// 非表示のときも ImGui のフレームは回す必要があるので毎フレーム呼ぶ。
	void Build(Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
	           Screen *screen);

	// このフレームでユーザーが触った項目 (Settings::Field のビット和)。
	// 保存ボタンは無く「触った時点で保存する」ので、メインループがこれを
	// 拾って ini へ書き戻す。読んだら 0 に戻る。
	unsigned TakeChangedFields() {
		const unsigned f = changedFields_;
		changedFields_ = 0;
		return f;
	}

	// 組み立てた UI を今のレンダラへ描く。Screen::Draw と Present の間で呼ぶ。
	void Render(Screen *screen);

	// スキンが選ばれたらここに名前が入る。メインループが拾って画面を
	// 作り直し、済んだら ClearPendingSkin() を呼ぶ。
	const std::string &pendingSkin() const { return pendingSkin_; }
	void ClearPendingSkin() { pendingSkin_.clear(); }

	// スキンの一覧を取り直す（フォルダを足したとき用）。
	void ScanSkins();

	// 日本語フォントが読めたか。読めなければ ImGui 既定の ASCII フォント。
	bool hasJapaneseFont() const { return hasJapaneseFont_; }

private:
	// 倍率が変わったら true（ダイアログの大きさも作り直すため）。
	bool ApplyScale(float scale);
	// 画面を作り直す。ステータス欄は変化があったときしか描かないので、
	// 作り直したあとは Player に積み直しを頼む。
	void Rebuild(DrawScreen *draw, Player *player);

	// 今の配色を skin/<名前>/theme.mxv として保存する。書き込み先は
	// ユーザーフォルダ側（同梱ぶんは読み取り専用なので触らない）。
	// 名前が今のスキンと違えば、今のスキンを土台にした新しいスキンを作り、
	// 保存したあとそのスキンへ切り替える。
	void SaveThemeAs(const std::string &name, Settings *settings, DrawScreen *draw);

	bool ready_;
	bool visible_;
	bool hasJapaneseFont_;
	float styleScale_;
	ImGuiStyle baseStyle_;

	// ImGui は実解像度で動かすので、SDL から届く論理座標のマウス位置を
	// この倍率で直してからバックエンドへ渡す。Build で毎フレーム更新する。
	float inputScale_;

	// 表示倍率 (%) は「入力を確定してから少し待って」適用する。
	// 操作中に適用するとウィンドウの大きさが変わり、それに合わせて
	// コントロール自身の座標も変わるので、同じ場所を押しているだけで値が
	// 行き来してしまう。
	int pendingZoom_;         // 0 = 適用待ちなし
	uint32_t zoomApplyAtMs_;  // 0 = 適用待ちなし

	AssetPaths paths_;
	std::vector<std::string> skinNames_;
	std::string pendingSkin_;

	// PDX パスの入力欄。std::string を直接は編集できないので固定長で持つ。
	char pdxPathBuf_[512];

	// ユーザーが触った項目。TakeChangedFields() で取り出す。
	unsigned changedFields_;

	// モーダルの開閉を ImGui のポップアップ状態と合わせる。
	bool SyncModal(const char *title, bool *wanted);
	// 自分が開けたモーダルの題名 (ポインタ比較)。0 なら開けていない。
	const char *openedModal_;
	// どれか 1 つでもダイアログが開いているか。モーダルなので、開いている
	// 間は別のものを開けない（先にそれを閉じてもらう）。
	bool busy() const {
		return visible_ || showTheme_ || showAbout_ || showFolder_ || showHelp_;
	}

	// 操作方法のダイアログ。
	//
	// 元の文面は空白で桁を揃えてあるが、同梱フォントはプロポーショナルなので
	// そのまま出すと崩れる。「キー名」と「説明」に切り分けて持っておき、
	// 表示するときに幅を測って揃える。
	struct HelpRow {
		std::string key;   // 見出し行のときは見出しそのもの
		std::string desc;  // 見出し行と説明の無い行では空
		bool header;

		HelpRow() : header(false) {}
	};
	void BuildHelpWindow();
	bool showHelp_;
	std::vector<HelpRow> helpRows_;

	// テーマの色のダイアログ
	void BuildThemeWindow(Settings *settings, DrawScreen *draw, Player *player);
	// その先頭。テーマ名・保存・読み直す・参照元のスキン。
	void BuildThemeSaveRow(Settings *settings, DrawScreen *draw);
	bool showTheme_;
	// 保存先のスキン名。開いたときに今のスキン名で埋め直す。
	char themeNameBuf_[128];
	bool themeNameReset_;
	std::string themeError_;  // 保存できなかった理由（ダイアログに出す）
	// 文言は一番下に出るので、出したフレームだけそこまでスクロールする
	// （ダイアログは縦がいっぱいで、足すと画面の外へ出てしまう）。
	bool themeErrorFresh_;
	// 上書き確認。テーマのダイアログの中に入れ子で開く。
	void BuildOverwriteWindow(Settings *settings, DrawScreen *draw);
	std::string overwriteName_;  // 確認中の名前
	bool openOverwrite_;         // 次のフレームで開く
	bool overwriteOpen_;         // いま開いている（ESC の判断に使う）
	bool closeOverwrite_;        // ESC で閉じてほしい

	// フォルダを選ぶダイアログ。選んだ結果の行き先は 2 つある。
	//   kFolderTargetFiler … ファイラを動かす (L キー / メニュー)
	//   kFolderTargetPdx   … PDX の探索先に入れる (設定ウィンドウの [参照...])
	enum FolderTarget {
		kFolderTargetFiler = 0,
		kFolderTargetPdx,
	};
	// ダイアログの題名。"###" 以降が ImGui の id なので、見出しを変えても
	// 同じポップアップとして扱われる。
	const char *folderTitle() const;
	void BuildFolderWindow(Settings *settings);
	FolderTarget folderTarget_;
	// 設定ウィンドウから呼ばれたときの往復。モーダルは入れ子にせず、
	// 設定ウィンドウが閉じきってからフォルダ選択を出し、閉じたら開き直す。
	bool folderReturnToSettings_;
	bool folderOpenPending_;
	// 子フォルダの一覧だけ作り直す（入力欄には触らない）。
	void RelistFolder(const std::string &dir);
	// 一覧で選んだものを入力欄へ移す。中へは入らない（そこはダブルクリック）。
	void SelectFolderEntry(const std::string &path);
	// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
	void SetFolderDir(const std::string &dir);
	bool showFolder_;
	std::string folderDir_;                    // 今 一覧に出しているフォルダ
	std::string folderSelected_;               // 一覧で選ばれている行のパス
	std::vector<std::string> folderEntries_;   // その中の子フォルダ名
	std::string folderError_;                  // 開けなかったときの文言
	std::string requestedFolder_;              // kRequestSetFolder の行き先
	char folderPathBuf_[512];                  // パスの入力欄

	// コンテキストメニュー
	void BuildContextMenu(DrawScreen *draw, Player *player, Filer *filer);
	bool openContextMenu_;
	// 直前のフレームでメニューが開いていたか（ESC を食う判断に使う）と、
	// ESC で閉じてほしいという印。
	bool contextMenuOpen_;
	bool closeContextMenu_;
	Request request_;
	bool showAbout_;
	// 中身をドラッグしてスクロール中。ダイアログはモーダルで一度に 1 つしか
	// 開かないので、どのダイアログでもこの 1 つを使い回す。
	bool dragScroll_;
	// そのドラッグで実際にスクロールしたか。一覧の上から掴めるようにした
	// ぶん、離したときに行を選ばせないための印。
	bool dragMoved_;
	std::string aboutHeader_;  // 名前・版・ビルド日付・著作権表示
	std::string aboutText_;    // NOTICE の中身（初回に読む）

	SettingsUi(const SettingsUi &);
	SettingsUi &operator=(const SettingsUi &);
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_H
