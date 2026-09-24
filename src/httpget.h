// mxv2 - HTTPS で 1 つの URL を取ってくる（更新チェック用）
//
// 外部ライブラリは使わず、OS の持っているものを呼ぶ:
//   Windows … WinHTTP（TLS も OS 任せ）
//   Android … Java の HttpURLConnection（net.gorry.mxv2.HttpBridge を JNI で）
// それ以外の環境では「使えない」と返す。
//
// 呼ぶのは作業スレッドからでよいが、**Prepare() だけはメインスレッドから**
// 先に呼ぶこと（Android の FindClass はメインスレッドでないとアプリの
// クラスが見えない。safaccess.cpp と同じ事情）。

#ifndef MXV2_HTTPGET_H
#define MXV2_HTTPGET_H

#include <string>

namespace mxv2 {
namespace httpget {

// この環境で HTTP の取得ができるか。
bool Available();

// 下ごしらえ（メインスレッドから）。何度呼んでもよい。
void Prepare();

// url を GET する。accept は Accept ヘッダの値（空なら付けない）。
// 応答が 200 以外でも本文は返し、*status にその番号を入れる（通信そのものが
// できなかったときは 0）。false のときは *err に理由（英語のまま。ログ用）。
// 本文は kMaxBodyBytes で打ち切る。
bool Get(const std::string &url, const std::string &userAgent, const std::string &accept,
         int *status, std::string *body, std::string *err);

const size_t kMaxBodyBytes = 1024 * 1024;

}  // namespace httpget
}  // namespace mxv2

#endif  // MXV2_HTTPGET_H
