// mxv2 - メッセージカタログ
//
// 画面とログに出す文言はコードに直接書かず、
// assets/locale/<ロケール>/message.ini へまとめてある。
// 同梱ぶんを読んでから、ユーザーフォルダ側の同じ場所にあるファイルを
// **キー単位で**重ねる（一部だけ差し替えられる。assetpath.h の考え方と同じ）。
//
// 決めごと:
//   ・**訳す必要のある文言だけ**を置く。オプション名 (-zoom) やログの
//     接頭辞 ("warning  : ") のような書式・構文は、コード側に残す。
//   ・引数は printf の書式ではなく {0} {1} … で入れる。数の書式
//     (%+d や %.1f) は呼ぶ側で MsgNum() を通してから渡す。翻訳した文で
//     書式指定を書き間違えても落ちないようにするため。
//   ・カタログを読む前でも Msg() は呼べる（キーがそのまま返る）。
//     起動直後の警告を出す都合で、読み込みより前に呼ぶ場所がある。
//
// ロケールの切り替えは起動時のみ（-locale）。実行中に読み直すことは
// 考えていない（題名など、1 度だけ組み立てて使い回している文言がある）。

#ifndef MXV2_MESSAGE_H
#define MXV2_MESSAGE_H

#include <string>
#include <vector>

namespace mxv2 {

struct AssetPaths;

// 既定のロケール名。assets/locale/<名前>/message.ini を読む。
extern const char *kDefaultLocale;

// カタログを読む。同梱ぶんが 1 つも読めなければ false（文言はキーのまま
// 出るが、動きはする）。
bool LoadMessages(const AssetPaths &paths, const std::string &locale);

// 読み込んだロケール名。
const std::string &MessageLocale();

// 文言を引く。キーは "<セクション>.<キー>"。
// 無ければキーそのものを返す（画面が空になるより、どのキーが無いのかが
// 見えたほうがよい）。返り値はプロセスが終わるまで有効。
const char *Msg(const char *key);

// {0} {1} … を置き換えたものを返す。
std::string MsgF(const char *key, const std::string &a0);
std::string MsgF(const char *key, const std::string &a0, const std::string &a1);
std::string MsgF(const char *key, const std::string &a0, const std::string &a1,
                 const std::string &a2);
std::string MsgF(const char *key, const std::string &a0, const std::string &a1,
                 const std::string &a2, const std::string &a3);

// カタログから取り出したあとの文字列に {0} {1} を埋める。一覧
// （[UsageOptions] など）のように、キーで引かずに回すものに使う。
std::string MsgFill(const std::string &text, const std::string &a0, const std::string &a1);

// 文言に埋める数を作る。書式は printf と同じ（"%+d" など）。
std::string MsgNum(const char *fmt, int v);
std::string MsgNum(const char *fmt, double v);

// 順番のあるセクション（[HelpKeys] など）の中身。ini に書いてある順で返る。
// 無いセクションなら空。
struct MsgRow {
	std::string key;
	std::string value;
};
const std::vector<MsgRow> &MsgList(const char *section);

// 文言を桁揃えして出すための表示幅（全角は 2、半角は 1）。
// -h の出力と [操作方法] ダイアログで使う。
int MsgDisplayWidth(const std::string &s);

}  // namespace mxv2

#endif  // MXV2_MESSAGE_H
