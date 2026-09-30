// mxv2 - 設定 UI: [画面の遅れ] / [遅延時間] ダイアログ
//
// [mxv2 の設定] の [演奏] と [Bluetooth（機器名）] の [設定…] から開く。
// settingsui_settings.cpp から切り出した（2026-09-30）。

#include "settingsui.h"

#include <cfloat>

#include "imgui.h"

#include "settingsui_internal.h"

#include "drawscreen.h"
#include "player.h"
#include "screen.h"
#include "settings.h"
#include "skin.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// 次のダイアログを、ファイラーの側へ寄せて置く。鍵盤（ファイラー以外の側）を
// 見ながら値を動かせるように。ファイラーの矩形の中央に置き、はみ出すほど
// 大きければ画面の端（ファイラーの外側の辺）へ寄せる。
void PlaceNextWindowNearFiler(const DrawScreen *draw, const Screen *screen, ImGuiCond cond) {
	const Skin &l = draw->layout();
	const SDL_Rect cr = screen->CanvasRect();
	const float sx = (float)cr.w / (float)draw->width();
	const float sy = (float)cr.h / (float)draw->height();
	// ファイラーの矩形（一覧とスクロールバーを合わせたもの）を出力の座標へ。
	const float fx = cr.x + (float)l.fileListX * sx;
	const float fy = cr.y + (float)l.fileListY * sy;
	const float fw = (float)(l.fileListW + l.scrollW) * sx;
	const float fh = (float)l.fileListH * sy;
	ImVec2 pos(fx + fw * 0.5f, fy + fh * 0.5f);
	ImVec2 pivot(0.5f, 0.5f);
	switch (l.filerSide) {
		case FilerSide::Top:
			pos.y = (float)cr.y;
			pivot.y = 0.0f;
			break;
		case FilerSide::Left:
			pos.x = (float)cr.x;
			pivot.x = 0.0f;
			break;
		case FilerSide::Right:
			pos.x = (float)(cr.x + cr.w);
			pivot.x = 1.0f;
			break;
		case FilerSide::Bottom:
		default:
			pos.y = (float)(cr.y + cr.h);
			pivot.y = 1.0f;
			break;
	}
	ImGui::SetNextWindowPos(pos, cond, pivot);
}

// ms を「秒」と「ミリ秒」の 2 本のスライダーの値へ。負は秒 0 のミリ秒側へ入れる
// （手動の画面の遅れだけが負を取る。Settings::kLatencyMsMin）。
void SplitLatency(int ms, int *sec, int *milli) {
	if (ms < 0) {
		*sec = 0;
		*milli = ms;
	} else {
		*sec = ms / 1000;
		*milli = ms % 1000;
	}
}

}  // namespace

const char *LatencyDialog::Title() const {
	return (target_ == Target::Bluetooth) ? kBtLatencyTitle : kDisplayLatencyTitle;
}

void LatencyDialog::Open(SettingsUi &ui, Target target, const std::string &device) {
	target_ = target;
	device_ = device;
	handoff_.Start();
	ui.SetSettingsVisible(false);
}

void LatencyDialog::PollOpen() {
	handoff_.OpenWhenCallerClosed(&show_, kSettingsTitle);
}

bool LatencyDialog::TakeReturn() {
	return handoff_.TakeReturn(show_, Title(), false);
}

void LatencyDialog::Build(SettingsUi &ui, Settings *settings, DrawScreen *draw, Player *player,
                          Screen *screen) {
	const char *title = Title();
	if (!ui.SyncModal(title, &show_)) return;

	PlaceNextWindowNearFiler(draw, screen, ui.placeCond());
	ImGui::SetNextWindowSize(ui.DialogSize(360.0f, 0.0f), ui.placeCond());
	if (!ui.BeginDialog(title, &show_,
	                            ui.DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const bool bluetooth = (target_ == Target::Bluetooth);
	Settings::BtDevice device;
	int ms = 0;
	int minMs = 0;
	int maxMs = 0;
	if (bluetooth) {
		device = settings->BluetoothDeviceFor(device_);
		ms = device.latencyMs;
		maxMs = Settings::kBluetoothLatencyMsMax;
		TextNote(MsgF("Settings.LatencyDialogDevice", device_).c_str());
	} else {
		ms = settings->latencyMs;
		minMs = Settings::kLatencyMsMin;
		maxMs = Settings::kLatencyMsMax;
	}

	int sec = 0;
	int milli = 0;
	SplitLatency(ms, &sec, &milli);
	bool changed = false;
	ImGui::SetNextItemWidth(-ImGui::CalcTextSize(Msg("Settings.LatencyMillis")).x -
	                        ImGui::GetStyle().ItemInnerSpacing.x);
	changed |= ImGui::SliderInt(Msg("Settings.LatencySeconds"), &sec, 0, maxMs / 1000, "%d");
	ImGui::SetNextItemWidth(-ImGui::CalcTextSize(Msg("Settings.LatencyMillis")).x -
	                        ImGui::GetStyle().ItemInnerSpacing.x);
	changed |= ImGui::SliderInt(Msg("Settings.LatencyMillis"), &milli, (minMs < 0) ? minMs : 0,
	                            999, "%d");
	if (changed) {
		ms = sec * 1000 + milli;
		if (ms < minMs) ms = minMs;
		if (ms > maxMs) ms = maxMs;
		if (bluetooth) {
			device.latencyMs = ms;
			settings->SetBluetoothDevice(device);
			ui.changedFields_ |= Settings::Field::Bluetooth;
		} else {
			settings->latencyMs = ms;
			ui.changedFields_ |= Settings::Field::Latency;
			player->SetDisplayLatency(false, MsToFrames(ms, player));
		}
	}
	TextNote(MsgF("Settings.LatencyDialogTotal", MsgNum(bluetooth ? "%d" : "%+d", ms)).c_str());
	TextNote(Msg("Settings.LatencyDialogHint"));

	if (ImGui::Button(Msg("Button.Close"), ImVec2(-FLT_MIN, 0.0f))) show_ = false;

	ImGui::EndPopup();
}

}  // namespace mxv2
