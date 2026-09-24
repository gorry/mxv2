// mxv2 - 設定 UI: [配色設定] ダイアログ (F2) と、スキンへの保存
//
// settingsui.cpp から切り出した。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "colors.h"
#include "drawscreen.h"
#include "fileutil.h"
#include "ini.h"
#include "player.h"
#include "settings.h"
#include "skin.h"

namespace mxv2 {

using namespace settingsui;

namespace {

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

// スキンの配色を編集するダイアログ。以前は設定ウィンドウの中の
// CollapsingHeader だったが、項目数が多く設定ウィンドウが縦に伸びるので
// 別ダイアログにした。F2 と右クリックメニューから開ける。
void SettingsUi::BuildColorsWindow(Settings *settings, DrawScreen *draw, Player *player) {
	if (!SyncModal(kColorsTitle, &showColors_)) return;

	// 設定ウィンドウと同じ作法。開くたびに中央、画面からはみ出さない大きさ。
	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(380.0f, 464.0f), placeCond());

	if (ImGui::BeginPopupModal(kColorsTitle, &showColors_, DialogFlags())) {
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

		// スライダーのラベルが横で切れないように（PushLabeledItemWidth）。
		// 色見本 (ColorEdit3 の NoInputs) は正方形で幅を使わないので入れない。
		static const char *const kItemLabels[] = {
			"Colors.ImageGain",    "Colors.BgStrength",   "Colors.BlackKey",
			"Colors.WhiteKey",     "Colors.PressedKey",   "Colors.TextStrength",
			"Colors.BackStrength", "Colors.CursorStrength", "Colors.ButtonGain",
		};
		PushLabeledItemWidth(kItemLabels, (int)(sizeof(kItemLabels) / sizeof(kItemLabels[0])));

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

		// OPM レジスタ一覧（regmap.md）は BlitTo で乗せるので、Reload を待たず
		// 次のフレームから効く（Rebuild はほかと同じに呼んでおく）。
		if (GroupHeader(Msg("Colors.RegMap"))) {
			if (ColorRow(L("Colors.Text", "##rm"), &t.regMap.color)) dirty = true;
			if (AlphaRow(L("Colors.TextStrength", "##rm"), &t.regMap.colorBright)) dirty = true;
			if (ColorRow(L("Colors.Back", "##rm"), &t.regMap.backColor)) dirty = true;
			if (AlphaRow(L("Colors.BackStrength", "##rm"), &t.regMap.backColorBright)) dirty = true;
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

		ImGui::PopItemWidth();
		if (dirty) Rebuild(draw, player);

		BuildOverwriteWindow(settings, draw);

		DragToScroll(&dragScroll_, &dragMoved_, true, false);
		ImGui::EndPopup();
	}
}

namespace {

// 配色設定の先頭の「見出し + 値」の行の見出し。値は x（見出しの列の右）から
// 始める。値が入力欄でも文字でも高さが揃うように、字をフレームの中央へ下げる。
void SaveRowLabel(const char *label, float x) {
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	ImGui::SameLine(x);
}

}  // namespace

// 配色設定の先頭。保存先のスキン名と、保存・読み直しのボタン。
//
// 保存先はスキンの名前で指定する。今のスキンの名前のままなら上書き、
// 別の名前にすれば「名前を付けて保存」で新しいスキンができる。
// 同梱ぶんは読み取り専用なので、書き込み先は必ずユーザーフォルダ側。
//
// 並びは 1 行に 1 つ（2026-09-24、ユーザーの指示）:
//   スキン名 [Phone]
//   保存先   skin/Phone
//   [保存] [読み直す]
//   参照元   (なし)
// 以前はボタンの右に保存先を並べていたが、320px 幅の携帯 (XS17) の縦画面で
// 右が切れて読めなかった。見出しの列は 3 つのうち最も長いものに揃える。
void SettingsUi::BuildSkinSaveRow(Settings *settings, DrawScreen *draw) {
	const char *const kLabels[] = {
	    Msg("Colors.SkinName"), Msg("Colors.SaveTo"), Msg("Colors.BaseSkin"),
	};
	float labelW = 0.0f;
	for (size_t i = 0; i < sizeof(kLabels) / sizeof(kLabels[0]); i++) {
		labelW = std::max(labelW, ImGui::CalcTextSize(kLabels[i]).x);
	}
	const float valueX = labelW + ImGui::GetStyle().ItemSpacing.x * 2.0f;

	SaveRowLabel(kLabels[0], valueX);
	ImGui::SetNextItemWidth(-FLT_MIN);
	const bool entered = ImGui::InputText("##skinname", skinNameBuf_, sizeof(skinNameBuf_),
	                                      ImGuiInputTextFlags_EnterReturnsTrue);

	// 保存先。書き込むのは skin/<名前>/colors.ini だが、行を短くするために
	// フォルダまでを出し、完全なパスはツールチップに出す。
	{
		const std::string name = TrimSpaces(skinNameBuf_);
		SaveRowLabel(kLabels[1], valueX);
		ImGui::TextDisabled("skin/%s", name.c_str());
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", JoinPath(paths_.UserSkinDir(name), kColorsFile).c_str());
		}
	}

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

	// 土台にしているスキン (layout.ini の [Skin] Base)。レイアウトと素材が
	// どこから来ているかは、配色をいじるときに知りたいことが多い。
	{
		const std::string &base = draw->skin().baseRef();
		SaveRowLabel(kLabels[2], valueX);
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
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
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

}  // namespace mxv2
