// mxv2 - 設定 UI: Chromecast へ送る（[表示] の [キャスト…]。cast.h / memo/cast.md）
//
// 並びは送っていてもいなくても同じ（大きさやボタンの位置が変わると使いにくい。
// 2026-09-25、ユーザーの指示）:
//   説明 / 送り先の一覧（映像を出せるものだけ。送っている間は触れず、送り先に印）
//   [送る] / [キャストを終了]（同じ場所の 1 つのボタン）
//   状態（高さ固定。説明か前回の終わりかた、または送り先と状態）
//   ── 1 列で画面に収まらないとき（横画面など）はここから右の列 ──
//   [キャスト品質]（送っている間は触れない）、[送っている間は手元の音を消す]、
//   [映像を早める] と注記（mxv2.ini の [Cast]）、
//   流れの様子（英語）、[閉じる]
// 探すのはこのダイアログを開いている間だけ。

#include "settingsui.h"

#include <cfloat>
#include <cstdio>

#include "imgui.h"

#include "settingsui_internal.h"

#include "cast.h"
#include "settings.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// 品質の段の見出し（「低  854x480 30fps 2Mbps」の形）。
void QualityLabel(const char *name, const cast::QualityPreset &p, char *out, size_t size) {
	snprintf(out, size, "%s  %dx%d %dfps %gMbps", name, p.width, p.height, p.fps,
	         p.videoKbps / 1000.0);
}

}  // namespace

void CastDialog::Build(SettingsUi &ui, Settings *settings) {
	if (!cast::Available()) {
		show_ = false;
		return;
	}
	const bool open = ui.SyncModal(kCastTitle, &show_);
	// 開いた瞬間に探し始め、閉じたらやめる（mDNS の問い合わせと、Android の
	// マルチキャストのロックを持ち続けないため）。
	if (open && !wasOpen_) {
		cast::StartDiscovery();
		selected_ = -1;
		lastSize_ = ImVec2(0.0f, 0.0f);
		recenter_ = false;
		scrollY_ = false;
	} else if (!open && wasOpen_) {
		cast::StopDiscovery();
	}
	wasOpen_ = open;
	if (!open) return;

	// 幅は決め、高さは中身に合わせる（一覧は 4 行ぶん。送り先はふつう数台なので
	// それで足りる。高さを決め打ちにすると、下の調整項目が画面からはみ出した。
	// 2026-09-25、ユーザーの指示）。
	// 大きさは中身任せなので、画面を回して 2 列 ⇔ 1 列が替わると、置き直した次の
	// フレームで大きさが変わり、下へはみ出した（横画面で開いてから縦画面にしたとき。
	// 2026-09-25、ユーザーの指摘）。大きさが変わったフレームの次は中央へ置き直す。
	CenterNextWindow(recenter_ ? ImGuiCond_Always : ui.placeCond());
	recenter_ = false;
	// 画面の縁の余白（DisplaySafeAreaPadding）の内側に収める。画面の幅ちょうどに
	// すると、ImGui が「収まらない」とみて、中身は収まっているのに縦のスクロール
	// バーを出した（Pixel 7a、2026-09-25）。
	const ImGuiIO &io = ImGui::GetIO();
	const ImGuiStyle &st = ImGui::GetStyle();
	// 1 列では画面の高さに収まらないとき（Android の横画面など）は 2 列にする:
	// 左 … 一覧と状態、右 … ボタンと調整項目（2026-09-25、ユーザーの指摘）。
	// 1 列の高さの見積もり: 字の行が 14（説明 1・一覧 4・状態 3・名札 2・注記 1・
	// 流れの様子 1・折り返しの余裕 2）、部品の行が 5（ボタン・品質・チェック・
	// スライダー・[閉じる]）、
	// 題名の帯、余白（ウィンドウの上下と一覧の枠の上下）。
	const float oneColumnH = ImGui::GetTextLineHeightWithSpacing() * 14.0f +
	                         ImGui::GetFrameHeightWithSpacing() * 6.0f +
	                         st.WindowPadding.y * 4.0f + st.ItemSpacing.y * 2.0f;
	const bool wide = io.DisplaySize.x > io.DisplaySize.y &&
	                  oneColumnH > io.DisplaySize.y - st.DisplaySafeAreaPadding.y * 2.0f;
	float width = ui.DialogSize(480.0f, 360.0f).x * (wide ? 2.0f : 1.0f);
	const float maxW = io.DisplaySize.x - st.DisplaySafeAreaPadding.x * 2.0f;
	if (width > maxW) width = maxW;
	// 高さが画面に収まらず縦のスクロールバーが出ると、ImGui はその幅を中身の幅に
	// **足して**ウィンドウを広げる。画面いっぱいの幅だと、中央に置いたウィンドウの左右が画面から
	// 切れた（r77 の 480x640 縦画面、2026-09-30）。出ていたら、そのぶん中身を詰める。
	if (scrollY_) width -= st.ScrollbarSize;
	// 幅は中身の幅で決める（SetNextWindowContentSize。高さは 0＝中身任せ）。
	// SetNextWindowSizeConstraints で幅を決めると、中身が収まっているのに縦の
	// スクロールバーが出た（Pixel 7a、2026-09-25）。
	ImGui::SetNextWindowContentSize(ImVec2(width - st.WindowPadding.x * 2.0f, 0.0f));
	if (!ui.BeginDialog(kCastTitle, &show_,
	                            ui.DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const cast::State state = cast::GetState();
	const bool idle = (state == cast::kIdle);
	const std::vector<cast::Device> devices = cast::Devices();
	if (selected_ >= (int)devices.size()) selected_ = -1;
	// 送っている間は、送り先の行に印を付ける（名前で探す）。
	int castingIndex = -1;
	if (!idle) {
		const std::string name = cast::DeviceName();
		for (int i = 0; i < (int)devices.size(); i++) {
			if (devices[i].name == name) castingIndex = i;
		}
	}

	const bool twoColumns =
	    wide && ImGui::BeginTable("##castcols", 2, ImGuiTableFlags_SizingStretchSame);
	if (twoColumns) ImGui::TableNextColumn();

	// 並び（2026-09-25、ユーザーの指示）:
	//   説明 / 一覧 / [送る]・[キャストを終了] / 状態
	//   （2 列のときはここで右の列へ）
	//   [キャスト品質] / [手元の音を消す] / [映像を早める] と注記 / 流れの様子 / [閉じる]
	ConfirmText(Msg("Cast.Choose"));
	int startIndex = -1;
	{
		const ImGuiStyle &style = ImGui::GetStyle();
		// 行数は 4（2 列のときは 3。横画面の高さに収めるため。多ければ一覧の中で
		// スクロールする）。
		const float rows = twoColumns ? 3.0f : 4.0f;
		const float listH = ImGui::GetTextLineHeightWithSpacing() * rows + style.WindowPadding.y * 2.0f;
		ImGui::BeginChild("##castlist", ImVec2(0, listH), ImGuiChildFlags_Borders);
		if (devices.empty()) ImGui::TextDisabled("%s", Msg("Cast.Searching"));
		ImGui::BeginDisabled(!idle);
		for (int i = 0; i < (int)devices.size(); i++) {
			char label[256];
			snprintf(label, sizeof(label), "%s  (%s)##cast%d", devices[i].name.c_str(),
			         devices[i].model.c_str(), i);
			const bool selected = idle ? (i == selected_) : (i == castingIndex);
			if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick) &&
			    !ui.dragMoved_ && idle) {
				selected_ = i;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) startIndex = i;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", devices[i].address.c_str());
		}
		ImGui::EndDisabled();
		DragToScroll(&ui.dragScroll_, &ui.dragMoved_, false, true);
		ImGui::EndChild();
	}

	// [送る] と [キャストを終了] は同じ場所。
	if (idle) {
		ImGui::BeginDisabled(selected_ < 0);
		if (ImGui::Button(Msg("Cast.Start"), ImVec2(-FLT_MIN, 0.0f))) startIndex = selected_;
		ImGui::EndDisabled();
		if (startIndex >= 0 && startIndex < (int)devices.size()) {
			cast::SetQuality(settings->castQuality);
			cast::SetMuteLocal(settings->castMuteLocal);
			cast::Start(devices[startIndex]);
		}
	} else {
		ImGui::BeginDisabled(state == cast::kStopping);
		// 終えるのはメインループ（先に手元を一時停止する。main.cpp の kRequestStopCast）。
		if (ImGui::Button(Msg("Cast.Stop"), ImVec2(-FLT_MIN, 0.0f))) {
			ui.request_ = SettingsUi::kRequestStopCast;
		}
		ImGui::EndDisabled();
	}

	// 状態。高さは決めておき（3 行ぶん。2 列のときは幅が広いので 2 行）、中身が
	// 変わっても下が動かないようにする。
	{
		const float statusH = ImGui::GetTextLineHeightWithSpacing() * (twoColumns ? 2.0f : 3.0f);
		ImGui::BeginChild("##caststatus", ImVec2(0, statusH), ImGuiChildFlags_None,
		                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (idle) {
			// 前に送っていたときの終わりかた（受信側で別のアプリにした・つながらない）。
			const std::string err = cast::LastError();
			const std::string errKey = cast::LastErrorKey();
			if (!errKey.empty()) {
				TextError(Msg(errKey.c_str()));
			} else if (!err.empty()) {
				TextError(MsgF("Cast.LastError", err).c_str());
			} else {
				TextNote(Msg("Cast.Hint"));
			}
		} else {
			const std::string name = cast::DeviceName();
			switch (state) {
				case cast::kConnecting:
					ConfirmText(MsgF("Cast.Connecting", name).c_str());
					break;
				case cast::kStarting:
					ConfirmText(MsgF("Cast.Starting", name).c_str());
					break;
				case cast::kCasting:
					ConfirmText(MsgF("Cast.Casting", name).c_str());
					break;
				default:
					ConfirmText(Msg("Cast.Stopping"));
					break;
			}
		}
		ImGui::EndChild();
	}

	if (twoColumns) ImGui::TableNextColumn();

	// 送る品質（大きさ・fps・映像のビットレート）。送り始めるときに決まるので、
	// 送っている間は触れない（一覧と同じ）。名札は上の行、選ぶ欄は幅いっぱい
	// （[映像を早める] と同じ。幅の狭い画面ではみ出さないように）。
	{
		static const char *const kKeys[cast::kQualityCount] = {
		    "Cast.QualityLowest", "Cast.QualityLow", "Cast.QualityMedium", "Cast.QualityHigh",
		    "Cast.QualityHighest",
		};
		int q = settings->castQuality;
		if (q < 0 || q >= cast::kQualityCount) q = Settings::kCastQualityDefault;
		char shown[128];
		ImGui::TextUnformatted(Msg("Cast.Quality"));
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::BeginDisabled(!idle);
		QualityLabel(Msg(kKeys[q]), cast::GetQualityPreset(q), shown, sizeof(shown));
		if (ImGui::BeginCombo("##castquality", shown)) {
			for (int i = 0; i < cast::kQualityCount; i++) {
				const bool selected = (i == q);
				char label[128];
				QualityLabel(Msg(kKeys[i]), cast::GetQualityPreset(i), label, sizeof(label));
				if (ImGui::Selectable(label, selected)) {
					settings->castQuality = i;
					ui.changedFields_ |= Settings::kFieldCast;
					cast::SetQuality(i);
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
	}

	bool mute = settings->castMuteLocal;
	if (ImGui::Checkbox(Msg("Cast.MuteLocal"), &mute)) {
		settings->castMuteLocal = mute;
		ui.changedFields_ |= Settings::kFieldCast;
		cast::SetMuteLocal(mute);
	}
	// 映像を早める量。鍵盤が音より遅れて見えるときに、見ながら合わせる。
	{
		// 名札は上の行に置き、スライダーは幅いっぱい。右に名札を付けると、幅の狭い
		// 画面で名札がダイアログの外へはみ出し、縦のスクロールバーまで出た。
		int ms = settings->castVideoAdvanceMs;
		ImGui::TextUnformatted(Msg("Cast.VideoAdvance"));
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::SliderInt("##castadvance", &ms, 0, Settings::kCastAdvanceMsMax, "%d ms")) {
			settings->castVideoAdvanceMs = ms;
			ui.changedFields_ |= Settings::kFieldCast;
			cast::SetVideoAdvanceMs(ms);
		}
		// すぐ効くが、TV は流れを数秒溜めて再生しているので、見えるのはそのあと
		// （何秒かは TV による）。送っていなくても出す（ダイアログの高さを変えない）。
		TextNote(Msg("Cast.AdvanceNote"));
	}
	// 流れの様子（英語のまま。調べるときの手掛かり）。送っていないときも 1 行とって
	// おく（ダイアログの高さを変えない）。
	{
		const std::string stats = cast::StatsText();
		ImGui::TextDisabled("%s", stats.empty() ? " " : stats.c_str());
	}
	if (ImGui::Button(Msg("Button.Close"))) show_ = false;
	if (twoColumns) ImGui::EndTable();
	const ImVec2 size = ImGui::GetWindowSize();
	if (lastSize_.x > 0.0f && (size.x != lastSize_.x || size.y != lastSize_.y)) {
		recenter_ = true;
	}
	lastSize_ = size;
	const bool scrollY = ImGui::GetScrollMaxY() > 0.0f;
	if (scrollY != scrollY_) {
		scrollY_ = scrollY;
		recenter_ = true;
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
