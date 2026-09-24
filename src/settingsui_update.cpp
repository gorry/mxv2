// mxv2 - 設定 UI: 更新チェック（[mxv2 の設定] の [通信] と、結果のダイアログ）
//
// 通信と「いつ確かめるか」は main.cpp（updatecheck.h）が持ち、ここは見せる
// だけ。仕様は 2026-09-24 のユーザーの指示:
//   ・1 日 1 回、別スレッドで GitHub Releases API を見る
//   ・新しい版があればダイアログで知らせ、リリースページを開くボタンを出す
//   ・自動の確かめで失敗しても知らせない（ログだけ）
//   ・[今すぐ更新チェックを行う] は 24 時間の間隔を無視し、失敗も知らせる

#include "settingsui.h"

#include <cstdio>
#include <ctime>

#include "imgui.h"

#include "settingsui_internal.h"

#include "appprofile.h"  // CMake が Profile.ini から生成する
#include "settings.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// UNIX 時間をその環境の地方時で "2026-09-25 22:30" にする。
std::string LocalTimeText(long long t) {
	const time_t tt = (time_t)t;
	struct tm tmv;
#if defined(_WIN32)
	if (localtime_s(&tmv, &tt) != 0) return std::string();
#else
	if (localtime_r(&tt, &tmv) == 0) return std::string();
#endif
	char buf[32];
	if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv) == 0) return std::string();
	return buf;
}

}  // namespace

// [mxv2 の設定] の [通信]。更新チェックが使えない環境では見出しごと出さない。
void SettingsUi::BuildNetworkGroup(Settings *settings) {
	if (!updateAvailable_) return;
	if (!GroupHeader(Msg("Settings.Network"))) return;

	bool on = settings->updateCheck;
	if (ImGui::Checkbox(Msg("Settings.UpdateCheck"), &on)) {
		settings->updateCheck = on;
		changedFields_ |= Settings::kFieldUpdateCheck;
	}

	// 次の更新チェックの時刻。確かめている最中・OFF・もう過ぎている
	// （使える状態になりしだい確かめる）ときは、それぞれの言い方にする。
	if (updateRunning_) {
		TextNote(Msg("Settings.UpdateRunning"));
	} else if (!settings->updateCheck) {
		TextNote(Msg("Settings.UpdateOff"));
	} else if (settings->nextUpdateCheck <= (long long)time(0)) {
		TextNote(Msg("Settings.UpdateNextSoon"));
	} else {
		TextNote(MsgF("Settings.UpdateNext", LocalTimeText(settings->nextUpdateCheck)).c_str());
	}

	// 24 時間の間隔を無視してすぐ確かめる。[更新チェックを行う] が OFF でも
	// 押せる（明示の操作なので）。結果は成功・失敗とも知らせる。
	ImGui::BeginDisabled(updateRunning_);
	if (ImGui::Button(Msg("Settings.UpdateCheckNow"))) updateCheckNow_ = true;
	ImGui::EndDisabled();
	GroupTrailingSpace();
}

// 預かった結果をどこで開くか決める。
//   [今すぐ…] の結果で、[mxv2 の設定] が開いたまま … その中に重ねて開く
//   それ以外 … ほかのダイアログがすべて閉じるのを待って単独で開く
void SettingsUi::ScheduleUpdateWindow() {
	// 入れ子で開いていたのに [mxv2 の設定] が閉じた。子のポップアップも
	// ImGui が一緒に閉じているので、こちらの控えを戻す。
	if (updateNested_ && !visible_ && !ImGui::IsPopupOpen(kSettingsTitle)) {
		updateNested_ = false;
		updateAsk_ = false;
		updateOpen_ = false;
		updateClose_ = false;
	}
	if (!updatePending_ || updateOpen_ || updateAsk_) return;
	if (updateResult_.manual && visible_) {
		updateNested_ = true;
		updateAsk_ = true;
		updatePending_ = false;
		return;
	}
	if (!anyDialogOpen()) {
		updateNested_ = false;
		updateAsk_ = true;
		updatePending_ = false;
	}
}

// 結果のダイアログ。新しい版があるときは [リリースページを開く] を添える。
void SettingsUi::BuildUpdateWindow() {
	if (updateAsk_) {
		updateAsk_ = false;
		ImGui::OpenPopup(kUpdateTitle);
	}
	updateOpen_ = ImGui::IsPopupOpen(kUpdateTitle);
	if (!updateOpen_) {
		updateClose_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kUpdateTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const UpdateResult &r = updateResult_;
	switch (r.status) {
		case UpdateResult::kNewer:
			ConfirmText(MsgF("Update.Newer", r.latest).c_str());
			ConfirmNote(MsgF("Update.Current", MXV2_APP_VERSION).c_str());
			break;
		case UpdateResult::kLatest:
			ConfirmText(MsgF("Update.Latest", MXV2_APP_VERSION).c_str());
			break;
		default:
			ConfirmText(Msg("Update.Failed"));
			// 理由は英語のまま（OS や通信の誤り）。調べるときの手掛かり。
			if (!r.error.empty()) ConfirmNote(r.error.c_str());
			break;
	}
	ImGui::Separator();

	bool close = false;
	if (r.status == UpdateResult::kNewer) {
		if (ImGui::Button(Msg("Button.OpenReleasePage"))) {
			OpenUrl(r.pageUrl.c_str());
			close = true;
		}
		SameLineOrWrap(Msg("Button.Close"));
	}
	if (ImGui::Button(Msg("Button.Close")) || updateClose_) close = true;
	if (close) {
		updateClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
