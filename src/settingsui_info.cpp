// mxv2 - 設定 UI: [操作方法] (F11)・[バージョン情報] (F12)・起動時の警告
//
// settingsui.cpp から切り出した。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include <SDL.h>

#include "imgui.h"

#include "settingsui_internal.h"

#include "appprofile.h"  // CMake が Profile.ini から生成する
#include "fileutil.h"
#include "kinsoku.h"
#include "safaccess.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// バージョン情報から開く場所。NOTICE は配布物の中身と同じものが GitHub に
// あるので、そこへ飛ばす（画面の字はコピーできないため。2026-09-16、
// ユーザーの指示）。値は mxv2/Profile.ini の [URL] Project / Notice
// （著作者専用）から CMake が appprofile.h に写したもの。
const char *kNoticeUrl = MXV2_URL_NOTICE;
const char *kProjectUrl = MXV2_URL_PROJECT;

// 既定のブラウザで url を開く。SDL_OpenURL は Windows でも Android でも効く。
void OpenUrl(const char *url) {
	if (SDL_OpenURL(url) != 0) {
		printf("warning  : %s\n", MsgF("Log.OpenUrlFailed", url, SDL_GetError()).c_str());
		fflush(stdout);
	}
}

// バージョン情報の字の大きさを決める物差し。NOTICE は等幅 80 桁で書いて
// あるので、余裕をみた 88 桁ぶんが横に収まるようにする。
const char kAboutRuler88[] =
    "****************************************"   // 40
    "****************************************"   // 40
    "********";                                  // 8

// バージョン情報に出す文面。実行ファイルの隣の NOTICE をそのまま読む。
// 配布物に入れるファイルなので、同じ文面をソースに二重に持たない。
std::string LoadAboutText() {
	std::vector<uint8_t> data;
	if (ReadWholeFile(JoinPath(ExecutableDir(), "NOTICE"), &data) && !data.empty()) {
		return std::string((const char *)&data[0], data.size());
	}
	return std::string(Msg("About.NoticeMissing")) + "\n" + Msg("About.NoticeHint");
}

}  // namespace

// フォルダを選ぶダイアログ (L)。旧 mxv の MX_GetNewDirFileList にあたる。
// 原典は SHBrowseForFolder を出していたが、あれは Windows 専用なので、
// パスの打ち込みと子フォルダの一覧を持つ自前のダイアログにしてある。
// 決まった行き先は request_ に積んで、実際の移動はメインループに任せる
// （ファイラーの持ち物はあちらなので、コンテキストメニューと同じ作法）。
void SettingsUi::SetStartupWarnings(const std::vector<StartupWarning> &lines) {
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

	if (!ImGui::BeginPopupModal(kStartupTitle, &showStartup_, DialogFlags())) {
		return;
	}

	{
		const float foot = ImGui::GetFrameHeightWithSpacing();
		ImGui::BeginChild("##startup", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		for (size_t i = 0; i < startupLines_.size(); i++) {
			const StartupWarning &w = startupLines_[i];
			ImGui::Bullet();
			TextWrappedKinsoku(w.text.c_str());
			// アクセス許可が失われた SAF なら、その場で取り直せるボタンを
			// 添える（[ファイルシステムの設定] まで辿らなくて済むように）。
			// ピッカーを出している間は他の項目のボタンを押せなくする。
			if (!w.regrantRef.empty() && !w.done) {
				ImGui::PushID((int)i);
				ImGui::Indent();
				ImGui::BeginDisabled(safPicking_ || !SafAvailable());
				if (ImGui::Button(Msg("Button.Regrant"))) {
					FileSystem *fs = (vfs_ != 0) ? vfs_->FindByMountRef(w.regrantRef) : 0;
					if (fs != 0 && SafPickTree(fs->Root())) {
						safPicking_ = true;
						safRegrantRef_ = w.regrantRef;
						safRegrantFromFiler_ = false;
					}
				}
				ImGui::EndDisabled();
				ImGui::Unindent();
				ImGui::PopID();
			}
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, false);
		ImGui::EndChild();
	}
	if (ImGui::Button(Msg("Button.Close"), ImVec2(-FLT_MIN, 0.0f))) showStartup_ = false;

	ImGui::EndPopup();
}

// [操作方法] の中身をカタログから作る。-h の出力と同じ一覧
// （[HelpKeys] [HelpMouse] [HelpPad]）を、見出しを挟んで並べたもの。
void SettingsUi::LoadHelpRows() {
	static const char *kParts[3][2] = {
		{ "Help.Keys", "HelpKeys" },
		{ "Help.Mouse", "HelpMouse" },
		{ "Help.Pad", "HelpPad" },
	};

	helpRows_.clear();
	for (int i = 0; i < 3; i++) {
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

	if (ImGui::BeginPopupModal(kHelpTitle, &showHelp_, DialogFlags())) {
		ImGui::BeginChild("##help", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);

		// キー名の桁を揃えて 2 段組で出す。空白で揃えないのは、同梱フォントが
		// プロポーショナルで桁が合わないため。
		//
		// キー名の列幅は「一番広いキー名」で決め、説明は残りの幅で**折り返す**
		// （禁則つき。kinsoku.h）。以前は横に収まらないと字を縮めていたが、
		// 指で操作する端末では字に下限があって縮められず、横スクロールに
		// なっていた（2026-09-15、英語の [操作方法] で発覚）。字は縮めない。
		// キー名の列は表示幅の 4 割で頭打ちにし、それより長いキー名は
		// キー名のほうを折り返す（説明の幅を食いつぶさないように）。
		const float avail = ImGui::GetContentRegionAvail().x;
		const float unit = ImGui::CalcTextSize(" ").x;  // 字下げと段間に使う
		float keyW = 0.0f;
		for (size_t i = 0; i < helpRows_.size(); i++) {
			if (helpRows_[i].header) continue;
			const float k = ImGui::CalcTextSize(helpRows_[i].key.c_str()).x;
			if (k > keyW) keyW = k;
		}
		const float keyWMax = avail * 0.4f;
		if (keyW > keyWMax) keyW = keyWMax;
		const float x0 = ImGui::GetCursorPosX();
		const float keyX = x0 + unit * 2.0f;   // 行頭の字下げ
		const float descX = keyX + keyW + unit * 2.0f;
		float descW = x0 + avail - descX;
		if (descW < unit) descW = unit;
		const float spacingY = ImGui::GetStyle().ItemSpacing.y;

		for (size_t i = 0; i < helpRows_.size(); i++) {
			const HelpRow &row = helpRows_[i];
			if (row.header) {
				if (i != 0) ImGui::Spacing();
				ImGui::TextUnformatted(row.key.c_str());
				continue;
			}
			// キー名と説明は別々に折り返して横に並べる。行の高さは高いほうに
			// 合わせる（SameLine は前の項目の上端に戻すので、説明のほうが
			// 短いとキー名の 2 行目に次の行が重なる）。
			const std::string key = WrapTextKinsoku(row.key, keyW);
			const std::string desc = WrapTextKinsoku(row.desc, descW);
			const float y0 = ImGui::GetCursorPosY();
			float h = ImGui::CalcTextSize(key.c_str()).y;
			ImGui::SetCursorPosX(keyX);
			ImGui::TextUnformatted(key.c_str());
			if (!row.desc.empty()) {
				const float hd = ImGui::CalcTextSize(desc.c_str()).y;
				if (hd > h) h = hd;
				// SameLine の位置はウィンドウ左端からの距離なので、
				// Indent ではなくこちらで直に指定する。
				ImGui::SameLine(descX);
				ImGui::TextUnformatted(desc.c_str());
			}
			// カーソルを動かすだけで終わると ImGui が「項目を置け」と assert する
			// （境界を伸ばすのは項目）ので、空の Dummy を置いて高さを確定する。
			const float yEnd = y0 + h + spacingY;
			if (ImGui::GetCursorPosY() < yEnd) {
				ImGui::SetCursorPosY(yEnd - spacingY);
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}

		DragToScroll(&dragScroll_, &dragMoved_, false, false);
		ImGui::EndChild();
		ImGui::EndPopup();
	}
}

// バージョン情報 (F12)。見出しは main から渡されたもの、本文は NOTICE。
void SettingsUi::BuildAboutWindow() {
	if (SyncModal(kAboutTitle, &showAbout_)) {
		// NOTICE は初めて開くときに読む（メニューからでも F12 からでも通る）。
		if (aboutText_.empty()) aboutText_ = LoadAboutText();

		// 設定ウィンドウと同じく、開くたびに画面の中央から出す。
		// 大きさは画面からはみ出さないように詰める。スキンの横幅の下限は
		// 480px なので、既定の 560px はそのままでは入らない。
		CenterNextWindow(placeCond());
		ImGui::SetNextWindowSize(DialogSize(560.0f, 420.0f), placeCond());
		if (ImGui::BeginPopupModal(kAboutTitle, &showAbout_, DialogFlags())) {
			// 見出し（名前・版・著作権の 2 行）は折り返す。320px 幅の携帯
			// (XS17) の縦画面で右が切れていた（2026-09-24、ユーザーの指示）。
			TextWrappedKinsoku(aboutHeader_.c_str());
			// GitHub のページと、そこにある NOTICE をブラウザで開く。
			if (ImGui::Button(Msg("About.OpenGitHub"))) OpenUrl(kProjectUrl);
			SameLineOrWrap(Msg("About.OpenNotice"));
			if (ImGui::Button(Msg("About.OpenNotice"))) OpenUrl(kNoticeUrl);
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

}  // namespace mxv2
