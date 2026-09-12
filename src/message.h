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
// ロケールは起動時（-locale と mxv2.ini の [UI] Locale）に決まるが、
// **設定ウィンドウの [言語] で実行中に切り替えられる**。切り替えは
// LoadMessages() を呼び直すだけでよい——古い世代の文言はそのまま生かして
// おくので、Msg() が返した番地を持ち続けている場所（ダイアログの題名など）
// があってもぶら下がらない。ただし**中身は古いまま**なので、覚えている側は
// 取り直すこと（settingsui.cpp の ResetTitles / LoadHelpRows）。

#ifndef MXV2_MESSAGE_H
#define MXV2_MESSAGE_H

#include <string>
#include <vector>

namespace mxv2 {

struct AssetPaths;

// 既定のロケール名。assets/locale/<名前>/message.ini を読む。
extern const char *kDefaultLocale;

// 落とし先のロケール。頼まれたロケールに無いキーはこちらから引く。
extern const char *kFallbackLocale;

// カタログを読む。読む順は
//   落とし先 (同梱 -> ユーザー) -> 頼まれたロケール (同梱 -> ユーザー)
// で、後から読んだものがキー単位で勝つ。知らないロケール名を渡されても
// 落とし先だけは載るので、画面がキー名だらけにはならない。
//
// 1 つも読めなければ false。頼まれたロケールが無くて落とし先で代用した
// ときは usedFallback に true が入る（0 を渡してもよい）。
bool LoadMessages(const AssetPaths &paths, const std::string &locale,
                  bool *usedFallback = 0);

// 頼まれたロケール名（既定 ja-JP）。実際に載っている文言は、キーによっては
// 落とし先のものかもしれない。
const std::string &MessageLocale();

// 選べる言語。設定ウィンドウの [言語] に並べる。
struct LocaleInfo {
	std::string name;         // フォルダ名。"ja-JP" など
	std::string displayName;  // その言語自身での呼び名（[Locale] Name）
	bool user;                // ユーザーフォルダ側にも中身があるか

	LocaleInfo() : user(false) {}
};

// 同梱ぶんとユーザーフォルダの locale/ を数え上げる（message.ini のある
// フォルダだけ）。**ユーザーは言語を足せる**:
//   ・同梱と同じ名前 … その言語に重ねる（キー単位。変えたい行だけ書けばよい）
//   ・同梱に無い名前 … 新しい言語として一覧に出る（落とし先の上に載る）
// 同じ名前が両方にあるときは 1 つにまとめ、**フォルダ名は同梱ぶんの綴り**、
// 呼び名 ([Locale] Name) はユーザーぶんを優先する。並びはフォルダ名順。
void ListLocales(const AssetPaths &paths, std::vector<LocaleInfo> *out);

// want に一番近いものを list から選ぶ。"ja_JP" のような書き方や大小の
// 違いは無視し、完全一致 -> 言語だけ一致 ("ja" と "ja-JP") -> 落とし先 ->
// 先頭、の順に落ちる。list が空なら want をそのまま返す。
std::string MatchLocale(const std::vector<LocaleInfo> &list, const std::string &want);

// 文言を引く。キーは "<セクション>.<キー>"。
// 無ければキーそのものを返す（画面が空になるより、どのキーが無いのかが
// 見えたほうがよい）。返り値はプロセスが終わるまで有効。
const char *Msg(const char *key);
// キーがカタログにあるか。任意のキー（無くてもよい補足文など）を引く前に
// 見る。Msg() は無いキーを警告つきでキー名に置き換えるので、それを避ける。
bool HasMsg(const char *key);

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
