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
void PdxPathsDialog::Build(SettingsUi &ui, Settings *settings, Filer *filer) {
	if (!ui.BeginModal(kPdxPathsTitle, &show_, 460.0f, 360.0f)) return;
	if (ui.vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	std::vector<std::string> &list = settings->pdxPaths;
	const int count = (int)list.size();
	if (selected_ >= count) selected_ = count - 1;
	if (selected_ < 0 && count > 0) selected_ = 0;

	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##pdxlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		if (count == 0) ImGui::TextDisabled("%s", Msg("PdxPath.Empty"));
		for (int i = 0; i < count; i++) {
			char label[512];
			snprintf(label, sizeof(label), "%s##pdx%d", ui.vfs_->DisplayPath(list[i]).c_str(), i);
			if (ImGui::Selectable(label, i == selected_) && !ui.dragMoved_) {
				selected_ = i;
				error_.clear();
			}
			// 表示は見やすさ優先で DisplayPath なので、生の ref はここで見せる。
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].c_str());
		}
		DragToScroll(&ui.dragScroll_, &ui.dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (error_.empty()) {
		TextNote(Msg("PdxPath.Hint"));
	} else {
		TextError(error_.c_str());
	}

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(selected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		std::swap(list[selected_], list[selected_ - 1]);
		selected_--;
		ui.changedFields_ |= Settings::kFieldPdxPaths;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selected_ < 0 || selected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		std::swap(list[selected_], list[selected_ + 1]);
		selected_++;
		ui.changedFields_ |= Settings::kFieldPdxPaths;
	}
	ImGui::EndDisabled();

	// [追加…] はフォルダ選択ダイアログを経由して、選んだフォルダを選択位置へ
	// 挿し込む（ブックマークの設定と同じ往復）。開始位置は選んでいる探索先
	// （Build() の folder_.openPending_ のところ）。
	(void)filer;
	const bool full = (count >= Settings::kMaxPdxPaths);
	ImGui::SameLine();
	ImGui::BeginDisabled(full);
	if (ImGui::Button(Msg("Button.AddPdxPath"))) {
		error_.clear();
		ui.OpenFolderFor(FolderDialog::kFolderTargetPdx, FolderDialog::kFolderReturnPdx);
		show_ = false;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", full ? Msg("PdxPath.AddFull") : Msg("PdxPath.AddBrowse"));
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(selected_ < 0);
	if (ImGui::Button(Msg("Button.Remove"))) {
		error_.clear();
		remove_.ask = true;
	}
	ImGui::EndDisabled();

	BuildRemoveWindow(ui, settings);

	ImGui::EndPopup();
}

void PdxPathsDialog::OpenFromSettings(SettingsUi &ui) {
	selected_ = 0;
	error_.clear();
	handoff_.Start();
	ui.SetSettingsVisible(false);
}

void PdxPathsDialog::Insert(std::vector<std::string> *list, const std::string &ref) {
	const int count = (int)list->size();
	const int at = (selected_ >= 0 && selected_ <= count) ? selected_ : count;
	list->insert(list->begin() + at, ref);
	selected_ = at;
}

// 「本当に削除するか」。PDX の探索先の中に入れ子で開く。
void PdxPathsDialog::BuildRemoveWindow(SettingsUi &ui, Settings *settings) {
	if (!remove_.Begin(ui, kPdxRemoveTitle)) return;

	std::vector<std::string> &list = settings->pdxPaths;
	const bool valid = (selected_ >= 0 && selected_ < (int)list.size());
	ConfirmText(MsgF("PdxPath.RemoveConfirm",
	                 (valid && ui.vfs_ != 0) ? ui.vfs_->DisplayPath(list[selected_])
	                                      : std::string())
	                .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (valid) {
			list.erase(list.begin() + selected_);
			if (selected_ >= (int)list.size()) selected_ = (int)list.size() - 1;
			ui.changedFields_ |= Settings::kFieldPdxPaths;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	remove_.CancelButton(Msg("Button.Cancel"));
	ImGui::EndPopup();
}

}  // namespace mxv2
