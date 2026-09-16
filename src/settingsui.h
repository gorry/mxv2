// mxv2 - 設定 UI（Dear ImGui）
//
// 旧 mxv のプロパティシート (mxvprop.cpp) と、配色 (<テーマ名>.mxv) の編集にあたる。
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
#include "message.h"

namespace mxv2 {

class DirLister;
class DrawScreen;
class SongLoader;
class Filer;
class Player;
class Screen;
class Vfs;
struct Settings;

class SettingsUi {
public:
	SettingsUi();
	~SettingsUi();

	// ImGui の初期化。スキンとフォントは paths から探す。
	bool Init(Screen *screen, const AssetPaths &paths, std::string *err);

	// フォルダ選択とファイルシステムの設定で使う VFS。Init のあとに渡す。
	void SetVfs(Vfs *vfs) { vfs_ = vfs; }

	// 曲の読み込みスレッド。ファイルシステムを取り外す前に止めるために
	// 名前を知っておく（読んでいる最中に実体が消えると落ちる）。
	void SetSongLoader(SongLoader *loader) { songLoader_ = loader; }

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

	// 縦横切り替えの状態。main が毎フレーム教える（screen_orientation.md）。
	void SetOrientationState(bool enabled, int orientation) {
		orientEnabled_ = enabled;
		orientation_ = orientation;
	}

	// 設定ウィンドウが閉じた瞬間かどうかを 1 度だけ返す。
	// 「縦横の切り替えかた」は**閉じてから**効かせる決まりなので、その合図に使う。
	bool TakeSettingsClosed() {
		const bool v = settingsClosed_;
		settingsClosed_ = false;
		return v;
	}

	// スキンの配色を編集するダイアログ (F2)。設定ウィンドウとは独立に開閉する。
	// F2 も開くだけ。閉じるのは ESC か × ボタン（設定ウィンドウと同じ）。
	void OpenColors() {
		if (busy()) return;
		showColors_ = true;
		// 名前の欄は、開いたときに今のスキン名で埋め直す。
		skinNameReset_ = true;
	}

	// フォルダを選ぶダイアログ (L)。旧 mxv の MX_GetNewDirFileList にあたる。
	// 原典は SHBrowseForFolder だったが Windows 専用なので、パスの打ち込みと
	// フォルダ一覧を持つ自前のダイアログにしてある。
	// dir は最初に見せるフォルダの ref（ふつうはファイラーの今の場所）。
	void OpenFolder(const std::string &dir) {
		if (busy()) return;
		folderTarget_ = kFolderTargetFiler;
		folderReturnToSettings_ = false;
		SetFolderDir(dir);
		showFolder_ = true;
	}

	// ファイルシステムの設定 (F3)。ファイラーのルートに並べる顔ぶれと順番を
	// 決める。ファイラーの "[Setting]" とコンテキストメニューからも開く。
	void OpenFileSystems() {
		if (busy()) return;
		showFileSystems_ = true;
		fsSelected_ = 0;
	}

	// ブックマークの設定 (F4)。よく開く場所を控えておいて、そこへ移る。
	// 管理が主で、ジャンプもできる。コンテキストメニューと、ファイラーの
	// "Bookmarks>" の中の "[Setting]" からも開く。
	void OpenBookmarks() {
		if (busy()) return;
		showBookmarks_ = true;
		bmSelected_ = 0;
		bmError_.clear();
	}

	// ブックマークの一覧 (M)。ファイラーを "Bookmarks>"（ジャンプ専用の
	// 一覧）へ移す。コンテキストメニューの [ブックマークを開く] も同じ。
	// 実際の移動は TakeRequest() 経由でメインループが行う。
	void OpenBookmarkList();

	// ファイラーの "Bookmarks>" で選んだブックマークへ移る。行き先が
	// ファイルになっていたら「そのファイルのあるフォルダ」へ控え直してから
	// 開く（設定ダイアログの [開く] と同じ手順）。実際の処理は次の Build()。
	void OpenBookmarkRef(const std::string &ref) {
		if (busy()) return;
		bmJumpRef_ = ref;
		bmJumpPending_ = true;
	}

	// カレントフォルダをブックマークへ追加する / から削除する (Shift+M)。
	// どちらも確認してから実行する。ダイアログを開かずにメイン画面から
	// 直に使うので、確認だけが単独のモーダルとして出る。
	void OpenBookmarkToggle() {
		if (busy()) return;
		bmOpenToggle_ = true;
	}

	// 終了の確認。Android の戻るキーで、もう戻る先が無いときに出す
	// （いきなり閉じると押し間違いで演奏が止まるため）。
	// [終了] を選ぶと TakeRequest() が kRequestQuit を返す。
	void OpenQuitConfirm() {
		if (busy()) return;
		quitAsk_ = true;
	}

	// GL コンテキストが失われたあと、ImGui が持っているテクスチャを
	// 作り直させる（次のフレームで自分で作り直す）。
	void HandleDeviceReset();

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
		// 上書き確認は配色設定の中に入れ子で開くので、先に閉じる。
		if (overwriteOpen_) { closeOverwrite_ = true; return true; }
		// 削除の確認はファイルシステムの設定の中に入れ子で開く。
		if (fsConfirmOpen_) { fsCloseConfirm_ = true; return true; }
		// ブックマークも同じ作り。Shift+M の確認だけは単独で開く。
		if (bmRemoveOpen_) { bmCloseRemove_ = true; return true; }
		if (bmToggleOpen_) { bmCloseToggle_ = true; return true; }
		if (quitOpen_) { quitClose_ = true; return true; }
		if (showAbout_) { showAbout_ = false; return true; }
		if (showHelp_) { showHelp_ = false; return true; }
		// ファイルシステムの追加はその設定ダイアログの中に入れ子で開く。
		if (addFsShow_) { addFsClose_ = true; return true; }
		if (showStartup_) { showStartup_ = false; return true; }
		if (showFileSystems_) { showFileSystems_ = false; return true; }
		if (showBookmarks_) { showBookmarks_ = false; return true; }
		if (showFolder_) { showFolder_ = false; return true; }
		if (showColors_) { showColors_ = false; return true; }
		if (visible_) { visible_ = false; return true; }
		return false;
	}

	// 右クリックで開くコンテキストメニュー（旧 mxv の TrackPopupMenu 相当）。
	// 設定ウィンドウが閉じていても出す。
	void OpenContextMenu() { openContextMenu_ = true; }
	// 開いていれば閉じる。画面が回転してスキンが替わるときに呼ぶ。
	// 回転するとメニューが画面をはみ出すことがあり、配置を計算し直して
	// 出し直すより閉じるほうがスマート（ユーザーの判断）。
	// 実際に閉じるのは次の Build()。
	void CloseContextMenu() {
		if (contextMenuOpen_) closeContextMenu_ = true;
	}

	// OS の「フォルダを探す」ダイアログを開いてほしい、という要求。
	// 開いている間はアプリが止まるので、フレームを描き終えたメインループに
	// やってもらう（pendingSkin() と同じ作法）。
	bool pendingBrowse() const { return pendingBrowse_; }
	void ClearPendingBrowse() { pendingBrowse_ = false; }
	// 最初に見せるフォルダ（打ちかけのパス）。
	const std::string &browseStart() const { return browseStart_; }
	// 選ばれた **OS ネイティブのパス** を入力欄へ入れる。
	void SetBrowsedPath(const std::string &path);

	// 起動時に出た警告。ウィンドウが開く前に出たものを持ち越して、
	// 最初のフレームでダイアログとして出す（ログにも同じものが出ている）。
	// 空なら何も出ない。Init() のあとに渡すこと。
	void SetStartupWarnings(const std::vector<std::string> &lines);

	// バージョン情報の見出し（名前・版・ビルド日付・著作権表示）。
	// 文言は main.cpp が持っているので渡してもらう。
	void SetAboutHeader(const std::string &text) { aboutHeader_ = text; }

	// メニューから出た「メインループにやってもらうこと」。読んだら消える。
	// フォルダの移動と終了はメインループが状態を持っているので、
	// ここでは要求だけ返す。
	enum Request {
		kRequestNone = 0,
		kRequestSetFolder,    // ファイラーを requestedFolder() へ移す
		kRequestQuit,
	};
	Request TakeRequest() {
		const Request r = request_;
		request_ = kRequestNone;
		return r;
	}

	// kRequestSetFolder の行き先。
	const std::string &requestedFolder() const { return requestedFolder_; }

	// チュートリアル (tutorial.cpp) が吹き出しを描くのに要るもの。
	// ダイアログの字の倍率、「指で操作する」の判定結果、メニューの開閉。
	float uiScale() const { return styleScale_; }
	bool touchUi() const { return touchUi_; }
	bool contextMenuOpen() const { return contextMenuOpen_; }
	// 何かしら開いているか（ダイアログ・メニュー・終了の確認）。
	// チュートリアルは起動時の警告を閉じてから始めるので、その見張りに使う。
	bool anyDialogOpen() const { return busy() || contextMenuOpen_ || quitOpen_; }

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

	// 言語が入れ替わったフレームで 1 度だけ true。読んだら消える。
	// カタログから引いた文言を**自分で持っている**ところ（通知のラベルなど）を
	// メインループに取り直してもらうために使う。
	bool TakeLocaleChanged() {
		const bool c = localeChanged_;
		localeChanged_ = false;
		return c;
	}

	// 出力サンプリングレートが選ばれたらここに入る。0 なら変更なし。
	// レートを変えるには MXDRV とオーディオ装置を開き直すしかないので、
	// 実際の入れ替えはメインループに任せる（スキンの pendingSkin_ と同じ作法）。
	int pendingSampleRate() const { return pendingSampleRate_; }
	void ClearPendingSampleRate() { pendingSampleRate_ = 0; }

	// 日本語フォントが読めたか。読めなければ ImGui 既定の ASCII フォント。
	bool hasJapaneseFont() const { return hasJapaneseFont_; }

private:
	// 倍率か「指で操作する」が変わったら true（ダイアログの大きさも
	// 作り直すため）。
	bool ApplyScale(float scale, bool touch);

	// ダイアログを出す位置と大きさに使う ImGuiCond。ふつうは Appearing
	// （開いたときだけ中央に出し、あとは掴んで動かせる）。倍率が変わった
	// フレームと**表示サイズが変わったフレーム（画面の回転・窓のリサイズ）**
	// だけ Always にして、開いているダイアログを矩形ごと置き直す。
	// ImGui はウィンドウの矩形をピクセルで覚えているので、放っておくと
	// 回転後の画面からはみ出したままになる。**すべてのダイアログがこれを
	// 使うこと**（ImGuiCond_Appearing を直に書かない）。
	ImGuiCond placeCond() const { return relayout_ ? ImGuiCond_Always : ImGuiCond_Appearing; }
	bool relayout_;
	ImVec2 lastDisplaySize_;  // 前のフレームの io.DisplaySize

	// ダイアログの既定の大きさ。渡すのは倍率 1 倍のときの大きさで、
	// 表示倍率と「指で操作する」ぶんを掛けてから画面に収まるまで詰める。
	// h に 0 を渡すと高さは中身任せ（AlwaysAutoResize と組で使う）。
	ImVec2 DialogSize(float w, float h) const;
	// 画面を作り直す。ステータス欄は変化があったときしか描かないので、
	// 作り直したあとは Player に積み直しを頼む。
	void Rebuild(DrawScreen *draw, Player *player);

	// 今の配色を skin/<名前>/colors.ini として保存する。書き込み先は
	// ユーザーフォルダ側（同梱ぶんは読み取り専用なので触らない）。
	// 名前が今のスキンと違えば、今のスキンを土台にした新しいスキンを作り、
	// 保存したあとそのスキンへ切り替える。
	void SaveColorsAs(const std::string &name, Settings *settings, DrawScreen *draw);

	bool ready_;
	bool visible_;
	bool settingsWasVisible_;  // 前のフレームの visible_（閉じた瞬間を拾う）
	bool settingsClosed_;
	bool hasJapaneseFont_;
	// ダイアログのフォントの中身。ImGui のアトラスが参照し続けるので、
	// 終了まで持つ（FontDataOwnedByAtlas=false で 2 つの源に渡している）。
	std::vector<uint8_t> fontData_;
	float styleScale_;
	ImGuiStyle baseStyle_;

	// 指で操作する端末向けの余白。押せるところの高さが
	// kTouchTargetMm を下回らないように FramePadding.y と ItemSpacing.y を
	// 広げる（ApplyScale）。字は kTouchFontMm を下限にするだけ。
	bool touchUi_;
	// 押せるところの高さの下限（実ピクセル）。touchUi_ でなければ 0。
	float touchMinPx_;
	// そのときの字の大きさの下限（実ピクセル）。kTouchFontMm から出す。
	// 行の高さとは切り離してあるので、touchMinPx_ とは連動しない。
	float touchFontPx_;
	// 行が太くなったぶん、ダイアログも広げる倍率。ふつうは 1 倍。
	float dialogGrow_;

	// ImGui は実解像度で動かすので、SDL から届く論理座標のマウス位置を
	// この倍率で直してからバックエンドへ渡す。Build で毎フレーム更新する。
	// マウス座標を「窓の座標」から「実ピクセル」へ直す倍率（ふつう 1 倍）。
	float inputScale_;

	// 表示倍率 (%) は「入力を確定してから少し待って」適用する。
	// 操作中に適用するとウィンドウの大きさが変わり、それに合わせて
	// コントロール自身の座標も変わるので、同じ場所を押しているだけで値が
	// 行き来してしまう。
	int pendingZoom_;         // 0 = 適用待ちなし
	uint32_t zoomApplyAtMs_;  // 0 = 適用待ちなし

	AssetPaths paths_;
	Vfs *vfs_;
	// 選べるスキン。縦横切り替え（screen_orientation.md）のために、
	// 名前だけでなく**縦横どちら向けか**も持つ。
	struct SkinItem {
		std::string ref;
		bool portrait;  // 正方形は縦扱い
	};
	std::vector<SkinItem> skins_;
	std::string pendingSkin_;
	int pendingSampleRate_;

	// 縦横切り替えが有効か（起動オプションで決まる）と、いまの向き。
	// main が毎フレーム渡す。
	bool orientEnabled_;
	int orientation_;  // Screen::Orientation

	// スキン 1 つぶんの表示名。頭に縦横の印を付ける。
	std::string SkinLabel(const SkinItem &item) const;
	// スキンを選ぶドロップダウン。portraitSlot が true なら縦向きのスキンを
	// 上へまとめる。選び直されたら true。
	bool SkinCombo(const char *label, bool portraitSlot, std::string *value);

	// 言語。選べるのは同梱ぶんだけ（Init で数え上げる）。
	std::vector<LocaleInfo> locales_;
	// 選ばれた言語を実際に読み込むまでの控え。**設定ウィンドウが閉じきって
	// から**入れ替える（題名も文言なので、開いたまま替えると ImGui の
	// ポップアップの id が途中で変わる）。
	std::string pendingLocale_;
	bool localeApplyPending_;
	bool localeChanged_;
	// 言語を選んだときの控えと、実際の入れ替え。
	void SelectLocale(Settings *settings, const std::string &name);
	void ApplyLocale();

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
		return visible_ || showColors_ || showAbout_ || showFolder_ || showHelp_ ||
		       showFileSystems_ || showBookmarks_ || showStartup_;
	}

	// 操作方法のダイアログ。
	//
	// 中身はメッセージカタログの [HelpKeys] [HelpMouse]（-h の出力と同じもの）。
	// 同梱フォントはプロポーショナルなので、空白で桁は揃えられない。
	// 「キー名」と「説明」に分けて持っておき、表示するときに幅を測って揃える。
	struct HelpRow {
		std::string key;   // 見出し行のときは見出しそのもの
		std::string desc;  // 見出し行と説明の無い行では空
		bool header;

		HelpRow() : header(false) {}
	};
	void LoadHelpRows();
	void BuildHelpWindow();
	bool showHelp_;
	std::vector<HelpRow> helpRows_;

	// 配色設定のダイアログ
	void BuildColorsWindow(Settings *settings, DrawScreen *draw, Player *player);
	// その先頭。スキン名・保存・読み直す・参照元のスキン。
	void BuildSkinSaveRow(Settings *settings, DrawScreen *draw);
	bool showColors_;
	// 保存先のスキン名。開いたときに今のスキン名で埋め直す。
	char skinNameBuf_[128];
	bool skinNameReset_;
	std::string saveError_;  // 保存できなかった理由（ダイアログに出す）
	// 文言は一番下に出るので、出したフレームだけそこまでスクロールする
	// （ダイアログは縦がいっぱいで、足すと画面の外へ出てしまう）。
	bool saveErrorFresh_;
	// 上書き確認。配色設定の中に入れ子で開く。
	void BuildOverwriteWindow(Settings *settings, DrawScreen *draw);
	std::string overwriteName_;  // 確認中の名前
	bool openOverwrite_;         // 次のフレームで開く
	bool overwriteOpen_;         // いま開いている（ESC の判断に使う）
	bool closeOverwrite_;        // ESC で閉じてほしい

	// フォルダを選ぶダイアログ。選んだ結果の行き先は 2 つある。
	//   kFolderTargetFiler … ファイラーを動かす (L キー / メニュー)
	//   kFolderTargetPdx   … PDX の探索先に入れる (設定ウィンドウの [参照...])
	//   kFolderTargetBookmark … ブックマークに足す ([ブックマークの設定] の [追加])
	enum FolderTarget {
		kFolderTargetFiler = 0,
		kFolderTargetPdx,
		kFolderTargetBookmark,
	};
	// ダイアログの題名。"###" 以降が ImGui の id なので、見出しを変えても
	// 同じポップアップとして扱われる。
	const char *folderTitle() const;
	void BuildFolderWindow(Settings *settings, Filer *filer);
	FolderTarget folderTarget_;
	// 設定ウィンドウ / ブックマークの設定から呼ばれたときの往復。モーダルは
	// 入れ子にせず、呼び出し元が閉じきってからフォルダ選択を出し、閉じたら
	// 開き直す。どちらへ戻るかは folderTarget_ で分かる。
	bool folderReturnToSettings_;
	bool folderReturnToBookmarks_;
	bool folderOpenPending_;
	// 子フォルダの一覧だけ作り直す（入力欄には触らない）。**読むのは
	// 別スレッド**なので、中身が入るのはあとのフレーム (PollFolderDir)。
	void RelistFolder(const std::string &dir);
	// 別スレッドが読み終えた中身を取り込む。ダイアログを組み立てる前に
	// 毎フレーム呼ぶこと。開けなかったときは元の場所へ戻す。
	void PollFolderDir();
	// 一覧で選んだものを入力欄へ移す。中へは入らない（そこはダブルクリック）。
	void SelectFolderEntry(const std::string &path);
	// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
	void SetFolderDir(const std::string &dir);
	bool showFolder_;
	// 一覧に出しているフォルダの ref。空ならファイルシステムの選択。
	std::string folderDir_;
	std::string folderSelected_;               // 一覧で選ばれている行の ref
	// その中の子フォルダ（表示名と ref）。ファイルシステムの選択のときは
	// マウントされている FS が並ぶ。
	struct FolderEntry {
		std::string name;
		std::string ref;
	};
	std::vector<FolderEntry> folderEntries_;
	// 一覧は別スレッドで読む。外部ファイルシステムでは 1 回の List に
	// 通信が要るので、打ち込むたびにここで待つと入力ごと固まる。
	SongLoader *songLoader_;
	DirLister *folderLister_;
	// Vfs の一覧 (all_) を触る前に、それを読んでいるスレッドの手を離させる。
	// 追加 (Vfs::Add) も削除 (RemoveMounted) も、必ずこれを通してから。
	void QuiesceVfsReaders(Filer *filer);
	bool folderLoading_;
	uint32_t folderTicks_;                     // 読み始めた時刻
	// 開けなかったときの戻り先（打ち込みの途中は開けない場所を通るので、
	// そのたびに一覧が消えないよう、読めるまで前のものを出しておく）。
	bool folderHasPrev_;
	std::string folderPrevDir_;
	std::string folderPrevSelected_;
	std::vector<FolderEntry> folderPrevEntries_;
	std::string folderError_;                  // 開けなかったときの文言
	std::string requestedFolder_;              // kRequestSetFolder の行き先 (ref)
	char folderPathBuf_[512];                  // パスの入力欄

	// ファイルシステムの設定 (F3)。マウントする顔ぶれと並び順を決める。
	// 実体は Vfs のマウント一覧で、変えたら [FileSystem] へ書き戻す。
	void BuildFileSystemsWindow(Filer *filer);
	// 削除の確認。このダイアログの中に入れ子で開く。
	void BuildFsRemoveWindow(Filer *filer);
	bool showFileSystems_;
	int fsSelected_;         // 一覧で選んでいる行
	std::string fsError_;    // 「カレントは削除できない」などの文言
	// ファイルシステムの追加 (dir:)。場所は OS ネイティブのパスなので、
	// 打ち込みと **OS の「フォルダを探す」ダイアログ**で決める
	// （ファイラーのフォルダ選択は ref を選ぶための別物）。
	void BuildAddFsWindow(Filer *filer);
	void PollSafPicked(Filer *filer);
	bool safPicking_;  // SAF の選択画面を出していて、結果を待っている
	bool addFsOpen_;   // 次のフレームで開く
	bool addFsShow_;   // いま開いている（ESC の判断に使う）
	bool addFsClose_;  // ESC で閉じてほしい
	char addFsPathBuf_[512];
	std::string addFsError_;
	bool pendingBrowse_;
	std::string browseStart_;

	bool fsOpenConfirm_;     // 次のフレームで確認を開く
	bool fsConfirmOpen_;     // いま開いている（ESC の判断に使う）
	bool fsCloseConfirm_;    // ESC で閉じてほしい

	// ブックマークの設定 (F4)。控えるのはフォルダの ref で、実体は
	// Settings::bookmarks（ファイルシステムの設定と違って Vfs 側には
	// 持たない。UI が直に触っても設定と食い違わないようにするため）。
	void BuildBookmarksWindow(Settings *settings, Filer *filer);
	// 削除の確認。このダイアログの中に入れ子で開く。
	void BuildBookmarkRemoveWindow(Settings *settings);
	// Shift+M の確認。メイン画面から単独で開くので、入れ子の削除確認とは
	// ポップアップの id を分けてある（同じ id を 2 か所から開こうとすると
	// 開き直しに失敗する）。
	void BuildBookmarkToggleWindow(Settings *settings, Filer *filer);
	void BuildQuitWindow();
	// index のブックマークを開く。ファイルを指していたら「そのファイルの
	// あるフォルダ」へ直してから開く（開けなければ bmError_ に理由）。
	void OpenBookmark(Settings *settings, int index);
	// ファイラーの "Bookmarks>" から。OpenBookmark と同じだが、開けない
	// ときはそのままファイラーに開かせて、あちらの「開けなければ元の場所に
	// 留まる」に任せる（ダイアログは出ていないので bmError_ は見せられない）。
	void JumpToBookmarkRef(Settings *settings, const std::string &ref);
	// ref をブックマークに控えられるか。ファイルシステムの選択（空）と
	// "Bookmarks>" 自身は控えられない。
	bool CanBookmark(const std::string &ref) const;
	bool bmJumpPending_;       // 次の Build() で bmJumpRef_ へ移る
	std::string bmJumpRef_;
	// 同じ場所を指す行を探す。無ければ -1。
	int FindBookmark(const std::vector<std::string> &list, const std::string &ref) const;
	bool showBookmarks_;
	int bmSelected_;           // 一覧で選んでいる行。空のときは -1
	std::string bmError_;      // 「そのフォルダは見つかりません。」など
	bool bmOpenRemove_;        // 次のフレームで削除確認を開く
	bool bmRemoveOpen_;        // いま開いている（ESC の判断に使う）
	bool bmCloseRemove_;       // ESC で閉じてほしい
	bool bmOpenToggle_;        // 次のフレームで Shift+M の確認を開く
	bool bmToggleOpen_;
	bool bmCloseToggle_;
	bool quitAsk_;             // 次のフレームで終了の確認を開く
	bool quitOpen_;
	bool quitClose_;
	std::string bmToggleRef_;  // Shift+M の確認にかけている場所

	// 起動時の警告。ウィンドウが出る前の printf を持ち越したもの。
	void BuildStartupWindow();
	bool showStartup_;
	std::vector<std::string> startupLines_;

	// コンテキストメニュー
	void BuildContextMenu(Settings *settings, DrawScreen *draw, Player *player,
	                      Filer *filer);
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
