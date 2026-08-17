// mxv2 - 文字コード変換
//
// MDX ファイル内のタイトル / PDX ファイル名は Shift_JIS で格納されている。
// mxv2 の内部表現は UTF-8 に統一するため、読み込み時にここで変換する。

#ifndef MXV2_TEXT_H
#define MXV2_TEXT_H

#include <string>

namespace mxv2 {

// Shift_JIS -> UTF-8。
// TODO(Phase 7): 現在 Windows では OS の CP932 変換に頼っている。
//   Android / Emscripten 用に自前の変換テーブルへ差し替える。
//   ここが mxv2 に残っている唯一のプラットフォーム依存。
std::string SjisToUtf8(const std::string &sjis);

// 末尾の制御文字 (CR / LF / EOF / 空白) を落とす。
std::string TrimTrailingControl(const std::string &s);

}  // namespace mxv2

#endif  // MXV2_TEXT_H
