// mxv2 - Dear ImGui 向けの、禁則つき折り返し
//
// ImGui の TextWrapped は英文向けで、空白で単語を区切って折る。日本語には
// 空白が無いので句読点までがひとつの「単語」になり、行に収まらなければ
// **幅が尽きた文字の手前で機械的に切る**。「。」「、」「」」が行頭に、
// 「「」「（」が行末に来る。ここでは自分で改行を入れてから ImGui に渡す。
//
// 規則:
//   ・ASCII の語（空白で区切られた並び）は途中で切らない
//   ・日本語は 1 文字ずつ切ってよい
//   ・行頭に来てはいけない字（。、」）！？ や小書きの仮名、長音）と
//     行末に来てはいけない字（「（『【 など）は、前後の字と一緒に動かす
//     （追い出し。ぶら下げはしない——窓の縁で切れて見えるため）
//   ・行に収まらない語（長い ASCII の語）はそのまま置く（はみ出す）
// 幅の計測は ImGui の今のフォント（CalcTextSize）。

#ifndef MXV2_KINSOKU_H
#define MXV2_KINSOKU_H

#include <string>

namespace mxv2 {

// wrapWidth（実ピクセル）に収まるように改行を入れた文字列を返す。
// 入力の改行は段落の区切りとしてそのまま残す。
std::string WrapTextKinsoku(const std::string &text, float wrapWidth);

// ImGui::TextWrapped の代わり。今の残り幅（GetContentRegionAvail().x）で
// 折り返して TextUnformatted で出す。
void TextWrappedKinsoku(const char *text);

// 幅を指定して折り返して出す。
void TextWrappedKinsoku(const char *text, float wrapWidth);

}  // namespace mxv2

#endif  // MXV2_KINSOKU_H
