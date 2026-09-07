// mxv2 - 演奏状態の通知（Android の通知領域）
//
// バックグラウンドへ回っても演奏を続けるので、いま何が鳴っているかを OS の
// 通知で見せ、そこから前後の曲・一時停止・停止を操作できるようにする。
// 通知をタップするとアプリの画面へ戻る。
//
// Android では前面サービス (net.gorry.mxv2.PlaybackService) が実体で、
// ここはその窓口。**Android 以外では何もしない**（Available() が false）。
//
// 文言は Java 側に持たせず、message.ini から引いたものを SetLabels で渡す
// （[Notify] の一式）。呼ぶのはメインスレッドから。

#ifndef MXV2_NOWPLAYING_H
#define MXV2_NOWPLAYING_H

#include <cstdint>
#include <string>

namespace mxv2 {
namespace nowplaying {

// 通知（とヘッドホン・他アプリの都合）から届く要求。
enum Request {
	kRequestNone = 0,
	kRequestPlay,   // 一時停止を解く
	kRequestPause,  // 一時停止する
	kRequestPrev,
	kRequestNext,
	kRequestStop,
	// 他のアプリに音を譲った / ヘッドホンが抜けた。鳴っていたら止める。
	kRequestFocusLost,
	// 音を返してもらった。kRequestFocusLost で止めたぶんだけ再開する。
	kRequestFocusGained,
};

// 通知に出す文言。SetLabels は Update より先に一度だけ呼ぶ。
struct Labels {
	std::string channel;      // 通知チャンネルの名前（端末の設定に出る）
	std::string channelDesc;  // その説明
	std::string prev;         // ボタン
	std::string play;
	std::string pause;
	std::string next;
	std::string stop;
};

// いま見せたい状態。
struct State {
	bool active;   // 曲を持っている（false なら通知を消す）
	bool playing;  // 鳴っている（false は一時停止）
	std::string title;
	std::string text;  // 状態の行（「演奏中」＋ CONT / REPEAT など）
	uint32_t posMs;
	uint32_t durMs;

	State() : active(false), playing(false), posMs(0), durMs(0) {}
};

// この環境で通知を出せるか。
bool Available();

void SetLabels(const Labels &labels);

// 状態を伝える。**変わっていなければ何もしない**ので毎フレーム呼んでよい
// （位置 posMs だけの変化では出し直さない。通知側が自分で進める）。
void Update(const State &state);

// 通知を消してサービスを止める。終了時に呼ぶ。
void Shutdown();

// 通知から届いた要求を 1 つ取り出す。無ければ kRequestNone。
Request TakeRequest();

}  // namespace nowplaying
}  // namespace mxv2

#endif  // MXV2_NOWPLAYING_H
