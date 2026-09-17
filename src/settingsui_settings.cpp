// mxv2 - 設定 UI: [mxv2 の設定] ウィンドウ (F1)
//
// settingsui.cpp から切り出した。スキンの一覧と言語の入れ替えもここ。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "drawscreen.h"
#include "filer.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "skin.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// 表示倍率を変えたあと、実際に適用するまでの待ち時間。
const uint32_t kZoomApplyDelayMs = 200;

// 画面の遅れ (ms) とサンプル数の相互変換。
int MsToFrames(int ms, const Player *player) {
	return ms * player->sampleRate() / 1000;
}

float FramesToMs(int frames, const Player *player) {
	return frames * 1000.0f / (float)player->sampleRate();
}

}  // namespace

// キー (Ctrl +/-) から頼まれた倍率の増減を入れる。1 段は keybind.cpp の
// kZoomKeyStep で、端は Screen::kZoomMin / kZoomMax で止まる。窓へ掛けるのは
// ダイアログと同じく kZoomApplyDelayMs だけ待ってから（押しっぱなしで
// リピートしても、手が止まってから 1 回だけ作り直す）。
void SettingsUi::ApplyZoomStep(Settings *settings) {
	if (zoomStepRequest_ == 0) return;
	int zoom = settings->zoomPercent + zoomStepRequest_;
	zoomStepRequest_ = 0;
	if (zoom < Screen::kZoomMin) zoom = Screen::kZoomMin;
	if (zoom > Screen::kZoomMax) zoom = Screen::kZoomMax;
	if (zoom == settings->zoomPercent) return;
	settings->zoomPercent = zoom;
	changedFields_ |= Settings::kFieldZoom;
	pendingZoom_ = zoom;
	zoomApplyAtMs_ = SDL_GetTicks() + kZoomApplyDelayMs;
}

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

// 設定ウィンドウ本体。Build() のフレーム処理から切り出したもの。
void SettingsUi::BuildSettingsWindow(Settings *settings, DrawScreen *draw, Player *player,
                                     Filer *filer, Screen *screen) {
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

		// フルスクリーン表示（デスクトップだけ）。窓の大きさが変わるだけ
		// なので即時に掛けてよく、キャンバスは次のフレームで窓に合わせて
		// 作り直される（main の SyncCanvasToWindow）。
		if (Screen::CanFullScreen()) {
			bool full = screen->fullScreen();
			if (ImGui::Checkbox(Msg("Settings.FullScreen"), &full)) {
				screen->SetFullScreen(full);
				// 掛けられなかったときのために、旗ではなく実際の状態を写す。
				settings->fullScreen = screen->fullScreen();
				changedFields_ |= Settings::kFieldFullScreen;
			}
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

		// PDX の探索先は一覧なので、別のダイアログ [PDX の探索先] で管理する
		// （ブックマークの設定と同じ作り。2026-09-16、ユーザーの指示。それまでは
		// 1 本だけを打ち込み欄と [参照…] で指定していた）。ここは件数と [編集…]
		// だけ。モーダル同士は入れ子にせず、設定ウィンドウを閉じてから開き、
		// 閉じたらまた開く（Build() の pdxOpenPending_ / pdxReturnToSettings_）。
		ImGui::TextUnformatted(Msg("Settings.PdxPath"));
		ImGui::SameLine();
		ImGui::TextDisabled("%s", MsgF("Settings.PdxPathCount",
		                                MsgNum("%d", (int)settings->pdxPaths.size()))
		                               .c_str());
		ImGui::SameLine();
		if (ImGui::Button(Msg("Button.Edit"))) {
			pdxSelected_ = 0;
			pdxError_.clear();
			pdxOpenPending_ = true;
			pdxReturnToSettings_ = true;
			visible_ = false;
		}
		GroupTrailingSpace();
	}

	// ---- 動作 ----------------------------------------------------------
	// 初回起動のチュートリアル (tutorial.md)。ini の [Tutorial] Done を
	// 「次回起動時に表示する」の裏返しで見せる。入れておくと次の起動で
	// 出て、見終える（終了する）とまた外れる。
	if (GroupHeader(Msg("Settings.Behavior"))) {
		bool showTutorial = !settings->tutorialDone;
		if (ImGui::Checkbox(Msg("Settings.TutorialNext"), &showTutorial)) {
			settings->tutorialDone = !showTutorial;
			changedFields_ |= Settings::kFieldTutorial;
		}
		GroupTrailingSpace();
	}

	// 保存ボタンは無い。触った時点で mxv2.ini へ書き戻す（スマートフォンでの
	// 作法に合わせてある。デスクトップでも不自然ではないという判断）。
	DragToScroll(&dragScroll_, &dragMoved_, true, false);
	ImGui::EndPopup();
}

}  // namespace mxv2
