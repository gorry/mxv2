// mxv2_uitest - 設定 UI のダイアログの行き来を、本物のアプリで確かめる
//
// mxv2 と同じソースを MXV2_UITEST 付きでビルドし、main.cpp のメインループから
// 1 フレームに 1 回 AfterBuild を呼ばせる。キーは SDL のイベントとして積み
// （本物と同じ道を通る）、マウスは ImGui へ直に渡す。どの部品がどこにあるかは
// ImGui のテストエンジン用のフック（IMGUI_ENABLE_TEST_ENGINE）で集める。
// 走らせ方は Makefile の test-ui（SDL の dummy ドライバで、ウィンドウは出ない）。

#ifndef MXV2_TOOLS_UITEST_H
#define MXV2_TOOLS_UITEST_H

#include <SDL.h>

namespace mxv2 {

class SettingsUi;

namespace uitest {

// SettingsUi::Build の直後に毎フレーム呼ぶ。true を返したら終える。
bool AfterBuild(SettingsUi *ui, SDL_Window *window);

// プロセスの終了コード。すべての手順が通れば 0。
int ExitCode();

}  // namespace uitest
}  // namespace mxv2

#endif  // MXV2_TOOLS_UITEST_H
