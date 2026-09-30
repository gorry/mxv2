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
enum class Request {
	None = 0,
	Play,   // 一時停止を解く
	Pause,  // 一時停止する
	Prev,
	Next,
	Stop,
	// 他のアプリに音を譲った / ヘッドホンが抜けた。鳴っていたら止める。
	FocusLost,
	// 音を返してもらった。FocusLost で止めたぶんだけ再開する。
	FocusGained,
	// 早送り・巻き戻し（Bluetooth 側の長押しなど）。決まった幅だけ飛ぶ。
	SeekForward,
	SeekBack,
	// 位置を指定して飛ぶ（車の画面・ロック画面の位置の棒）。位置は TakeSeekMs。
	SeekTo,
	// 出力先が外れた (BECOMING_NOISY。Bluetooth が切れた・ヘッドホンが抜けた)。
	// 鳴っていたら止める。
	RouteLost,
	// Bluetooth の出力機器がつながった。設定の [Bluetooth] ResumeOnReconnect が
	// 入っていれば、RouteLost で止めたぶんだけ再開する。
	BluetoothConnected,
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
	std::string text;  // 状態の行（「演奏中」＋ CONT / REPEAT など）。通知の本文だけ
	// MediaSession のアーティスト欄とアルバム欄（車や AV アンプの画面に出る）。
	// MDX には曲名しか無いので、アーティストは曲のファイル名、アルバムは
	// フォルダ名を当てる（2026-09-28、ユーザー確認）。
	std::string artist;
	std::string album;
	// フォルダの中で何曲目か（1 から）と曲数。分からなければ 0。
	int trackNumber;
	int trackCount;
	uint32_t posMs;
	uint32_t durMs;

	State()
	    : active(false), playing(false), trackNumber(0), trackCount(0), posMs(0), durMs(0) {}
};

// この環境で通知を出せるか。
bool Available();

void SetLabels(const Labels &labels);

// 状態を伝える。**変わっていなければ何もしない**ので毎フレーム呼んでよい
// （位置 posMs だけの変化では出し直さない。通知側が自分で進める）。
void Update(const State &state);

// 通知を消してサービスを止める。終了時に呼ぶ。
void Shutdown();

// 通知から届いた要求を 1 つ取り出す。無ければ Request::None。
Request TakeRequest();
// Request::SeekTo の行き先 (ms)。
uint32_t TakeSeekMs();

}  // namespace nowplaying
}  // namespace mxv2

#endif  // MXV2_NOWPLAYING_H
