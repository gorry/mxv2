// mxv2 - 多重起動の抑止（旧 mxv と同じ「2 つめは 1 つめへ渡して終わる」）
//
// 旧 mxv (mxv.cpp の「多重起動チェック」) は、`FindWindow` で自分と同じ
// ウィンドウクラスを探し、見つかったら **共有メモリに argv[1] を書いて
// `WM_USER_START` を送り、自分は即終了**していた。受け取った側はその曲を
// 鳴らし、ファイラーをそのファイルへ移し、最後に `SetForegroundWindow` +
// `SetFocus` で前面に出る。mxv2 でも同じ振る舞いにする。
//
// **ここだけは OS ごとの実装になる。** SDL にプロセス間のやりとりは無い。
//   Windows … 名前付きミューテックスで「もう居るか」を見て、メッセージ
//              専用ウィンドウへ `WM_COPYDATA` でパスを渡す。
//   Android / iOS … **不要**。2 つめのプロセスはそもそも作られず、OS が
//              同じアクティビティを前面に戻す。
//   その他（Linux / macOS / Emscripten）… **未実装**（何もしないので、
//              今までどおり何個でも起動できる）。入れるときは、ユーザーの
//              ランタイムディレクトリ（`$XDG_RUNTIME_DIR` など）に
//              ロックファイルと UNIX ドメインソケットを置き、そこへパスを
//              流す形になる。Emscripten はタブごとに別世界なので、
//              そもそも抑止する意味が無い。

#ifndef MXV2_SINGLEINSTANCE_H
#define MXV2_SINGLEINSTANCE_H

#include <string>

namespace mxv2 {
namespace singleinstance {

// 起動のいちばん早いところで呼ぶ（SDL_Init より前でよい）。
//
// すでに動いている mxv2 があれば、`target`（開いてほしいもの。native な
// 絶対パス。空でもよい）をそちらへ渡して **true** を返す。呼んだ側はその
// まま終了すること。居なければ自分を「唯一のインスタンス」として登録して
// **false** を返す。
//
// 相手が見つからない・作れないなど、うまくいかないときは false を返す
// （起動できないより、2 つ出るほうがまし）。
bool HandOffToExisting(const std::string &target);

// 窓ができてから呼ぶ。以後、他のインスタンスからの依頼を Poll() で拾える。
// nativeWindowHandle は Screen::nativeWindowHandle()（前面へ出すために使う。
// 0 でもよく、そのときは前面へ出さないだけ）。
// HandOffToExisting() が false を返した起動でだけ意味を持つ。
void Start(void *nativeWindowHandle);

// 届いた依頼を 1 つ取り出す（毎フレーム、空になるまで呼ぶ）。
// **窓を前面へ出す（最小化なら戻す）のは受け取った時点で済ませてある**ので、
// 呼び出し側は target を開くだけでよい。target が空なら「前面へ出せ」だけの
// 依頼（曲を渡さずに 2 つめを起動したとき）。
bool Poll(std::string *target);

// 終了時に呼ぶ。
void Shutdown();

}  // namespace singleinstance
}  // namespace mxv2

#endif  // MXV2_SINGLEINSTANCE_H
