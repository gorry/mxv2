// mxv2 - キーとマウスの割り当て（main.cpp から切り出し）
//
// キーの一覧は message.ini の [HelpKeys]（[操作方法] と -h に出るもの）と
// 合わせておくこと。

#ifndef MXV2_KEYBIND_H
#define MXV2_KEYBIND_H

#include <string>

#include <SDL.h>

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
class SettingsUi;

namespace app {

struct PlayContext;

// 操作が触るもの一式。メインループの持ち物を指しているだけ。
struct InputTargets {
	const PlayContext *ctx;
	mxv2::Filer *filer;
	mxv2::SettingsUi *ui;
	mxv2::Player *player;
	mxv2::DrawScreen *draw;
	bool *autoNext;         // CONT
	bool *autoRepeat;       // REPEAT
	bool *chromeRefresh;
	bool *fileListRefresh;
	std::string *currentPath;
};

// マウス・タッチの操作 (MouseInput::Handle の戻り値) を実行する。
void HandleMouseRequest(int request, const InputTargets &t);
// キーが押された。
void HandleKeyDown(const SDL_KeyboardEvent &ev, const InputTargets &t);

}  // namespace app
}  // namespace mxv2

#endif  // MXV2_KEYBIND_H
