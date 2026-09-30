// mxv2 - 設定 UI: [PDX の探索先] ダイアログ
//
// settingsui.cpp から切り出した。作りはブックマークの設定と同じ。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "filer.h"
#include "settings.h"
#include "vfs.h"

namespace mxv2 {

using namespace settingsui;

// PDX の探索先の設定。設定ウィンドウの [編集…] から。ブックマークの設定と
// 同じ作りで、[開く] が無いだけ。並び順がそのまま探す順（MDX と同じフォルダの
// 次に、上から順）。
void SettingsUi::BuildPdxPathsWindow(Settings *settings, Filer *filer) {
	if (!SyncModal(kPdxPathsTitle, &pdx_.show)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 360.0f), placeCond());

	if (!BeginDialog(kPdxPathsTitle, &pdx_.show, DialogFlags())) {
		return;
	}
	if (vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	std::vector<std::string> &list = settings->pdxPaths;
	const int count = (int)list.size();
	if (pdx_.selected >= count) pdx_.selected = count - 1;
	if (pdx_.selected < 0 && count > 0) pdx_.selected = 0;

	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##pdxlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		if (count == 0) ImGui::TextDisabled("%s", Msg("PdxPath.Empty"));
		for (int i = 0; i < count; i++) {
			char label[512];
			snprintf(label, sizeof(label), "%s##pdx%d", vfs_->DisplayPath(list[i]).c_str(), i);
			if (ImGui::Selectable(label, i == pdx_.selected) && !dragMoved_) {
				pdx_.selected = i;
				pdx_.error.clear();
			}
			// 表示は見やすさ優先で DisplayPath なので、生の ref はここで見せる。
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].c_str());
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (pdx_.error.empty()) {
		TextNote(Msg("PdxPath.Hint"));
	} else {
		TextError(pdx_.error.c_str());
	}

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(pdx_.selected <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		std::swap(list[pdx_.selected], list[pdx_.selected - 1]);
		pdx_.selected--;
		changedFields_ |= Settings::kFieldPdxPaths;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(pdx_.selected < 0 || pdx_.selected >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		std::swap(list[pdx_.selected], list[pdx_.selected + 1]);
		pdx_.selected++;
		changedFields_ |= Settings::kFieldPdxPaths;
	}
	ImGui::EndDisabled();

	// [追加…] はフォルダ選択ダイアログを経由して、選んだフォルダを選択位置へ
	// 挿し込む（ブックマークの設定と同じ往復）。開始位置は選んでいる探索先
	// （Build() の folder_.openPending のところ）。
	(void)filer;
	const bool full = (count >= Settings::kMaxPdxPaths);
	ImGui::SameLine();
	ImGui::BeginDisabled(full);
	if (ImGui::Button(Msg("Button.AddPdxPath"))) {
		pdx_.error.clear();
		OpenFolderFor(kFolderTargetPdx, kFolderReturnPdx);
		pdx_.show = false;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", full ? Msg("PdxPath.AddFull") : Msg("PdxPath.AddBrowse"));
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(pdx_.selected < 0);
	if (ImGui::Button(Msg("Button.Remove"))) {
		pdx_.error.clear();
		pdx_.remove.ask = true;
	}
	ImGui::EndDisabled();

	BuildPdxRemoveWindow(settings);

	ImGui::EndPopup();
}

void SettingsUi::OpenPdxPathsFromSettings() {
	pdx_.selected = 0;
	pdx_.error.clear();
	pdx_.handoff.Start();
	SetSettingsVisible(false);
}

void SettingsUi::InsertPdxPath(std::vector<std::string> *list, const std::string &ref) {
	const int count = (int)list->size();
	const int at = (pdx_.selected >= 0 && pdx_.selected <= count) ? pdx_.selected : count;
	list->insert(list->begin() + at, ref);
	pdx_.selected = at;
}

// 「本当に削除するか」。PDX の探索先の中に入れ子で開く。
void SettingsUi::BuildPdxRemoveWindow(Settings *settings) {
	if (pdx_.remove.ask) {
		pdx_.remove.ask = false;
		ImGui::OpenPopup(kPdxRemoveTitle);
	}

	pdx_.remove.open = ImGui::IsPopupOpen(kPdxRemoveTitle);
	if (!pdx_.remove.open) {
		pdx_.remove.close = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kPdxRemoveTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->pdxPaths;
	const bool valid = (pdx_.selected >= 0 && pdx_.selected < (int)list.size());
	ConfirmText(MsgF("PdxPath.RemoveConfirm",
	                 (valid && vfs_ != 0) ? vfs_->DisplayPath(list[pdx_.selected])
	                                      : std::string())
	                .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (valid) {
			list.erase(list.begin() + pdx_.selected);
			if (pdx_.selected >= (int)list.size()) pdx_.selected = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldPdxPaths;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || pdx_.remove.close) {
		pdx_.remove.close = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
