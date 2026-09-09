// mxv2 - 端末の向きの固定（Android）
//
// スキンの縦横切り替え（screen_orientation.md）で「切り替えない」「起動時のみ
// 切り替える」を選んだときに、端末そのものの向きを固定する窓口。
//
// **なぜ SDL でやらないか。** SDL_HINT_ORIENTATIONS はウィンドウを作るときと
// SDL_SetWindowResizable のときにしか読まれず、しかも SDL_SetWindowResizable は
// 「フラグが実際に変わるとき」しかバックエンドを呼ばない。つまり実行中に
// 向きの許可を変える手段が SDL 側に無い。Activity.setRequestedOrientation を
// 直に叩くほうが素直で、上下反転を許すかどうかも正確に選べる。
//
// **Android 以外では何もしない**（Available() が false）。呼ぶのはメイン
// スレッドから（safaccess.cpp / nowplaying.cpp と同じ事情）。

#ifndef MXV2_ORIENTLOCK_H
#define MXV2_ORIENTLOCK_H

namespace mxv2 {
namespace orientlock {

// 端末に要求する向き。
enum Request {
	// 好きに回してよい（端末の回転ロックの設定には従う）。
	// Android の SCREEN_ORIENTATION_FULL_USER。既定の状態。
	kFree = 0,
	// 縦で固定。**上下反転は許す**（SCREEN_ORIENTATION_SENSOR_PORTRAIT）。
	kPortrait,
	// 横で固定。同じく左右どちら向きでもよい（SENSOR_LANDSCAPE）。
	kLandscape,
};

// この環境で向きを固定できるか。
bool Available();

// 要求を出す。同じ要求を続けて出しても害は無い（Java 側で弾く）。
void Set(Request request);

}  // namespace orientlock
}  // namespace mxv2

#endif  // MXV2_ORIENTLOCK_H
