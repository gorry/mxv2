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
	if (!SyncModal(kPdxPathsTitle, &showPdxPaths_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 360.0f), placeCond());

	if (!ImGui::BeginPopupModal(kPdxPathsTitle, &showPdxPaths_,
	                            ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	if (vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	std::vector<std::string> &list = settings->pdxPaths;
	const int count = (int)list.size();
	if (pdxSelected_ >= count) pdxSelected_ = count - 1;
	if (pdxSelected_ < 0 && count > 0) pdxSelected_ = 0;

	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##pdxlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		if (count == 0) ImGui::TextDisabled("%s", Msg("PdxPath.Empty"));
		for (int i = 0; i < count; i++) {
			char label[512];
			snprintf(label, sizeof(label), "%s##pdx%d", vfs_->DisplayPath(list[i]).c_str(), i);
			if (ImGui::Selectable(label, i == pdxSelected_) && !dragMoved_) {
				pdxSelected_ = i;
				pdxError_.clear();
			}
			// 表示は見やすさ優先で DisplayPath なので、生の ref はここで見せる。
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].c_str());
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (pdxError_.empty()) {
		TextNote(Msg("PdxPath.Hint"));
	} else {
		TextError(pdxError_.c_str());
	}

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(pdxSelected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		std::swap(list[pdxSelected_], list[pdxSelected_ - 1]);
		pdxSelected_--;
		changedFields_ |= Settings::kFieldPdxPaths;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(pdxSelected_ < 0 || pdxSelected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		std::swap(list[pdxSelected_], list[pdxSelected_ + 1]);
		pdxSelected_++;
		changedFields_ |= Settings::kFieldPdxPaths;
	}
	ImGui::EndDisabled();

	// [追加…] はフォルダ選択ダイアログを経由して、選んだフォルダを選択位置へ
	// 挿し込む（ブックマークの設定と同じ往復）。開始位置は選んでいる探索先
	// （Build() の folderOpenPending_ のところ）。
	(void)filer;
	const bool full = (count >= Settings::kMaxPdxPaths);
	ImGui::SameLine();
	ImGui::BeginDisabled(full);
	if (ImGui::Button(Msg("Button.AddPdxPath"))) {
		pdxError_.clear();
		folderTarget_ = kFolderTargetPdx;
		folderReturnToPdx_ = true;
		folderOpenPending_ = true;
		showPdxPaths_ = false;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", full ? Msg("PdxPath.AddFull") : Msg("PdxPath.AddBrowse"));
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(pdxSelected_ < 0);
	if (ImGui::Button(Msg("Button.Remove"))) {
		pdxError_.clear();
		pdxOpenRemove_ = true;
	}
	ImGui::EndDisabled();

	BuildPdxRemoveWindow(settings);

	ImGui::EndPopup();
}

// 「本当に削除するか」。PDX の探索先の中に入れ子で開く。
void SettingsUi::BuildPdxRemoveWindow(Settings *settings) {
	if (pdxOpenRemove_) {
		pdxOpenRemove_ = false;
		ImGui::OpenPopup(kPdxRemoveTitle);
	}

	pdxRemoveOpen_ = ImGui::IsPopupOpen(kPdxRemoveTitle);
	if (!pdxRemoveOpen_) {
		pdxCloseRemove_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kPdxRemoveTitle, NULL,
	                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                                ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->pdxPaths;
	const bool valid = (pdxSelected_ >= 0 && pdxSelected_ < (int)list.size());
	ConfirmText(MsgF("PdxPath.RemoveConfirm",
	                 (valid && vfs_ != 0) ? vfs_->DisplayPath(list[pdxSelected_])
	                                      : std::string())
	                .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (valid) {
			list.erase(list.begin() + pdxSelected_);
			if (pdxSelected_ >= (int)list.size()) pdxSelected_ = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldPdxPaths;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || pdxCloseRemove_) {
		pdxCloseRemove_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
