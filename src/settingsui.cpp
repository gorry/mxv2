// mxv2 - 設定 UI（Dear ImGui）

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "drawscreen.h"
#include "fileutil.h"
#include "filer.h"
#include "ini.h"
#include "message.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "skin.h"
#include "vfs.h"

namespace mxv2 {

namespace {

// 同梱フォント。assets/ に置いてあるので、どのプラットフォームでも読める。
// M PLUS 1p Regular (SIL OFL 1.1)。ライセンス全文は assets/MPLUS1p-OFL.txt。
// 差し替え用の font.ttf は、ユーザーフォルダ側に置いても効く (AssetPaths)。
// JIS 第1+2水準を含み、CP932 変換で出る U+FF5E (～) や U+2015 (―) も持つ。
const char *kBundledFont = "MPLUS1p-Regular.ttf";

// 差し替え用のスロット。ここに置けば同梱フォントより優先される。
const char *kUserFont = "font.ttf";

// 同梱フォントが失われていたときの保険。システムのフォントを拾う。
const char *kFontCandidates[] = {
#ifdef _WIN32
	"C:\\Windows\\Fonts\\meiryo.ttc",
	"C:\\Windows\\Fonts\\YuGothM.ttc",
	"C:\\Windows\\Fonts\\msgothic.ttc",
#else
	"/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
	"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
	"/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
#endif
};
const int kNumFontCandidates = (int)(sizeof(kFontCandidates) / sizeof(kFontCandidates[0]));

const float kFontSizePx = 15.0f;

// ダイアログの題名。文言はカタログ、"###" 以降は ImGui の id。
// SyncModal が題名をポインタで見分けるので、**毎フレーム同じ番地**を
// 返さないといけない。カタログの文字列はそのまま使えるが、id を繋いだ
// ものは 1 度だけ組み立てて使い回す。
const char *kSettingsTitle;
const char *kColorsTitle;
const char *kOverwriteTitle;
const char *kFolderTitle;
const char *kPdxFolderTitle;
const char *kHelpTitle;
const char *kFileSystemsTitle;
const char *kFsRemoveTitle;
const char *kBookmarksTitle;
const char *kBmRemoveTitle;
const char *kBmToggleTitle;
const char *kAboutTitle;
const char *kStartupTitle;

// 題名を作る。id 付きのものは文字列を静的に持ってから返す。
const char *TitleWithId(const char *key, const char *id) {
	static std::vector<std::string *> keep;  // 後始末は要らない（起動時に 1 度）
	std::string *s = new std::string(std::string(Msg(key)) + id);
	keep.push_back(s);
	return s->c_str();
}

void InitTitles() {
	if (kSettingsTitle != 0) return;
	kSettingsTitle = Msg("Dialog.Settings");
	kColorsTitle = Msg("Dialog.Colors");
	kOverwriteTitle = Msg("Dialog.Overwrite");
	// 見出しだけ用途で変えて、ポップアップとしては同じものとして扱う。
	kFolderTitle = TitleWithId("Dialog.Folder", "###mxv2folder");
	kPdxFolderTitle = TitleWithId("Dialog.PdxFolder", "###mxv2folder");
	kHelpTitle = Msg("Dialog.Help");
	kFileSystemsTitle = Msg("Dialog.FileSystems");
	kFsRemoveTitle = Msg("Dialog.FsRemove");
	kBookmarksTitle = Msg("Dialog.Bookmarks");
	// 見出しはファイルシステムの削除確認と同じなので、別のポップアップとして
	// 扱ってもらうために "###" で id を分ける。
	kBmRemoveTitle = TitleWithId("Dialog.BookmarkRemove", "###mxv2bmremove");
	// Shift+M の確認。ダイアログを開かずにメイン画面から直に出す。
	kBmToggleTitle = TitleWithId("Dialog.BookmarkToggle", "###mxv2bmtoggle");
	kAboutTitle = Msg("Dialog.About");
	kStartupTitle = Msg("Dialog.Startup");
}

// 文言に ImGui の id を足した名札。同じ文言を 1 つの画面で何度も使うため。
std::string L(const char *key, const char *id) {
	return std::string(Msg(key)) + id;
}

// 表示倍率を変えたあと、実際に適用するまでの待ち時間。
const uint32_t kZoomApplyDelayMs = 200;

// Rgb <-> ImGui の float[3]
void ToFloat3(const Rgb &c, float *f) {
	f[0] = c.r / 255.0f;
	f[1] = c.g / 255.0f;
	f[2] = c.b / 255.0f;
}

Rgb FromFloat3(const float *f) {
	return MakeRgb((int)(f[0] * 255.0f + 0.5f), (int)(f[1] * 255.0f + 0.5f),
	               (int)(f[2] * 255.0f + 0.5f));
}

// 色見本 + カラーピッカー。変わったら true。
bool ColorRow(const std::string &label, Rgb *c) {
	float f[3];
	ToFloat3(*c, f);
	if (!ImGui::ColorEdit3(label.c_str(), f, ImGuiColorEditFlags_NoInputs)) return false;
	*c = FromFloat3(f);
	return true;
}

// Bright 値のスライダ。同じ「強さ」でも意味が 2 通りあるので、行の種類を
// 分けてある（colors.h 参照）。変わったら true。
//
// 合成の強さ。背景に対する alpha なので 0..100 で意味が閉じている。
bool AlphaRow(const std::string &label, int *v) {
	return ImGui::SliderInt(label.c_str(), v, 0, 100);
}

// 素材に掛ける乗算ゲイン。100 が素通しで、それより上は明るくなる。
bool GainRow(const std::string &label, int *v) {
	return ImGui::SliderInt(label.c_str(), v, 0, 200);
}

// バージョン情報の字の大きさを決める物差し。NOTICE は等幅 80 桁で書いて
// あるので、余裕をみた 88 桁ぶんが横に収まるようにする。
const char kAboutRuler88[] =
    "****************************************"   // 40
    "****************************************"   // 40
    "********";                                  // 8

// 今の ImGui ウィンドウを、中身のドラッグでスクロールさせる。指で使う
// ことを想定したもの（スクロールバーを摘まむのは細かすぎる）。
// 中身を組み終わったあと、End/EndChild/EndPopup の直前で呼ぶ。
//
// 掴み始めの条件は 4 つ。
//   ・スクロールする余地がある（無ければ何もしない）
//   ・押した先が部品でない。スライダ・入力欄・見出しなど、ドラッグを
//     自分で使う部品はそちらが優先（要件どおり部品が勝つ）
//   ・押した先がタイトルバーでない（あちらはウィンドウを動かす場所）
//   ・押した先がスクロールバーでない（あちらは摘まむ場所）
// 一度掴んだら、枠から出ても離すまで続ける。
//
// hasTitleBar: タイトルバーのあるウィンドウなら true。子ウィンドウは false。
// fromItems:   部品の上からでも掴んでよいなら true。一覧のように
//              「並んでいるのが全部 Selectable」だと、部品を避けていては
//              どこも掴めない。Selectable はドラッグを使わないので譲る必要もない。
// moved:       実際にスクロールしたら true にする。押した先の部品を
//              反応させないため（ドラッグしたつもりが選択になるのを防ぐ）に使う。
void DragToScroll(bool *dragging, bool *moved, bool hasTitleBar, bool fromItems) {
	if (*dragging) {
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			*dragging = false;
			return;
		}
		const ImVec2 d = ImGui::GetIO().MouseDelta;
		if (ImGui::GetScrollMaxX() > 0.0f && d.x != 0.0f) {
			ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
			*moved = true;
		}
		if (ImGui::GetScrollMaxY() > 0.0f && d.y != 0.0f) {
			ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
			*moved = true;
		}
		return;
	}

	const float maxX = ImGui::GetScrollMaxX();
	const float maxY = ImGui::GetScrollMaxY();
	if (maxX <= 0.0f && maxY <= 0.0f) return;
	if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
	// 押した先が部品だと、ImGui はその時点で「別の部品が掴んでいる」として
	// ウィンドウを hover 扱いしなくなる。部品の上から掴みたいときは
	// AllowWhenBlockedByActiveItem を足して、その判定を外す。
	const ImGuiHoveredFlags hoverFlags =
	    fromItems ? ImGuiHoveredFlags_AllowWhenBlockedByActiveItem : ImGuiHoveredFlags_None;
	if (!ImGui::IsWindowHovered(hoverFlags)) return;
	// 部品が拾える押下は部品に譲る。ImGui はこのフレームの分まで
	// 当たり判定を済ませているので、中身を組んだあとなら正しく見える。
	if (!fromItems && (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())) return;

	// タイトルバーとスクロールバーを除く。タイトルバーの高さは枠 1 行分。
	// ここは ImGui の内部 API を使わずに済ませたいので、公開されている
	// 値から組み立てている。
	const ImGuiStyle &style = ImGui::GetStyle();
	const ImVec2 pos = ImGui::GetWindowPos();
	const ImVec2 size = ImGui::GetWindowSize();
	const ImVec2 m = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
	const float top = pos.y + (hasTitleBar ? ImGui::GetFrameHeight() : 0.0f);
	const float right = pos.x + size.x - ((maxY > 0.0f) ? style.ScrollbarSize : 0.0f);
	const float bottom = pos.y + size.y - ((maxX > 0.0f) ? style.ScrollbarSize : 0.0f);
	if (m.x < pos.x || m.x >= right || m.y < top || m.y >= bottom) return;

	*dragging = true;
}

// 画面の遅れ (ms) とサンプル数の相互変換。
int MsToFrames(int ms, const Player *player) {
	return ms * player->sampleRate() / 1000;
}

float FramesToMs(int frames, const Player *player) {
	return frames * 1000.0f / (float)player->sampleRate();
}

// フォルダ名を並べるときの順。ファイラー (filer.cpp) と同じ規則。
bool LessPathNoCase(const std::string &a, const std::string &b) {
	return CompareNoCase(a, b) < 0;
}

// バージョン情報に出す文面。実行ファイルの隣の NOTICE をそのまま読む。
// 配布物に入れるファイルなので、同じ文面をソースに二重に持たない。
std::string LoadAboutText() {
	std::vector<uint8_t> data;
	if (ReadWholeFile(JoinPath(ExecutableDir(), "NOTICE"), &data) && !data.empty()) {
		return std::string((const char *)&data[0], data.size());
	}
	return std::string(Msg("About.NoticeMissing")) + "\n" + Msg("About.NoticeHint");
}

std::string TrimSpaces(const std::string &s) {
	size_t b = 0;
	size_t e = s.size();
	while (b < e && (unsigned char)s[b] <= ' ') b++;
	while (e > b && (unsigned char)s[e - 1] <= ' ') e--;
	return s.substr(b, e - b);
}

// スキン名はそのままフォルダ名になるので、使えないものを弾く。
bool CheckSkinName(const std::string &name, std::string *err) {
	if (name.empty()) {
		*err = Msg("Colors.NameEmpty");
		return false;
	}
	if (name == "." || name == ".." || name[name.size() - 1] == '.') {
		*err = Msg("Colors.NameReserved");
		return false;
	}
	if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
		*err = Msg("Colors.NameChars");
		return false;
	}
	return true;
}

}  // namespace

SettingsUi::SettingsUi()
    : ready_(false),
      visible_(false),
      hasJapaneseFont_(false),
      styleScale_(0.0f),
      inputScale_(1.0f),
      vfs_(0),
      pendingSampleRate_(0),
      pendingZoom_(0),
      zoomApplyAtMs_(0),
      changedFields_(0),
      openContextMenu_(false),
      contextMenuOpen_(false),
      closeContextMenu_(false),
      request_(kRequestNone),
      showAbout_(false),
      dragScroll_(false),
      dragMoved_(false),
      showColors_(false),
      skinNameReset_(true),
      saveErrorFresh_(false),
      openOverwrite_(false),
      overwriteOpen_(false),
      closeOverwrite_(false),
      showHelp_(false),
      showFileSystems_(false),
      showBookmarks_(false),
      showStartup_(false),
      bmSelected_(-1),
      bmOpenRemove_(false),
      bmRemoveOpen_(false),
      bmCloseRemove_(false),
      bmOpenToggle_(false),
      bmToggleOpen_(false),
      bmCloseToggle_(false),
      fsSelected_(0),
      fsOpenConfirm_(false),
      fsConfirmOpen_(false),
      fsCloseConfirm_(false),
      showFolder_(false),
      folderTarget_(kFolderTargetFiler),
      folderReturnToSettings_(false),
      folderOpenPending_(false),
      openedModal_(0) {
	pdxPathBuf_[0] = '\0';
	folderPathBuf_[0] = '\0';
	skinNameBuf_[0] = '\0';
}

SettingsUi::~SettingsUi() {
	Shutdown();
}

bool SettingsUi::Init(Screen *screen, const AssetPaths &paths, std::string *err) {
	if (ready_) return true;
	if (screen == 0 || screen->window() == 0 || screen->renderer() == 0) {
		*err = Msg("Error.NeedWindow");
		return false;
	}

	paths_ = paths;
	InitTitles();
	LoadHelpRows();
	ScanSkins();

	// 最初のイベントが来る前に倍率を知っておく。
	screen->GetRenderScale(&inputScale_, 0);
	if (inputScale_ <= 0.0f) inputScale_ = 1.0f;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	// imgui.ini を勝手に作らない。ウィンドウ位置はこちらで決める。
	io.IniFilename = NULL;
	io.LogFilename = NULL;
	// ウィンドウはタイトルバーを掴んだときだけ動かす。中身をドラッグしても
	// 動かないようにしておかないと、バージョン情報の本文をなぞろうとして
	// ウィンドウごと引きずってしまう。
	io.ConfigWindowsMoveFromTitleBarOnly = true;

	// 日本語フォント。ImGui 1.92 以降はグリフを要求時に焼くので、
	// GlyphRanges を渡さなくても日本語が出る。
	{
		std::string path = paths_.Find(kUserFont);
		if (path.empty()) path = paths_.Find(kBundledFont);
		for (int i = 0; path.empty() && i < kNumFontCandidates; i++) {
			if (FileExists(kFontCandidates[i])) path = kFontCandidates[i];
		}
		if (!path.empty() && io.Fonts->AddFontFromFileTTF(path.c_str(), kFontSizePx) != 0) {
			hasJapaneseFont_ = true;
		}
	}

	ImGui::StyleColorsDark();
	{
		ImGuiStyle &style = ImGui::GetStyle();
		style.FontSizeBase = kFontSizePx;
		// 画面の中に置くので、既定より詰める。
		style.WindowRounding = 2.0f;
		style.FrameRounding = 2.0f;
		style.WindowPadding = ImVec2(6, 6);
		style.FramePadding = ImVec2(4, 2);
		style.ItemSpacing = ImVec2(6, 3);
		style.ScrollbarSize = 12.0f;
		style.Colors[ImGuiCol_WindowBg].w = 0.94f;
		// モーダルの背後を曇らせない（ImGui の既定は白を薄く被せる）。
		// ダイアログが手前にあるのは見れば分かるし、[配色設定] で色を
		// 詰めているときに元の画面が白っぽくなるのは邪魔でしかない。
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		baseStyle_ = style;  // 拡大率が変わったらここから作り直す
	}

	if (!ImGui_ImplSDL2_InitForSDLRenderer(screen->window(), screen->renderer())) {
		*err = Msg("Error.ImGuiSdl");
		ImGui::DestroyContext();
		return false;
	}
	if (!ImGui_ImplSDLRenderer2_Init(screen->renderer())) {
		*err = Msg("Error.ImGuiRenderer");
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	ready_ = true;
	return true;
}

void SettingsUi::Shutdown() {
	if (!ready_) return;
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	ready_ = false;
}

void SettingsUi::ProcessEvent(const SDL_Event &ev) {
	if (!ready_) return;

	// ImGui は実解像度で動かすが、SDL は論理座標でイベントを届ける。
	// バックエンドは SDL_MOUSEMOTION の座標をそのまま io へ流すので、
	// **渡す前に**倍率へ直す。ここで直さずに後から io.MousePos を上書きしても、
	// ImGui::NewFrame() がキューを適用する際に上書きが打ち消され、
	// 拡大前の位置のコントロールが 1 フレームだけ反応してしまう。
	if (ev.type == SDL_MOUSEMOTION) {
		SDL_Event scaled = ev;
		scaled.motion.x = (int)(ev.motion.x * inputScale_ + 0.5f);
		scaled.motion.y = (int)(ev.motion.y * inputScale_ + 0.5f);
		ImGui_ImplSDL2_ProcessEvent(&scaled);
		return;
	}
	ImGui_ImplSDL2_ProcessEvent(&ev);
}

// 設定ウィンドウが閉じていても、右クリックのメニューやバージョン情報が
// 開いていれば ImGui が入力を掴む。ImGui 自身のフラグは何も出ていなければ
// false になるので、visible_ で先に切ってはいけない
// （切ると、メニューの外を押した扱いになって開いた瞬間に閉じてしまう）。
bool SettingsUi::wantCaptureMouse() const {
	if (!ready_) return false;
	return ImGui::GetIO().WantCaptureMouse;
}

bool SettingsUi::wantCaptureKeyboard() const {
	if (!ready_) return false;
	return ImGui::GetIO().WantCaptureKeyboard;
}

// 表示倍率に合わせてスタイルとフォントの大きさを作り直す。
// ImGui 1.92 はフォントを要求時に焼き直すので、拡大しても字がぼけない。
bool SettingsUi::ApplyScale(float scale) {
	if (scale == styleScale_) return false;
	styleScale_ = scale;

	ImGuiStyle &style = ImGui::GetStyle();
	style = baseStyle_;
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = scale;
	return true;
}

// ---------------------------------------------------------------------------

void SettingsUi::ScanSkins() {
	paths_.ListSkinRefs(&skinNames_);
}

// 同梱のスキンは読み取り専用なので、配色はユーザーフォルダ側の
// skin/<名前>/ へ書く。次に読むときは、そちらが同梱の colors.ini より
// 先に見つかる (Skin::FindFile)。
//
// 保存先は必ずユーザーフォルダ側のスキンなので、名前に "assets:" は付かない。
// それが今のスキンでなければ「名前を付けて保存」で、まだ無い名前なら
// 今のスキンを土台にした layout.ini も置く。レイアウトと素材は元のスキンの
// ものがそのまま使われる（Default-Midnight と同じ作り）。同梱スキンを
// 編集していたときは Base が "assets:<名前>" になる。
void SettingsUi::SaveColorsAs(const std::string &name, Settings *settings, DrawScreen *draw) {
	saveError_.clear();

	const bool isNewSkin = !paths_.UserSkinExists(name);
	// 土台には、今のスキンが実際に指しているフォルダを名指しする ref を書く
	// （同梱ぶんなら "assets:X"）。ここで接頭辞を落とすと、いま作ろうと
	// している同名のユーザースキン自身を指してしまう。
	const std::string baseRef = paths_.CanonicalSkinRef(settings->skinName);
	// 今のスキンそのものへの保存か。同梱ぶんを編集していたのなら、
	// 保存先のユーザースキンは別のスキンになる。
	const bool isCurrent =
	    !IsBundledSkinRef(baseRef) && CompareNoCase(name, SkinRefName(baseRef)) == 0;
	const std::string dir = paths_.UserSkinDir(name);

	if (!MakeDirectories(dir)) {
		saveError_ = MsgF("Colors.MakeDirFailed", dir);
		return;
	}
	if (isNewSkin && !isCurrent && !baseRef.empty()) {
		Ini ini;
		ini.SetString("Skin", "Base", baseRef);
		if (!ini.Save(JoinPath(dir, "layout.ini"))) {
			saveError_ = MsgF("Colors.LayoutFailed", dir);
			return;
		}
	}
	if (!draw->colors().Save(JoinPath(dir, kColorsFile))) {
		saveError_ = MsgF("Colors.SaveFailed", JoinPath(dir, kColorsFile));
		return;
	}
	// 旧い名前のファイルが残っていると、colors.ini に隠れて読まれなくなる。
	// 手で直しても何も変わらない罠になるので、書き換えたこの場で片付ける
	// （消すのはユーザーフォルダ側だけ。同梱ぶんには触っていない）。
	RemoveFile(JoinPath(dir, kLegacyColorsFile));

	// ユーザーフォルダ側に新しくフォルダができることがある。
	ScanSkins();

	// 別名で保存したのなら、そのスキンへ移る。ここで移らないと、編集した
	// 配色は「今のスキン」には保存されていないままなので、閉じて開き直すと
	// 元に戻ってしまう。
	if (!isCurrent) {
		pendingSkin_ = name;
		changedFields_ |= Settings::kFieldSkin;
	}
}

void SettingsUi::Rebuild(DrawScreen *draw, Player *player) {
	draw->Reload();
	if (player != 0) player->RequestStatusRefresh();
}

// ---------------------------------------------------------------------------

void SettingsUi::Build(Settings *settings, DrawScreen *draw, Player *player, Filer *filer,
                       Screen *screen) {
	if (!ready_) return;

	// 待たせていたウィンドウ倍率をここで適用する。フレームの先頭でやるので、
	// この後の倍率の計算とマウス座標の直しは新しい大きさで揃う。
	if (zoomApplyAtMs_ != 0 && SDL_GetTicks() >= zoomApplyAtMs_) {
		zoomApplyAtMs_ = 0;
		if (pendingZoom_ > 0) screen->SetZoom(pendingZoom_);
		pendingZoom_ = 0;
	}

	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();

	// ImGui は実解像度で描く。SDL_RenderSetLogicalSize が入っているせいで
	// マウス座標だけは論理座標に直って届くので、こちらで拡大率を掛け戻す。
	float scale = 1.0f;
	screen->GetRenderScale(&scale, 0);
	if (scale <= 0.0f) scale = 1.0f;
	inputScale_ = scale;
	{
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = ImVec2(screen->width() * scale, screen->height() * scale);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}
	const bool scaleChanged = ApplyScale(scale);

	ImGui::NewFrame();

	// ドラッグでスクロール中の印は、ボタンを離したところで落とす。
	// ドラッグの途中でダイアログが閉じても、次に開いたものへ持ち越さない。
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragScroll_ = false;
	// 「実際にスクロールしたか」は離したフレームでもまだ要る（押した行を
	// 選ばせないため）ので、落とすのは次に押したときにする。
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragMoved_ = false;

	// 右クリックのメニューとバージョン情報は、設定ウィンドウが閉じていても出す。
	BuildContextMenu(settings, draw, player, filer);
	BuildColorsWindow(settings, draw, player);

	// 設定ウィンドウの [参照...] から来た往復。ImGui のポップアップは
	// 同じ階層で掛け替えられないので、片方が閉じきってからもう片方を開く。
	if (folderOpenPending_ && !ImGui::IsPopupOpen(kSettingsTitle)) {
		folderOpenPending_ = false;
		// PDX の探索先が入っていればそこから、無ければファイラーの今の場所から。
		std::string start = filer->currentRef();
		if (vfs_ != 0 && pdxPathBuf_[0] != '\0') {
			std::string ref;
			if (vfs_->Resolve(pdxPathBuf_, filer->currentRef(), &ref) && vfs_->IsDir(ref)) {
				start = ref;
			}
		}
		SetFolderDir(start);
		showFolder_ = true;
	}
	BuildFolderWindow(settings);
	BuildFileSystemsWindow(filer);
	BuildBookmarksWindow(settings, filer);
	BuildBookmarkToggleWindow(settings, filer);
	BuildStartupWindow();
	BuildHelpWindow();
	if (folderReturnToSettings_ && !showFolder_ && !folderOpenPending_ &&
	    !ImGui::IsPopupOpen(folderTitle())) {
		folderReturnToSettings_ = false;
		visible_ = true;
	}

	// ここから下は設定ウィンドウ。モーダルなので、開いている間はメイン画面も
	// 他のダイアログも操作できない。
	if (!SyncModal(kSettingsTitle, &visible_)) return;

	// 開くたびに画面の左上から出す (ImGuiCond_Appearing)。出したあとは
	// 掴んで動かせる。倍率が変わったフレームだけは Always にして矩形ごと
	// 作り直す。ImGui はウィンドウの矩形をピクセルで覚えているので、
	// 放っておくと中身だけ大きくなって枠が付いてこない。
	const ImGuiCond cond = scaleChanged ? ImGuiCond_Always : ImGuiCond_Appearing;
	ImGui::SetNextWindowPos(ImVec2(0, 0), cond);
	ImGui::SetNextWindowSize(ImVec2(380 * scale, 464 * scale), cond);

	// p_open に visible_ をそのまま渡す。× で閉じられたときは ImGui が
	// false にして閉じてくれるし、F1 で false にした場合も同じ経路で閉じる。
	if (!ImGui::BeginPopupModal(kSettingsTitle, &visible_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// ---- 画面 ----------------------------------------------------------
	if (ImGui::CollapsingHeader(Msg("Settings.Screen"), ImGuiTreeNodeFlags_DefaultOpen)) {
		// スキン。画面サイズごと変わりうるので、選ばれた名前を置いておいて
		// 実際の作り直しはメインループに任せる。
		int current = -1;
		for (size_t i = 0; i < skinNames_.size(); i++) {
			if (skinNames_[i] == settings->skinName) current = (int)i;
		}
		if (ImGui::BeginCombo(Msg("Settings.Skin"), (current >= 0) ? skinNames_[current].c_str()
		                                              : settings->skinName.c_str())) {
			for (size_t i = 0; i < skinNames_.size(); i++) {
				const bool selected = ((int)i == current);
				if (ImGui::Selectable(skinNames_[i].c_str(), selected)) {
					pendingSkin_ = skinNames_[i];
					changedFields_ |= Settings::kFieldSkin;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button(Msg("Button.Rescan"))) {
			ScanSkins();
			pendingSkin_ = settings->skinName;
		}

		// 表示倍率 (%)。100 でドット等倍。
		// 押している間・入力中に適用してはいけない。ウィンドウが大きくなると
		// このコントロール自身の座標も変わるので、同じ場所を押しているだけで
		// 値が行き来してしまう。確定してから少し待って適用する。
		int zoom = settings->zoomPercent;
		ImGui::SetNextItemWidth(80.0f * styleScale_);
		const bool edited = ImGui::InputInt(Msg("Settings.Zoom"), &zoom, 25, 100);
		if (edited) {
			if (zoom < Screen::kZoomMin) zoom = Screen::kZoomMin;
			if (zoom > Screen::kZoomMax) zoom = Screen::kZoomMax;
			settings->zoomPercent = zoom;
			changedFields_ |= Settings::kFieldZoom;
		}
		// 触られるたびに期限を先送りする（デバウンス）。こうすると
		// -/+ の連打でも、桁を打っている途中でも、手が止まってから適用される。
		// InputInt の -/+ ボタンでは IsItemDeactivatedAfterEdit() が来ないので、
		// edited だけに頼らず両方を見る。
		if (edited || ImGui::IsItemDeactivatedAfterEdit()) {
			pendingZoom_ = settings->zoomPercent;
			zoomApplyAtMs_ = SDL_GetTicks() + kZoomApplyDelayMs;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(Msg("Settings.ZoomSystem"))) {
			settings->zoomPercent = Screen::SystemZoomPercent();
			changedFields_ |= Settings::kFieldZoom;
			pendingZoom_ = settings->zoomPercent;
			zoomApplyAtMs_ = SDL_GetTicks() + kZoomApplyDelayMs;
		}

		// 拡大時の補間方法。ウィンドウの大きさは変わらないので即時に反映してよい。
		{
			struct Item {
				Screen::ScaleMode mode;
				const char *label;
			};
			static const Item kItems[] = {
				{ Screen::kScaleSharp, Msg("Settings.FilterSharp") },
				{ Screen::kScaleNearest, Msg("Settings.FilterNearest") },
				{ Screen::kScaleLinear, Msg("Settings.FilterLinear") },
			};
			const int count = (int)(sizeof(kItems) / sizeof(kItems[0]));
			const Screen::ScaleMode now = screen->scaleMode();
			const char *label = kItems[0].label;
			for (int i = 0; i < count; i++) {
				if (kItems[i].mode == now) label = kItems[i].label;
			}
			if (ImGui::BeginCombo(Msg("Settings.Filter"), label)) {
				for (int i = 0; i < count; i++) {
					const bool selected = (kItems[i].mode == now);
					if (ImGui::Selectable(kItems[i].label, selected)) {
						screen->SetScaleMode(kItems[i].mode);
						settings->scaleFilter = Screen::ScaleModeName(kItems[i].mode);
					changedFields_ |= Settings::kFieldFilter;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
	}

	// ---- ファイラー ------------------------------------------------------
	if (ImGui::CollapsingHeader(Msg("Settings.Filer"), ImGuiTreeNodeFlags_DefaultOpen)) {
		bool largeFont = (settings->fileListFontSize != 0);
		if (ImGui::Checkbox(Msg("Settings.LargeFont"), &largeFont)) {
			settings->fileListFontSize = largeFont ? 1 : 0;
			changedFields_ |= Settings::kFieldFontSize;
			draw->SetFileListFontSize(settings->fileListFontSize);
			filer->SetViewMetrics(draw->fileListRows(), draw->fileListItemH());
		}

		bool folderFirst = settings->folderFirst;
		if (ImGui::Checkbox(Msg("Settings.FolderFirst"), &folderFirst)) {
			settings->folderFirst = folderFirst;
			changedFields_ |= Settings::kFieldFolderFirst;
			filer->SetFolderFirst(folderFirst);
			filer->Refresh();
		}
	}

	// ---- 演奏 ----------------------------------------------------------
	if (ImGui::CollapsingHeader(Msg("Settings.Play"), ImGuiTreeNodeFlags_DefaultOpen)) {
		// 出力サンプリングレート。96kHz を選べるのは、繋いでいる
		// portable_mdx が対応している版のときだけ（player.h の
		// X68SOUND_SUPPORT_96KHZ）。対応していなければ項目自体を出さない。
		if (Player::kSupports96kHz) {
			int idx = (player->sampleRate() == 96000) ? 1 : 0;
			if (ImGui::Combo(Msg("Settings.SampleRate"), &idx, "48000 Hz\0" "96000 Hz\0")) {
				const int rate = idx ? 96000 : 48000;
				if (rate != player->sampleRate()) {
					// 実際の切り替え（MXDRV とオーディオ装置の開き直し）は
					// メインループがやる。
					pendingSampleRate_ = rate;
				}
				settings->sampleRate = rate;
				changedFields_ |= Settings::kFieldSampleRate;
			}
			ImGui::TextDisabled("%s", Msg("Settings.SampleRateNote"));
		}

		int loops = settings->loops;
		if (ImGui::SliderInt(Msg("Settings.Loops"), &loops, 1, 10)) {
			settings->loops = loops;
			changedFields_ |= Settings::kFieldLoops;
			player->SetLoopConfig(settings->loops, settings->fadeout);
		}
		bool fadeout = settings->fadeout;
		if (ImGui::Checkbox(Msg("Settings.Fadeout"), &fadeout)) {
			settings->fadeout = fadeout;
			changedFields_ |= Settings::kFieldFadeout;
			player->SetLoopConfig(settings->loops, settings->fadeout);
		}
		ImGui::TextDisabled(Msg("Settings.LoopNote"));

		// マスター音量。メイン画面の音量バーとは別で、実際の音量は 2 つの和。
		int vol = player->masterVolume();
		if (ImGui::SliderInt(Msg("Settings.MasterVolume"), &vol, Player::kVolumeMin, Player::kVolumeMax,
		                     "%+d")) {
			player->SetMasterVolume(vol);
			changedFields_ |= Settings::kFieldVolume;
		}
		settings->masterVolume = player->masterVolume();
		ImGui::TextDisabled(
		    "%s", MsgF("Settings.VolumeNote", MsgNum("%+d", player->masterVolume()),
		               MsgNum("%+d", player->mainVolume()),
		               MsgNum("%+d", player->effectiveVolume()))
		              .c_str());

		// 画面を音に合わせて遅らせる量。イベントはサンプル位置で打刻して
		// あるので、ずれる原因はオーディオ装置のバッファぶんだけ。ふつうは
		// 自動でよく、装置がさらに段を持っていて音が遅れて聞こえるときだけ
		// 手で足す。
		{
			bool autoLatency = settings->latencyAuto;
			if (ImGui::Checkbox(Msg("Settings.LatencyAuto"), &autoLatency)) {
				settings->latencyAuto = autoLatency;
				changedFields_ |= Settings::kFieldLatency;
				player->SetDisplayLatency(autoLatency,
				                          MsToFrames(settings->latencyMs, player));
			}
			if (!autoLatency) {
				int ms = settings->latencyMs;
				if (ImGui::SliderInt(Msg("Settings.Latency"), &ms, Settings::kLatencyMsMin,
				                     Settings::kLatencyMsMax, "%+d")) {
					settings->latencyMs = ms;
					changedFields_ |= Settings::kFieldLatency;
					player->SetDisplayLatency(false, MsToFrames(ms, player));
				}
			}
			ImGui::TextDisabled(
			    "%s",
			    MsgF("Settings.LatencyNow",
			         MsgNum("%+.1f", FramesToMs(player->displayLatencyFrames(), player)),
			         MsgNum("%d", player->audioBufferFrames()))
			        .c_str());
		}

		if (pdxPathBuf_[0] == '\0' && !settings->pdxPath.empty()) {
			snprintf(pdxPathBuf_, sizeof(pdxPathBuf_), "%s", settings->pdxPath.c_str());
		}
		// 打ち込みでも、[参照...] で L キーと同じフォルダ選択からでも指定できる。
		// ラベルは上の行に出す。横に並べると入力欄と参照ボタンが入らない
		// （スキンの下限は横 480px）。
		ImGui::TextUnformatted(Msg("Settings.PdxPath"));
		{
			const ImGuiStyle &style = ImGui::GetStyle();
			const float browseW =
			    ImGui::CalcTextSize(Msg("Button.Browse")).x + style.FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(-(browseW + style.ItemSpacing.x));
		}
		if (ImGui::InputText("##pdxpath", pdxPathBuf_, sizeof(pdxPathBuf_))) {
			settings->pdxPath = pdxPathBuf_;
			changedFields_ |= Settings::kFieldPdxPath;
		}
		ImGui::SameLine();
		if (ImGui::Button(Msg("Button.Browse"))) {
			// モーダル同士は入れ子にせず、いったん設定ウィンドウを閉じてから
			// フォルダ選択を出す。戻ってきたらまた開く。
			folderTarget_ = kFolderTargetPdx;
			folderReturnToSettings_ = true;
			folderOpenPending_ = true;
			visible_ = false;
		}
	}

	// 保存ボタンは無い。触った時点で mxv2.ini へ書き戻す（スマートフォンでの
	// 作法に合わせてある。デスクトップでも不自然ではないという判断）。
	DragToScroll(&dragScroll_, &dragMoved_, true, false);
	ImGui::EndPopup();
}

// モーダルの開閉を ImGui のポップアップ状態と同期する。
// 「これから中身を組み立てるべきか」を返す。
//
// ここが要点: ImGui は **こちらに断りなくポップアップを閉じる**
// （ESC キー、× ボタン）。それを見落として *wanted を true のままにすると、
// 次のフレームで開き直してしまい「閉じられないダイアログ」になる。
// 自分が開けたものかどうかを openedModal_ で覚えておいて、
// 開いているはずなのに閉じていたら *wanted を折る。
bool SettingsUi::SyncModal(const char *title, bool *wanted) {
	const bool isOpen = ImGui::IsPopupOpen(title);
	const bool mine = (openedModal_ == title);

	if (*wanted) {
		if (isOpen) return true;
		if (mine) {
			// ImGui 側が閉じた (ESC など)
			*wanted = false;
			openedModal_ = 0;
			return false;
		}
		ImGui::OpenPopup(title);
		openedModal_ = title;
		return true;
	}

	// 閉じたい。開いていれば BeginPopupModal に p_open=false を渡すことで
	// ImGui 自身に閉じてもらう（CloseCurrentPopup は中でしか呼べない）。
	if (isOpen) return true;
	if (mine) openedModal_ = 0;
	return false;
}

// スキンの配色を編集するダイアログ。以前は設定ウィンドウの中の
// CollapsingHeader だったが、項目数が多く設定ウィンドウが縦に伸びるので
// 別ダイアログにした。F2 と右クリックメニューから開ける。
void SettingsUi::BuildColorsWindow(Settings *settings, DrawScreen *draw, Player *player) {
	if (!SyncModal(kColorsTitle, &showColors_)) return;

	// 設定ウィンドウと同じ作法。開くたびに左上、画面からはみ出さない大きさ。
	const ImGuiIO &io = ImGui::GetIO();
	float w = 380.0f * styleScale_;
	float h = 464.0f * styleScale_;
	if (w > io.DisplaySize.x) w = io.DisplaySize.x;
	if (h > io.DisplaySize.y) h = io.DisplaySize.y;
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (ImGui::BeginPopupModal(kColorsTitle, &showColors_,
	                           ImGuiWindowFlags_NoCollapse |
	                               ImGuiWindowFlags_NoSavedSettings)) {
		// 開いた直後は、保存先の名前を今のスキン名にしておく。書き込み先は
		// 必ずユーザーフォルダなので "assets:" は外す（同梱スキンを編集して
		// いるときは、同じ名前のユーザースキンが作られることになる）。
		if (skinNameReset_) {
			skinNameReset_ = false;
			snprintf(skinNameBuf_, sizeof(skinNameBuf_), "%s",
			         SkinRefName(settings->skinName).c_str());
			saveError_.clear();
		}

		// 保存まわりはダイアログの先頭に置く。色の項目は縦に長く、
		// 下に置くと毎回スクロールしないと届かない。
		BuildSkinSaveRow(settings, draw);

		Colors &t = draw->colors();
		bool dirty = false;

		ImGui::SeparatorText(Msg("Colors.Background"));
		{
			bool useBitmap = (t.back.bitmap != 0);
			if (ImGui::Checkbox(Msg("Colors.UseImage"), &useBitmap)) {
				t.back.bitmap = useBitmap ? 1 : 0;
				dirty = true;
			}
			if (AlphaRow(Msg("Colors.ImageGain"), &t.back.bitmapBright)) dirty = true;
			if (ColorRow(Msg("Colors.BgColor"), &t.back.color)) dirty = true;
			if (AlphaRow(Msg("Colors.BgStrength"), &t.back.colorBright)) dirty = true;
		}

		ImGui::SeparatorText(Msg("Colors.Keyboard"));
		{
			if (GainRow(Msg("Colors.BlackKey"), &t.kb.blackBright)) dirty = true;
			if (GainRow(Msg("Colors.WhiteKey"), &t.kb.whiteBright)) dirty = true;
			if (GainRow(Msg("Colors.PressedKey"), &t.kb.bright)) dirty = true;
		}

		ImGui::SeparatorText(Msg("Colors.Status"));
		{
			if (ColorRow(L("Colors.Text", "##st"), &t.status.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##st"), &t.status.colorBright)) dirty = true;
			if (ColorRow(L("Colors.Back", "##st"), &t.status.backColor)) dirty = true;
			if (AlphaRow(L("Colors.BackStrength", "##st"), &t.status.backColorBright)) dirty = true;
		}

		ImGui::SeparatorText(Msg("Colors.Title"));
		{
			if (ColorRow(L("Colors.Text", "##ti"), &t.mdxTitle.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##ti"), &t.mdxTitle.colorBright)) dirty = true;
			if (ColorRow(L("Colors.Back", "##ti"), &t.mdxTitle.backColor)) dirty = true;
			if (AlphaRow(L("Colors.BackStrength", "##ti"), &t.mdxTitle.backColorBright)) dirty = true;
		}

		ImGui::SeparatorText(Msg("Colors.Filer"));
		{
			if (ColorRow(L("Colors.Cursor", "##fi"), &t.filer.cursorColor)) dirty = true;
			if (AlphaRow(L("Colors.CursorStrength", "##fi"), &t.filer.cursorColorBright)) dirty = true;
			// 「文字の強さ」は下の 4 つの色すべてに効くので、そのあとに置く。
			if (ColorRow(L("Colors.Text", "##fi"), &t.filer.color)) dirty = true;
			if (ColorRow(L("Colors.FolderText", "##fi"), &t.filer.folderColor)) dirty = true;
			if (ColorRow(L("Colors.DriveText", "##fi"), &t.filer.driveColor)) dirty = true;
			if (ColorRow(L("Colors.FileSystemText", "##fi"), &t.filer.fileSystemColor)) {
				dirty = true;
			}
			if (AlphaRow(L("Colors.TextStrength", "##fi"), &t.filer.colorBright)) dirty = true;
			if (ColorRow(L("Colors.Back", "##fi"), &t.filer.backColor)) dirty = true;
			if (AlphaRow(L("Colors.BackStrength", "##fi"), &t.filer.backColorBright)) dirty = true;
		}

		ImGui::SeparatorText(Msg("Colors.PlayKey"));
		{
			if (ColorRow(L("Colors.Text", "##pk"), &t.playKey.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##pk"), &t.playKey.colorBright)) dirty = true;
			if (GainRow(L("Colors.ButtonGain", "##pk"), &t.playKey.keyBright)) dirty = true;
		}

		if (dirty) Rebuild(draw, player);

		BuildOverwriteWindow(settings, draw);

		DragToScroll(&dragScroll_, &dragMoved_, true, false);
		ImGui::EndPopup();
	}
}

// 配色設定の先頭。保存先のスキン名と、保存・読み直しのボタン。
//
// 保存先はスキンの名前で指定する。今のスキンの名前のままなら上書き、
// 別の名前にすれば「名前を付けて保存」で新しいスキンができる。
// 同梱ぶんは読み取り専用なので、書き込み先は必ずユーザーフォルダ側。
void SettingsUi::BuildSkinSaveRow(Settings *settings, DrawScreen *draw) {
	ImGui::TextUnformatted(Msg("Colors.SkinName"));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	const bool entered = ImGui::InputText("##skinname", skinNameBuf_, sizeof(skinNameBuf_),
	                                      ImGuiInputTextFlags_EnterReturnsTrue);

	if (ImGui::Button(Msg("Button.Save")) || entered) {
		const std::string name = TrimSpaces(skinNameBuf_);
		saveError_.clear();
		if (!CheckSkinName(name, &saveError_)) {
			// 文言は CheckSkinName が入れている
		} else if (paths_.UserSkinExists(name)) {
			// すでにある名前。上書きしてよいか訊く。同梱ぶんに同じ名前が
			// あっても、そちらは別のスキン (assets:<名前>) なので訊かない。
			overwriteName_ = name;
			openOverwrite_ = true;
		} else {
			SaveColorsAs(name, settings, draw);
		}
		saveErrorFresh_ = !saveError_.empty();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Reload"))) {
		pendingSkin_ = settings->skinName;
	}
	ImGui::SameLine();
	{
		const std::string name = TrimSpaces(skinNameBuf_);
		ImGui::TextDisabled("skin/%s/colors.ini", name.c_str());
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", JoinPath(paths_.UserSkinDir(name), kColorsFile).c_str());
		}
	}

	// 土台にしているスキン (layout.ini の [Skin] Base)。レイアウトと素材が
	// どこから来ているかは、配色をいじるときに知りたいことが多い。
	{
		const std::string &base = draw->skin().baseRef();
		ImGui::TextUnformatted(Msg("Colors.BaseSkin"));
		ImGui::SameLine();
		if (base.empty()) {
			ImGui::TextDisabled("%s", Msg("Colors.BaseNone"));
		} else {
			ImGui::TextDisabled("%s", base.c_str());
		}
	}

	if (!saveError_.empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", saveError_.c_str());
		if (saveErrorFresh_) {
			saveErrorFresh_ = false;
			ImGui::SetScrollHereY(0.0f);
		}
	}
}

// 上書きの確認。配色設定の**中で**開く。ImGui のモーダルは
// 入れ子なら素直に重なる（同じ階層で掛け替えようとすると失敗する）。
void SettingsUi::BuildOverwriteWindow(Settings *settings, DrawScreen *draw) {
	if (openOverwrite_) {
		openOverwrite_ = false;
		ImGui::OpenPopup(kOverwriteTitle);
	}

	overwriteOpen_ = ImGui::IsPopupOpen(kOverwriteTitle);
	if (!overwriteOpen_) {
		closeOverwrite_ = false;
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(kOverwriteTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("%s", MsgF("Colors.OverwriteMessage", overwriteName_).c_str());
	ImGui::TextUnformatted(Msg("Colors.OverwriteQuestion"));
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Overwrite"))) {
		SaveColorsAs(overwriteName_, settings, draw);
		saveErrorFresh_ = !saveError_.empty();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || closeOverwrite_) {
		closeOverwrite_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ファイルシステムの設定 (F3)。ファイラーのルートに並べる顔ぶれと順番を
// 決める。実体は Vfs のマウント一覧なので、触ったらその場で効く。
// 並びは [FileSystem] へ書き戻す（changedFields_ 経由でメインループが書く）。
void SettingsUi::BuildFileSystemsWindow(Filer *filer) {
	if (!SyncModal(kFileSystemsTitle, &showFileSystems_)) return;

	const ImGuiIO &io = ImGui::GetIO();
	float w = 460.0f * styleScale_;
	float h = 360.0f * styleScale_;
	if (w > io.DisplaySize.x) w = io.DisplaySize.x;
	if (h > io.DisplaySize.y) h = io.DisplaySize.y;
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal(kFileSystemsTitle, &showFileSystems_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	if (vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	const int count = vfs_->count();
	if (fsSelected_ >= count) fsSelected_ = count - 1;
	if (fsSelected_ < 0) fsSelected_ = 0;

	// 一覧。削除できないもの（初回起動時から使えるもの）は薄く出して、
	// 削除できないことを見て分かるようにする。
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##fslist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		for (int i = 0; i < count; i++) {
			const FileSystem *fs = vfs_->at(i);
			char label[256];
			snprintf(label, sizeof(label), "%s  %s##fs%d", fs->prefix(),
			         fs->label().c_str(), i);
			const bool fixed = !fs->removable();
			if (fixed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			if (ImGui::Selectable(label, i == fsSelected_) && !dragMoved_) {
				fsSelected_ = i;
				fsError_.clear();
			}
			if (fixed) ImGui::PopStyleColor();
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (fsError_.empty()) {
		ImGui::TextDisabled("%s", Msg("FileSystems.FixedNote"));
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", fsError_.c_str());
	}

	const FileSystem *sel = (count > 0) ? vfs_->at(fsSelected_) : 0;

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(fsSelected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		vfs_->Move(fsSelected_, -1);
		fsSelected_--;
		changedFields_ |= Settings::kFieldFileSystems;
		filer->Refresh();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(fsSelected_ < 0 || fsSelected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		vfs_->Move(fsSelected_, 1);
		fsSelected_++;
		changedFields_ |= Settings::kFieldFileSystems;
		filer->Refresh();
	}
	ImGui::EndDisabled();

	// [追加] は外部ファイルシステム（SAF / Web / SMB …）用。まだ無いので
	// 押せない。
	ImGui::SameLine();
	ImGui::BeginDisabled(true);
	ImGui::Button(Msg("Button.AddFs"));
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", Msg("FileSystems.AddNone"));
	}

	// [削除]。削除できないものはグレーアウト。カレントのものは押せるが、
	// 押したときに断る（仕様どおり）。
	ImGui::SameLine();
	ImGui::BeginDisabled(sel == 0 || !sel->removable());
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (sel != 0 && filer != 0 && filer->fs() == sel) {
			fsError_ = Msg("FileSystems.RemoveCurrent");
		} else {
			fsError_.clear();
			fsOpenConfirm_ = true;
		}
	}
	ImGui::EndDisabled();

	BuildFsRemoveWindow(filer);

	ImGui::EndPopup();
}

// 「本当に削除するか」。ファイルシステムの設定の中に入れ子で開く。
void SettingsUi::BuildFsRemoveWindow(Filer *filer) {
	if (fsOpenConfirm_) {
		fsOpenConfirm_ = false;
		ImGui::OpenPopup(kFsRemoveTitle);
	}

	fsConfirmOpen_ = ImGui::IsPopupOpen(kFsRemoveTitle);
	if (!fsConfirmOpen_) {
		fsCloseConfirm_ = false;
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(kFsRemoveTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const FileSystem *sel =
	    (vfs_ != 0 && fsSelected_ >= 0 && fsSelected_ < vfs_->count()) ? vfs_->at(fsSelected_)
	                                                                  : 0;
	ImGui::Text("%s", MsgF("FileSystems.RemoveConfirm",
	                       sel != 0 ? sel->label() : std::string())
	                      .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		// 動的に足したファイルシステムは実体も捨てるので、タイトルを
		// 読んでいるスレッドの手が離れるのを待ってからにする。
		if (filer != 0) filer->WaitTitles();
		vfs_->RemoveMounted(fsSelected_);
		if (fsSelected_ >= vfs_->count()) fsSelected_ = vfs_->count() - 1;
		if (fsSelected_ < 0) fsSelected_ = 0;
		changedFields_ |= Settings::kFieldFileSystems;
		if (filer != 0) filer->Refresh();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || fsCloseConfirm_) {
		fsCloseConfirm_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// ブックマーク (F4 / M)
//
// 控えるのはフォルダの ref だけ。実体は Settings::bookmarks で、触ったら
// kFieldBookmarks を立ててメインループに ini へ書き戻してもらう。
// ---------------------------------------------------------------------------

int SettingsUi::FindBookmark(const std::vector<std::string> &list,
                             const std::string &ref) const {
	if (vfs_ == 0 || ref.empty()) return -1;
	for (size_t i = 0; i < list.size(); i++) {
		if (vfs_->SameRef(list[i], ref)) return (int)i;
	}
	return -1;
}

// ブックマークを開く。控えた先がファイルだったときは「そのファイルのある
// フォルダ」へ直し、控えの方も書き換えてから開く（仕様どおり、直した結果が
// 他と重なったら相手側を消す）。開けなければ bmError_ に理由を入れて残る。
void SettingsUi::OpenBookmark(Settings *settings, int index) {
	std::vector<std::string> &list = settings->bookmarks;
	if (vfs_ == 0 || index < 0 || index >= (int)list.size()) return;

	std::string ref = list[index];
	if (!vfs_->IsDir(ref)) {
		const std::string parent = vfs_->Parent(ref);
		if (!vfs_->Exists(ref) || parent.empty() || !vfs_->IsDir(parent)) {
			bmError_ = Msg("Bookmark.NotFound");
			return;
		}
		list[index] = parent;
		// 重複したら相手側を消す。消したのが上の行なら、こちらの位置も繰り上がる。
		for (int i = (int)list.size() - 1; i >= 0; i--) {
			if (i == index) continue;
			if (!vfs_->SameRef(list[i], parent)) continue;
			list.erase(list.begin() + i);
			if (i < index) index--;
		}
		bmSelected_ = index;
		changedFields_ |= Settings::kFieldBookmarks;
		ref = parent;
	}

	// ファイラーを動かすのはメインループの持ち物なので、要求だけ積む
	// （フォルダ選択やコンテキストメニューと同じ作法）。
	requestedFolder_ = ref;
	request_ = kRequestSetFolder;
	bmError_.clear();
	showBookmarks_ = false;
}

void SettingsUi::BuildBookmarksWindow(Settings *settings, Filer *filer) {
	if (!SyncModal(kBookmarksTitle, &showBookmarks_)) return;

	const ImGuiIO &io = ImGui::GetIO();
	float w = 460.0f * styleScale_;
	float h = 360.0f * styleScale_;
	if (w > io.DisplaySize.x) w = io.DisplaySize.x;
	if (h > io.DisplaySize.y) h = io.DisplaySize.y;
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal(kBookmarksTitle, &showBookmarks_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	if (vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const int count = (int)list.size();
	if (bmSelected_ >= count) bmSelected_ = count - 1;
	if (bmSelected_ < 0 && count > 0) bmSelected_ = 0;

	// 一覧。クリックで選ぶだけ、ダブルクリックで開く（メイン画面の
	// ファイラーと同じ操作感）。開くのは一覧を組み終わってからにする。
	int openIndex = -1;
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 3.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##bmlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		if (count == 0) ImGui::TextDisabled("%s", Msg("Bookmark.Empty"));
		for (int i = 0; i < count; i++) {
			char label[512];
			snprintf(label, sizeof(label), "%s##bm%d", vfs_->DisplayPath(list[i]).c_str(), i);
			if (ImGui::Selectable(label, i == bmSelected_,
			                      ImGuiSelectableFlags_AllowDoubleClick) &&
			    !dragMoved_) {
				bmSelected_ = i;
				bmError_.clear();
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openIndex = i;
			}
			// 表示は見やすさ優先で DisplayPath なので、生の ref はここで見せる。
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].c_str());
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (bmError_.empty()) {
		ImGui::TextDisabled("%s", Msg("Bookmark.Hint"));
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", bmError_.c_str());
	}

	// [開く] は 1 行を占有する大きなボタン（仕様どおり）。
	ImGui::BeginDisabled(bmSelected_ < 0);
	if (ImGui::Button(Msg("Button.Open"), ImVec2(-FLT_MIN, 0.0f))) openIndex = bmSelected_;
	ImGui::EndDisabled();

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(bmSelected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		std::swap(list[bmSelected_], list[bmSelected_ - 1]);
		bmSelected_--;
		changedFields_ |= Settings::kFieldBookmarks;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(bmSelected_ < 0 || bmSelected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		std::swap(list[bmSelected_], list[bmSelected_ + 1]);
		bmSelected_++;
		changedFields_ |= Settings::kFieldBookmarks;
	}
	ImGui::EndDisabled();

	// [追加] はカレントフォルダを選択位置へ挿し込む。ルート（ファイル
	// システムの選択）と、すでに控えてある場所は入れられない。
	const std::string cur = (filer != 0) ? filer->currentRef() : std::string();
	const bool dup = (FindBookmark(list, cur) >= 0);
	const bool full = (count >= Settings::kMaxBookmarks);
	ImGui::SameLine();
	ImGui::BeginDisabled(cur.empty() || dup || full);
	if (ImGui::Button(Msg("Button.Add"))) {
		const int at = (bmSelected_ >= 0) ? bmSelected_ : count;
		list.insert(list.begin() + at, cur);
		bmSelected_ = at;
		changedFields_ |= Settings::kFieldBookmarks;
		bmError_.clear();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		if (cur.empty()) {
			ImGui::SetTooltip("%s", Msg("Bookmark.AddRoot"));
		} else if (dup) {
			ImGui::SetTooltip("%s",
			                  MsgF("Bookmark.AddDuplicate", vfs_->DisplayPath(cur)).c_str());
		} else if (full) {
			ImGui::SetTooltip("%s", Msg("Bookmark.AddFull"));
		} else {
			ImGui::SetTooltip("%s",
			                  MsgF("Bookmark.AddHint", vfs_->DisplayPath(cur)).c_str());
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(bmSelected_ < 0);
	if (ImGui::Button(Msg("Button.Remove"))) {
		bmError_.clear();
		bmOpenRemove_ = true;
	}
	ImGui::EndDisabled();

	BuildBookmarkRemoveWindow(settings);

	if (openIndex >= 0) OpenBookmark(settings, openIndex);

	ImGui::EndPopup();
}

// 「本当に削除するか」。ブックマークの設定の中に入れ子で開く。
void SettingsUi::BuildBookmarkRemoveWindow(Settings *settings) {
	if (bmOpenRemove_) {
		bmOpenRemove_ = false;
		ImGui::OpenPopup(kBmRemoveTitle);
	}

	bmRemoveOpen_ = ImGui::IsPopupOpen(kBmRemoveTitle);
	if (!bmRemoveOpen_) {
		bmCloseRemove_ = false;
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(kBmRemoveTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const bool valid = (bmSelected_ >= 0 && bmSelected_ < (int)list.size());
	ImGui::Text("%s", MsgF("Bookmark.RemoveConfirm",
	                       (valid && vfs_ != 0) ? vfs_->DisplayPath(list[bmSelected_])
	                                            : std::string())
	                      .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (valid) {
			list.erase(list.begin() + bmSelected_);
			if (bmSelected_ >= (int)list.size()) bmSelected_ = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldBookmarks;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || bmCloseRemove_) {
		bmCloseRemove_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// Shift+M（とコンテキストメニュー）の確認。カレントフォルダが控えてあれば
// 削除、無ければ末尾へ追加する。メイン画面から直に出すので、他のダイアログの
// 入れ子ではなく単独のモーダルとして開く。
void SettingsUi::BuildBookmarkToggleWindow(Settings *settings, Filer *filer) {
	if (bmOpenToggle_) {
		bmOpenToggle_ = false;
		const std::string cur = (filer != 0) ? filer->currentRef() : std::string();
		// 追加できない場所（ファイルシステムの選択）では何もしない。
		if (!cur.empty()) {
			bmToggleRef_ = cur;
			ImGui::OpenPopup(kBmToggleTitle);
		}
	}

	bmToggleOpen_ = ImGui::IsPopupOpen(kBmToggleTitle);
	if (!bmToggleOpen_) {
		bmCloseToggle_ = false;
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(kBmToggleTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const int at = FindBookmark(list, bmToggleRef_);
	const std::string shown = (vfs_ != 0) ? vfs_->DisplayPath(bmToggleRef_) : bmToggleRef_;

	if (at >= 0) {
		ImGui::Text("%s", MsgF("Bookmark.RemoveConfirm", shown).c_str());
		ImGui::Separator();
		if (ImGui::Button(Msg("Button.Remove"))) {
			list.erase(list.begin() + at);
			if (bmSelected_ >= (int)list.size()) bmSelected_ = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldBookmarks;
			ImGui::CloseCurrentPopup();
		}
	} else {
		const bool full = ((int)list.size() >= Settings::kMaxBookmarks);
		ImGui::Text("%s", MsgF("Bookmark.AddConfirm", shown).c_str());
		if (full) {
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s",
			                   Msg("Bookmark.Full"));
		}
		ImGui::Separator();
		ImGui::BeginDisabled(full);
		if (ImGui::Button(Msg("Button.Add"))) {
			list.push_back(bmToggleRef_);  // 追加は末尾（仕様どおり）
			changedFields_ |= Settings::kFieldBookmarks;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || bmCloseToggle_) {
		bmCloseToggle_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// 子フォルダの一覧だけ作り直す。毎フレーム読み直すと重いので、
// 行き先が変わったときだけ列挙する。
// dir が空のときはファイルシステムの選択（マウントされている FS が並ぶ）。
void SettingsUi::RelistFolder(const std::string &dir) {
	folderDir_.clear();
	folderSelected_.clear();
	folderEntries_.clear();
	if (vfs_ == 0) return;

	FileSystem *fs = 0;
	std::string rel;
	if (!vfs_->Parse(dir, &fs, &rel)) return;
	folderDir_ = Vfs::MakeRef(fs, rel);

	if (fs == 0) {
		for (int i = 0; i < vfs_->count(); i++) {
			FileSystem *m = vfs_->at(i);
			if (!m->available()) continue;
			FolderEntry e;
			e.name = m->prefix();
			e.ref = Vfs::MakeRef(m, m->Root());
			folderEntries_.push_back(e);
		}
		return;
	}

	std::vector<DirEntry> entries;
	std::vector<std::string> names;
	if (fs->List(rel, &entries)) {
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].isDir) names.push_back(entries[i].name);
		}
	}
	std::sort(names.begin(), names.end(), LessPathNoCase);
	for (size_t i = 0; i < names.size(); i++) {
		FolderEntry e;
		e.name = names[i];
		e.ref = Vfs::MakeRef(fs, fs->Join(rel, names[i]));
		folderEntries_.push_back(e);
	}

	// ファイルシステムが足すもの（ローカル FS のドライブ一覧）。
	std::vector<FsExtraItem> extras;
	fs->AppendExtraItems(rel, &extras);
	for (size_t i = 0; i < extras.size(); i++) {
		FolderEntry e;
		e.name = extras[i].name;
		e.ref = Vfs::MakeRef(fs, extras[i].rel);
		folderEntries_.push_back(e);
	}
}

// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
void SettingsUi::SetFolderDir(const std::string &dir) {
	RelistFolder(dir);
	snprintf(folderPathBuf_, sizeof(folderPathBuf_), "%s", folderDir_.c_str());
	folderError_.clear();
}

// 一覧で選んだものを入力欄へ移すだけ。中へは入らない。
// メイン画面のファイラーと同じで、クリックは選ぶだけ・ダブルクリックで移動。
void SettingsUi::SelectFolderEntry(const std::string &path) {
	folderSelected_ = path;
	snprintf(folderPathBuf_, sizeof(folderPathBuf_), "%s", path.c_str());
	folderError_.clear();
}

// フォルダを選ぶダイアログ (L)。旧 mxv の MX_GetNewDirFileList にあたる。
// 原典は SHBrowseForFolder を出していたが、あれは Windows 専用なので、
// パスの打ち込みと子フォルダの一覧を持つ自前のダイアログにしてある。
// 決まった行き先は request_ に積んで、実際の移動はメインループに任せる
// （ファイラーの持ち物はあちらなので、コンテキストメニューと同じ作法）。
void SettingsUi::SetStartupWarnings(const std::vector<std::string> &lines) {
	startupLines_ = lines;
	showStartup_ = !startupLines_.empty();
}

// 起動時の警告。ウィンドウが出る前に起きたこと（ini から捨てた項目、
// スキンやフォントの取りこぼし）を、最初のフレームでまとめて見せる。
// ログにも同じものが出ているので、閉じたら二度と出さない。
void SettingsUi::BuildStartupWindow() {
	if (!SyncModal(kStartupTitle, &showStartup_)) return;

	const ImGuiIO &io = ImGui::GetIO();
	float w = 520.0f * styleScale_;
	float h = 280.0f * styleScale_;
	if (w > io.DisplaySize.x) w = io.DisplaySize.x;
	if (h > io.DisplaySize.y) h = io.DisplaySize.y;
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal(kStartupTitle, &showStartup_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	{
		const float foot = ImGui::GetFrameHeightWithSpacing();
		ImGui::BeginChild("##startup", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		for (size_t i = 0; i < startupLines_.size(); i++) {
			ImGui::Bullet();
			ImGui::TextWrapped("%s", startupLines_[i].c_str());
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, false);
		ImGui::EndChild();
	}
	if (ImGui::Button(Msg("Button.Close"), ImVec2(-FLT_MIN, 0.0f))) showStartup_ = false;

	ImGui::EndPopup();
}

// [操作方法] の中身をカタログから作る。-h の出力と同じ一覧
// （[HelpKeys] [HelpMouse]）を、見出しを挟んで並べたもの。
void SettingsUi::LoadHelpRows() {
	static const char *kParts[2][2] = {
		{ "Help.Keys", "HelpKeys" },
		{ "Help.Mouse", "HelpMouse" },
	};

	helpRows_.clear();
	for (int i = 0; i < 2; i++) {
		HelpRow head;
		head.header = true;
		head.key = Msg(kParts[i][0]);
		helpRows_.push_back(head);

		const std::vector<MsgRow> &rows = MsgList(kParts[i][1]);
		for (size_t j = 0; j < rows.size(); j++) {
			HelpRow row;
			row.key = rows[j].key;
			row.desc = rows[j].value;
			helpRows_.push_back(row);
		}
	}
}

// 操作方法のダイアログ (F11 / H)。中身は -h で出すものと同じ文面で、
// main.cpp から SetHelpText() で渡してもらう（文面を二重に持たない）。
void SettingsUi::BuildHelpWindow() {
	if (!SyncModal(kHelpTitle, &showHelp_)) return;

	const ImGuiIO &io = ImGui::GetIO();
	float w = 560.0f * styleScale_;
	float h = 460.0f * styleScale_;
	if (w > io.DisplaySize.x) w = io.DisplaySize.x;
	if (h > io.DisplaySize.y) h = io.DisplaySize.y;
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (ImGui::BeginPopupModal(kHelpTitle, &showHelp_,
	                           ImGuiWindowFlags_NoCollapse |
	                               ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::BeginChild("##help", ImVec2(0, 0), ImGuiChildFlags_None,
		                  ImGuiWindowFlags_HorizontalScrollbar);

		// キー名の桁を揃えて 2 段組で出す。空白で揃えないのは、同梱フォントが
		// プロポーショナルで桁が合わないため。
		//
		// 幅は「一番広いキー名」と「一番広い説明」から決める。横に収まらな
		// ければ字を小さくする。文字の幅は大きさに比例するので、今の大きさで
		// 測った比をそのまま倍率にしてよい。PushFont に渡すのは**倍率を
		// 掛ける前**の大きさ (FontSizeBase)。
		const float avail = ImGui::GetContentRegionAvail().x;
		float scale = 1.0f;
		{
			float keyW = 0.0f;
			float descW = 0.0f;
			for (size_t i = 0; i < helpRows_.size(); i++) {
				if (helpRows_[i].header) continue;
				const float k = ImGui::CalcTextSize(helpRows_[i].key.c_str()).x;
				const float d = ImGui::CalcTextSize(helpRows_[i].desc.c_str()).x;
				if (k > keyW) keyW = k;
				if (d > descW) descW = d;
			}
			const float unit = ImGui::CalcTextSize(" ").x;  // 字下げと段間に使う
			const float total = unit * 4.0f + keyW + descW;
			if (total > avail && avail > 0.0f && total > 0.0f) scale = avail / total;
		}
		const bool shrink = (scale < 1.0f);
		if (shrink) ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * scale);

		// 縮めたあとの大きさで測り直す。
		float keyW = 0.0f;
		for (size_t i = 0; i < helpRows_.size(); i++) {
			if (helpRows_[i].header) continue;
			const float k = ImGui::CalcTextSize(helpRows_[i].key.c_str()).x;
			if (k > keyW) keyW = k;
		}
		const float unit = ImGui::CalcTextSize(" ").x;
		const float x0 = ImGui::GetCursorPosX();
		const float keyX = x0 + unit * 2.0f;   // 行頭の字下げ
		const float descX = keyX + keyW + unit * 2.0f;

		for (size_t i = 0; i < helpRows_.size(); i++) {
			const HelpRow &row = helpRows_[i];
			if (row.header) {
				if (i != 0) ImGui::Spacing();
				ImGui::TextUnformatted(row.key.c_str());
				continue;
			}
			ImGui::SetCursorPosX(keyX);
			ImGui::TextUnformatted(row.key.c_str());
			if (row.desc.empty()) continue;
			// SameLine の位置はウィンドウ左端からの距離なので、
			// Indent ではなくこちらで直に指定する。
			ImGui::SameLine(descX);
			ImGui::TextUnformatted(row.desc.c_str());
		}

		if (shrink) ImGui::PopFont();

		DragToScroll(&dragScroll_, &dragMoved_, false, false);
		ImGui::EndChild();
		ImGui::EndPopup();
	}
}

const char *SettingsUi::folderTitle() const {
	return (folderTarget_ == kFolderTargetPdx) ? kPdxFolderTitle : kFolderTitle;
}

void SettingsUi::BuildFolderWindow(Settings *settings) {
	const char *title = folderTitle();
	if (!SyncModal(title, &showFolder_)) return;

	const ImGuiIO &io = ImGui::GetIO();
	float w = 460.0f * styleScale_;
	float h = 400.0f * styleScale_;
	if (w > io.DisplaySize.x) w = io.DisplaySize.x;
	if (h > io.DisplaySize.y) h = io.DisplaySize.y;
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal(title, &showFolder_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// パスは直接打ってもよい。ENTER は「開く」と同じ扱い。
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##folderpath", folderPathBuf_, sizeof(folderPathBuf_),
	                              ImGuiInputTextFlags_EnterReturnsTrue);

	// 打ち込んだ先が実在するフォルダなら、下の一覧もそこへ合わせる。
	// 入力欄そのものは書き換えない。打っている最中に正規化された文字列で
	// 差し替えると、カーソルごと飛んでしまって打てなくなる。
	if (ImGui::IsItemEdited()) {
		const std::string typed = folderPathBuf_;
		std::string ref;
		if (!typed.empty() && vfs_ != 0 && vfs_->Resolve(typed, folderDir_, &ref) &&
		    vfs_->IsDir(ref) && ref != folderDir_) {
			RelistFolder(ref);
		}
		folderError_.clear();
	}

	// 親へ戻るのは一覧の ".." が受け持つので、専用のボタンは置かない。
	// この行は、下の一覧がどこを出しているのかを常に知らせる。入力欄と
	// 同じことも多いが、一覧で選んだだけのときや打ち込みの途中は食い違う。
	// 見出しは付けず、上の入力欄と桁を揃える。入力欄の文字は枠の内側に
	// FramePadding のぶん寄っているので、こちらも同じだけ下げる。
	// ファイルシステムのルートの 1 つ上は「ファイルシステムの選択」(ref は空)。
	const std::string up = (vfs_ != 0) ? vfs_->Parent(folderDir_) : std::string();
	const bool hasUp = !folderDir_.empty();
	{
		const float inset = ImGui::GetStyle().FramePadding.x;
		ImGui::Indent(inset);
		ImGui::TextDisabled("%s", folderDir_.empty() ? Msg("Folder.FileSystem")
		                                             : folderDir_.c_str());
		ImGui::Unindent(inset);
	}

	// 子フォルダの一覧。クリックで選ぶだけ、ダブルクリックでその中へ入る。
	// メイン画面のファイラーと同じ操作感にしてある。
	// 一覧の作り直しは回している最中にやってはいけないので、行き先を
	// 控えてから動かす。
	std::string nextDir;
	bool nextDirValid = false;
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##folderlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);

		// 1 行ぶんの処理。AllowDoubleClick を付けると 1 回目のクリックでも
		// true が返るので、ダブルクリックかどうかを自分で見分ける。
		struct Row {
			static bool Hit(const char *label, const std::string &path,
			                const std::string &selected, bool *entered) {
				const bool on = (!selected.empty() && CompareNoCase(selected, path) == 0);
				if (!ImGui::Selectable(label, on, ImGuiSelectableFlags_AllowDoubleClick)) {
					return false;
				}
				*entered = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				return true;
			}
		};

		std::string pick;      // 選ばれたもの (ref)
		bool picked = false;   // ref は空にもなりうるので、有無は別に持つ
		bool entered = false;  // ダブルクリックだったか

		if (hasUp && Row::Hit("..", up, folderSelected_, &entered)) {
			pick = up;
			picked = true;
		}
		for (size_t i = 0; i < folderEntries_.size(); i++) {
			if (Row::Hit(folderEntries_[i].name.c_str(), folderEntries_[i].ref,
			             folderSelected_, &entered)) {
				pick = folderEntries_[i].ref;
				picked = true;
			}
		}

		// ドラッグでスクロールした指を離したときは、押した行を選ばない。
		// 中身は指に付いて動くので、離した先には押した行がそのまま居る。
		// これを拾ってしまうと「スクロールしたつもりが選択された」になる。
		if (picked && !dragMoved_) {
			if (entered) {
				nextDir = pick;
				nextDirValid = true;
			} else {
				SelectFolderEntry(pick);
			}
		}

		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (folderError_.empty()) {
		ImGui::TextDisabled("%s", Msg("Folder.Hint"));
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", folderError_.c_str());
	}
	if (ImGui::Button(Msg("Button.Open"))) apply = true;
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel"))) showFolder_ = false;

	if (nextDirValid) {
		SetFolderDir(nextDir);
	} else if (apply) {
		const std::string want = folderPathBuf_;
		std::string ref;
		const bool ok = (vfs_ != 0) && vfs_->Resolve(want, folderDir_, &ref);
		// ref が空なら「ファイルシステムの選択」。ファイラーは行けるが、
		// PDX の探索先には指定できない。
		if (!ok || (!ref.empty() && !vfs_->IsDir(ref))) {
			folderError_ = Msg("Folder.NotFound");
		} else if (folderTarget_ == kFolderTargetPdx) {
			if (ref.empty()) {
				folderError_ = Msg("Folder.NotFound");
			} else {
				settings->pdxPath = ref;
				snprintf(pdxPathBuf_, sizeof(pdxPathBuf_), "%s", ref.c_str());
				changedFields_ |= Settings::kFieldPdxPath;
				showFolder_ = false;
			}
		} else {
			// ファイラーを動かすのはメインループの持ち物なので、要求だけ積む。
			requestedFolder_ = ref;
			request_ = kRequestSetFolder;
			showFolder_ = false;
		}
	}

	// このダイアログのスクロールは中の一覧が受け持つので、ここでは呼ばない。
	// 同じ dragScroll_ を二重に使うと、一覧を掴んだ状態がここへ漏れる。
	ImGui::EndPopup();
}

// 旧 mxv の右クリックメニュー (mxv.cpp の CreateContextMenu) にあたる。
// 演奏の開始・曲送り・終了はメインループの持ち物なので request_ に積んで返す。
void SettingsUi::BuildContextMenu(Settings *settings, DrawScreen *draw, Player *player,
                                  Filer *filer) {
	(void)draw;

	// バナーを押したときはこちらから開ける（右クリックできない環境向け）。
	// BeginPopupContextVoid と同じ id なので、下の Begin がそのまま拾う。
	if (openContextMenu_) {
		openContextMenu_ = false;
		ImGui::OpenPopup("##mxv2ctx");
	}

	// どのウィンドウにも属さない場所での右クリック用の API を使う。
	// 素の OpenPopup + BeginPopup だと親ウィンドウが無い扱いになり、
	// 開いた次のフレームで ImGui に閉じられてしまう。
	if (ImGui::BeginPopupContextVoid("##mxv2ctx", ImGuiPopupFlags_MouseButtonRight)) {
		// ESC で閉じる。ImGui はキーボードナビを切ってあるとポップアップに
		// 対して WantCaptureKeyboard を立てないので、ESC はメインループ側へ
		// 素通りしてしまう（そのまま終了に使われていた）。main から
		// CloseDialog() 経由で来た印をここで始末する。CloseCurrentPopup は
		// ポップアップの中でしか呼べないので、この位置でないといけない。
		// 開いているサブメニューもまとめて閉じる。
		if (closeContextMenu_) {
			closeContextMenu_ = false;
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::MenuItem(Msg("Menu.Open"))) request_ = kRequestOpenCursor;
		if (ImGui::MenuItem(Msg("Menu.Folder"), "L")) {
			SetFolderDir(filer->currentRef());
			showFolder_ = true;
		}
		if (ImGui::MenuItem(Msg("Menu.Bookmarks"), "F4")) OpenBookmarks();
		{
			// カレントを控える / 控えを外す。どちらも確認してから実行するので、
			// ここでは印を立てるだけ（メニューの中で OpenPopup すると入れ子の
			// ポップアップになってしまう）。
			const std::string cur = filer->currentRef();
			const bool has = (FindBookmark(settings->bookmarks, cur) >= 0);
			if (ImGui::MenuItem(has ? Msg("Menu.BookmarkRemove") : Msg("Menu.BookmarkAdd"),
			                    "Shift+M", false, !cur.empty())) {
				bmOpenToggle_ = true;
			}
		}

		if (ImGui::BeginMenu(Msg("Menu.Control"))) {
			if (player->paused()) {
				if (ImGui::MenuItem(Msg("Menu.Resume"))) player->Resume();
			} else {
				if (ImGui::MenuItem(Msg("Menu.Pause"))) player->Pause();
			}
			if (ImGui::MenuItem(Msg("Menu.Replay"))) request_ = kRequestReplay;
			if (ImGui::MenuItem(Msg("Menu.Stop"))) player->Stop();
			if (ImGui::MenuItem(Msg("Menu.Fadeout"))) player->Fadeout();
			ImGui::Separator();
			if (ImGui::MenuItem(Msg("Menu.Prev"))) request_ = kRequestPrev;
			if (ImGui::MenuItem(Msg("Menu.Next"))) request_ = kRequestNext;
			ImGui::Separator();
			// 文言は短めにしてある。スキンの下限が横 480px で、そこでは
			// サブメニューを左右どちらにも逃がせず、長いとルートに重なる。
			if (ImGui::MenuItem(Msg("Menu.Cont"))) request_ = kRequestToggleCont;
			if (ImGui::MenuItem(Msg("Menu.Repeat"))) request_ = kRequestToggleRepeat;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(Msg("Menu.Mask"))) {
			// チェックが付いている = 鳴っている。ドライバのビットは
			// 「立っていると飛ばす」ので、表示は反転させる。
			static const char *kNames[16] = { "ch.1", "ch.2", "ch.3", "ch.4", "ch.5",
				                              "ch.6", "ch.7", "ch.8", "ch.P", "ch.Q",
				                              "ch.R", "ch.S", "ch.T", "ch.U", "ch.V",
				                              "ch.W" };
			const uint16_t mask = player->channelMask();
			for (int i = 0; i < 16; i++) {
				if (i == 8) ImGui::Separator();
				const bool on = ((mask & (1 << i)) == 0);
				if (ImGui::MenuItem(kNames[i], 0, on)) player->ToggleChannel(i);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(Msg("Menu.MaskFm"))) player->ToggleChannelGroup(0x00ff);
			if (ImGui::MenuItem(Msg("Menu.MaskPcm"))) player->ToggleChannelGroup(0xff00);
			if (ImGui::MenuItem(Msg("Menu.MaskAll"))) player->ToggleChannelGroup(0xffff);
			ImGui::EndMenu();
		}

		ImGui::Separator();
		if (ImGui::MenuItem(Msg("Menu.Settings"), "F1")) visible_ = true;
		// F2 と同じ経路を通す（スキン名の欄を埋め直すため）。
		if (ImGui::MenuItem(Msg("Menu.Colors"), "F2")) OpenColors();
		if (ImGui::MenuItem(Msg("Menu.FileSystems"), "F3")) OpenFileSystems();
		if (ImGui::MenuItem(Msg("Menu.Help"), "F11")) showHelp_ = true;
		if (ImGui::MenuItem(Msg("Menu.About"), "F12")) showAbout_ = true;
		ImGui::Separator();
		if (ImGui::MenuItem(Msg("Menu.Quit"))) request_ = kRequestQuit;

		ImGui::EndPopup();
	}

	// メニューが開いているかを覚えておく。ESC を「メニューを閉じる」に
	// 使ってよいかの判断に要る。id は BeginPopupContextVoid が今のウィンドウの
	// id スタックから作るので、同じ場所で聞かないと食い違う。
	contextMenuOpen_ = ImGui::IsPopupOpen("##mxv2ctx");
	if (!contextMenuOpen_) closeContextMenu_ = false;

	if (SyncModal(kAboutTitle, &showAbout_)) {
		// NOTICE は初めて開くときに読む（メニューからでも F12 からでも通る）。
		if (aboutText_.empty()) aboutText_ = LoadAboutText();

		// 設定ウィンドウと同じく、開くたびに画面の左上から出す。
		// 大きさは画面からはみ出さないように詰める。スキンの横幅の下限は
		// 480px なので、既定の 560px はそのままでは入らない。
		const ImGuiIO &io = ImGui::GetIO();
		float w = 560.0f * styleScale_;
		float h = 420.0f * styleScale_;
		if (w > io.DisplaySize.x) w = io.DisplaySize.x;
		if (h > io.DisplaySize.y) h = io.DisplaySize.y;
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
		if (ImGui::BeginPopupModal(kAboutTitle, &showAbout_,
		                           ImGuiWindowFlags_NoCollapse |
		                               ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextUnformatted(aboutHeader_.c_str());
			ImGui::Separator();
			// 下の枠は実行ファイルの隣の NOTICE をそのまま出したもの。
			// 見出しと同じ行が頭に来るが、あちらは独立した配布用の文書なので
			// 中身には手を入れない。
			ImGui::TextDisabled("NOTICE");
			ImGui::BeginChild("##about", ImVec2(0, 0), ImGuiChildFlags_None,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			// NOTICE は等幅 80 桁で書いてある。余裕をみて 88 桁ぶんが横に
			// 収まるところまで字を小さくする。物差しは区切り線と同じ "*"。
			// 同梱フォント (M PLUS 1p) は "=" がかなり広く、それに合わせると
			// 全体が小さくなりすぎたので、罫線ごと "*" に改めてある。
			// PushFont に渡すのは**倍率を掛ける前**の大きさ (FontSizeBase)。
			// GetFontSize() は掛けたあとの値なので渡してはいけない。
			const float rulerW = ImGui::CalcTextSize(kAboutRuler88).x;
			const float avail = ImGui::GetContentRegionAvail().x;
			const bool shrink = (rulerW > avail && avail > 0.0f && rulerW > 0.0f);
			if (shrink) ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * avail / rulerW);
			ImGui::TextUnformatted(aboutText_.c_str());
			if (shrink) ImGui::PopFont();

			// 本文はドラッグでスクロールする。子ウィンドウなのでタイトルバーは無い。
			DragToScroll(&dragScroll_, &dragMoved_, false, false);

			ImGui::EndChild();
			ImGui::EndPopup();
		}
	}
}

void SettingsUi::Render(Screen *screen) {
	if (!ready_) return;
	ImGui::Render();
	// 論理サイズを外して、実解像度のまま描く。
	screen->BeginNativeScale();
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), screen->renderer());
	screen->EndNativeScale();
}

}  // namespace mxv2
