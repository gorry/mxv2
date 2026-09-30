// mxv2 - 設定 UI: [画面の遅れ] / [遅延時間] ダイアログ
//
// [mxv2 の設定] の [演奏] と [Bluetooth（機器名）] の [設定…] から開く。設定
// ウィンドウには今の値と [設定…] だけを出し、スライダーはこのダイアログに
// 置く。5 秒近い遅れの機器があり、1 本のスライダーでは合わせられないので
// 「秒」と「ミリ秒」の 2 本に分けた。**鍵盤を見ながら合わせられるよう、画面の
// 中央でなくファイラーの側に寄せて出す**（2026-09-28、ユーザーの指示）。
// 開き方と戻り方は [PDX の探索先] と同じ（設定ウィンドウが閉じきってから開き、
// 閉じたら戻る。配線は SettingsUi::Build）。実装は settingsui_latency.cpp。
// クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_LATENCY_H
#define MXV2_SETTINGSUI_LATENCY_H

#include <string>

#include "settingsui_dialog.h"

namespace mxv2 {

class DrawScreen;
class Player;
class Screen;
class SettingsUi;
struct Settings;

class LatencyDialog : public ModalDialog {
public:
	enum Target {
		kLatencyTargetDisplay = 0,  // [演奏] の画面の遅れ（手動のとき）
		kLatencyTargetBluetooth,    // いまつないでいる Bluetooth の機器の遅延時間
	};

	LatencyDialog() : target_(kLatencyTargetDisplay) {}

	// [設定…] から。設定ウィンドウを閉じ、閉じきったら開く。device は
	// kLatencyTargetBluetooth のときの機器名。
	void Open(SettingsUi &ui, int target, const std::string &device);
	// ダイアログの題名。target で変わる。
	const char *Title() const;
	void Build(SettingsUi &ui, Settings *settings, DrawScreen *draw, Player *player,
	           Screen *screen);

private:
	friend class SettingsUi;

	Handoff handoff_;     // 設定ウィンドウとの往復
	int target_;          // Target
	std::string device_;  // kLatencyTargetBluetooth の機器名（開いたときのもの）
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_LATENCY_H
