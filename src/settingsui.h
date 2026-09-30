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
//
// 実装はダイアログ単位で settingsui_*.cpp に分けてある（一覧は
// settingsui_internal.h の頭）。

#ifndef MXV2_SETTINGSUI_H
#define MXV2_SETTINGSUI_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "imgui.h"

#include "assetpath.h"
#include "message.h"
#include "updatecheck.h"

// ダイアログはそれぞれ自分のクラス（settingsui_*.h）。SettingsUi はそれを
// 1 つずつ持ち、開閉の入口と往復の配線を受け持つ（memo/settingsui-split.md）。
#include "settingsui_dialog.h"
#include "settingsui_bookmarks.h"
#include "settingsui_cast.h"
#include "settingsui_colors.h"
#include "settingsui_filesystems.h"
#include "settingsui_folder.h"
#include "settingsui_info.h"
#include "settingsui_menu.h"
#include "settingsui_latency.h"
#include "settingsui_pdx.h"
#include "settingsui_settings.h"
#include "settingsui_update.h"

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
		settings_.Open();
	}
	bool visible() const { return settings_.isOpen(); }

	// 縦横切り替えの状態。main が毎フレーム教える（screen_orientation.md）。
	void SetOrientationState(bool enabled, Screen::Orientation orientation) {
		settings_.SetOrientationState(enabled, orientation);
	}

	// 設定ウィンドウが閉じた瞬間かどうかを 1 度だけ返す。
	// 「縦横の切り替えかた」は**閉じてから**効かせる決まりなので、その合図に使う。
	bool TakeSettingsClosed() {
		return settings_.TakeClosed();
	}

	// スキンの配色を編集するダイアログ (F2)。設定ウィンドウとは独立に開閉する。
	// F2 も開くだけ。閉じるのは ESC か × ボタン（設定ウィンドウと同じ）。
	void OpenColors() {
		if (busy()) return;
		colors_.Open();
	}

	// フォルダを選ぶダイアログ (L)。旧 mxv の MX_GetNewDirFileList にあたる。
	// 原典は SHBrowseForFolder だったが Windows 専用なので、パスの打ち込みと
	// フォルダ一覧を持つ自前のダイアログにしてある。
	// dir は最初に見せるフォルダの ref（ふつうはファイラーの今の場所）。
	void OpenFolder(const std::string &dir) {
		if (busy()) return;
		folder_.Open(*this, dir);
	}

	// ファイルシステムの設定 (F3)。ファイラーのルートに並べる顔ぶれと順番を
	// 決める。ファイラーの "[Setting]" とコンテキストメニューからも開く。
	void OpenFileSystems() {
		if (busy()) return;
		fs_.Open();
	}
	// 同じダイアログを、mountRef のファイルシステムを選んだ状態で開く。
	// アクセス許可が失われた SAF をファイラーで開こうとしたときに使う
	// （[許可を取り直す…] へ導く）。
	void OpenFileSystemsFor(const std::string &mountRef) { fs_.OpenFor(*this, mountRef); }

	// ブックマークの設定 (F4)。よく開く場所を控えておいて、そこへ移る。
	// 管理が主で、ジャンプもできる。コンテキストメニューと、ファイラーの
	// "Bookmarks>" の中の "[Setting]" からも開く。
	void OpenBookmarks() {
		if (busy()) return;
		bm_.Open();
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
		bm_.RequestJump(ref);
	}

	// カレントフォルダをブックマークへ追加する / から削除する (Shift+M)。
	// どちらも確認してから実行する。ダイアログを開かずにメイン画面から
	// 直に使うので、確認だけが単独のモーダルとして出る。
	void OpenBookmarkToggle() {
		if (busy()) return;
		bm_.AskToggle();
	}

	// 終了の確認。Android の戻るキーで、もう戻る先が無いときに出す
	// （いきなり閉じると押し間違いで演奏が止まるため）。
	// [終了] を選ぶと TakeRequest() が kRequestQuit を返す。
	void OpenQuitConfirm() {
		if (busy()) return;
		ctx_.AskQuit();
	}

	// GL コンテキストが失われたあと、ImGui が持っているテクスチャを
	// 作り直させる（次のフレームで自分で作り直す）。
	void HandleDeviceReset();

	// 操作方法のダイアログ (F11 / H)。文面は main.cpp の -h と同じもの。
	void OpenHelp() {
		if (busy()) return;
		info_.OpenHelp();
	}

	// [キャスト]ダイアログ (F9)。Chromecast へ送る（settingsui_cast.cpp）。
	void OpenCast() {
		if (busy()) return;
		cast_.Open();
	}

	// バージョン情報のダイアログ (F12 / A)。
	void OpenAbout() {
		if (busy()) return;
		info_.OpenAbout();
	}

	// 開いているダイアログを閉じる。閉じるものが無ければ false。
	// ESC 用。ImGui はキーボードナビを有効にしていないとポップアップを
	// ESC で閉じてくれないので、こちらで面倒を見る。
	// 一番内側のものから閉じる。ダイアログはモーダルで一度に 1 つしか開かない
	// ので、順番が効くのは入れ子のところだけ: メニュー、更新の知らせ（設定
	// ウィンドウの中に入れ子で開くことがある）、あとは各ダイアログが自分の
	// 入れ子の確認（削除・上書き・追加）を先に閉じてから自分を閉じる。
	bool CloseDialog() {
		return ctx_.CloseTop() || update_.CloseTop() || fs_.CloseTop() || bm_.CloseTop() ||
		       pdx_.CloseTop() || colors_.CloseTop() || info_.CloseTop() || cast_.CloseTop() ||
		       latency_.CloseTop() || folder_.CloseTop() || settings_.CloseTop();
	}

	// 右クリックで開くコンテキストメニュー（旧 mxv の TrackPopupMenu 相当）。
	// 設定ウィンドウが閉じていても出す。
	void OpenContextMenu() { ctx_.Open(); }
	// 開いていれば閉じる。画面が回転してスキンが替わるときに呼ぶ。
	// 回転するとメニューが画面をはみ出すことがあり、配置を計算し直して
	// 出し直すより閉じるほうがスマート（ユーザーの判断）。
	// 実際に閉じるのは次の Build()。
	void CloseContextMenu() {
		ctx_.RequestClose();
	}

	// OS の「フォルダを探す」ダイアログを開いてほしい、という要求。
	// 開いている間はアプリが止まるので、フレームを描き終えたメインループに
	// やってもらう（pendingSkin() と同じ作法）。
	bool pendingBrowse() const { return fs_.browsePending(); }
	void ClearPendingBrowse() { fs_.ClearBrowsePending(); }
	// 最初に見せるフォルダ（打ちかけのパス）。
	const std::string &browseStart() const { return fs_.browseStart(); }
	// 選ばれた **OS ネイティブのパス** を入力欄へ入れる。
	void SetBrowsedPath(const std::string &path) { fs_.SetBrowsedPath(path); }

	// 起動時に出た警告。ウィンドウが開く前に出たものを持ち越して、
	// 最初のフレームでダイアログとして出す（ログにも同じものが出ている）。
	// 空なら何も出ない。Init() のあとに渡すこと。
	// regrantRef が空でなければ「アクセス許可が失われた SAF」の項目で、文の
	// 隣に [許可を取り直す…] を添える（押すと OS のピッカーへ。2026-09-16）。
	typedef InfoDialogs::StartupWarning StartupWarning;
	void SetStartupWarnings(const std::vector<StartupWarning> &lines) {
		info_.SetStartupWarnings(lines);
	}

	// アクセス許可が失われた SAF (mountRef) の許可を取り直す。OS のピッカーを
	// 元のフォルダで出し、同じフォルダを選び直せばそのマウントが戻る。
	// ファイラーでその行や、その先を指す控えを開いたときに main から呼ぶ。
	// ピッカーを出せないとき・取り消したとき・別のフォルダを選んだときは
	// [ファイルシステムの設定] をその行と注記つきで開く。
	void RegrantAccess(const std::string &mountRef) { fs_.RegrantAccess(*this, mountRef); }
	bool safPicking() const { return fs_.safPicking(); }

	// バージョン情報の見出し（名前・版・ビルド日付・著作権表示）。
	// 文言は main.cpp が持っているので渡してもらう。
	void SetAboutHeader(const std::string &text) { info_.SetAboutHeader(text); }

	// メニューから出た「メインループにやってもらうこと」。読んだら消える。
	// フォルダの移動と終了はメインループが状態を持っているので、
	// ここでは要求だけ返す。
	enum class Request {
		None = 0,
		SetFolder,    // ファイラーを requestedFolder() へ移す
		Quit,
		OpenHanded,   // requestedHanded() を「落とされた」のと同じ扱いで開く
		EnterPip,     // 小窓（ピクチャー・イン・ピクチャー）に入る
		StopCast,     // [キャストを終了]。手元を一時停止してからキャストを終える
	
	};
	Request TakeRequest() {
		const Request r = request_;
		request_ = Request::None;
		return r;
	}

	// kRequestSetFolder の行き先。
	const std::string &requestedFolder() const { return requestedFolder_; }
	// kRequestOpenHanded の開くもの (ref)。
	const std::string &requestedHanded() const { return fs_.handedRef(); }

	// 外から渡された MDX（openintent.h）が、許可のあるフォルダの外にあった。
	// **どちらで開くかを尋ねる**（2026-09-18、ユーザーの指示）:
	//   [フォルダを許可する…] … SAF のピッカーでそのフォルダの許可を取り、
	//     マウントしてから開く。**同じフォルダの PDX も鳴る**。
	//   [このまま演奏する]   … ユーザーフォルダへ写して開く。すぐ鳴るが
	//     **PDX は付いてこない**。
	// どちらかが決まると TakeRequest() が kRequestOpenHanded を返す。
	// name は画面に出すファイル名。
	void OpenHandedChoice(const std::string &uri, const std::string &name) {
		if (busy() || handedBusy()) return;
		fs_.AskHanded(uri, name);
	}
	// 尋ねている最中（ピッカーの結果待ちを含む）。次に渡されたものは
	// これが収まるまで待たせる。
	bool handedBusy() const { return fs_.handedBusy(); }

	// チュートリアル (tutorial.cpp) が吹き出しを描くのに要るもの。
	// ダイアログの字の倍率、「指で操作する」の判定結果、メニューの開閉。
	float uiScale() const { return styleScale_; }
	bool touchUi() const { return touchUi_; }
	bool contextMenuOpen() const { return ctx_.isOpen(); }
	// 何かしら開いているか（ダイアログ・メニュー・終了の確認）。
	// チュートリアルは起動時の警告を閉じてから始めるので、その見張りに使う。
	bool anyDialogOpen() const {
		return busy() || ctx_.isOpen() || ctx_.quitOpen() || fs_.handedOpen() ||
		       update_.popup_.open;
	}

	// ---- 更新チェック（updatecheck.h） -------------------------------------
	// 通信と「いつ確かめるか」は main が持ち、ここは見せるだけ。
	//
	// 使える環境か・いま確かめている最中か。main が毎フレーム教える。
	// 使えない環境では [mxv2 の設定] に [通信] を出さない。
	void SetUpdateCheckState(bool available, bool running) {
		update_.available_ = available;
		update_.running_ = running;
	}
	// [今すぐ更新チェックを行う] が押されたか（1 度だけ true）。
	bool TakeUpdateCheckNow() {
		const bool v = update_.checkNow_;
		update_.checkNow_ = false;
		return v;
	}
	// 結果を見せる。見せるのは「新しい版がある」ときと、[今すぐ…] から
	// 始めたときの結果（最新だった・失敗した）。どれを渡すかは main が決める。
	// 他のダイアログが開いていれば、閉じるまで預かる。ただし [今すぐ…] の
	// 結果で [mxv2 の設定] が開いたままなら、その中に重ねて出す。
	void ShowUpdateResult(const UpdateResult &r) {
		update_.result_ = r;
		update_.pending_ = true;
	}

	// 1 フレーム分の UI を組み立てる。設定の変更はその場で反映する。
	// 非表示のときも ImGui のフレームは回す必要があるので毎フレーム呼ぶ。
	void Build(Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
	           Screen *screen);

	// キーから表示倍率を 1 段変える（Ctrl と +/-）。実際の計算と適用は
	// 次の Build()（設定ダイアログの [表示倍率] とまったく同じ経路を通る
	// ので、少し待ってからウィンドウへ掛かり、ini にも保存される）。
	// **フルスクリーン・最大化の間は呼ばないこと**
	// （keybind.cpp が Screen::windowSizeLocked() で弾いている）。
	void RequestZoomStep(int step) { settings_.RequestZoomStep(step); }

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


	// 言語が入れ替わったフレームで 1 度だけ true。読んだら消える。
	// カタログから引いた文言を**自分で持っている**ところ（通知のラベルなど）を
	// メインループに取り直してもらうために使う。
	bool TakeLocaleChanged() {
		return settings_.TakeLocaleChanged();
	}

	// 出力サンプリングレートが選ばれたらここに入る。0 なら変更なし。
	// レートを変えるには MXDRV とオーディオ装置を開き直すしかないので、
	// 実際の入れ替えはメインループに任せる（スキンの pendingSkin_ と同じ作法）。
	int pendingSampleRate() const { return settings_.pendingSampleRate(); }
	void ClearPendingSampleRate() { settings_.ClearPendingSampleRate(); }

	// スキンの一覧を取り直す（フォルダを足したとき用）。
	void ScanSkins() { settings_.ScanSkins(*this); }

	// 日本語フォントが読めたか。読めなければ ImGui 既定の ASCII フォント。
	bool hasJapaneseFont() const { return hasJapaneseFont_; }

private:
	// 倍率か「指で操作する」か画面の高さが変わったら true（ダイアログの
	// 大きさも作り直すため）。画面の高さは行の高さを詰めるかどうかに使う
	// （kTouchRowsMin）。
	bool ApplyScale(float scale, bool touch, float displayH);

	// ダイアログを出す位置と大きさに使う ImGuiCond。ふつうは Appearing
	// （開いたときだけ中央に出し、あとは掴んで動かせる。動かせるのはパソコン
	// だけ——DialogFlags）。倍率が変わった
	// フレームと**表示サイズが変わったフレーム（画面の回転・ウィンドウのリサイズ）**
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
	// ダイアログ（BeginPopupModal）に渡す共通のフラグ。**すべてのダイアログが
	// これを使うこと**（個別に足したいフラグは | で足す）。
	// リサイズはどの環境でも無効。指で操作するとき（touchUi_）は移動も無効
	// （2026-09-24、ユーザーの指示）。パソコンは移動だけ残す——[配色設定] で
	// 色を詰めるとき、ダイアログをどかして元の画面と見比べるため
	// （ModalWindowDimBg を透明にしてあるのも同じ理由）。
	ImGuiWindowFlags DialogFlags() const;
	// 題名の帯に × のあるダイアログ（p_open を渡すもの）は BeginPopupModal の
	// 代わりにこれを使う。指で操作するとき（touchUi_）は、× を帯の左端に自前で
	// 描き（題名は中央寄せ）、当たり判定を広げる。ImGui の × は右端に決め打ちで、
	// 字の大きさの正方形でしか反応せず、小さい画面では押しにくかった（r77 の
	// 480x640、2026-09-30、ユーザーの指摘）。
	bool BeginDialog(const char *title, bool *p_open, ImGuiWindowFlags flags);
	// ダイアログを開く定型をまとめたもの: SyncModal → 置き場所 → 大きさ →
	// BeginDialog。false なら何もせず return する（開いていない、または ImGui が
	// このフレームは組まない）。true なら中身を組んで ImGui::EndPopup() すること。
	// w, h は DialogSize に渡す倍率 1 倍のときの大きさ（h=0 は中身任せ。そのとき
	// は AlwaysAutoResize を足す）。extra は DialogFlags に足すフラグ。
	// 置き場所は画面の中央。それ以外（[遅延時間] のファイラー側）は各自で
	// SetNextWindowPos してから BeginDialog を呼ぶ。
	bool BeginModal(const char *title, bool *show, float w, float h, ImGuiWindowFlags extra = 0);

	// ダイアログのクラスは SettingsUi の中（共通の道具・main への出力・
	// 他のダイアログ）に手が届く。逆に SettingsUi も各クラスの中を触る
	// （往復の配線）ので、向こうも SettingsUi を friend にしている。
	friend class CastDialog;
	friend class UpdateDialog;
	friend class InfoDialogs;
	friend class PdxPathsDialog;
	friend class LatencyDialog;
	friend class BookmarksDialog;
	friend class ColorsDialog;
	friend class ContextMenu;
	friend class FolderDialog;
	friend class FileSystemsDialog;
	friend class SettingsWindow;
	friend struct NestedPopup;  // 入れ子の確認の開き方（placeCond / DialogFlags）

	bool ready_;
	bool hasJapaneseFont_;
	// ダイアログのフォントの中身。ImGui のアトラスが参照し続けるので、
	// 終了まで持つ（FontDataOwnedByAtlas=false で 2 つの源に渡している）。
	std::vector<uint8_t> fontData_;
	float styleScale_;
	float styleDisplayH_;  // ApplyScale が見た画面の高さ（実ピクセル）
	ImGuiStyle baseStyle_;

	// 指で操作する端末向けの余白。押せるところの高さが
	// kTouchTargetMm を下回らないように FramePadding.y と ItemSpacing.y を
	// 広げる（ApplyScale）。字は kTouchFontMm を下限にするだけ。
	// 画面の縦が足りない端末では、どちらも kTouchRowsMin に合わせて詰める。
	bool touchUi_;
	// 押せるところの高さの下限（実ピクセル）。touchUi_ でなければ 0。
	float touchMinPx_;
	// そのときの字の大きさの下限（実ピクセル）。kTouchFontMm から出す。
	// 行の高さとは切り離してあるので、touchMinPx_ とは連動しない
	// （行を詰めたときに kTouchFontRowRatio で頭打ちにするだけ）。
	float touchFontPx_;
	// 行が太くなったぶん、ダイアログも広げる倍率。ふつうは 1 倍。
	float dialogGrow_;

	// ImGui は実解像度で動かすので、SDL から届く論理座標のマウス位置を
	// この倍率で直してからバックエンドへ渡す。Build で毎フレーム更新する。
	// マウス座標を「ウィンドウの座標」から「実ピクセル」へ直す倍率（ふつう 1 倍）。
	float inputScale_;

	AssetPaths paths_;
	Vfs *vfs_;
	// 設定ウィンドウを開く・閉じる（別のダイアログとの往復で、閉じきってから
	// 相手を開くときに使う。settingsui_pdx.cpp / settingsui_latency.cpp）。
	void SetSettingsVisible(bool v) { settings_.SetVisible(v); }

	// [mxv2 の設定] (F1)。settingsui_settings.h。
	SettingsWindow settings_;

	// ユーザーが触った項目。TakeChangedFields() で取り出す。
	unsigned changedFields_;

	// モーダルの開閉を ImGui のポップアップ状態と合わせる。
	bool SyncModal(const char *title, bool *wanted);
	// 自分が開けたモーダルの題名 (ポインタ比較)。0 なら開けていない。
	const char *openedModal_;
	// どれか 1 つでもダイアログが開いているか。モーダルなので、開いている
	// 間は別のものを開けない（先にそれを閉じてもらう）。
	bool busy() const {
		return settings_.visible_ || colors_.isOpen() || folder_.isOpen() || fs_.isOpen() ||
		       bm_.isOpen() ||
		       pdx_.isOpen() || latency_.isOpen() || cast_.isOpen() || info_.isOpen();
	}

	// [配色設定] (F2)。settingsui_colors.h。
	ColorsDialog colors_;

	// フォルダを選ぶ (L)。settingsui_folder.h。[追加…] からの往復は
	// FolderDialog::OpenFor で頼み、閉じたら Build() が呼び出し元を開き直す。
	FolderDialog folder_;
	void OpenFolderFor(FolderDialog::Target target, FolderDialog::Return returnTo) {
		folder_.OpenFor(target, returnTo);
	}
	// 曲の読み込みスレッド（FileSystemsDialog がファイルシステムを取り外す前に
	// 止める相手）。
	SongLoader *songLoader_;

	// [ファイルシステムの設定] (F3)・SAF・渡されたファイル。settingsui_filesystems.h。
	FileSystemsDialog fs_;

	// ブックマークについて、ダイアログ以外（フォルダ選択・メニュー）からも
	// 使う道具。ref をブックマークに控えられるか（ファイルシステムの選択（空）と
	// "Bookmarks>" 自身は控えられない）と、同じ場所を指す行を探す（無ければ -1）。
	bool CanBookmark(const std::string &ref) const;
	int FindBookmark(const std::vector<std::string> &list, const std::string &ref) const;
	// [ブックマークの設定] (F4) と Shift+M の確認。settingsui_bookmarks.h。
	BookmarksDialog bm_;
	// PDX の探索先の設定。設定ウィンドウの [編集…] から開く。作りはブックマークの
	// 設定と同じ（一覧・[上へ][下へ][追加…][削除]、削除は入れ子の確認）で、
	// [開く] は無い。モーダル同士は入れ子にしないので、設定ウィンドウが閉じきって
	// から開き（pdx_.handoff_.openPending）、閉じたら設定ウィンドウを開き直す
	// （pdx_.handoff_.returnToCaller。[追加…] のフォルダ選択の往復をまたいでも保つ）。
	// 2026-09-16、ユーザーの指示（それまでは設定ウィンドウの打ち込み欄 1 本）。
	PdxPathsDialog pdx_;
	// [画面の遅れ] / [遅延時間]。settingsui_latency.h。
	LatencyDialog latency_;

	// 更新チェックの知らせと [通信]。settingsui_update.h。
	UpdateDialog update_;

	// コンテキストメニューと終了の確認。settingsui_menu.h。
	ContextMenu ctx_;
	Request request_;
	std::string requestedFolder_;  // kRequestSetFolder の行き先 (ref)
	// 選ばれたスキン（[mxv2 の設定] と [配色設定] の保存から）。main が画面を
	// 作り直して ClearPendingSkin() で消す。
	std::string pendingSkin_;
	// バージョン情報 (F12)・操作方法 (F11)・起動時の警告。settingsui_info.h。
	InfoDialogs info_;

	// [キャスト] (F9)。settingsui_cast.h。
	CastDialog cast_;
	// 中身をドラッグしてスクロール中。ダイアログはモーダルで一度に 1 つしか
	// 開かないので、どのダイアログでもこの 1 つを使い回す。
	bool dragScroll_;
	// そのドラッグで実際にスクロールしたか。一覧の上から掴めるようにした
	// ぶん、離したときに行を選ばせないための印。
	bool dragMoved_;

	SettingsUi(const SettingsUi &);
	SettingsUi &operator=(const SettingsUi &);
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_H
