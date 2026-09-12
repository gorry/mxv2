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
#include "dirlister.h"
#include "filer.h"
#include "songloader.h"
#include "ini.h"
#include "message.h"
#include "player.h"
#include "safaccess.h"
#include "screen.h"
#include "settings.h"
#include "skin.h"
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

// 指で押すところの高さの下限 (mm)。指の腹が当たる幅として Material Design は
// 48dp (約 7.6mm)、Apple は 44pt (約 7mm) を勧めているが、mxv2 のダイアログは
// 項目が多く画面が狭いので、下限として 6mm を採る。
const float kTouchTargetMm = 6.0f;

// 指で操作するときの字の大きさ (mm)。行だけ太くして字が小さいままだと
// 押せても読めないので、下限として持っている。日本語は仮想ボディいっぱいに
// 書かれるので、漢字の実寸はほぼこの値になる。
//
// **行の高さ (kTouchTargetMm) とは切り離してある**。かつては行の 0.6 倍
// (6mm の行に 3.6mm の字) にしていたが、それだと Android の縦画面で
// コンボやスライダーのラベルに 6 文字しか入らなかった。ImGui はラベルを
// 折り返さないので、はみ出したぶんは黙って切れる。
// 3.1mm は Android の 19sp 相当で、本文 (14sp = 約 2.2mm) より大きい。
// これでラベルはおよそ 7 文字ぶんになる。3.6mm では大きく 2.6mm では
// 小さいという実機での判断で、その中間を採った。
//
// 押せるところの高さは 6mm のままなので、**指での操作しやすさは変わらない**
// （字が小さくなったぶんは上下の余白に回る）。
const float kTouchFontMm = 3.1f;

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

// 補足や注意の 1 行。**折り返す**。指で操作する端末では字が大きくなるので、
// 折り返さないと携帯の狭い画面で右が切れて読めなくなる。
// 部品のラベル（チェックボックスの文言など）は ImGui が折り返してくれないので、
// そちらは短いままにしておくこと。
void TextWrapColor(const ImVec4 &color, const char *text) {
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
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
	ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + w);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
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

// 画面の遅れ (ms) とサンプル数の相互変換。
int MsToFrames(int ms, const Player *player) {
	return ms * player->sampleRate() / 1000;
}

float FramesToMs(int frames, const Player *player) {
	return frames * 1000.0f / (float)player->sampleRate();
}

// 「読み込み中」を出すまでの待ち時間 (ms)。filer.cpp と同じ考え方。
const uint32_t kLoadingDelayMs = 250;

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
      bmJumpPending_(false),
      bmToggleOpen_(false),
      quitAsk_(false),
      quitOpen_(false),
      quitClose_(false),
      bmCloseToggle_(false),
      fsSelected_(0),
      safPicking_(false),
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
      folderOpenPending_(false),
      songLoader_(0),
      folderLister_(new DirLister()),
      folderLoading_(false),
      folderTicks_(0),
      folderHasPrev_(false),
      openedModal_(0) {
	pdxPathBuf_[0] = '\0';
	folderPathBuf_[0] = '\0';
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

void SettingsUi::ScanSkins() {
	std::vector<std::string> refs;
	paths_.ListSkinRefs(&refs);

	skins_.clear();
	for (size_t i = 0; i < refs.size(); i++) {
		SkinItem item;
		item.ref = refs[i];
		// 縦横の分けは layout.ini の [Screen] Width/Height で決まるが、
		// **Base から画面サイズを継承しているスキンがある**（同梱の
		// Default-Midnight がそう）。
		// 名前や layout.ini の直読みでは決められないので、本体と同じ
		// 手順（Skin::Load）を通す。正方形は縦扱い（screen_orientation.md）。
		Skin s;
		std::string err;
		item.portrait = true;
		if (s.Load(paths_, item.ref, &err)) item.portrait = (s.screenW <= s.screenH);
		skins_.push_back(item);
	}
}

// 頭に付ける印は縦長四角形 (U+25AF) / 横長四角形 (U+25AD)。同梱フォントには
// 両方あるが、それが失われて ImGui の既定フォント（ASCII だけ）に落ちたときは
// 出せないので "|" / "-" にする（screen_orientation.md）。
std::string SettingsUi::SkinLabel(const SkinItem &item) const {
	const char *mark;
	if (hasJapaneseFont_) {
		mark = item.portrait ? "\xe2\x96\xaf" : "\xe2\x96\xad";
	} else {
		mark = item.portrait ? "|" : "-";
	}
	return std::string(mark) + " " + item.ref;
}

// スロットに合う向きのスキンを上へまとめて出す（screen_orientation.md）。
// 合わないほうも選べる——余白が出るだけで、ユーザーの選択として許す。
bool SettingsUi::SkinCombo(const char *label, bool portraitSlot, std::string *value) {
	std::string shown = *value;
	for (size_t i = 0; i < skins_.size(); i++) {
		if (skins_[i].ref == *value) {
			shown = SkinLabel(skins_[i]);
			break;
		}
	}

	bool changed = false;
	if (ImGui::BeginCombo(label, shown.c_str())) {
		for (int pass = 0; pass < 2; pass++) {
			const bool want = (pass == 0) ? portraitSlot : !portraitSlot;
			for (size_t i = 0; i < skins_.size(); i++) {
				if (skins_[i].portrait != want) continue;
				const bool selected = (skins_[i].ref == *value);
				if (ImGui::Selectable(SkinLabel(skins_[i]).c_str(), selected)) {
					*value = skins_[i].ref;
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

// [言語] で選ばれたときの控え。name が空なら「自動」で、動作環境の言語に
// 一番近い同梱ぶんを読む（ini に残すのは空のままなので、別の端末へ持って
// いってもその環境の言語になる）。
void SettingsUi::SelectLocale(Settings *settings, const std::string &name) {
	if (settings->locale == name) return;
	settings->locale = name;
	changedFields_ |= Settings::kFieldLocale;

	pendingLocale_ = name.empty() ? MatchLocale(locales_, Screen::SystemLocale()) : name;
	localeApplyPending_ = true;
	// 題名ごと入れ替わるので、いったん閉じる。開き直すのは Build() の頭。
	visible_ = false;
}

// 実際にカタログを読み直す。**設定ウィンドウが閉じている間に呼ぶこと。**
void SettingsUi::ApplyLocale() {
	const std::string want = pendingLocale_;
	pendingLocale_.clear();
	if (want.empty()) return;

	if (!LoadMessages(paths_, want)) {
		// カタログが読めなかった。前の言語の文言がそのまま残るので、
		// 画面は動き続ける（この 1 本だけ英語で知らせる）。
		printf("warning  : message catalog not found: locale %s\n", want.c_str());
		return;
	}
	// 覚えている文言を取り直す。題名はポインタ、操作方法の一覧は写しで
	// 持っているので、どちらも作り直しが要る。
	ResetTitles();
	LoadHelpRows();
	localeChanged_ = true;
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
	BuildColorsWindow(settings, draw, player);

	// [言語] で選ばれた言語をここで入れ替える。ダイアログの題名も文言なので、
	// **設定ウィンドウが閉じきってから**でないと、開いている最中に ImGui の
	// ポップアップの id が変わってしまう（下の [参照...] と同じ作法）。
	if (localeApplyPending_ && !ImGui::IsPopupOpen(kSettingsTitle)) {
		localeApplyPending_ = false;
		ApplyLocale();
		visible_ = true;  // 新しい題名で開き直す
	}

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
	BuildFolderWindow(settings, filer);
	PollSafPicked(filer);
	BuildFileSystemsWindow(filer);
	// ファイラーの "Bookmarks>" で選ばれたぶん。
	if (bmJumpPending_) {
		bmJumpPending_ = false;
		JumpToBookmarkRef(settings, bmJumpRef_);
	}
	BuildBookmarksWindow(settings, filer);
	BuildBookmarkToggleWindow(settings, filer);
	BuildQuitWindow();
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

	// 開くたびに画面の中央から出す。出したあとは掴んで動かせる。倍率や
	// 表示サイズが変わったフレームだけは矩形ごと作り直す（placeCond）。
	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(380, 464), placeCond());

	// p_open に visible_ をそのまま渡す。× で閉じられたときは ImGui が
	// false にして閉じてくれるし、F1 で false にした場合も同じ経路で閉じる。
	if (!ImGui::BeginPopupModal(kSettingsTitle, &visible_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// ---- 画面 ----------------------------------------------------------
	// 見出し (CollapsingHeader) は**畳んだ状態から始まる**（ユーザーの指示、
	// 2026-09-08）。ImGuiTreeNodeFlags_DefaultOpen を付けないだけでよく、
	// 開け閉めした状態は ImGui がウィンドウごとに覚えているので、
	// ダイアログを閉じて開き直しても保たれる（アプリを起動し直すと畳んだ
	// 状態に戻る。imgui.ini は書いていないので何も残らない）。[配色設定] も同じ。
	//
	// 言語。ダイアログの文言がまるごと入れ替わるので一番上に置く。
	// 中身は同梱ぶん (assets/locale/<名前>) だけで、名前はその言語自身での
	// 呼び名を出す（読めない言語の名前で並べても選べない）。
	if (GroupHeader(Msg("Settings.Language"))) {
		// 空なら「自動」。いま実際に使っている言語ではなく**設定の値**を
		// 見せる（自動のまま日本語で動いているのか、日本語を選んだのかは
		// 別のことなので）。
		const std::string cur = settings->locale;
		const char *label = Msg("Settings.LanguageAuto");
		for (size_t i = 0; i < locales_.size(); i++) {
			if (locales_[i].name == cur) label = locales_[i].displayName.c_str();
		}
		// 見出しと同じ文言なので、id は "###" で分ける。
		if (ImGui::BeginCombo(L("Settings.Language", "###language").c_str(), label)) {
			if (ImGui::Selectable(Msg("Settings.LanguageAuto"), cur.empty())) {
				SelectLocale(settings, std::string());
			}
			if (cur.empty()) ImGui::SetItemDefaultFocus();
			for (size_t i = 0; i < locales_.size(); i++) {
				const bool selected = (locales_[i].name == cur);
				if (ImGui::Selectable(locales_[i].displayName.c_str(), selected)) {
					SelectLocale(settings, locales_[i].name);
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		GroupTrailingSpace();
	}

	if (GroupHeader(Msg("Settings.Screen"))) {
		// 表示倍率 (%)。100 でドット等倍。
		// 押している間・入力中に適用してはいけない。ウィンドウが大きくなると
		// このコントロール自身の座標も変わるので、同じ場所を押しているだけで
		// 値が行き来してしまう。確定してから少し待って適用する。
		int zoom = settings->zoomPercent;
		// -/+ ボタンは高さと同じ幅の正方形。余白や字が大きくなるとその分だけ
		// 場所を食うので、幅は決め打ちにせず実測で組み立てる。
		{
			const ImGuiStyle &st = ImGui::GetStyle();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000").x + st.FramePadding.x * 2.0f +
			                        (ImGui::GetFrameHeight() + st.ItemInnerSpacing.x) * 2.0f);
		}
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
		// ここは Button で置くこと。SmallButton は FramePadding.y が 0 なので
		// [再読込] など他のボタンより背が低くなり、指で操作するときの
		// 6mm も満たさない。
		SameLineOrWrap(Msg("Settings.ZoomSystem"));
		if (ImGui::Button(Msg("Settings.ZoomSystem"))) {
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
			// **static にしないこと。** 一度だけ組み立てると、言語を替えた
			// あとも古いカタログの文言を指したままになる（実際に踏んだ）。
			const Item kItems[] = {
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

		// 指で操作する端末向けの余白。押せるところの高さを 6mm 確保する。
		// 自動なら端末で決まる（Android は有効、PC は無効）ので、ふつうは
		// 触らなくてよい。触れる画面の PC や、逆に Android にマウスを
		// 繋いだときのために手で決められるようにしてある。
		{
			static const char *const kKeys[] = {
				"Settings.TouchAuto", "Settings.TouchOn", "Settings.TouchOff",
			};
			int mode = settings->touchUi;
			if (mode < 0 || mode > 2) mode = Settings::kTouchAuto;
			if (ImGui::BeginCombo(Msg("Settings.Touch"), Msg(kKeys[mode]))) {
				for (int i = 0; i < 3; i++) {
					const bool selected = (i == mode);
					if (ImGui::Selectable(Msg(kKeys[i]), selected)) {
						settings->touchUi = i;
						changedFields_ |= Settings::kFieldTouchUi;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			// いま効いているか。自動のときに端末をどう見ているかが分かる。
			if (touchUi_) {
				const float mmPerPx = 1.0f / Screen::PixelsPerMm();
				TextNote(MsgF("Settings.TouchNow", MsgNum("%.1f", kTouchTargetMm),
				              MsgNum("%d", (int)(touchMinPx_ + 0.5f)),
				              MsgNum("%.1f", touchFontPx_ * mmPerPx))
				             .c_str());
			} else {
				TextNote(Msg("Settings.TouchOffNow"));
			}
		}
		GroupTrailingSpace();
	}

	// ---- スキン ----------------------------------------------------------
	// 2026-09-10 にユーザーの指示で [画面] から切り出した。
	//
	// 縦横切り替え（screen_orientation.md）が有効なときだけ、縦画面用と
	// 横画面用の 2 つを選ぶ形になる。無効なときは今までどおり 1 つ。
	// 画面サイズごと変わりうるので、選ばれた名前を置いておいて実際の
	// 作り直しはメインループに任せる。
	if (GroupHeader(Msg("Settings.SkinGroup"))) {
		if (!orientEnabled_) {
			// 縦横切り替えが無効なときは今までどおり。**印も並べ替えも
			// しない**——縦横の区別が意味を持たないので、名前の順のまま出す。
			if (ImGui::BeginCombo(Msg("Settings.Skin"), settings->skinName.c_str())) {
				for (size_t i = 0; i < skins_.size(); i++) {
					const bool selected = (skins_[i].ref == settings->skinName);
					if (ImGui::Selectable(skins_[i].ref.c_str(), selected)) {
						settings->skinName = skins_[i].ref;
						pendingSkin_ = settings->skinName;
						changedFields_ |= Settings::kFieldSkin;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			SameLineOrWrap(Msg("Button.Rescan"));
			if (ImGui::Button(Msg("Button.Rescan"))) {
				ScanSkins();
				pendingSkin_ = settings->skinName;
			}
		} else {
			// いま出ている向きのほうを選び直したときだけ、その場で作り直す。
			// もう片方は ini を書き換えるだけ（次にその向きになったら効く）。
			if (SkinCombo(Msg("Settings.SkinPortrait"), true, &settings->skinPortrait)) {
				changedFields_ |= Settings::kFieldOrientSkin;
				if (orientation_ == Screen::kPortrait) pendingSkin_ = settings->skinPortrait;
			}
			if (SkinCombo(Msg("Settings.SkinLandscape"), false, &settings->skinLandscape)) {
				changedFields_ |= Settings::kFieldOrientSkin;
				if (orientation_ == Screen::kLandscape) pendingSkin_ = settings->skinLandscape;
			}

			// 切り替えかた。**選び直しても、効くのはダイアログを閉じてから**
			// （screen_orientation.md）。開いている最中に端末を回されると、
			// 何を設定しているのか分からなくなるため。
			{
				static const char *const kKeys[Settings::kNumOrientModes] = {
					"Settings.OrientPortraitOnly", "Settings.OrientLandscapeOnly",
					"Settings.OrientStartup", "Settings.OrientAlways",
				};
				int mode = settings->orientationMode;
				if (mode < 0 || mode >= Settings::kNumOrientModes) {
					mode = Settings::kOrientAlways;
				}
				if (ImGui::BeginCombo(Msg("Settings.OrientMode"), Msg(kKeys[mode]))) {
					for (int i = 0; i < Settings::kNumOrientModes; i++) {
						const bool selected = (i == mode);
						if (ImGui::Selectable(Msg(kKeys[i]), selected)) {
							settings->orientationMode = i;
							changedFields_ |= Settings::kFieldOrientMode;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			if (ImGui::Button(Msg("Button.Rescan"))) {
				ScanSkins();
			}
			TextNote(Msg("Settings.OrientNote"));
		}
		GroupTrailingSpace();
	}

	// ---- ファイラー ------------------------------------------------------
	if (GroupHeader(Msg("Settings.Filer"))) {
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

		// 曲名が桁に収まらない行を横へ送るか。
		{
			static const char *kKeys[] = {
			    "Settings.TitleScrollNone",
			    "Settings.TitleScrollCursor",
			    "Settings.TitleScrollAll",
			};
			int mode = settings->fileListScroll;
			if (mode < 0 || mode > 2) mode = Settings::kScrollCursor;
			if (ImGui::BeginCombo(Msg("Settings.TitleScroll"), Msg(kKeys[mode]))) {
				for (int i = 0; i < 3; i++) {
					const bool selected = (i == mode);
					if (ImGui::Selectable(Msg(kKeys[i]), selected)) {
						settings->fileListScroll = i;
						changedFields_ |= Settings::kFieldFileListScroll;
						draw->SetFileListScroll(i);
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		GroupTrailingSpace();
	}

	// ---- 演奏 ----------------------------------------------------------
	if (GroupHeader(Msg("Settings.Play"))) {
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
			TextNote(Msg("Settings.SampleRateNote"));
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
		TextNote(Msg("Settings.LoopNote"));

		// マスター音量。メイン画面の音量バーとは別で、実際の音量は 2 つの和。
		int vol = player->masterVolume();
		if (ImGui::SliderInt(Msg("Settings.MasterVolume"), &vol, Player::kVolumeMin, Player::kVolumeMax,
		                     "%+d")) {
			player->SetMasterVolume(vol);
			changedFields_ |= Settings::kFieldVolume;
		}
		settings->masterVolume = player->masterVolume();
		TextNote(MsgF("Settings.VolumeNote", MsgNum("%+d", player->masterVolume()),
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
			TextNote(MsgF("Settings.LatencyNow",
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
		GroupTrailingSpace();
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

	// 設定ウィンドウと同じ作法。開くたびに中央、画面からはみ出さない大きさ。
	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(380.0f, 464.0f), placeCond());

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

		// 部品ごとの見出しは、設定ウィンドウと同じ**畳める見出し**
		// (CollapsingHeader) にしてある。ここは縦に長いので、見ていない
		// ところを閉じられるほうがよい。**起動直後は全部畳んだ状態**で、
		// 開け閉めはアプリが動いている間だけ覚えている（設定ウィンドウと同じ）。

		Colors &t = draw->colors();
		bool dirty = false;

		if (GroupHeader(Msg("Colors.Background"))) {
			bool useBitmap = (t.back.bitmap != 0);
			if (ImGui::Checkbox(Msg("Colors.UseImage"), &useBitmap)) {
				t.back.bitmap = useBitmap ? 1 : 0;
				dirty = true;
			}
			if (AlphaRow(Msg("Colors.ImageGain"), &t.back.bitmapBright)) dirty = true;
			if (ColorRow(Msg("Colors.BgColor"), &t.back.color)) dirty = true;
			if (AlphaRow(Msg("Colors.BgStrength"), &t.back.colorBright)) dirty = true;
			GroupTrailingSpace();
		}

		if (GroupHeader(Msg("Colors.Keyboard"))) {
			if (GainRow(Msg("Colors.BlackKey"), &t.kb.blackBright)) dirty = true;
			if (GainRow(Msg("Colors.WhiteKey"), &t.kb.whiteBright)) dirty = true;
			if (GainRow(Msg("Colors.PressedKey"), &t.kb.bright)) dirty = true;
			GroupTrailingSpace();
		}

		if (GroupHeader(Msg("Colors.Status"))) {
			if (ColorRow(L("Colors.Text", "##st"), &t.status.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##st"), &t.status.colorBright)) dirty = true;
			if (ColorRow(L("Colors.Back", "##st"), &t.status.backColor)) dirty = true;
			if (AlphaRow(L("Colors.BackStrength", "##st"), &t.status.backColorBright)) dirty = true;
			GroupTrailingSpace();
		}

		if (GroupHeader(Msg("Colors.Title"))) {
			if (ColorRow(L("Colors.Text", "##ti"), &t.mdxTitle.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##ti"), &t.mdxTitle.colorBright)) dirty = true;
			if (ColorRow(L("Colors.Back", "##ti"), &t.mdxTitle.backColor)) dirty = true;
			if (AlphaRow(L("Colors.BackStrength", "##ti"), &t.mdxTitle.backColorBright)) dirty = true;
			GroupTrailingSpace();
		}

		if (GroupHeader(Msg("Colors.Filer"))) {
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
			GroupTrailingSpace();
		}

		if (GroupHeader(Msg("Colors.PlayKey"))) {
			if (ColorRow(L("Colors.Text", "##pk"), &t.playKey.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##pk"), &t.playKey.colorBright)) dirty = true;
			if (GainRow(L("Colors.ButtonGain", "##pk"), &t.playKey.keyBright)) dirty = true;
			GroupTrailingSpace();
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
		TextWrapColor(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), saveError_.c_str());
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

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kOverwriteTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ConfirmText(MsgF("Colors.OverwriteMessage", overwriteName_).c_str());
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

// 終了の確認。Android の戻るキーで、ダイアログも戻る先も無いときに出す。
// メイン画面から直に開くので、単独のモーダルとして扱う。
void SettingsUi::BuildQuitWindow() {
	if (quitAsk_) {
		quitAsk_ = false;
		ImGui::OpenPopup(kQuitTitle);
	}

	quitOpen_ = ImGui::IsPopupOpen(kQuitTitle);
	if (!quitOpen_) {
		quitClose_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kQuitTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ConfirmText(Msg("Dialog.QuitQuestion"));
	ImGui::Separator();
	// Enter でも終了できるようにする。ESC で開いて ESC で閉じられる一方、
	// 「はい」がマウスでしか押せないと、キーボードだけでは終われなくなる。
	const bool enter =
	    ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
	if (ImGui::Button(Msg("Button.Quit")) || enter) {
		request_ = kRequestQuit;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || quitClose_) {
		quitClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ファイルシステムの設定 (F3)。ファイラーのルートに並べる顔ぶれと順番を
// 決める。実体は Vfs のマウント一覧なので、触ったらその場で効く。
// 並びは [FileSystem] へ書き戻す（changedFields_ 経由でメインループが書く）。
void SettingsUi::BuildFileSystemsWindow(Filer *filer) {
	if (!SyncModal(kFileSystemsTitle, &showFileSystems_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 360.0f), placeCond());

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
		TextNote(Msg("FileSystems.FixedNote"));
	} else {
		TextError(fsError_.c_str());
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

	// [追加] で足せる種類はプラットフォームごとに 1 つしかないので、種類の
	// 選択は省いて場所を選ぶ画面を直に出す（filesystem.md）。
	//   Android … 端末のフォルダ (SAF)。選ぶ画面は OS が出し、結果は
	//             あとから届くので PollSafPicked() が拾う。
	//   その他  … フォルダマウント (dir:) の追加ダイアログ。
	// 3 種類以上になったら、ここに種類の選択を挟むこと。
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.AddFs"))) {
		fsError_.clear();
		if (SafAvailable()) {
			if (SafPickTree()) {
				safPicking_ = true;
			} else {
				fsError_ = Msg("FileSystems.PickFailed");
			}
		} else {
			addFsOpen_ = true;
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", Msg("FileSystems.AddHint"));
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

	BuildAddFsWindow(filer);
	BuildFsRemoveWindow(filer);

	ImGui::EndPopup();
}

void SettingsUi::SetBrowsedPath(const std::string &path) {
	snprintf(addFsPathBuf_, sizeof(addFsPathBuf_), "%s", path.c_str());
	addFsError_.clear();
}

// ファイルシステムの追加。今のところ足せるのはフォルダマウント (dir:) だけ
// なので、種類の選択は省いて場所だけを決める（filesystem.md）。
// 場所は **OS ネイティブのパス**なので、打ち込みと OS の「フォルダを探す」
// ダイアログで取る。ファイラーのフォルダ選択（VFS の中を辿る自前のもの）は
// ここでは使わない。
// 端末の「フォルダを選ぶ」画面 (SAF) の結果を拾う。あちらが出ている間
// mxv2 はバックグラウンドへ回っているので、結果が届くのは戻ってきたあと。
// Vfs::all_ は std::vector で、Add の push_back で再確保が起きうる。
// 曲名・フォルダ・曲の 3 本の読みスレッドは const Vfs* 越しに Parse で
// all_ を舐めるので、足すときも消すときも、先に読みかけを捨てさせて
// 手が離れるまで待つ（それぞれの Quiesce は待ち合わせまで行う）。
// 読みかけは捨てられるが、ファイラーは操作の直後に Refresh() で
// 読み直すので見た目には残らない。
void SettingsUi::QuiesceVfsReaders(Filer *filer) {
	if (filer != 0) filer->WaitIo();
	if (folderLister_ != 0) folderLister_->Quiesce();
	if (songLoader_ != 0) songLoader_->Quiesce();
}

void SettingsUi::PollSafPicked(Filer *filer) {
	if (!safPicking_) return;

	std::string uri;
	if (!SafPollPicked(&uri)) return;
	safPicking_ = false;
	if (uri.empty()) return;  // 取り消した
	if (vfs_ == 0) return;

	FileSystem *made = vfs_->CreateFromMountRef(std::string("saf:") + uri);
	if (made == 0) {
		fsError_ = Msg("AddFs.NotFound");
		return;
	}
	QuiesceVfsReaders(filer);
	if (!vfs_->Add(made)) {
		fsError_ = MsgF("AddFs.Duplicate", made->mountRef());
		delete made;
		return;
	}
	// 選んでいた位置へ挿し込む（[追加] ダイアログと同じ）。
	const int at = (fsSelected_ >= 0) ? fsSelected_ : vfs_->count();
	vfs_->MountAt(at, made);
	fsSelected_ = at;
	changedFields_ |= Settings::kFieldFileSystems;
	if (filer != 0) filer->Refresh();
}

void SettingsUi::BuildAddFsWindow(Filer *filer) {
	if (addFsOpen_) {
		addFsOpen_ = false;
		addFsPathBuf_[0] = '\0';
		addFsError_.clear();
		ImGui::OpenPopup(kAddFsTitle);
	}

	addFsShow_ = ImGui::IsPopupOpen(kAddFsTitle);
	if (!addFsShow_) {
		addFsClose_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 0.0f), placeCond());
	if (!ImGui::BeginPopupModal(kAddFsTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted(Msg("AddFs.Path"));
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##addfspath", addFsPathBuf_, sizeof(addFsPathBuf_),
	                              ImGuiInputTextFlags_EnterReturnsTrue);
	TextNote(Msg("AddFs.Hint"));
	if (!addFsError_.empty()) {
		TextError(addFsError_.c_str());
	}
	ImGui::Separator();

	// OS のダイアログが無い環境（Android など）では打ち込みだけ。
	if (HasFolderBrowser()) {
		if (ImGui::Button(Msg("Button.Browse"))) {
			browseStart_ = TrimSpaces(addFsPathBuf_);
			pendingBrowse_ = true;
		}
		ImGui::SameLine();
	}
	if (ImGui::Button(Msg("Button.Add"))) apply = true;
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || addFsClose_) {
		addFsClose_ = false;
		ImGui::CloseCurrentPopup();
	}

	if (apply) {
		const std::string path = TrimSpaces(addFsPathBuf_);
		FileSystem *made =
		    (path.empty() || !IsDirectory(path) || vfs_ == 0)
		        ? 0
		        : vfs_->CreateFromMountRef(std::string("dir:") + path);
		if (made != 0) QuiesceVfsReaders(filer);
		if (made == 0) {
			addFsError_ = Msg("AddFs.NotFound");
		} else if (!vfs_->Add(made)) {
			addFsError_ = MsgF("AddFs.Duplicate", made->mountRef());
			delete made;
		} else {
			// 選んでいた位置へ挿し込む（filesystem.md）。
			const int at = (fsSelected_ >= 0) ? fsSelected_ : vfs_->count();
			vfs_->MountAt(at, made);
			fsSelected_ = at;
			changedFields_ |= Settings::kFieldFileSystems;
			if (filer != 0) filer->Refresh();
			ImGui::CloseCurrentPopup();
		}
	}

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

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kFsRemoveTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const FileSystem *sel =
	    (vfs_ != 0 && fsSelected_ >= 0 && fsSelected_ < vfs_->count()) ? vfs_->at(fsSelected_)
	                                                                  : 0;
	ConfirmText(MsgF("FileSystems.RemoveConfirm", sel != 0 ? sel->label() : std::string())
	                .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		// 動的に足したファイルシステムは実体も捨てるので、フォルダと
		// タイトルを読んでいるスレッドの手が離れるのを待ってからにする。
		QuiesceVfsReaders(filer);
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
// ブックマーク (F4)。ジャンプ専用の一覧はファイラーの "Bookmarks>" (M)。
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

void SettingsUi::OpenBookmarkList() {
	if (busy() || vfs_ == 0) return;
	requestedFolder_ = vfs_->BookmarkRootRef();
	if (requestedFolder_.empty()) return;
	request_ = kRequestSetFolder;
}

void SettingsUi::JumpToBookmarkRef(Settings *settings, const std::string &ref) {
	if (vfs_ == 0 || ref.empty()) return;
	const int index = FindBookmark(settings->bookmarks, ref);
	if (index >= 0) {
		OpenBookmark(settings, index);
		if (bmError_.empty()) return;  // request_ に積まれた
		bmError_.clear();
	}
	// 控えに無い（並べ替え中に消えた）か、開けない。ファイラーに任せる。
	requestedFolder_ = ref;
	request_ = kRequestSetFolder;
}

bool SettingsUi::CanBookmark(const std::string &ref) const {
	if (vfs_ == 0 || ref.empty()) return false;
	FileSystem *fs = 0;
	std::string rel;
	if (!vfs_->Parse(ref, &fs, &rel) || fs == 0) return false;
	return !fs->isJumpList();
}

void SettingsUi::BuildBookmarksWindow(Settings *settings, Filer *filer) {
	if (!SyncModal(kBookmarksTitle, &showBookmarks_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 360.0f), placeCond());

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
		TextNote(Msg("Bookmark.Hint"));
	} else {
		TextError(bmError_.c_str());
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
	// システムの選択）と "Bookmarks>" 自身、すでに控えてある場所は入れられない。
	const std::string cur = (filer != 0) ? filer->currentRef() : std::string();
	const bool bookmarkable = CanBookmark(cur);
	const bool dup = (FindBookmark(list, cur) >= 0);
	const bool full = (count >= Settings::kMaxBookmarks);
	ImGui::SameLine();
	ImGui::BeginDisabled(!bookmarkable || dup || full);
	if (ImGui::Button(Msg("Button.Add"))) {
		const int at = (bmSelected_ >= 0) ? bmSelected_ : count;
		list.insert(list.begin() + at, cur);
		bmSelected_ = at;
		changedFields_ |= Settings::kFieldBookmarks;
		bmError_.clear();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		if (!bookmarkable) {
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

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kBmRemoveTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const bool valid = (bmSelected_ >= 0 && bmSelected_ < (int)list.size());
	ConfirmText(MsgF("Bookmark.RemoveConfirm",
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
		// 追加できない場所（ファイルシステムの選択、"Bookmarks>"）では何もしない。
		if (CanBookmark(cur)) {
			bmToggleRef_ = cur;
			ImGui::OpenPopup(kBmToggleTitle);
		}
	}

	bmToggleOpen_ = ImGui::IsPopupOpen(kBmToggleTitle);
	if (!bmToggleOpen_) {
		bmCloseToggle_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kBmToggleTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const int at = FindBookmark(list, bmToggleRef_);
	const std::string shown = (vfs_ != 0) ? vfs_->DisplayPath(bmToggleRef_) : bmToggleRef_;

	if (at >= 0) {
		ConfirmText(MsgF("Bookmark.RemoveConfirm", shown).c_str());
		ImGui::Separator();
		if (ImGui::Button(Msg("Button.Remove"))) {
			list.erase(list.begin() + at);
			if (bmSelected_ >= (int)list.size()) bmSelected_ = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldBookmarks;
			ImGui::CloseCurrentPopup();
		}
	} else {
		const bool full = ((int)list.size() >= Settings::kMaxBookmarks);
		ConfirmText(MsgF("Bookmark.AddConfirm", shown).c_str());
		if (full) {
			TextError(Msg("Bookmark.Full"));
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
	if (vfs_ == 0) return;

	FileSystem *fs = 0;
	std::string rel;
	if (!vfs_->Parse(dir, &fs, &rel)) return;
	// "Bookmarks>" の中からはファイルシステムの選択から始める（あちらは
	// フォルダの一覧ではないので、開いても空になるだけ）。
	if (fs != 0 && fs->isJumpList()) {
		fs = 0;
		rel.clear();
	}
	const std::string want = Vfs::MakeRef(fs, rel);

	// 読めなかったときに戻る先を控える。読み込み中の（まだ空の）姿は
	// 控えない（続けて打ち込まれたときに空へ戻ってしまう）。
	if (!folderLoading_) {
		folderPrevDir_ = folderDir_;
		folderPrevSelected_ = folderSelected_;
		folderPrevEntries_ = folderEntries_;
		folderHasPrev_ = true;
	}

	folderLoading_ = false;
	folderLister_->Cancel();
	folderDir_ = want;
	folderSelected_.clear();
	folderEntries_.clear();

	// ファイルシステムの選択。読み込みは要らない。
	if (fs == 0) {
		for (int i = 0; i < vfs_->count(); i++) {
			FileSystem *m = vfs_->at(i);
			if (!m->available()) continue;
			if (m->isJumpList()) continue;  // "Bookmarks>" はフォルダではない
			FolderEntry e;
			e.name = m->prefix();
			e.ref = Vfs::MakeRef(m, m->Root());
			folderEntries_.push_back(e);
		}
		return;
	}

	folderLoading_ = true;
	folderTicks_ = SDL_GetTicks();
	folderLister_->Start(vfs_, folderDir_);
}

// 別スレッドが読み終えた中身を一覧へ入れる。
void SettingsUi::PollFolderDir() {
	DirLister::Result r;
	if (!folderLister_->Take(&r)) return;
	if (!folderLoading_ || r.ref != folderDir_) return;

	folderLoading_ = false;

	if (!r.ok) {
		// 開けない場所（打ち込みの途中、読めないドライブ）。前の一覧に戻す。
		if (folderHasPrev_) {
			folderDir_ = folderPrevDir_;
			folderSelected_ = folderPrevSelected_;
			folderEntries_ = folderPrevEntries_;
		}
		return;
	}

	FileSystem *fs = 0;
	std::string rel;
	if (vfs_ == 0 || !vfs_->Parse(folderDir_, &fs, &rel) || fs == 0) return;

	folderEntries_.clear();
	std::vector<std::string> names;
	for (size_t i = 0; i < r.entries.size(); i++) {
		if (r.entries[i].isDir) names.push_back(r.entries[i].name);
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
	// folderDir_ は読み込みを始めた時点で行き先になっている（中身だけが
	// あとから届く）ので、入力欄はそのまま合わせてよい。
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

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(520.0f, 280.0f), placeCond());

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

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(560.0f, 460.0f), placeCond());

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

void SettingsUi::BuildFolderWindow(Settings *settings, Filer *filer) {
	const char *title = folderTitle();
	if (!SyncModal(title, &showFolder_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 400.0f), placeCond());

	if (!ImGui::BeginPopupModal(title, &showFolder_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// 一覧は別スレッドが読んでいる。届いていれば取り込む。
	PollFolderDir();

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
		// 打ち込みの途中は開けない場所を通るが、**それを確かめるのも
		// 別スレッドの仕事**（ここで IsDir を呼ぶと 1 打鍵ごとに固まる）。
		// 開けなければ PollFolderDir が前の一覧に戻す。
		if (!typed.empty() && vfs_ != 0 && vfs_->Resolve(typed, folderDir_, &ref) &&
		    ref != folderDir_) {
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
		TextNote(folderDir_.empty() ? Msg("Folder.FileSystem") : folderDir_.c_str());
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

		// 手間取っているときだけ知らせる。ローカルのフォルダは一瞬で
		// 届くので、すぐ出すとちらつくだけになる。
		if (folderLoading_ && (SDL_GetTicks() - folderTicks_) >= kLoadingDelayMs) {
			ImGui::TextDisabled("%s", Msg("Filer.Loading"));
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
		TextNote(Msg("Folder.Hint"));
	} else {
		TextError(folderError_.c_str());
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
		// 今一覧に出している場所なら読めているのが分かっているので、
		// IsDir は省く（遅いファイルシステムでの往復を 1 回減らす）。
		const bool known = ok && (ref == folderDir_) && !folderLoading_;
		if (!ok || (!ref.empty() && !known && !vfs_->IsDir(ref))) {
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
// 終了はメインループの持ち物なので request_ に積んで返す。
//
// **[操作] と [マスク] は 2026-09-10 に外した**（ユーザーの指示）。旧 mxv で
// まだ画面をクリックして操作できなかった頃の名残で、操作ボタン・鍵盤・
// ステータス欄を押せるようになった今はもう要らない。モバイルでは階層のある
// メニューが扱いづらく、横画面では項目数そのものに余裕が無い、という事情もある。
void SettingsUi::BuildContextMenu(Settings *settings, DrawScreen *draw, Player *player,
                                  Filer *filer) {
	(void)draw;
	(void)player;

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
		if (closeContextMenu_) {
			closeContextMenu_ = false;
			ImGui::CloseCurrentPopup();
		}

		// 並びは bookmark.md の「メインメニュー」のとおり:
		// 移動（フォルダを開く / ブックマークを開く / ブックマークに追加）→
		// 設定 4 つ → 操作方法・バージョン情報 →（デスクトップのみ）終了。
		if (ImGui::MenuItem(Msg("Menu.Folder"), "L")) {
			SetFolderDir(filer->currentRef());
			showFolder_ = true;
		}
		// ファイラーの "Bookmarks>"（ジャンプ専用）。設定ダイアログは下の段。
		if (ImGui::MenuItem(Msg("Menu.BookmarkList"), "M")) OpenBookmarkList();
		{
			// カレントを控える / 控えを外す。どちらも確認してから実行するので、
			// ここでは印を立てるだけ（メニューの中で OpenPopup すると入れ子の
			// ポップアップになってしまう）。
			const std::string cur = filer->currentRef();
			const bool has = (FindBookmark(settings->bookmarks, cur) >= 0);
			if (ImGui::MenuItem(has ? Msg("Menu.BookmarkRemove") : Msg("Menu.BookmarkAdd"),
			                    "Shift+M", false, CanBookmark(cur))) {
				bmOpenToggle_ = true;
			}
		}

		ImGui::Separator();
		if (ImGui::MenuItem(Msg("Menu.Settings"), "F1")) visible_ = true;
		// F2 と同じ経路を通す（スキン名の欄を埋め直すため）。
		if (ImGui::MenuItem(Msg("Menu.Colors"), "F2")) OpenColors();
		if (ImGui::MenuItem(Msg("Menu.FileSystems"), "F3")) OpenFileSystems();
		if (ImGui::MenuItem(Msg("Menu.Bookmarks"), "F4")) OpenBookmarks();

		ImGui::Separator();
		if (ImGui::MenuItem(Msg("Menu.Help"), "F11")) showHelp_ = true;
		if (ImGui::MenuItem(Msg("Menu.About"), "F12")) showAbout_ = true;
		// [終了] はモバイルには置かない（区切り線ごと）。
		if (Screen::CanQuitApp()) {
			ImGui::Separator();
			if (ImGui::MenuItem(Msg("Menu.Quit"))) request_ = kRequestQuit;
		}

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

		// 設定ウィンドウと同じく、開くたびに画面の中央から出す。
		// 大きさは画面からはみ出さないように詰める。スキンの横幅の下限は
		// 480px なので、既定の 560px はそのままでは入らない。
		CenterNextWindow(placeCond());
		ImGui::SetNextWindowSize(DialogSize(560.0f, 420.0f), placeCond());
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
