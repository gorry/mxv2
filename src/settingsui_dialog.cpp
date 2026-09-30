// mxv2 - 設定 UI: ダイアログのクラスが共通に使う小さな型（settingsui_dialog.h）

#include "settingsui_dialog.h"

#include "imgui.h"

#include "settingsui.h"
#include "settingsui_internal.h"

namespace mxv2 {

using namespace settingsui;

bool NestedPopup::Begin(SettingsUi &ui, const char *title) {
	if (ask) {
		ask = false;
		ImGui::OpenPopup(title);
	}
	open = ImGui::IsPopupOpen(title);
	if (!open) {
		close = false;
		return false;
	}
	CenterNextWindow(ui.placeCond());
	return ImGui::BeginPopupModal(title, NULL,
	                              ui.DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize);
}

bool NestedPopup::CancelButton(const char *label) {
	const bool pressed = ImGui::Button(label);
	if (!pressed && !close) return false;
	close = false;
	ImGui::CloseCurrentPopup();
	return true;
}

bool NestedPopup::TakeClose() {
	if (!close) return false;
	close = false;
	ImGui::CloseCurrentPopup();
	return true;
}

}  // namespace mxv2
