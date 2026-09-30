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
		settings_.visible = true;
	}
	bool visible() const { return settings_.visible; }

	// 縦横切り替えの状態。main が毎フレーム教える（screen_orientation.md）。
	void SetOrientationState(bool enabled, int orientation) {
		settings_.orientEnabled = enabled;
		settings_.orientation = orientation;
	}

	// 設定ウィンドウが閉じた瞬間かどうかを 1 度だけ返す。
	// 「縦横の切り替えかた」は**閉じてから**効かせる決まりなので、その合図に使う。
	bool TakeSettingsClosed() {
		const bool v = settings_.closed;
		settings_.closed = false;
		return v;
	}

	// スキンの配色を編集するダイアログ (F2)。設定ウィンドウとは独立に開閉する。
	// F2 も開くだけ。閉じるのは ESC か × ボタン（設定ウィンドウと同じ）。
	void OpenColors() {
		if (busy()) return;
		colors_.show = true;
		// 名前の欄は、開いたときに今のスキン名で埋め直す。
		colors_.skinNameReset = true;
	}

	// フォルダを選ぶダイアログ (L)。旧 mxv の MX_GetNewDirFileList にあたる。
	// 原典は SHBrowseForFolder だったが Windows 専用なので、パスの打ち込みと
	// フォルダ一覧を持つ自前のダイアログにしてある。
	// dir は最初に見せるフォルダの ref（ふつうはファイラーの今の場所）。
	void OpenFolder(const std::string &dir) {
		if (busy()) return;
		folder_.target = kFolderTargetFiler;
		folder_.returnTo = kFolderReturnNone;
		SetFolderDir(dir);
		folder_.show = true;
	}

	// ファイルシステムの設定 (F3)。ファイラーのルートに並べる顔ぶれと順番を
	// 決める。ファイラーの "[Setting]" とコンテキストメニューからも開く。
	void OpenFileSystems() {
		if (busy()) return;
		fs_.show = true;
		fs_.selected = 0;
	}
	// 同じダイアログを、mountRef のファイルシステムを選んだ状態で開く。
	// アクセス許可が失われた SAF をファイラーで開こうとしたときに使う
	// （[許可を取り直す…] へ導く。実体は settingsui.cpp）。
	void OpenFileSystemsFor(const std::string &mountRef);

	// ブックマークの設定 (F4)。よく開く場所を控えておいて、そこへ移る。
	// 管理が主で、ジャンプもできる。コンテキストメニューと、ファイラーの
	// "Bookmarks>" の中の "[Setting]" からも開く。
	void OpenBookmarks() {
		if (busy()) return;
		bm_.show = true;
		bm_.selected = 0;
		bm_.error.clear();
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
		bm_.jumpRef = ref;
		bm_.jumpPending = true;
	}

	// カレントフォルダをブックマークへ追加する / から削除する (Shift+M)。
	// どちらも確認してから実行する。ダイアログを開かずにメイン画面から
	// 直に使うので、確認だけが単独のモーダルとして出る。
	void OpenBookmarkToggle() {
		if (busy()) return;
		bm_.toggle.ask = true;
	}

	// 終了の確認。Android の戻るキーで、もう戻る先が無いときに出す
	// （いきなり閉じると押し間違いで演奏が止まるため）。
	// [終了] を選ぶと TakeRequest() が kRequestQuit を返す。
	void OpenQuitConfirm() {
		if (busy()) return;
		ctx_.quit.ask = true;
	}

	// GL コンテキストが失われたあと、ImGui が持っているテクスチャを
	// 作り直させる（次のフレームで自分で作り直す）。
	void HandleDeviceReset();

	// 操作方法のダイアログ (F11 / H)。文面は main.cpp の -h と同じもの。
	void OpenHelp() {
		if (busy()) return;
		info_.showHelp = true;
	}

	// [キャスト]ダイアログ (F9)。Chromecast へ送る（settingsui_cast.cpp）。
	void OpenCast() {
		if (busy()) return;
		cast_.show = true;
	}

	// バージョン情報のダイアログ (F12 / A)。
	void OpenAbout() {
		if (busy()) return;
		info_.showAbout = true;
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
	void OpenContextMenu() { ctx_.openRequested = true; }
	// 開いていれば閉じる。画面が回転してスキンが替わるときに呼ぶ。
	// 回転するとメニューが画面をはみ出すことがあり、配置を計算し直して
	// 出し直すより閉じるほうがスマート（ユーザーの判断）。
	// 実際に閉じるのは次の Build()。
	void CloseContextMenu() {
		if (ctx_.isOpen) ctx_.closeWanted = true;
	}

	// OS の「フォルダを探す」ダイアログを開いてほしい、という要求。
	// 開いている間はアプリが止まるので、フレームを描き終えたメインループに
	// やってもらう（pendingSkin() と同じ作法）。
	bool pendingBrowse() const { return fs_.addFs.browsePending; }
	void ClearPendingBrowse() { fs_.addFs.browsePending = false; }
	// 最初に見せるフォルダ（打ちかけのパス）。
	const std::string &browseStart() const { return fs_.addFs.browseStart; }
	// 選ばれた **OS ネイティブのパス** を入力欄へ入れる。
	void SetBrowsedPath(const std::string &path);

	// 起動時に出た警告。ウィンドウが開く前に出たものを持ち越して、
	// 最初のフレームでダイアログとして出す（ログにも同じものが出ている）。
	// 空なら何も出ない。Init() のあとに渡すこと。
	// regrantRef が空でなければ「アクセス許可が失われた SAF」の項目で、文の
	// 隣に [許可を取り直す…] を添える（押すと OS のピッカーへ。2026-09-16）。
	struct StartupWarning {
		std::string text;
		std::string regrantRef;  // 取り直す相手の mountRef。無ければ空
		bool done;               // 取り直せた（ボタンを消して文を差し替える）
		StartupWarning() : done(false) {}
	};
	void SetStartupWarnings(const std::vector<StartupWarning> &lines);

	// アクセス許可が失われた SAF (mountRef) の許可を取り直す。OS のピッカーを
	// 元のフォルダで出し、同じフォルダを選び直せばそのマウントが戻る。
	// ファイラーでその行や、その先を指す控えを開いたときに main から呼ぶ。
	// ピッカーを出せないとき・取り消したとき・別のフォルダを選んだときは
	// [ファイルシステムの設定] をその行と注記つきで開く。
	void RegrantAccess(const std::string &mountRef);
	// 同じことを、ダイアログの [許可を取り直す…] から（settingsui_filesystems.cpp）。
	// ピッカーを出せたら true。fromFiler は RegrantAccess を見ること。
	bool StartSafRegrant(const std::string &mountRef, bool fromFiler);
	bool safPicking() const { return fs_.saf.picking; }

	// バージョン情報の見出し（名前・版・ビルド日付・著作権表示）。
	// 文言は main.cpp が持っているので渡してもらう。
	void SetAboutHeader(const std::string &text) { info_.aboutHeader = text; }

	// メニューから出た「メインループにやってもらうこと」。読んだら消える。
	// フォルダの移動と終了はメインループが状態を持っているので、
	// ここでは要求だけ返す。
	enum Request {
		kRequestNone = 0,
		kRequestSetFolder,    // ファイラーを requestedFolder() へ移す
		kRequestQuit,
		kRequestOpenHanded,   // requestedHanded() を「落とされた」のと同じ扱いで開く
		kRequestEnterPip,     // 小窓（ピクチャー・イン・ピクチャー）に入る
		kRequestStopCast,     // [キャストを終了]。手元を一時停止してからキャストを終える
	};
	Request TakeRequest() {
		const Request r = request_;
		request_ = kRequestNone;
		return r;
	}

	// kRequestSetFolder の行き先。
	const std::string &requestedFolder() const { return requestedFolder_; }
	// kRequestOpenHanded の開くもの (ref)。
	const std::string &requestedHanded() const { return fs_.handed.ref; }

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
		fs_.handed.uri = uri;
		fs_.handed.name = name;
		fs_.handed.popup.ask = true;
	}
	// 尋ねている最中（ピッカーの結果待ちを含む）。次に渡されたものは
	// これが収まるまで待たせる。
	bool handedBusy() const {
		return fs_.handed.popup.ask || fs_.handed.popup.open || !fs_.saf.handedUri.empty();
	}

	// チュートリアル (tutorial.cpp) が吹き出しを描くのに要るもの。
	// ダイアログの字の倍率、「指で操作する」の判定結果、メニューの開閉。
	float uiScale() const { return styleScale_; }
	bool touchUi() const { return touchUi_; }
	bool contextMenuOpen() const { return ctx_.isOpen; }
	// 何かしら開いているか（ダイアログ・メニュー・終了の確認）。
	// チュートリアルは起動時の警告を閉じてから始めるので、その見張りに使う。
	bool anyDialogOpen() const {
		return busy() || ctx_.isOpen || ctx_.quit.open || fs_.handed.popup.open ||
		       update_.popup.open;
	}

	// ---- 更新チェック（updatecheck.h） -------------------------------------
	// 通信と「いつ確かめるか」は main が持ち、ここは見せるだけ。
	//
	// 使える環境か・いま確かめている最中か。main が毎フレーム教える。
	// 使えない環境では [mxv2 の設定] に [通信] を出さない。
	void SetUpdateCheckState(bool available, bool running) {
		update_.available = available;
		update_.running = running;
	}
	// [今すぐ更新チェックを行う] が押されたか（1 度だけ true）。
	bool TakeUpdateCheckNow() {
		const bool v = update_.checkNow;
		update_.checkNow = false;
		return v;
	}
	// 結果を見せる。見せるのは「新しい版がある」ときと、[今すぐ…] から
	// 始めたときの結果（最新だった・失敗した）。どれを渡すかは main が決める。
	// 他のダイアログが開いていれば、閉じるまで預かる。ただし [今すぐ…] の
	// 結果で [mxv2 の設定] が開いたままなら、その中に重ねて出す。
	void ShowUpdateResult(const UpdateResult &r) {
		update_.result = r;
		update_.pending = true;
	}

	// 1 フレーム分の UI を組み立てる。設定の変更はその場で反映する。
	// 非表示のときも ImGui のフレームは回す必要があるので毎フレーム呼ぶ。
	void Build(Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
	           Screen *screen);

	// キーから表示倍率を 1 段変える（Ctrl と +/-）。実際の計算と適用は
	// 次の Build()（設定ダイアログの [表示倍率] とまったく同じ経路を通る
	// ので、少し待ってから窓へ掛かり、ini にも保存される）。
	// **フルスクリーン・最大化の間は呼ばないこと**
	// （keybind.cpp が Screen::windowSizeLocked() で弾いている）。
	void RequestZoomStep(int step) { settings_.zoomStepRequest += step; }

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
		const bool c = settings_.localeChanged;
		settings_.localeChanged = false;
		return c;
	}

	// 出力サンプリングレートが選ばれたらここに入る。0 なら変更なし。
	// レートを変えるには MXDRV とオーディオ装置を開き直すしかないので、
	// 実際の入れ替えはメインループに任せる（スキンの pendingSkin_ と同じ作法）。
	int pendingSampleRate() const { return settings_.pendingSampleRate; }
	void ClearPendingSampleRate() { settings_.pendingSampleRate = 0; }

	// 日本語フォントが読めたか。読めなければ ImGui 既定の ASCII フォント。
	bool hasJapaneseFont() const { return hasJapaneseFont_; }

private:
	// 倍率か「指で操作する」か画面の高さが変わったら true（ダイアログの
	// 大きさも作り直すため）。画面の高さは行の高さを詰めるかどうかに使う
	// （kTouchRowsMin）。
	bool ApplyScale(float scale, bool touch, float displayH);
	// [mxv2 の設定] の本体 (settingsui_settings.cpp)。Build() がフレームの
	// 支度とダイアログの呼び出しを済ませてから呼ぶ。
	void BuildSettingsWindow(Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
	                         Screen *screen);
	// RequestZoomStep() で頼まれた増減を settings へ入れる。Build() の頭から
	// 1 回だけ呼ぶ（settingsui_settings.cpp。倍率まわりを 1 か所に集める）。
	void ApplyZoomStep(Settings *settings);

	// ダイアログを出す位置と大きさに使う ImGuiCond。ふつうは Appearing
	// （開いたときだけ中央に出し、あとは掴んで動かせる。動かせるのはパソコン
	// だけ——DialogFlags）。倍率が変わった
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
	// 画面を作り直す。ステータス欄は変化があったときしか描かないので、
	// 作り直したあとは Player に積み直しを頼む。
	void Rebuild(DrawScreen *draw, Player *player);

	// 今の配色を skin/<名前>/colors.ini として保存する。書き込み先は
	// ユーザーフォルダ側（同梱ぶんは読み取り専用なので触らない）。
	// 名前が今のスキンと違えば、今のスキンを土台にした新しいスキンを作り、
	// 保存したあとそのスキンへ切り替える。
	void SaveColorsAs(const std::string &name, Settings *settings, DrawScreen *draw);

	// ---- ダイアログごとの状態の共通の型（memo/settingsui-split.md） -------------
	// 状態は 1 ファイル（settingsui_*.cpp）につき 1 つの struct にまとめ、
	// SettingsUi はそれを 1 つずつ持つ。struct の中身を別のファイルから直接
	// 触らない（開く・閉じるは SettingsUi のメンバ関数を通す）。

	// 入れ子で開く確認ダイアログ（上書き・削除・終了・渡されたファイル・更新の
	// 知らせなど）。ImGui のポップアップは組み立ての途中でしか開閉できないので、
	// 「次のフレームで開いて」「いま開いている」「ESC で閉じてほしい」の 3 つで
	// 受け渡す。
	struct NestedPopup {
		bool ask;    // 次のフレームで開く
		bool open;   // いま開いている（ESC の判断に使う）
		bool close;  // ESC で閉じてほしい
		NestedPopup() : ask(false), open(false), close(false) {}
		// CloseDialog() 用。開いていれば閉じる印を付けて true。
		bool CloseTop() {
			if (!open) return false;
			close = true;
			return true;
		}
	};
	// 設定ウィンドウなどから開き、閉じたら呼び出し元へ戻る往復。モーダルは
	// 入れ子にしないので、呼び出し元が閉じきってから開き、閉じたら開き直す。
	struct Handoff {
		bool openPending;     // 呼び出し元が閉じきったら開く
		bool returnToCaller;  // 閉じたら呼び出し元を開き直す
		Handoff() : openPending(false), returnToCaller(false) {}
		void Start() { openPending = returnToCaller = true; }
	};

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
	// マウス座標を「窓の座標」から「実ピクセル」へ直す倍率（ふつう 1 倍）。
	float inputScale_;

	AssetPaths paths_;
	Vfs *vfs_;
	// 選べるスキン。縦横切り替え（screen_orientation.md）のために、
	// 名前だけでなく**縦横どちら向けか**も持つ。
	struct SkinItem {
		std::string ref;
		bool portrait;  // 正方形は縦扱い
	};
	// スキン 1 つぶんの表示名。頭に縦横の印を付ける。
	std::string SkinLabel(const SkinItem &item) const;
	// スキンを選ぶドロップダウン。portraitSlot が true なら縦向きのスキンを
	// 上へまとめる。選び直されたら true。
	bool SkinCombo(const char *label, bool portraitSlot, std::string *value);
	// 言語を選んだときの控えと、実際の入れ替え。
	void SelectLocale(Settings *settings, const std::string &name);
	void ApplyLocale();
	// 設定ウィンドウを開く・閉じる（別のダイアログとの往復で、閉じきってから
	// 相手を開くときに使う。settingsui_pdx.cpp / settingsui_latency.cpp）。
	void SetSettingsVisible(bool v) { settings_.visible = v; }

	// [mxv2 の設定] 本体の状態（settingsui_settings.cpp）。
	struct SettingsWindowState {
		bool visible;
		bool wasVisible;  // 前のフレームの visible（閉じた瞬間を拾う）
		bool closed;      // 閉じた瞬間（TakeSettingsClosed で 1 度だけ読める）
		// 表示倍率 (%) は「入力を確定してから少し待って」適用する。
		// 操作中に適用するとウィンドウの大きさが変わり、それに合わせて
		// コントロール自身の座標も変わるので、同じ場所を押しているだけで値が
		// 行き来してしまう。
		int pendingZoom;         // 0 = 適用待ちなし
		uint32_t zoomApplyAtMs;  // 0 = 適用待ちなし
		// キー (Ctrl +/-) から頼まれた増減 (%)。0 = 頼まれていない。
		// 押しっぱなしのリピートで何回来ても、次の Build() でまとめて足す。
		int zoomStepRequest;
		std::vector<SkinItem> skins;
		int pendingSampleRate;  // 選ばれた出力レート。0 なら変更なし
		// 縦横切り替えが有効か（起動オプションで決まる）と、いまの向き。
		// main が毎フレーム渡す。
		bool orientEnabled;
		int orientation;  // Screen::Orientation（Screen はここでは前方宣言だけなので
		                  // 既定値は SettingsUi のコンストラクタが入れる）
		// 言語。選べるのは同梱ぶんだけ（Init で数え上げる）。
		std::vector<LocaleInfo> locales;
		// 選ばれた言語を実際に読み込むまでの控え。**設定ウィンドウが閉じきって
		// から**入れ替える（題名も文言なので、開いたまま替えると ImGui の
		// ポップアップの id が途中で変わる）。
		std::string pendingLocale;
		bool localeApplyPending;
		bool localeChanged;
		bool CloseTop() {
			if (!visible) return false;
			visible = false;
			return true;
		}
		SettingsWindowState()
		    : visible(false), wasVisible(false), closed(false), pendingZoom(0), zoomApplyAtMs(0),
		      zoomStepRequest(0), pendingSampleRate(0), orientEnabled(false), orientation(0),
		      localeApplyPending(false), localeChanged(false) {}
	};
	SettingsWindowState settings_;

	// ユーザーが触った項目。TakeChangedFields() で取り出す。
	unsigned changedFields_;

	// モーダルの開閉を ImGui のポップアップ状態と合わせる。
	bool SyncModal(const char *title, bool *wanted);
	// 自分が開けたモーダルの題名 (ポインタ比較)。0 なら開けていない。
	const char *openedModal_;
	// どれか 1 つでもダイアログが開いているか。モーダルなので、開いている
	// 間は別のものを開けない（先にそれを閉じてもらう）。
	bool busy() const {
		return settings_.visible || colors_.show || folder_.show || fs_.show || bm_.show ||
		       pdx_.show || latency_.show || cast_.show || info_.showAbout || info_.showHelp ||
		       info_.showStartup;
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

	// 配色設定のダイアログ
	void BuildColorsWindow(Settings *settings, DrawScreen *draw, Player *player);
	// その先頭。スキン名・保存・読み直す・参照元のスキン。
	void BuildSkinSaveRow(Settings *settings, DrawScreen *draw);
	// 上書き確認。配色設定の中に入れ子で開く。
	void BuildOverwriteWindow(Settings *settings, DrawScreen *draw);
	struct ColorsDialogState {
		bool show;
		// 保存先のスキン名。開いたときに今のスキン名で埋め直す。
		char skinNameBuf[128];
		bool skinNameReset;
		std::string saveError;  // 保存できなかった理由（ダイアログに出す）
		// 文言は一番下に出るので、出したフレームだけそこまでスクロールする
		// （ダイアログは縦がいっぱいで、足すと画面の外へ出てしまう）。
		bool saveErrorFresh;
		NestedPopup overwrite;      // 上書き確認
		std::string overwriteName;  // 確認中の名前
		bool CloseTop() {
			if (overwrite.CloseTop()) return true;
			if (!show) return false;
			show = false;
			return true;
		}
		ColorsDialogState() : show(false), skinNameReset(true), saveErrorFresh(false) {
			skinNameBuf[0] = '\0';
		}
	};
	ColorsDialogState colors_;

	// フォルダを選ぶダイアログ。選んだ結果の行き先は 2 つある。
	//   kFolderTargetFiler … ファイラーを動かす (L キー / メニュー)
	//   kFolderTargetPdx   … PDX の探索先に足す ([PDX の探索先] の [追加…])
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
	// ブックマークの設定 / PDX の探索先の [追加…] から。モーダルは入れ子にせず、
	// 呼び出し元が閉じきってからフォルダ選択を出し（Build()）、閉じたら
	// returnTo の呼び出し元を開き直す。呼び出し元は自分を閉じること。
	enum FolderReturn {
		kFolderReturnNone = 0,
		kFolderReturnBookmarks,
		kFolderReturnPdx,
	};
	void OpenFolderFor(FolderTarget target, FolderReturn returnTo) {
		folder_.target = target;
		folder_.returnTo = returnTo;
		folder_.openPending = true;
	}
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
	// Vfs の一覧 (all_) を触る前に、それを読んでいるスレッドの手を離させる。
	// 追加 (Vfs::Add) も削除 (RemoveMounted) も、必ずこれを通してから。
	void QuiesceVfsReaders(Filer *filer);
	// 曲の読み込みスレッド（QuiesceVfsReaders が止める相手）。
	SongLoader *songLoader_;
	// 子フォルダ（表示名と ref）。ファイルシステムの選択のときはマウントされて
	// いる FS が並ぶ。
	struct FolderEntry {
		std::string name;
		std::string ref;
	};
	struct FolderDialogState {
		bool show;
		FolderTarget target;
		FolderReturn returnTo;
		bool openPending;      // 呼び出し元が閉じきったら開く（OpenFolderFor）
		// 一覧に出しているフォルダの ref。空ならファイルシステムの選択。
		std::string dir;
		std::string selected;  // 一覧で選ばれている行の ref
		std::vector<FolderEntry> entries;
		// 一覧は別スレッドで読む。外部ファイルシステムでは 1 回の List に
		// 通信が要るので、打ち込むたびにここで待つと入力ごと固まる。
		// 実体は SettingsUi のコンストラクタが作る（ここでは型が不完全）。
		DirLister *lister;
		bool loading;
		uint32_t ticks;        // 読み始めた時刻
		// 開けなかったときの戻り先（打ち込みの途中は開けない場所を通るので、
		// そのたびに一覧が消えないよう、読めるまで前のものを出しておく）。
		bool hasPrev;
		std::string prevDir;
		std::string prevSelected;
		std::vector<FolderEntry> prevEntries;
		std::string error;  // 開けなかったときの文言
		char pathBuf[512];  // パスの入力欄
		bool CloseTop() {
			if (!show) return false;
			show = false;
			return true;
		}
		FolderDialogState()
		    : show(false), target(kFolderTargetFiler), returnTo(kFolderReturnNone),
		      openPending(false), lister(0), loading(false), ticks(0), hasPrev(false) {
			pathBuf[0] = '\0';
		}
	};
	FolderDialogState folder_;

	// ファイルシステムの設定 (F3)。マウントする顔ぶれと並び順を決める。
	// 実体は Vfs のマウント一覧で、変えたら [FileSystem] へ書き戻す。
	void BuildFileSystemsWindow(Filer *filer);
	// 削除の確認。このダイアログの中に入れ子で開く。
	void BuildFsRemoveWindow(Filer *filer);
	// ファイルシステムの追加 (dir:)。場所は OS ネイティブのパスなので、
	// 打ち込みと **OS の「フォルダを探す」ダイアログ**で決める
	// （ファイラーのフォルダ選択は ref を選ぶための別物）。
	void BuildAddFsWindow(Filer *filer);
	void PollSafPicked(Filer *filer);
	// 選び終わったツリーをマウントに反映する（PollSafPicked の本体）。
	void ApplyPickedTree(Filer *filer, const std::string &uri, const std::string &regrant,
	                     bool fromFiler, bool handed);
	// 外から渡された MDX の「どちらで開くか」。OpenHandedChoice を見ること。
	void BuildHandedWindow(Filer *filer);
	// OS の許可は残っているのに一覧から外れているツリーなら、一覧へ戻して
	// ref を作る（尋ねない）。戻せなければ false。
	bool MountGrantedTree(Filer *filer, const std::string &uri, std::string *ref);
	// ピッカーから戻ったあとの始末（許可が取れていれば saf: で、
	// 取り消されたらもう一度尋ねる）。
	void FinishHandedAfterPick(bool cancelled);
	// 写して開く（[このまま演奏する] と、許可を取っても見つからなかったとき）。
	void OpenHandedByCopy();
	struct FileSystemsState {
		bool show;
		int selected;       // 一覧で選んでいる行
		std::string error;  // 「カレントは削除できない」などの文言
		// 削除の確認（ダイアログの中に入れ子）。「OS のアクセス許可も取り消す」は
		// SAF のときだけ出す。[削除] はマウントを外すだけで許可は残るので、残すと
		// 「一覧に無いのに許可はある」状態になる（openintent.h の自動マウント）。
		struct Remove {
			NestedPopup popup;
			bool revoke;
			Remove() : revoke(true) {}
		} remove;
		// ファイルシステムの追加 (dir:)。場所は OS ネイティブのパスなので、打ち込みと
		// OS の「フォルダを探す」ダイアログ（browsePending。main が出す）で決める。
		struct AddFs {
			NestedPopup popup;
			char pathBuf[512];
			std::string error;
			bool browsePending;
			std::string browseStart;
			AddFs() : browsePending(false) { pathBuf[0] = '\0'; }
		} addFs;
		// 外から渡された MDX の「どちらで開くか」（OpenHandedChoice）。
		struct Handed {
			NestedPopup popup;
			std::string uri;   // 尋ねている相手（渡された URI）
			std::string name;  // 画面に出す名前
			std::string ref;   // kRequestOpenHanded の開くもの
		} handed;
		// SAF のピッカー（Android）。
		struct Saf {
			bool picking;  // 選択画面を出していて、結果を待っている
			// 結果を待っている渡された URI（空なら [追加…] などの普通の選択）。
			std::string handedUri;
			// [許可を取り直す…] で出した選択画面なら、取り直す相手の mountRef。
			// 空なら [追加…]（新しくマウントする）。
			std::string regrantRef;
			// ファイラー（RegrantAccess）から出したピッカーか。うまくいかなかった
			// ときに [ファイルシステムの設定] へ落とすかどうかの印。
			bool regrantFromFiler;
			Saf() : picking(false), regrantFromFiler(false) {}
		} saf;
		bool CloseTop() {
			if (remove.popup.CloseTop()) return true;
			if (addFs.popup.CloseTop()) return true;
			if (handed.popup.CloseTop()) return true;  // 単独で開くが、閉じ方は同じ
			if (!show) return false;
			show = false;
			return true;
		}
		FileSystemsState() : show(false), selected(0) {}
	};
	FileSystemsState fs_;

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
	// あるフォルダ」へ直してから開く（開けなければ bm_.error に理由）。
	void OpenBookmark(Settings *settings, int index);
	// ファイラーの "Bookmarks>" から。OpenBookmark と同じだが、開けない
	// ときはそのままファイラーに開かせて、あちらの「開けなければ元の場所に
	// 留まる」に任せる（ダイアログは出ていないので bm_.error は見せられない）。
	void JumpToBookmarkRef(Settings *settings, const std::string &ref);
	// ref をブックマークに控えられるか。ファイルシステムの選択（空）と
	// "Bookmarks>" 自身は控えられない。
	bool CanBookmark(const std::string &ref) const;
	// 同じ場所を指す行を探す。無ければ -1。
	int FindBookmark(const std::vector<std::string> &list, const std::string &ref) const;
	// フォルダ選択で選んだ ref を、一覧で選んでいた行の位置（無ければ末尾）へ
	// 挿し、その行を選ぶ（settingsui_folder.cpp から）。
	void InsertBookmark(std::vector<std::string> *list, const std::string &ref);
	struct BookmarksState {
		bool show;
		int selected;            // 一覧で選んでいる行。空のときは -1
		std::string error;       // 「そのフォルダは見つかりません。」など
		NestedPopup remove;      // 削除の確認（ダイアログの中に入れ子）
		NestedPopup toggle;      // Shift+M の確認（メイン画面から単独で開く）
		std::string toggleRef;   // Shift+M の確認にかけている場所
		bool jumpPending;        // 次の Build() で jumpRef へ移る
		std::string jumpRef;
		bool CloseTop() {
			if (remove.CloseTop()) return true;
			if (toggle.CloseTop()) return true;  // 単独で開くが、閉じ方は同じ
			if (!show) return false;
			show = false;
			return true;
		}
		BookmarksState() : show(false), selected(-1), jumpPending(false) {}
	};
	BookmarksState bm_;
	// PDX の探索先の設定。設定ウィンドウの [編集…] から開く。作りはブックマークの
	// 設定と同じ（一覧・[上へ][下へ][追加…][削除]、削除は入れ子の確認）で、
	// [開く] は無い。モーダル同士は入れ子にしないので、設定ウィンドウが閉じきって
	// から開き（pdx_.handoff.openPending）、閉じたら設定ウィンドウを開き直す
	// （pdx_.handoff.returnToCaller。[追加…] のフォルダ選択の往復をまたいでも保つ）。
	// 2026-09-16、ユーザーの指示（それまでは設定ウィンドウの打ち込み欄 1 本）。
	void BuildPdxPathsWindow(Settings *settings, Filer *filer);
	void BuildPdxRemoveWindow(Settings *settings);
	// 設定ウィンドウの [編集…] から。設定ウィンドウを閉じ、閉じきったら開く。
	void OpenPdxPathsFromSettings();
	// フォルダ選択で選んだ ref を、一覧で選んでいた行の位置（無ければ末尾）へ
	// 挿し、その行を選ぶ（settingsui_folder.cpp から）。
	void InsertPdxPath(std::vector<std::string> *list, const std::string &ref);
	struct PdxPathsState {
		bool show;
		Handoff handoff;    // 設定ウィンドウとの往復
		int selected;       // 一覧で選んでいる行。空のときは -1
		std::string error;
		NestedPopup remove; // 削除の確認
		bool CloseTop() {
			if (remove.CloseTop()) return true;
			if (!show) return false;
			show = false;
			return true;
		}
		PdxPathsState() : show(false), selected(0) {}
	};
	PdxPathsState pdx_;
	// [画面の遅れ]（[演奏]）と [遅延時間]（[Bluetooth（機器名）]）。設定ウィンドウには
	// 今の値と [設定…] だけを出し、スライダーはこのダイアログに置く。5 秒近い
	// 遅れの機器があり、1 本のスライダーでは合わせられないので「秒」と「ミリ秒」の
	// 2 本に分けた。**鍵盤を見ながら合わせられるよう、画面の中央でなくファイラーの
	// 側に寄せて出す**（2026-09-28、ユーザーの指示）。開き方と戻り方は
	// [PDX の探索先] と同じ（設定ウィンドウが閉じきってから開き、閉じたら戻る）。
	enum LatencyTarget {
		kLatencyTargetDisplay = 0,  // [演奏] の画面の遅れ（手動のとき）
		kLatencyTargetBluetooth,    // いまつないでいる Bluetooth の機器の遅延時間
	};
	// settingsui_latency.cpp。
	void BuildLatencyWindow(Settings *settings, DrawScreen *draw, Player *player,
	                        Screen *screen);
	void OpenLatencyWindow(int target, const std::string &device);
	const char *LatencyTitle() const;
	struct LatencyDialogState {
		bool show;
		Handoff handoff;     // 設定ウィンドウとの往復
		int target;          // LatencyTarget
		std::string device;  // kLatencyTargetBluetooth の機器名（開いたときのもの）
		bool CloseTop() {
			if (!show) return false;
			show = false;
			return true;
		}
		LatencyDialogState() : show(false), target(kLatencyTargetDisplay) {}
	};
	LatencyDialogState latency_;

	// 更新チェック（settingsui_update.cpp）。
	struct UpdateDialogState {
		bool available;  // [通信] を出すか
		bool running;    // 確かめている最中（[今すぐ…] を押せなくする）
		bool checkNow;   // [今すぐ…] が押された
		UpdateResult result;
		bool pending;    // 見せる結果を預かっている
		NestedPopup popup;
		// [mxv2 の設定] の中に入れ子で開いたか。ImGui のポップアップの id は
		// 開いた場所の id スタックで決まるので、開いた場所と同じところで組む。
		bool nested;
		bool CloseTop() { return popup.CloseTop(); }
		UpdateDialogState()
		    : available(false), running(false), checkNow(false), pending(false), nested(false) {}
	};
	UpdateDialogState update_;
	// [mxv2 の設定] の [通信] と、結果のダイアログ。
	void BuildNetworkGroup(Settings *settings);
	void BuildUpdateWindow();
	// 預かった結果をどこで開くか決める（Build の頭から毎フレーム）。
	void ScheduleUpdateWindow();

	// 起動時の警告。ウィンドウが出る前の printf を持ち越したもの。
	void BuildStartupWindow();

	// コンテキストメニュー
	void BuildContextMenu(Settings *settings, DrawScreen *draw, Player *player,
	                      Filer *filer);
	// その項目（ポップアップの中から）。
	void BuildContextMenuItems(Settings *settings, DrawScreen *draw, Player *player,
	                           Filer *filer);
	// 指で操作するとき（touchUi_）、[表示] と [その他] は ImGui のサブメニュー
	// ではなく、同じポップアップの中身を入れ替える「ページ」にする。携帯の
	// 縦画面ではメニューの左右どちらにもサブメニューを置く幅が無く、ImGui は
	// 親に重ねて出すが、重ねた子は親の下に描かれて見えない（2026-09-16、
	// Pixel 7a でユーザーの報告）。ページなら幅は要らない。
	enum CtxPage {
		kCtxPageMain = 0,
		kCtxPageView,   // [表示] の中身
		kCtxPageOther,  // [その他] の中身
		kCtxPageCount,
	};
	struct ContextMenuState {
		bool openRequested;  // 次のフレームで開く（OpenContextMenu）
		// 直前のフレームでメニューが開いていたか（ESC を食う判断に使う）と、
		// ESC で閉じてほしいという印。
		bool isOpen;
		bool closeWanted;
		int page;  // CtxPage。メニューを開くたびに kCtxPageMain へ戻す
		// ページの切り替えは横スクロールで見せる（2026-09-16、ユーザーの要望。
		// 下るときは右から、戻るときは左から入ってくる）。各ページは自分の
		// 子ウィンドウに描く（MenuItem の列幅はウィンドウごとなので、2 ページを
		// 同じウィンドウに描くと列が揃ってしまう）。遷移中は 2 つの子を
		// 横にずらして描き、ポップアップの大きさは前→次へ補間する。
		int switchTo;      // このフレームで押された行き先（無ければ -1）
		int animFrom;      // 滑り出ていく前のページ（遷移中でなければ -1）
		bool animForward;  // 下る（次が右から入る）か、戻る（左から）か
		Uint32 animStartMs;
		// 各ページの中身の大きさ。描くたびに測り直す（未測は 0）。
		ImVec2 pageSize[kCtxPageCount];
		// 子ウィンドウの列幅が決まっているか。ImGui はウィンドウが出直すたびに
		// 列幅を測り直すので、最初の 1 フレームはラベルと短縮キーが重なる。
		// 決まるまではポップアップごと隠す（ポップアップ自身が最初のフレームで
		// やっているのと同じ）。
		bool childWarm[kCtxPageCount];
		// 短縮キーの列を出さない。どれかのページが画面の横に入りきらないと
		// 分かったら立て、メニューを閉じるまで保つ（毎フレーム決め直すと、
		// 列を消して幅が縮んだ途端に「入る」に戻ってちらつく）。指で操作する
		// 端末はキーボードが無いことが多いので、消しても失うものは少ない
		// （2026-09-24、320px 幅の XS17 で "Shift+M" が切れていた）。
		bool noShortcut;
		NestedPopup quit;  // 終了の確認（settingsui_menu.cpp）
		// メニューは ImGui のポップアップの中からしか閉じられないので、印だけ
		// 付けて実際の始末は Build() に任せる。
		bool CloseTop() {
			if (isOpen) { closeWanted = true; return true; }
			return quit.CloseTop();
		}
		ContextMenuState()
		    : openRequested(false), isOpen(false), closeWanted(false), page(kCtxPageMain),
		      switchTo(-1), animFrom(-1), animForward(true), animStartMs(0), noShortcut(false) {
			for (int i = 0; i < kCtxPageCount; i++) {
				pageSize[i] = ImVec2(0.0f, 0.0f);
				childWarm[i] = false;
			}
		}
	};
	ContextMenuState ctx_;
	// ページへ入る / 戻る項目。サブメニューの見出しと同じ見た目（右端に ▶、
	// 戻るは ◀）で、押してもポップアップを閉じない。押されたら true。
	bool CtxPageItem(const char *label, bool back);
	// MenuItem。子ウィンドウの中では ImGui が自分でポップアップを閉じないので、
	// 押されたらここで閉じる。
	bool CtxMenuItem(const char *label, const char *shortcut, bool selected, bool enabled);
	// 1 ページぶんの項目。paged でなければ [表示] [その他] は ImGui のサブメニュー。
	void BuildCtxPageItems(int page, bool paged, Settings *settings, DrawScreen *draw,
	                       Player *player, Filer *filer);
	// ページを子ウィンドウに描き、中身の大きさを ctx_.pageSize に測る。
	// 子が丸ごと切り取られて何も描けなかったら false。
	bool BuildCtxPageChild(int page, const ImVec2 &pos, Settings *settings, DrawScreen *draw,
	                       Player *player, Filer *filer);
	Request request_;
	std::string requestedFolder_;  // kRequestSetFolder の行き先 (ref)
	// 選ばれたスキン（[mxv2 の設定] と [配色設定] の保存から）。main が画面を
	// 作り直して ClearPendingSkin() で消す。
	std::string pendingSkin_;
	// バージョン情報 (F12)。settingsui_info.cpp。
	void BuildAboutWindow();
	// settingsui_info.cpp のダイアログ 3 つ（バージョン情報・操作方法・起動時の警告）。
	struct InfoDialogsState {
		bool showAbout;
		std::string aboutHeader;  // 名前・版・ビルド日付・著作権表示
		std::string aboutText;    // NOTICE の中身（初回に読む）
		bool showHelp;
		std::vector<HelpRow> helpRows;
		bool showStartup;
		std::vector<StartupWarning> startupLines;
		bool CloseTop() {
			if (showAbout) { showAbout = false; return true; }
			if (showHelp) { showHelp = false; return true; }
			if (showStartup) { showStartup = false; return true; }
			return false;
		}
		InfoDialogsState() : showAbout(false), showHelp(false), showStartup(false) {}
	};
	InfoDialogsState info_;

	// Chromecast へ送る（settingsui_cast.cpp / cast.h）。[表示] の [キャスト…]。
	void BuildCastWindow(Settings *settings);
	struct CastDialogState {
		bool show;
		bool wasOpen;     // 前のフレームで開いていた（開いた・閉じた瞬間を拾う）
		int selected;     // 一覧で選んでいる行
		ImVec2 lastSize;  // 前のフレームのダイアログの大きさ（変わったら中央へ置き直す）
		bool recenter;    // 次のフレームで中央へ置き直す
		bool scrollY;     // 前のフレームで縦のスクロールバーが出ていた（その幅を空ける）
		bool CloseTop() {
			if (!show) return false;
			show = false;
			return true;
		}
		CastDialogState()
		    : show(false), wasOpen(false), selected(-1), lastSize(0.0f, 0.0f), recenter(false),
		      scrollY(false) {}
	};
	CastDialogState cast_;
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
