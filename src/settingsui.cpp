// mxv2 - 設定 UI（Dear ImGui）

#include "settingsui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "drawscreen.h"
#include "fileutil.h"
#include "filer.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "skin.h"

namespace mxv2 {

namespace {

// 同梱フォント。assets/ に置いてあるので、どのプラットフォームでも読める。
// M PLUS 1p Regular (SIL OFL 1.1)。ライセンス全文は assets/MPLUS1p-OFL.txt。
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
bool ColorRow(const char *label, Rgb *c) {
	float f[3];
	ToFloat3(*c, f);
	if (!ImGui::ColorEdit3(label, f, ImGuiColorEditFlags_NoInputs)) return false;
	*c = FromFloat3(f);
	return true;
}

// Bright 値（0..100 が alpha、それ以上は乗算）。変わったら true。
bool BrightRow(const char *label, int *v) {
	return ImGui::SliderInt(label, v, 0, 200);
}

}  // namespace

SettingsUi::SettingsUi()
    : ready_(false),
      visible_(false),
      hasJapaneseFont_(false),
      styleScale_(0.0f),
      inputScale_(1.0f),
      pendingZoom_(0),
      zoomApplyAtMs_(0),
      changedFields_(0) {
	pdxPathBuf_[0] = '\0';
}

SettingsUi::~SettingsUi() {
	Shutdown();
}

bool SettingsUi::Init(Screen *screen, const std::string &assetsDir, std::string *err) {
	if (ready_) return true;
	if (screen == 0 || screen->window() == 0 || screen->renderer() == 0) {
		*err = "設定 UI の初期化にはウィンドウが要ります。";
		return false;
	}

	assetsDir_ = assetsDir;
	skinRootDir_ = JoinPath(assetsDir_, "skin");
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

	// 日本語フォント。ImGui 1.92 以降はグリフを要求時に焼くので、
	// GlyphRanges を渡さなくても日本語が出る。
	{
		std::string path = JoinPath(assetsDir_, kUserFont);
		if (!FileExists(path)) path = JoinPath(assetsDir_, kBundledFont);
		if (!FileExists(path)) path.clear();
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
		baseStyle_ = style;  // 拡大率が変わったらここから作り直す
	}

	if (!ImGui_ImplSDL2_InitForSDLRenderer(screen->window(), screen->renderer())) {
		*err = "ImGui_ImplSDL2_InitForSDLRenderer に失敗しました。";
		ImGui::DestroyContext();
		return false;
	}
	if (!ImGui_ImplSDLRenderer2_Init(screen->renderer())) {
		*err = "ImGui_ImplSDLRenderer2_Init に失敗しました。";
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

bool SettingsUi::wantCaptureMouse() const {
	if (!ready_ || !visible_) return false;
	return ImGui::GetIO().WantCaptureMouse;
}

bool SettingsUi::wantCaptureKeyboard() const {
	if (!ready_ || !visible_) return false;
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
	ListSkins(skinRootDir_, &skinNames_);
}

std::string SettingsUi::SkinDir(const std::string &name) const {
	return JoinPath(skinRootDir_, name);
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

	if (!visible_) return;

	// 倍率が変わったフレームは、ダイアログの位置と大きさも作り直す。
	// ImGui はウィンドウの矩形をピクセルで覚えているので、放っておくと
	// 中身だけ大きくなって枠が付いてこない。
	const ImGuiCond cond = scaleChanged ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	ImGui::SetNextWindowPos(ImVec2(8 * scale, 8 * scale), cond);
	ImGui::SetNextWindowSize(ImVec2(380 * scale, 464 * scale), cond);

	bool open = true;
	if (!ImGui::Begin("mxv2 の設定", &open)) {
		ImGui::End();
		if (!open) visible_ = false;
		return;
	}
	if (!open) visible_ = false;

	// ---- 画面 ----------------------------------------------------------
	if (ImGui::CollapsingHeader("画面", ImGuiTreeNodeFlags_DefaultOpen)) {
		// スキン。画面サイズごと変わりうるので、選ばれた名前を置いておいて
		// 実際の作り直しはメインループに任せる。
		int current = -1;
		for (size_t i = 0; i < skinNames_.size(); i++) {
			if (skinNames_[i] == settings->skinName) current = (int)i;
		}
		if (ImGui::BeginCombo("スキン", (current >= 0) ? skinNames_[current].c_str()
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
		if (ImGui::Button("再読込")) {
			ScanSkins();
			pendingSkin_ = settings->skinName;
		}

		// 表示倍率 (%)。100 でドット等倍。
		// 押している間・入力中に適用してはいけない。ウィンドウが大きくなると
		// このコントロール自身の座標も変わるので、同じ場所を押しているだけで
		// 値が行き来してしまう。確定してから少し待って適用する。
		int zoom = settings->zoomPercent;
		ImGui::SetNextItemWidth(80.0f * styleScale_);
		const bool edited = ImGui::InputInt("表示倍率 (%)", &zoom, 25, 100);
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
		if (ImGui::SmallButton("システムに合わせる")) {
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
				{ Screen::kScaleSharp, "sharp-bilinear（既定）" },
				{ Screen::kScaleNearest, "最近傍" },
				{ Screen::kScaleLinear, "バイリニア" },
			};
			const int count = (int)(sizeof(kItems) / sizeof(kItems[0]));
			const Screen::ScaleMode now = screen->scaleMode();
			const char *label = kItems[0].label;
			for (int i = 0; i < count; i++) {
				if (kItems[i].mode == now) label = kItems[i].label;
			}
			if (ImGui::BeginCombo("拡大の補間", label)) {
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

	// ---- ファイラ ------------------------------------------------------
	if (ImGui::CollapsingHeader("ファイラ", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool largeFont = (settings->fileListFontSize != 0);
		if (ImGui::Checkbox("大きい文字で表示する", &largeFont)) {
			settings->fileListFontSize = largeFont ? 1 : 0;
			changedFields_ |= Settings::kFieldFontSize;
			draw->SetFileListFontSize(settings->fileListFontSize);
			filer->SetVisibleRows(draw->fileListRows());
		}

		bool folderFirst = settings->folderFirst;
		if (ImGui::Checkbox("フォルダを先に並べる", &folderFirst)) {
			settings->folderFirst = folderFirst;
			changedFields_ |= Settings::kFieldFolderFirst;
			filer->SetFolderFirst(folderFirst);
			filer->Refresh();
		}
	}

	// ---- 演奏 ----------------------------------------------------------
	if (ImGui::CollapsingHeader("演奏", ImGuiTreeNodeFlags_DefaultOpen)) {
		int loops = settings->loops;
		if (ImGui::SliderInt("ループ数", &loops, 1, 10)) {
			settings->loops = loops;
			changedFields_ |= Settings::kFieldLoops;
			player->SetLoopConfig(settings->loops, settings->fadeout);
		}
		bool fadeout = settings->fadeout;
		if (ImGui::Checkbox("最後にフェードアウトする", &fadeout)) {
			settings->fadeout = fadeout;
			changedFields_ |= Settings::kFieldFadeout;
			player->SetLoopConfig(settings->loops, settings->fadeout);
		}
		ImGui::TextDisabled("ループ数とフェードアウトは次の曲から効きます");

		// マスター音量。メイン画面の音量バーとは別で、実際の音量は 2 つの和。
		int vol = player->masterVolume();
		if (ImGui::SliderInt("マスター音量", &vol, Player::kVolumeMin, Player::kVolumeMax,
		                     "%+d")) {
			player->SetMasterVolume(vol);
			changedFields_ |= Settings::kFieldVolume;
		}
		settings->masterVolume = player->masterVolume();
		ImGui::TextDisabled("実際の音量 = マスター %+d + メイン画面 %+d = %+d",
		                    player->masterVolume(), player->mainVolume(),
		                    player->effectiveVolume());

		if (pdxPathBuf_[0] == '\0' && !settings->pdxPath.empty()) {
			snprintf(pdxPathBuf_, sizeof(pdxPathBuf_), "%s", settings->pdxPath.c_str());
		}
		if (ImGui::InputText("PDX の探索先", pdxPathBuf_, sizeof(pdxPathBuf_))) {
			settings->pdxPath = pdxPathBuf_;
			changedFields_ |= Settings::kFieldPdxPath;
		}
	}

	// ---- テーマの色 ----------------------------------------------------
	if (ImGui::CollapsingHeader("テーマの色")) {
		Theme &t = draw->theme();
		bool dirty = false;

		ImGui::SeparatorText("背景");
		{
			bool useBitmap = (t.back.bitmap != 0);
			if (ImGui::Checkbox("背景画像を使う", &useBitmap)) {
				t.back.bitmap = useBitmap ? 1 : 0;
				dirty = true;
			}
			if (BrightRow("画像の明るさ", &t.back.bitmapBright)) dirty = true;
			if (ColorRow("背景色", &t.back.color)) dirty = true;
			if (BrightRow("背景色の強さ", &t.back.colorBright)) dirty = true;
		}

		ImGui::SeparatorText("鍵盤");
		{
			if (BrightRow("黒鍵", &t.kb.blackBright)) dirty = true;
			if (BrightRow("白鍵", &t.kb.whiteBright)) dirty = true;
			if (BrightRow("全体", &t.kb.bright)) dirty = true;
		}

		ImGui::SeparatorText("ステータス");
		{
			if (ColorRow("文字色##st", &t.status.color)) dirty = true;
			if (BrightRow("文字の強さ##st", &t.status.colorBright)) dirty = true;
			if (ColorRow("背景色##st", &t.status.backColor)) dirty = true;
			if (BrightRow("背景の強さ##st", &t.status.backColorBright)) dirty = true;
		}

		ImGui::SeparatorText("曲名");
		{
			if (ColorRow("文字色##ti", &t.mdxTitle.color)) dirty = true;
			if (BrightRow("文字の強さ##ti", &t.mdxTitle.colorBright)) dirty = true;
			if (ColorRow("背景色##ti", &t.mdxTitle.backColor)) dirty = true;
			if (BrightRow("背景の強さ##ti", &t.mdxTitle.backColorBright)) dirty = true;
		}

		ImGui::SeparatorText("ファイルリスト");
		{
			if (ColorRow("カーソル##fi", &t.filer.cursorBright)) dirty = true;
			if (ColorRow("文字色##fi", &t.filer.color)) dirty = true;
			if (BrightRow("文字の強さ##fi", &t.filer.colorBright)) dirty = true;
			if (ColorRow("フォルダ##fi", &t.filer.folderColor)) dirty = true;
			if (ColorRow("ドライブ##fi", &t.filer.driveColor)) dirty = true;
			if (ColorRow("背景色##fi", &t.filer.backColor)) dirty = true;
			if (BrightRow("背景の強さ##fi", &t.filer.backColorBright)) dirty = true;
		}

		ImGui::SeparatorText("操作キー");
		{
			if (ColorRow("文字色##pk", &t.playKey.color)) dirty = true;
			if (BrightRow("文字の強さ##pk", &t.playKey.colorBright)) dirty = true;
			if (BrightRow("キーの明るさ##pk", &t.playKey.keyBright)) dirty = true;
		}

		if (dirty) Rebuild(draw, player);

		ImGui::Separator();
		// 保存先は「今のスキンのフォルダ」。土台から継承していても、
		// 書き込むのは自分のフォルダ側。
		if (ImGui::Button("テーマを保存")) {
			draw->theme().Save(JoinPath(SkinDir(settings->skinName), "theme.mxv"));
		}
		ImGui::SameLine();
		if (ImGui::Button("読み直す")) {
			pendingSkin_ = settings->skinName;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("skin/%s/theme.mxv", settings->skinName.c_str());
	}

	ImGui::Separator();
	// 保存ボタンは無い。触った時点で mxv2.ini へ書き戻す（スマートフォンでの
	// 作法に合わせてある。デスクトップでも不自然ではないという判断）。
	ImGui::TextDisabled("変更はすぐに保存されます / F1 で閉じる");

	ImGui::End();
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
