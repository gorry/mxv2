// mxv2 - 小窓（ピクチャー・イン・ピクチャー。Android）
//
// ホームへ戻ったときなどに、曲名・状態・時刻と前の曲 / 一時停止・再開 /
// 次の曲のボタンを小窓で出し続ける（memo/pip.md）。小窓の中身は Java の
// View (PipView) が通知と同じ状態 (PlaybackBridge) から描くので、ここから
// 渡すのは「ホームへ戻ったときのふるまい」と「いま入って」の 2 つだけ。
//
// 小窓の間は SDL の面を隠すので、メインループには
// SDL_APP_WILLENTERBACKGROUND が届き、今のバックグラウンドの経路に入る。
// Active() はその前後の「ウィンドウの大きさが落ち着いていない」間も真を返すので、
// その間はキャンバスの作り直しとスキンの向きの切り替えをしないこと。
//
// **Android 以外では何もしない**（Available() が false）。呼ぶのはメイン
// スレッドから（orientlock.cpp と同じ事情）。

#ifndef MXV2_PIP_H
#define MXV2_PIP_H

namespace mxv2 {
namespace pip {

// この端末で小窓が使えるか（Android 8.0 以降で、機能を持っている端末）。
bool Available();

// ホームへ戻ったときのふるまい。値は Settings::PipMode。
void SetMode(int mode);

// いま小窓に入る（メニューの [小窓で表示]）。曲が無ければ何もしない。
void Enter();

// 小窓の中身を出している（か、出し入れの途中）。
bool Active();

}  // namespace pip
}  // namespace mxv2

#endif  // MXV2_PIP_H
