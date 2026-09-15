// mxv2 - ゲームパッド（gamepad.h を見ること）

#include "gamepad.h"

#include <cstdio>

#include "message.h"

namespace mxv2 {

Gamepad::Gamepad() : dpadDir_(0), stickDir_(0), heldDir_(0), nextRepeatMs_(0) {}

Gamepad::~Gamepad() {
	for (size_t i = 0; i < pads_.size(); i++) SDL_GameControllerClose(pads_[i]);
	pads_.clear();
}

bool Gamepad::Init(std::string *err) {
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
		if (err != 0) *err = SDL_GetError();
		return false;
	}
	// 起動時点で繋がっているぶんは SDL_CONTROLLERDEVICEADDED が届くので、
	// ここで数えて開く必要は無い。
	return true;
}

void Gamepad::Open(int deviceIndex) {
	if (!SDL_IsGameController(deviceIndex)) return;  // マッピングの無いジョイスティックは使わない
	SDL_GameController *pad = SDL_GameControllerOpen(deviceIndex);
	if (pad == 0) return;
	pads_.push_back(pad);
	const char *name = SDL_GameControllerName(pad);
	printf("gamepad  : %s\n", MsgF("Log.GamepadOpened", (name != 0) ? name : "?").c_str());
	fflush(stdout);
}

void Gamepad::Close(SDL_JoystickID id) {
	for (size_t i = 0; i < pads_.size(); i++) {
		SDL_Joystick *js = SDL_GameControllerGetJoystick(pads_[i]);
		if (js == 0 || SDL_JoystickInstanceID(js) != id) continue;
		SDL_GameControllerClose(pads_[i]);
		pads_.erase(pads_.begin() + (long)i);
		break;
	}
	// 抜かれたら押しっぱなしも解く。
	dpadDir_ = 0;
	stickDir_ = 0;
	UpdateHeldDir(SDL_GetTicks());
}

void Gamepad::PushKey(SDL_Keycode key, bool down, bool repeat) {
	SDL_Event ev;
	SDL_zero(ev);
	ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.timestamp = SDL_GetTicks();
	SDL_Window *focus = SDL_GetKeyboardFocus();
	ev.key.windowID = (focus != 0) ? SDL_GetWindowID(focus) : 0;
	ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.key.repeat = repeat ? 1 : 0;
	ev.key.keysym.scancode = SDL_GetScancodeFromKey(key);
	ev.key.keysym.sym = key;
	ev.key.keysym.mod = KMOD_NONE;
	SDL_PushEvent(&ev);
}

void Gamepad::UpdateHeldDir(uint32_t nowMs) {
	// 十字キーを優先し、押していなければスティック。
	const int dir = (dpadDir_ != 0) ? dpadDir_ : stickDir_;
	if (dir == heldDir_) return;
	if (heldDir_ != 0) PushKey(KeyForDir(heldDir_), false, false);
	heldDir_ = dir;
	if (heldDir_ != 0) {
		PushKey(KeyForDir(heldDir_), true, false);
		nextRepeatMs_ = nowMs + kRepeatDelayMs;
	}
}

void Gamepad::Handle(const SDL_Event &ev) {
	switch (ev.type) {
		case SDL_CONTROLLERDEVICEADDED:
			Open(ev.cdevice.which);  // ここでは which は装置番号
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			Close(ev.cdevice.which);  // ここでは which はインスタンス id
			break;

		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP: {
			const bool down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
			switch (ev.cbutton.button) {
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
					if (down) dpadDir_ = -1;
					else if (dpadDir_ < 0) dpadDir_ = 0;
					UpdateHeldDir(ev.cbutton.timestamp);
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					if (down) dpadDir_ = 1;
					else if (dpadDir_ > 0) dpadDir_ = 0;
					UpdateHeldDir(ev.cbutton.timestamp);
					break;
				// 第 1〜3 ボタン。左右 (DPAD_LEFT / RIGHT) は空けてある。
				case SDL_CONTROLLER_BUTTON_A:
					PushKey(SDLK_RETURN, down, false);
					break;
				case SDL_CONTROLLER_BUTTON_B:
					PushKey(SDLK_BACKSPACE, down, false);
					break;
				case SDL_CONTROLLER_BUTTON_X:
					PushKey(SDLK_m, down, false);
					break;
				default:
					break;
			}
			break;
		}

		case SDL_CONTROLLERAXISMOTION: {
			if (ev.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY) break;
			const int v = ev.caxis.value;  // 上が負
			int dir = stickDir_;
			if (v <= -kStickPressThreshold) dir = -1;
			else if (v >= kStickPressThreshold) dir = 1;
			else if (v > -kStickReleaseThreshold && v < kStickReleaseThreshold) dir = 0;
			if (dir != stickDir_) {
				stickDir_ = dir;
				UpdateHeldDir(ev.caxis.timestamp);
			}
			break;
		}

		default:
			break;
	}
}

void Gamepad::Poll(uint32_t nowMs) {
	if (heldDir_ == 0) return;
	if ((int32_t)(nowMs - nextRepeatMs_) < 0) return;
	PushKey(KeyForDir(heldDir_), true, true);
	nextRepeatMs_ = nowMs + kRepeatIntervalMs;
}

}  // namespace mxv2
