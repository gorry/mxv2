// mxv2 - ゲームパッド（SDL_GameController）でのファイラー操作
//
// 上下でカーソル、第 1 ボタン (A) で ENTER、第 2 ボタン (B) で BACKSPACE、
// 第 3 ボタン (X) で M（ブックマークの一覧）。左右は空けてある
// （2026-09-15、ユーザーの指示）。
//
// **コントローラの入力は、対応するキーの SDL_KEYDOWN / SDL_KEYUP に写して
// イベントキューへ積み直す**（SDL_PushEvent）。ファイラーの操作・ダイアログが
// 開いているときの扱い・チュートリアルの幕（AllowsKey）は全部キーの経路に
// あるので、そこへ合流させれば同じ振る舞いになり、二重に書かずに済む。
// 積んだイベントは同じ SDL_PollEvent のループの中で続けて取り出される。
//
// 上下はキーボードと同じくオートリピートする（OS のキーリピートは合成した
// イベントには無いので、ここで作る）。ボタン 3 つは繰り返さない。
// 十字キーと左スティックの上下はどちらも同じ「上下」として扱う。

#ifndef MXV2_GAMEPAD_H
#define MXV2_GAMEPAD_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

namespace mxv2 {

class Gamepad {
public:
	Gamepad();
	~Gamepad();

	// SDL_INIT_GAMECONTROLLER を開く。失敗しても致命ではない（ゲームパッドが
	// 使えないだけ）。false のとき *err に SDL のエラー文。
	bool Init(std::string *err);

	// コントローラのイベントを 1 つ処理する。キーに写せたらキーイベントを積む。
	// コントローラ以外のイベントは無視する。
	void Handle(const SDL_Event &ev);

	// 上下の押し続けの繰り返し。毎フレーム 1 回呼ぶ。
	void Poll(uint32_t nowMs);

	// 開いているコントローラの数（ログ用）。
	int count() const { return (int)pads_.size(); }

	// 上下のオートリピート。押してから kRepeatDelayMs 待ち、以降
	// kRepeatIntervalMs ごと。Windows のキーリピート（既定 250ms / 30ms 前後）に
	// 近い値で、ファイラーの行送りとして落ち着く速さ。
	static const uint32_t kRepeatDelayMs = 300;
	static const uint32_t kRepeatIntervalMs = 60;

	// 左スティックの上下を「押した」とみなす閾値（±32767 のうち）と、
	// 「離した」とみなす閾値。ヒステリシスを持たせて境目でばたつかないように。
	static const int kStickPressThreshold = 16384;
	static const int kStickReleaseThreshold = 8192;

private:
	void Open(int deviceIndex);
	void Close(SDL_JoystickID id);

	// 十字キーとスティックの状態から「今押している上下」を決め直し、
	// 変わっていればキーを積む。dir は -1（上）/ 0 / +1（下）。
	void UpdateHeldDir(uint32_t nowMs);
	static void PushKey(SDL_Keycode key, bool down, bool repeat);
	static SDL_Keycode KeyForDir(int dir) { return (dir < 0) ? SDLK_UP : SDLK_DOWN; }

	std::vector<SDL_GameController *> pads_;
	int dpadDir_;    // 十字キーの上下（-1 / 0 / +1）
	int stickDir_;   // 左スティックの上下
	int heldDir_;    // 今キーを押していることにしている上下
	uint32_t nextRepeatMs_;
};

}  // namespace mxv2

#endif  // MXV2_GAMEPAD_H
