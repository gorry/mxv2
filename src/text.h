// mxv2 - 文字コード変換
//
// MDX ファイル内のタイトル / PDX ファイル名は Shift_JIS で格納されている。
// mxv2 の内部表現は UTF-8 に統一するため、読み込み時にここで変換する。

#ifndef MXV2_TEXT_H
#define MXV2_TEXT_H

#include <string>

namespace mxv2 {

// Shift_JIS (CP932) -> UTF-8。
// 変換表 src/cp932table.h を自前で引くので、どのプラットフォームでも同じ
// 結果になる（表は Windows の CP932 変換から書き出したもの）。
// 対応の無いバイト列は CP932 の既定文字 U+30FB (・) に落ちる。
std::string SjisToUtf8(const std::string &sjis);

// 末尾の制御文字 (CR / LF / EOF / 空白) を落とす。
std::string TrimTrailingControl(const std::string &s);

}  // namespace mxv2

#endif  // MXV2_TEXT_H
