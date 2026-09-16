// mxv2 - 設定 UI（Dear ImGui）
//
// ここにあるのは初期化・フレームごとの支度 (Build)・共通の小道具。
// ダイアログの中身はダイアログ単位で settingsui_*.cpp に分けてある
// （一覧は settingsui_internal.h の頭）。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "settingsui_internal.h"

#include "dirlister.h"
#include "drawscreen.h"
#include "fileutil.h"
#include "filer.h"
#include "kinsoku.h"
#include "message.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "vfs.h"

namespace mxv2 {

namespace {

// 同梱フォント。assets/ に置いてあるので、どのプラットフォームでも読める。
// M PLUS 1p Regular (SIL OFL 1.1)。ライセンス全文は assets/MPLUS1p-OFL.txt。
// JIS 第1+2水準を含み、CP932 変換で出る U+FF5E (～) や U+2015 (―) も持つ。
//
// **ダイアログは必ずこれを使う**（ユーザーの指示。2026-09-08）。
// スキンやユーザーフォルダの font.ttf は**キャンバスの文字だけ**に効く
// (textrender.cpp)。ダイアログの字は行の高さや mm 換算と噛み合っていて、
// 幅の広いフォントや字数の入らないフォントを差されるとラベルが切れたり
// 押せるところの寸法が狂ったりするので、ここは固定にする。
const char *kBundledFont = "MPLUS1p-Regular.ttf";

// 同梱フォントが見つからなかったときだけ見る差し替え用のスロット。
// **ふつうは使わない**（上の理由で、あくまで最後の保険）。
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

// ダブルクリックの判定を SDL（＝メイン画面のファイラーの clicks）に揃える。
// ImGui の既定は 300ms・6 実ピクセル未満で、指では事実上成立しない
// （Pixel 7a では 6px ≒ 0.4mm）。SDL は 500ms・縦横それぞれ 32 窓ピクセル
// 以内（SDL_mouse.c。「タッチ向けにこのくらい」との注記）。ImGui は直線
// 距離で見るので、SDL の箱を覆うように √2 倍しておく。窓ピクセルから
// 実ピクセルへの換算 (inputScale_) は毎フレーム変わりうるので、Build() で
// 毎回入れ直す。
const float kDoubleClickTimeSec = 0.5f;
const float kDoubleClickRadiusWindowPx = 32.0f;
const float kDoubleClickBoxToCircle = 1.4143f;

// 三点リーダー (U+2026) を下付きにするための下げ幅 (em)。
//
// UI 文言の「…」（省略と「選ぶと次にダイアログが出る」の印。Android の流儀）
// は欧文フォントではベースライン上の点だが、M PLUS 1p は JIS の作法で
// **行の中央**に置く（点は yMin 290〜yMax 430 / 1000、ピリオドは 0〜140）。
// そこで同じ TTF を U+2026 だけもう 1 度重ね読みし、GlyphOffset で
// 0.29 em 下げて点の下端をベースラインに揃える（本体側はこの 1 文字を
// GlyphExcludeRanges で外す。ImGui 1.92 は「先に読んだ源が持つ字が勝つ」）。
// 値は同梱フォントを測って決めたものなので、**同梱フォントのときだけ効かせる**
// （欧文フォントに落ちたときに二重に下がらないように）。
const float kEllipsisDropEm = 0.29f;
const ImWchar kEllipsisRange[] = { 0x2026, 0x2026, 0 };

}  // namespace

// ---------------------------------------------------------------------------
// settingsui_*.cpp で共有する小道具（settingsui_internal.h）
// ---------------------------------------------------------------------------
namespace settingsui {

// ダイアログの題名。文言はカタログ、"###" 以降は ImGui の id。
// SyncModal が題名をポインタで見分けるので、**毎フレーム同じ番地**を
// 返さないといけない。カタログの文字列はそのまま使えるが、id を繋いだ
// ものは 1 度だけ組み立てて使い回す。
const char *kSettingsTitle;
const char *kColorsTitle;
const char *kOverwriteTitle;
const char *kFolderTitle;
const char *kPdxFolderTitle;
const char *kBookmarkFolderTitle;
const char *kHelpTitle;
const char *kFileSystemsTitle;
const char *kFsRemoveTitle;
const char *kBookmarksTitle;
const char *kBmRemoveTitle;
const char *kBmToggleTitle;
const char *kPdxPathsTitle;
const char *kPdxRemoveTitle;
const char *kAboutTitle;
const char *kStartupTitle;
const char *kAddFsTitle;
const char *kQuitTitle;

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
	kBookmarkFolderTitle = TitleWithId("Dialog.BookmarkFolder", "###mxv2folder");
	kHelpTitle = Msg("Dialog.Help");
	kFileSystemsTitle = Msg("Dialog.FileSystems");
	kFsRemoveTitle = Msg("Dialog.FsRemove");
	kBookmarksTitle = Msg("Dialog.Bookmarks");
	// 見出しはファイルシステムの削除確認と同じなので、別のポップアップとして
	// 扱ってもらうために "###" で id を分ける。
	kBmRemoveTitle = TitleWithId("Dialog.BookmarkRemove", "###mxv2bmremove");
	kPdxPathsTitle = Msg("Dialog.PdxPaths");
	kPdxRemoveTitle = TitleWithId("Dialog.PdxRemove", "###mxv2pdxremove");
	// Shift+M の確認。ダイアログを開かずにメイン画面から直に出す。
	kBmToggleTitle = TitleWithId("Dialog.BookmarkToggle", "###mxv2bmtoggle");
	kAboutTitle = Msg("Dialog.About");
	kStartupTitle = Msg("Dialog.Startup");
	kAddFsTitle = Msg("Dialog.AddFs");
	kQuitTitle = Msg("Dialog.Quit");
}

// 言語を入れ替えたあと、題名を新しいカタログから取り直す。古いほうの番地は
// message.cpp が生かしたままにしてくれるので、途中で持っていても落ちない
// （中身が古いだけ）。
void ResetTitles() {
	kSettingsTitle = 0;
	InitTitles();
}

// 文言に ImGui の id を足した名札。同じ文言を 1 つの画面で何度も使うため。
std::string L(const char *key, const char *id) {
	return std::string(Msg(key)) + id;
}

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

// 補足や注意の 1 行。**折り返す**。指で操作する端末では字が大きくなるので、
// 折り返さないと携帯の狭い画面で右が切れて読めなくなる。
// 部品のラベル（チェックボックスの文言など）は ImGui が折り返してくれないので、
// そちらは短いままにしておくこと。
void TextWrapColor(const ImVec4 &color, const char *text) {
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	// 折り返しは禁則つきで自前に（kinsoku.h）。ImGui の折り返しは
	// 「。」や「」」を行頭に置いてしまう。
	TextWrappedKinsoku(text);
	ImGui::PopStyleColor();
}

void TextNote(const char *text) {
	TextWrapColor(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], text);
}

void TextError(const char *text) {
	TextWrapColor(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), text);
}

// 確認ダイアログの本文。`AlwaysAutoResize` のままだと、長いパスが 1 行で
// 伸びてダイアログが画面からはみ出す。折り返す位置を決めて幅を頭打ちにする。
void ConfirmText(const char *text) {
	const ImGuiIO &io = ImGui::GetIO();
	const ImGuiStyle &style = ImGui::GetStyle();
	float w = ImGui::GetFontSize() * 24.0f;  // 24 文字ぶんを目安に
	const float max = io.DisplaySize.x - style.WindowPadding.x * 2.0f;
	if (w > max) w = max;
	TextWrappedKinsoku(text, w);
}

// 横に並べる。ただし次に置くものが残り幅に入らないなら、並べずに次の行へ
// 落とす。字が大きくなると「入力欄 + ラベル + ボタン」が 1 行に収まらなく
// なるので、そのときだけ折り返る。label には次に置くボタンの文言を渡す。
void SameLineOrWrap(const char *label) {
	const ImGuiStyle &style = ImGui::GetStyle();
	const float need = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
	// 直前の項目を置き終わったところなので、カーソルは次の行の頭にある。
	const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
	if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + need <= right) ImGui::SameLine();
}

// グループの見出し (CollapsingHeader)。**帯を明るくして目立たせる**
// （2026-09-08、ユーザーの指示）。ImGui の既定は薄い青
// (ImGuiCol_Header は同じ色の 31% 透過) で、**コンボやスライダなど他の
// 部品と濃さが変わらず、見出しなのか項目なのか区別が付かない**。
//
// 色は押し引きで**この呼び出しの間だけ**変える。`ImGuiCol_Header` は
// Selectable の選択行やコンボの候補とも共用なので、style をそのまま
// 書き換えるとファイラーやスキンの一覧まで明るくなってしまう。
//
// 開いていても畳んでいても同じ色にしてある（見出しの並びとして読めるほうが
// よい）。指を乗せたとき・押したときだけさらに濃くする。
bool GroupHeader(const char *label) {
	// 既定の青をそのまま使い、透過だけ濃くする。配色を変えても浮かない。
	ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Header);
	c.w = 0.75f;
	ImVec4 hovered = c;
	hovered.w = 0.90f;
	ImVec4 active = c;
	active.w = 1.00f;
	ImGui::PushStyleColor(ImGuiCol_Header, c);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hovered);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, active);
	const bool open = ImGui::CollapsingHeader(label);
	ImGui::PopStyleColor(3);
	return open;
}

// 開いているグループの終わりに置く余白。次の見出しとの区切りを見せる。
// **畳んでいるグループには置かない**——見出しだけが並ぶところは詰めておきたい
// ので、CollapsingHeader が true のブロックの**中**に置くこと。
// 高さは字の高さの半分。指で操作するときは字も大きくなるので、余白も一緒に
// 広がる（決め打ちの画素にしない）。
void GroupTrailingSpace() {
	ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() * 0.5f));
}

// ダイアログを画面の中央に出す。基準点 (pivot) を真ん中にして渡すので、
// 大きさが決まっていないダイアログ（AlwaysAutoResize）でも中央に来る。
void CenterNextWindow(ImGuiCond cond) {
	const ImGuiIO &io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), cond,
	                        ImVec2(0.5f, 0.5f));
}

std::string TrimSpaces(const std::string &s) {
	size_t b = 0;
	size_t e = s.size();
	while (b < e && (unsigned char)s[b] <= ' ') b++;
	while (e > b && (unsigned char)s[e - 1] <= ' ') e--;
	return s.substr(b, e - b);
}

}  // namespace settingsui

using namespace settingsui;

SettingsUi::SettingsUi()
    : ready_(false),
      visible_(false),
      settingsWasVisible_(false),
      settingsClosed_(false),
      hasJapaneseFont_(false),
      styleScale_(0.0f),
      touchUi_(false),
      touchMinPx_(0.0f),
      touchFontPx_(0.0f),
      dialogGrow_(1.0f),
      inputScale_(1.0f),
      relayout_(false),
      lastDisplaySize_(0.0f, 0.0f),
      vfs_(0),
      pendingSampleRate_(0),
      orientEnabled_(false),
      orientation_(Screen::kLandscape),
      localeApplyPending_(false),
      localeChanged_(false),
      pendingZoom_(0),
      zoomApplyAtMs_(0),
      changedFields_(0),
      openContextMenu_(false),
      contextMenuOpen_(false),
      closeContextMenu_(false),
      ctxPage_(kCtxPageMain),
      ctxSwitchTo_(-1),
      ctxAnimFrom_(-1),
      ctxAnimForward_(true),
      ctxAnimStartMs_(0),
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
      showPdxPaths_(false),
      pdxOpenPending_(false),
      pdxReturnToSettings_(false),
      pdxSelected_(0),
      pdxOpenRemove_(false),
      pdxRemoveOpen_(false),
      pdxCloseRemove_(false),
      bmOpenToggle_(false),
      bmJumpPending_(false),
      bmToggleOpen_(false),
      quitAsk_(false),
      quitOpen_(false),
      quitClose_(false),
      bmCloseToggle_(false),
      fsSelected_(0),
      safPicking_(false),
      safRegrantFromFiler_(false),
      addFsOpen_(false),
      addFsShow_(false),
      addFsClose_(false),
      pendingBrowse_(false),
      fsOpenConfirm_(false),
      fsConfirmOpen_(false),
      fsCloseConfirm_(false),
      showFolder_(false),
      folderTarget_(kFolderTargetFiler),
      folderReturnToSettings_(false),
      folderReturnToBookmarks_(false),
      folderReturnToPdx_(false),
      folderOpenPending_(false),
      songLoader_(0),
      folderLister_(new DirLister()),
      folderLoading_(false),
      folderTicks_(0),
      folderHasPrev_(false),
      openedModal_(0) {
	folderPathBuf_[0] = '\0';
	for (int i = 0; i < kCtxPageCount; i++) {
		ctxPageSize_[i] = ImVec2(0.0f, 0.0f);
		ctxChildWarm_[i] = false;
	}
	addFsPathBuf_[0] = '\0';
	skinNameBuf_[0] = '\0';
}

SettingsUi::~SettingsUi() {
	Shutdown();
	delete folderLister_;
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
	// 選べる言語。同梱ぶんとユーザーフォルダの locale/ を数え上げる。
	// **起動時に 1 度だけ**なので、ユーザーが言語を足したら起動し直す。
	ListLocales(paths_, &locales_);

	// 最初のイベントが来る前に倍率を知っておく。
	inputScale_ = screen->WindowToOutputScale();
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
	//
	// **同梱フォントが最優先**（キャンバスの文字とは逆の順）。スキンや
	// ユーザーフォルダの font.ttf は、同梱フォントが失われていたときの
	// 保険としてしか見ない。理由は kBundledFont のところに書いた。
	{
		// 同梱ぶんを名指しで見る（Find はユーザーフォルダを先に見るので、
		// 同じ名前のものを置かれると入れ替わってしまう）。
		std::string path;
		if (!paths_.bundledDir.empty()) {
			const std::string bundled = JoinPath(paths_.bundledDir, kBundledFont);
			if (FileExists(bundled)) path = bundled;
		}
		if (path.empty()) path = paths_.Find(kBundledFont);
		const bool isBundled = !path.empty();  // 名指しで見つかった = 同梱ぶん
		if (path.empty()) path = paths_.Find(kUserFont);
		for (int i = 0; path.empty() && i < kNumFontCandidates; i++) {
			if (FileExists(kFontCandidates[i])) path = kFontCandidates[i];
		}
		// 中身は 1 度だけ読んで 2 つの源で共有する（所有権はこちら。
		// アトラスが生きている間は fontData_ を捨てないこと）。
		if (!path.empty() && ReadWholeFile(path, &fontData_) && !fontData_.empty()) {
			ImFontConfig cfg;
			cfg.FontDataOwnedByAtlas = false;
			if (isBundled) cfg.GlyphExcludeRanges = kEllipsisRange;
			if (io.Fonts->AddFontFromMemoryTTF(&fontData_[0], (int)fontData_.size(),
			                                   kFontSizePx, &cfg) != 0) {
				hasJapaneseFont_ = true;
				if (isBundled) {
					// 三点リーダーだけを下げた源を重ねる（kEllipsisDropEm）。
					ImFontConfig drop;
					drop.FontDataOwnedByAtlas = false;
					drop.MergeMode = true;
					drop.GlyphOffset = ImVec2(0.0f, kFontSizePx * kEllipsisDropEm);
					io.Fonts->AddFontFromMemoryTTF(&fontData_[0], (int)fontData_.size(),
					                               kFontSizePx, &drop);
				}
			}
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

	// ImGui は実ピクセルで動かす。SDL のマウスイベントは窓の座標で届くので、
	// **渡す前に**実ピクセルへ直す（HiDPI で食い違う環境のための倍率で、
	// ふつうは 1 倍）。ここで直さずに後から io.MousePos を上書きしても、
	// ImGui::NewFrame() がキューを適用する際に上書きが打ち消され、
	// ずれた位置のコントロールが 1 フレームだけ反応してしまう。
	if (ev.type == SDL_MOUSEMOTION && inputScale_ != 1.0f) {
		SDL_Event scaled = ev;
		scaled.motion.x = (int)(ev.motion.x * inputScale_ + 0.5f);
		scaled.motion.y = (int)(ev.motion.y * inputScale_ + 0.5f);
		ImGui_ImplSDL2_ProcessEvent(&scaled);
		return;
	}
	// 指のボタンは、押した位置も一緒に ImGui へ入れる。SDL は指をマウスに
	// 写すとき、前と同じ座標なら移動イベントを捨てる（SDL_mouse.c の
	// "Drop events that don't change state"）ので、こちらで指の位置を忘れさせた
	// あと（CtxPageItem）に同じ画素を触られると、ImGui は位置を知らないまま
	// ボタンだけ受け取って「外を押した」と取り違える。同じ位置なら ImGui 側が
	// 重複として捨てるので、いつも入れてよい。
	if ((ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) &&
	    ev.button.which == SDL_TOUCH_MOUSEID) {
		ImGuiIO &io = ImGui::GetIO();
		io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
		io.AddMousePosEvent(ev.button.x * inputScale_, ev.button.y * inputScale_);
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
//
// touch が立っているときは、押せるところの高さが kTouchTargetMm を下回らない
// ように余白を広げる。字は kTouchFontMm を**下限**にするだけで、それより
// 大きくはしない（携帯の狭い画面で字まで大きくすると、一度に読める項目が
// 減るうえ、ImGui が折り返さないラベルが横で切れてしまう）。
//
// 広げるのは 2 か所:
//   ・ボタン・チェックボックス・入力欄の高さ = 字の高さ + FramePadding.y * 2
//   ・一覧の行 (Selectable) の当たり判定 = 字の高さ + ItemSpacing.y
//     （ImGui は行同士に隙間ができないよう、Selectable の箱を ItemSpacing.y の
//     半分ずつ上下へ広げる。だから ItemSpacing.y を足すとそのまま行が太くなる）
bool SettingsUi::ApplyScale(float scale, bool touch) {
	if (scale == styleScale_ && touch == touchUi_) return false;
	styleScale_ = scale;
	touchUi_ = touch;

	touchMinPx_ = 0.0f;
	touchFontPx_ = 0.0f;
	dialogGrow_ = 1.0f;

	// 字の大きさ。ふつうはキャンバスの拡大率どおりだが、指で操作するときは
	// kTouchFontMm を下回らないところまで大きくする。**小さくはしない**ので、
	// キャンバスの拡大率のほうが大きい環境では今までどおり。
	float fontScale = scale;
	if (touch) {
		// ImGui は実ピクセルで描くので、mm から出した値はそのまま使える。
		touchMinPx_ = kTouchTargetMm * Screen::PixelsPerMm();
		touchFontPx_ = kTouchFontMm * Screen::PixelsPerMm();
		if (touchFontPx_ > kFontSizePx * fontScale) fontScale = touchFontPx_ / kFontSizePx;
	}

	ImGuiStyle &style = ImGui::GetStyle();
	style = baseStyle_;
	// 余白や角の丸めは**字に合わせて**拡げる。字だけ大きくすると、窓の内側の
	// 余白や区切りが相対的に痩せて見える。
	style.ScaleAllSizes(fontScale);
	style.FontScaleDpi = fontScale;

	if (touch) {
		const float lineH = kFontSizePx * fontScale;

		const float padY = (touchMinPx_ - lineH) * 0.5f;
		if (padY > style.FramePadding.y) style.FramePadding.y = padY;
		const float gapY = touchMinPx_ - lineH;
		if (gapY > style.ItemSpacing.y) style.ItemSpacing.y = gapY;
		// 縦だけ広げると横に潰れて見えるので、横も同じだけ確保する。
		if (padY > style.FramePadding.x) style.FramePadding.x = padY;

		// つまみも指で掴めるように。ただしスクロールバーの幅まで 6mm に
		// すると画面をかなり食うので、そこは 2/3 (4mm) を下限にする。
		const float bar = touchMinPx_ * 0.66f;
		if (style.ScrollbarSize < bar) style.ScrollbarSize = bar;
		if (style.GrabMinSize < bar) style.GrabMinSize = bar;

		// 行が太くなったぶんだけダイアログも広げないと中身が入らない。
		// 画面をはみ出すぶんは DialogSize が詰める。
		const float normal = kFontSizePx * scale + baseStyle_.ItemSpacing.y * scale;
		if (normal > 0.0f) dialogGrow_ = touchMinPx_ / normal;
		if (dialogGrow_ < 1.0f) dialogGrow_ = 1.0f;
	}
	return true;
}

// ダイアログの既定の大きさ。画面より大きくはしない。
ImVec2 SettingsUi::DialogSize(float w, float h) const {
	const ImGuiIO &io = ImGui::GetIO();
	ImVec2 s(w * styleScale_ * dialogGrow_, h * styleScale_ * dialogGrow_);
	if (s.x > io.DisplaySize.x) s.x = io.DisplaySize.x;
	if (h <= 0.0f) {
		s.y = 0.0f;  // 高さは中身任せ
	} else if (s.y > io.DisplaySize.y) {
		s.y = io.DisplaySize.y;
	}
	return s;
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

	// ImGui は実ピクセルで描く。字の大きさはキャンバスの拡大率に合わせ、
	// マウス座標は「窓 -> 実ピクセル」の倍率で直す（別物なので注意。
	// 前者は 2.25 倍でも、後者はふつう 1 倍）。
	float scale = 1.0f;
	screen->GetRenderScale(&scale, 0);
	if (scale <= 0.0f) scale = 1.0f;
	inputScale_ = screen->WindowToOutputScale();
	if (inputScale_ <= 0.0f) inputScale_ = 1.0f;
	{
		// ダブルクリックの判定（上の kDoubleClick*）。
		ImGuiIO &io = ImGui::GetIO();
		io.MouseDoubleClickTime = kDoubleClickTimeSec;
		io.MouseDoubleClickMaxDist =
		    kDoubleClickRadiusWindowPx * inputScale_ * kDoubleClickBoxToCircle;
	}
	{
		ImGuiIO &io = ImGui::GetIO();
		// 描く場所は窓の左上が原点（BeginNativeScale が論理サイズを外す）
		// なので、ImGui の画面はキャンバスではなく**実出力そのもの**にする。
		// キャンバスより広ければ帯のぶんまで使えるだけで、ダイアログは
		// 画面の真ん中に出る。
		int outW = 0, outH = 0;
		screen->GetOutputSize(&outW, &outH);
		io.DisplaySize = ImVec2((float)outW, (float)outH);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}
	// 「指で操作する」。自動なら端末で決まるが、ブラウザではあとからタッチ
	// 装置が見つかることがあるので毎フレーム見る（ApplyScale は変わった
	// ときだけ作り直す）。
	bool touch = (settings->touchUi == Settings::kTouchOn);
	if (settings->touchUi == Settings::kTouchAuto) touch = Screen::TouchPreferred();
	const bool scaleChanged = ApplyScale(scale, touch);
	// 表示サイズが変わったフレーム（回転・リサイズ）は、開いている
	// ダイアログを置き直す（placeCond）。
	{
		const ImVec2 now = ImGui::GetIO().DisplaySize;
		const bool displayChanged = (now.x != lastDisplaySize_.x || now.y != lastDisplaySize_.y);
		lastDisplaySize_ = now;
		relayout_ = scaleChanged || displayChanged;
	}

	ImGui::NewFrame();

	// **ソフトキーボード**。入力欄にカーソルが入っている間だけ出す。
	// ImGui の SDL2 バックエンドは 2023-04-06 に SDL_StartTextInput() を
	// 呼ぶのをやめている（IME 以外にも効いてしまうため）ので、こちらで
	// 面倒を見ないと、Android では入力欄を触ってもキーボードが出ない。
	//
	// `io.WantTextInput` は NewFrame() で決まるので、ここで見てよい。
	// **キーボードのあるプラットフォームでは触らない**——SDL は最初から
	// テキスト入力を受け付けており、止めたり始めたりすると IME の状態に
	// 触ってしまう（mxv2 は SDL_TEXTINPUT を自分では使っていないが、
	// 得るものが無いので触らない）。
#if defined(__ANDROID__) || defined(__IPHONEOS__) || defined(__EMSCRIPTEN__)
	{
		const bool wantText = ImGui::GetIO().WantTextInput;
		if (wantText != (SDL_IsTextInputActive() == SDL_TRUE)) {
			if (wantText) {
				SDL_StartTextInput();
			} else {
				SDL_StopTextInput();
			}
		}
	}
#endif

	// ドラッグでスクロール中の印は、ボタンを離したところで落とす。
	// ドラッグの途中でダイアログが閉じても、次に開いたものへ持ち越さない。
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragScroll_ = false;
	// 「実際にスクロールしたか」は離したフレームでもまだ要る（押した行を
	// 選ばせないため）ので、落とすのは次に押したときにする。
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragMoved_ = false;

	// 設定ウィンドウが閉じた瞬間を拾う。× でも ESC でも ImGui が visible_ を
	// false にするので、代入を追うのではなく変わり目を見る。**1 フレーム
	// 遅れて分かる**が、閉じてから効かせたいもの（縦横の切り替えかた）には
	// それで足りる。
	if (settingsWasVisible_ && !visible_) settingsClosed_ = true;
	settingsWasVisible_ = visible_;

	// 右クリックのメニューとバージョン情報は、設定ウィンドウが閉じていても出す。
	BuildContextMenu(settings, draw, player, filer);
	BuildAboutWindow();
	BuildColorsWindow(settings, draw, player);

	// [言語] で選ばれた言語をここで入れ替える。ダイアログの題名も文言なので、
	// **設定ウィンドウが閉じきってから**でないと、開いている最中に ImGui の
	// ポップアップの id が変わってしまう（下の [参照...] と同じ作法）。
	if (localeApplyPending_ && !ImGui::IsPopupOpen(kSettingsTitle)) {
		localeApplyPending_ = false;
		ApplyLocale();
		visible_ = true;  // 新しい題名で開き直す
	}

	// ブックマークの設定 / PDX の探索先の [追加…] から来た往復。
	// ImGui のポップアップは同じ階層で掛け替えられないので、呼び出し元が
	// 閉じきってからフォルダ選択を開く。
	if (folderOpenPending_ && !ImGui::IsPopupOpen(kSettingsTitle) &&
	    !ImGui::IsPopupOpen(kBookmarksTitle) && !ImGui::IsPopupOpen(kPdxPathsTitle)) {
		folderOpenPending_ = false;
		// PDX なら一覧で選んでいた探索先から（読めなければファイラーの今の
		// 場所から）。ブックマークはファイラーの今の場所から。
		std::string start = filer->currentRef();
		if (folderTarget_ == kFolderTargetPdx && vfs_ != 0 && pdxSelected_ >= 0 &&
		    pdxSelected_ < (int)settings->pdxPaths.size()) {
			std::string ref;
			if (vfs_->Resolve(settings->pdxPaths[pdxSelected_], filer->currentRef(), &ref) &&
			    vfs_->IsDir(ref)) {
				start = ref;
			}
		}
		SetFolderDir(start);
		showFolder_ = true;
	}
	// 設定ウィンドウの [編集…] から。同じ理由で、設定ウィンドウが閉じきってから。
	if (pdxOpenPending_ && !ImGui::IsPopupOpen(kSettingsTitle)) {
		pdxOpenPending_ = false;
		showPdxPaths_ = true;
	}
	BuildFolderWindow(settings, filer);
	PollSafPicked(filer);
	BuildFileSystemsWindow(filer);
	// ファイラーの "Bookmarks>" で選ばれたぶん。
	if (bmJumpPending_) {
		bmJumpPending_ = false;
		JumpToBookmarkRef(settings, bmJumpRef_);
	}
	BuildBookmarksWindow(settings, filer);
	BuildPdxPathsWindow(settings, filer);
	BuildBookmarkToggleWindow(settings, filer);
	BuildQuitWindow();
	BuildStartupWindow();
	BuildHelpWindow();
	if (folderReturnToSettings_ && !showFolder_ && !folderOpenPending_ &&
	    !ImGui::IsPopupOpen(folderTitle())) {
		folderReturnToSettings_ = false;
		visible_ = true;
	}
	if (folderReturnToBookmarks_ && !showFolder_ && !folderOpenPending_ &&
	    !ImGui::IsPopupOpen(folderTitle())) {
		folderReturnToBookmarks_ = false;
		showBookmarks_ = true;
	}
	if (folderReturnToPdx_ && !showFolder_ && !folderOpenPending_ &&
	    !ImGui::IsPopupOpen(folderTitle())) {
		folderReturnToPdx_ = false;
		showPdxPaths_ = true;
	}
	// [PDX の探索先] を閉じたら設定ウィンドウへ戻る。[追加…] のフォルダ選択へ
	// 出ている間（と、そこから戻ってくる途中）は戻らない。
	if (pdxReturnToSettings_ && !showPdxPaths_ && !pdxOpenPending_ && !showFolder_ &&
	    !folderOpenPending_ && !folderReturnToPdx_ && !ImGui::IsPopupOpen(kPdxPathsTitle) &&
	    !ImGui::IsPopupOpen(folderTitle())) {
		pdxReturnToSettings_ = false;
		visible_ = true;
	}

	BuildSettingsWindow(settings, draw, player, filer, screen);
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

// GL コンテキストが失われたあと。ImGui のバックエンドが持っている
// テクスチャ（フォントアトラス）は器ごと無効になっているので捨てる。
// 次の NewFrame() が自分で作り直す。
void SettingsUi::HandleDeviceReset() {
	if (!ready_) return;
	ImGui_ImplSDLRenderer2_DestroyDeviceObjects();
}

void SettingsUi::Render(Screen *screen) {
	if (!ready_) return;
	ImGui::Render();
	// 実解像度のまま描く（キャンバスの拡大は Screen::Draw が自分で行うので、
	// レンダラに論理サイズは入っていない）。
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), screen->renderer());
}

}  // namespace mxv2
