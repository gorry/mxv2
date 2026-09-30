// mxv2 - Chromecast へ送る（sdlcastg。memo/cast.md の手順 5）
//
// 演奏の画面と音を、LAN の Chromecast（TV の Cast 受信部など）へ送る。
// 仕組みは sdlcastg に任せ、ここは mxv2 からの使い方だけを受け持つ:
//   - 探す・つなぐ・やめる（つなぐ・やめるは別スレッド。メインループを止めない）
//   - 音: Player のオーディオコールバックから、装置へ渡すのと同じ PCM を渡す
//     （音量を掛けたあと）。送っている間は手元の音を消せる（既定で消す）
//   - 絵: 表示の直前に、キャンバスの範囲を読み出して渡す（ImGui のダイアログも
//     写る。2026-09-25、ユーザーの判断）。時刻は「いま聞こえている音」の
//     流れの上の時刻で、画面と音がそろう
//
// CMake の MXV2_CAST を付けずにビルドしたときは Available() が false を返し、
// ほかの関数は何もしない。
#ifndef MXV2_CAST_H
#define MXV2_CAST_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

namespace mxv2 {
namespace cast {

struct Device {
	std::string id;
	std::string name;
	std::string model;
	std::string address;
	int port;
	Device() : port(0) {}
};

enum class State {
	Idle = 0,     // 送っていない
	Connecting,   // つないでいる（受信側の返事待ち）
	Starting,     // 受信側でアプリを起こし、流れを読み込ませている
	Casting,      // 受信側で再生している
	Stopping,     // やめている（受信アプリを止めて切る）
};

// MXV2_CAST 付きでビルドしたか。
bool Available();

// 起動時と終了時。Shutdown は送っていれば止める（受信アプリも止める）。
void Init();
void Shutdown();

// 探す。見つかったもののうち、映像を出せるもの（画面の無いスピーカーは外す）を
// 名前の順に返す。
void StartDiscovery();
void StopDiscovery();
std::vector<Device> Devices();

// 送り始める / やめる。どちらもすぐ返り、裏で進む。
void Start(const Device &device);
void Stop();

// 毎フレーム、メインスレッドから。受信側で起きたことを返す。
enum class Event {
	None = 0,
	// 受信側が終わった（TV で別のアプリにした、受信アプリを閉じた、切れた）。
	// まだ手元の音を消したままなので、呼ぶ側は演奏を一時停止してから Stop() を
	// 呼ぶこと（ヘッドホンが抜けたときと同じ扱い。2026-09-25、ユーザーの指示）。
	Ended,
	// 受信側で一時停止された（TV のリモコンの PAUSE）。呼ぶ側は演奏を一時停止する。
	RemotePause,
	// 受信側で再開された（TV のリモコンの PLAY）。呼ぶ側は演奏を再開し、
	// ResumeRemote() を呼ぶ（止めていた間も流れは進んでいるので、TV に今の位置から
	// 読み込み直させる）。2026-09-25、ユーザーの指示。
	RemotePlay,
};
Event Poll();
// Event::RemotePlay のあとに。TV に流れを読み込み直させる（TV は数秒読み込み中になる）。
void ResumeRemote();
// 受信側で一時停止されたままか（手元で再開したら ResumeRemote() を呼ぶため）。
bool RemotePaused();

State GetState();
// 送り先の名前（送っていなければ空）。
std::string DeviceName();
// 最後に終わった理由（エラーや受信側の都合。無ければ空）。英語のまま。
std::string LastError();
// 最後に終わった理由に、利用者向けの説明（メッセージカタログのキー）があればそれ。
// 無ければ空（LastError をそのまま見せる）。
std::string LastErrorKey();
// 流れの様子（ログ・ダイアログ用。英語）。
std::string StatsText();

// 送る品質（[キャスト品質]。Settings::castQuality の段）。大きさ・fps・映像の
// ビットレートの組。次に送り始めるときから効く（送っている間は変えない）。
struct Quality {
	enum Type {
		Lowest = 0,  // 640x360・15fps・1Mbps、音声 128kbps
		Low,         // 854x480・30fps・2Mbps、音声 128kbps（Android の既定）
		Medium,      // 1280x720・30fps・2Mbps、音声 192kbps（パソコンの既定）
		High,        // 1920x1080・30fps・4Mbps、音声 192kbps
		Highest,     // 1920x1080・60fps・6Mbps、音声 192kbps
		Count
	};
};
struct QualityPreset {
	int width;
	int height;
	int fps;
	int videoKbps;
	int audioKbps;
};
// q の中身（範囲外なら端の段）。MXV2_CAST が無くても使える（ダイアログの表示用）。
const QualityPreset &GetQualityPreset(int q);
void SetQuality(int q);

// 送っている間、手元の音を消すか（既定は消す）。
void SetMuteLocal(bool mute);
// 送る映像の時刻を早める量 (ms)。すぐ効く。
void SetVideoAdvanceMs(int ms);

// Player のオーディオコールバックから（オーディオのスレッド）。
// pcm は装置へ渡す直前の int16 ステレオ、startFrame はその頭の
// Player::playedFrames()。送っていて手元の音を消す設定なら、pcm を無音にする。
void AudioTap(int16_t *pcm, int frames, int sampleRate, uint64_t startFrame);

// 1 フレームを描く前に BeginFrame、表示（SDL_RenderPresent）の直前に EndFrame。
// 送っている間は、ウィンドウと同じ大きさのテクスチャへ描かせ、EndFrame でキャンバスの
// 範囲（rect）を送る大きさへ GPU で縮めてから読み出して渡し、ウィンドウへ写す
// （sdlcastg_sdl.h の SDLCastG_BeginRendererFrame）。ウィンドウの大きさのまま読むと
// Xperia Ace III で 1 回 22ms かかり、30fps に届かなかった。
// visualFrame は**このフレームを描くのに使った** Player::visualFrame()
// （いま聞こえている音の位置）。読み出すときに取り直すと、描いてから読むまでの
// 間にオーディオのコールバックが挟まったとき、1 回ぶん（Android で 43ms）
// 遅い時刻が付く。
void BeginFrame(SDL_Renderer *renderer);
void EndFrame(SDL_Renderer *renderer, const SDL_Rect &rect, uint64_t visualFrame,
              int sampleRate);
// 描画の装置が作り直されたとき（SDL_RENDER_DEVICE_RESET など）。
void ResetRendererTextures();

}  // namespace cast
}  // namespace mxv2

#endif  // MXV2_CAST_H
