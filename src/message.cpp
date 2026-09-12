// mxv2 - メッセージカタログ

#include "message.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "assetpath.h"
#include "fileutil.h"
#include "ini.h"

namespace mxv2 {

const char *kDefaultLocale = "ja-JP";
const char *kFallbackLocale = "en";

namespace {

typedef std::map<std::string, std::string> Values;
typedef std::map<std::string, std::vector<MsgRow> > Lists;

// カタログはプロセスに 1 つ。文言は 200 か所以上から引くので、
// 全部の呼び出しに持ち回らせるより、ここに置くほうが素直。
//
// **読み直すときは丸ごと作り直し、古いほうは捨てない。** Msg() は map の中の
// 文字列を指すポインタを返していて、それを持ち続けている場所（ダイアログの
// 題名）があるので、消すとぶら下がる。言語の切り替えは人が選んだときだけ
// なので、数百キロバイトを置いておくほうが安い。
struct Catalog {
	Values *values;  // "<セクション>.<キー>" -> 文言
	Lists *lists;    // セクション -> 並び順つきの中身
	std::string locale;
	std::vector<Values *> oldValues;  // 生かしておくだけ。もう読まない
	std::vector<Lists *> oldLists;

	Catalog() : values(0), lists(0) { NewGeneration(); }

	void NewGeneration() {
		if (values != 0) oldValues.push_back(values);
		if (lists != 0) oldLists.push_back(lists);
		values = new Values();
		lists = new Lists();
	}
};

Catalog &Cat() {
	static Catalog cat;
	return cat;
}

// ini 1 つぶんを取り込む。あとから読んだものが勝つ（ユーザーぶんで
// 一部だけ差し替えられる）。読めなければ false。
//
// replaceLists は一覧のセクション（[HelpKeys] など）の扱い。**言語をまたぐ
// ときは true**。あちらは「キー=説明」の左側まで訳の対象なので、キー単位で
// 混ぜると落とし先の行がそのまま残り、英語と日本語が並んでしまう。
// 同じ言語の重ね方（同梱の上にユーザーぶん）では false にして、行の
// 差し替えと追加ができるようにする。
// 値の中の 2 文字 "\n"（バックスラッシュ + n）を改行にする。ini の値は
// 1 行なので、段落を分けたいときはこれで書く（2026-09-13）。
// 他のエスケープは扱わない（パスに使う "\\" はそのまま）。
std::string DecodeNewlines(const std::string &v) {
	std::string out;
	out.reserve(v.size());
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == 'n') {
			out.push_back('\n');
			i++;
		} else {
			out.push_back(v[i]);
		}
	}
	return out;
}

bool Merge(const std::string &path, bool replaceLists) {
	Ini ini;
	if (!ini.Load(path)) return false;

	Catalog &cat = Cat();
	const std::vector<std::string> sections = ini.Sections();
	for (size_t i = 0; i < sections.size(); i++) {
		const std::vector<std::string> keys = ini.Keys(sections[i]);
		std::vector<MsgRow> &list = (*cat.lists)[sections[i]];
		if (replaceLists && !keys.empty()) list.clear();
		for (size_t j = 0; j < keys.size(); j++) {
			const std::string value =
			    DecodeNewlines(ini.GetString(sections[i], keys[j], std::string()));
			(*cat.values)[sections[i] + "." + keys[j]] = value;

			// 並び順つきの一覧。同じキーが後から来たら差し替える。
			size_t at = list.size();
			for (size_t k = 0; k < list.size(); k++) {
				if (list[k].key == keys[j]) {
					at = k;
					break;
				}
			}
			MsgRow row;
			row.key = keys[j];
			row.value = value;
			if (at == list.size()) {
				list.push_back(row);
			} else {
				list[at] = row;
			}
		}
	}
	return true;
}

std::string LocaleFile(const std::string &root, const std::string &locale) {
	return JoinPath(JoinPath(JoinPath(root, "locale"), locale), "message.ini");
}

}  // namespace

bool LoadMessages(const AssetPaths &paths, const std::string &locale,
                  bool *usedFallback) {
	Catalog &cat = Cat();
	cat.NewGeneration();
	cat.locale = locale.empty() ? kDefaultLocale : locale;
	if (usedFallback != 0) *usedFallback = false;

	// まず落とし先を土台に敷く。同じロケールを頼まれているなら 1 度でよい。
	bool any = false;
	const bool sameAsFallback = (cat.locale == kFallbackLocale);
	if (!sameAsFallback) {
		if (Merge(LocaleFile(paths.bundledDir, kFallbackLocale), false)) any = true;
		if (Merge(LocaleFile(paths.userDir, kFallbackLocale), false)) any = true;
	}

	// 頼まれたロケールを上から重ねる。同梱ぶんが土台で、ユーザーフォルダ側が
	// さらに上（キー単位で差し替えられる）。一覧のセクションだけは、
	// **最初の 1 つ**が落とし先のぶんを置き換える（言語が混ざらないように）。
	bool found = false;
	bool first = !sameAsFallback;
	if (Merge(LocaleFile(paths.bundledDir, cat.locale), first)) {
		found = true;
		first = false;
	}
	if (Merge(LocaleFile(paths.userDir, cat.locale), first)) found = true;
	if (found) any = true;

	if (!found && !sameAsFallback && usedFallback != 0) *usedFallback = true;
	return any;
}

const std::string &MessageLocale() {
	return Cat().locale;
}

namespace {

// ロケール名をそろえる。"ja_JP" と "ja-JP"、"JA-jp" と "ja-JP" は同じもの。
std::string NormalizeLocale(const std::string &s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (c == '_') c = '-';
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		out += c;
	}
	return out;
}

// "ja-JP" の "ja" の部分。区切りが無ければ全部。
std::string LocaleLanguage(const std::string &s) {
	const std::string norm = NormalizeLocale(s);
	const size_t at = norm.find('-');
	return (at == std::string::npos) ? norm : norm.substr(0, at);
}

bool LessLocaleName(const LocaleInfo &a, const LocaleInfo &b) {
	return NormalizeLocale(a.name) < NormalizeLocale(b.name);
}

}  // namespace

namespace {

// root/locale/ を数え上げて out へ足す。すでにある言語（大小・区切りの違いは
// 同じものとみなす）は名前を増やさず、呼び名だけ上書きできる。
//
// bundled が false（＝ユーザーフォルダ）のときは、
//   ・同じ名前が同梱にもあれば **フォルダ名は同梱ぶんのまま**にする。
//     LoadMessages は同じ名前で両方を開くので、綴りは同梱ぶんに合わせて
//     おいたほうが安全（大小を区別する環境で、片方だけ開けなくなるのを防ぐ）。
//   ・呼び名を書いていればそちらを採る（重ねた側が名乗り直せる）。
void AddLocalesFrom(const std::string &root, bool bundled, std::vector<LocaleInfo> *out) {
	if (root.empty()) return;

	std::vector<DirEntry> entries;
	if (!ListDirectory(JoinPath(root, "locale"), &entries)) return;

	for (size_t i = 0; i < entries.size(); i++) {
		if (!entries[i].isDir) continue;
		// message.ini の無いフォルダは言語として数えない。
		Ini ini;
		if (!ini.Load(LocaleFile(root, entries[i].name))) continue;
		const std::string displayName = ini.GetString("Locale", "Name", std::string());

		size_t at = out->size();
		for (size_t j = 0; j < out->size(); j++) {
			if (NormalizeLocale((*out)[j].name) == NormalizeLocale(entries[i].name)) {
				at = j;
				break;
			}
		}
		if (at < out->size()) {
			if (bundled) (*out)[at].name = entries[i].name;
			// 呼び名はユーザーぶんが優先。ユーザー側が書いていなければ
			// 同梱ぶんのものを使う（重ねただけなら名乗りは変わらない）。
			if (!displayName.empty() && (!bundled || (*out)[at].displayName.empty())) {
				(*out)[at].displayName = displayName;
			}
			if (!bundled) (*out)[at].user = true;
			continue;
		}

		LocaleInfo info;
		info.name = entries[i].name;
		info.displayName = displayName;
		info.user = !bundled;
		out->push_back(info);
	}
}

}  // namespace

void ListLocales(const AssetPaths &paths, std::vector<LocaleInfo> *out) {
	out->clear();
	// ユーザーぶんを先に見る（呼び名はそちらが勝つ）。同梱ぶんは後から
	// フォルダ名の綴りをそろえる。
	AddLocalesFrom(paths.userDir, false, out);
	AddLocalesFrom(paths.bundledDir, true, out);

	// 呼び名が無ければフォルダ名をそのまま見せる（新しい言語を足した人が
	// [Locale] Name を書き忘れても選べる）。
	for (size_t i = 0; i < out->size(); i++) {
		if ((*out)[i].displayName.empty()) (*out)[i].displayName = (*out)[i].name;
	}
	std::sort(out->begin(), out->end(), LessLocaleName);
}

std::string MatchLocale(const std::vector<LocaleInfo> &list, const std::string &want) {
	if (list.empty()) return want;

	const std::string norm = NormalizeLocale(want);
	for (size_t i = 0; i < list.size(); i++) {
		if (NormalizeLocale(list[i].name) == norm) return list[i].name;
	}

	// 言語だけ合わせる。"ja" しか分からない端末でも ja-JP を選べるように。
	const std::string lang = LocaleLanguage(want);
	if (!lang.empty()) {
		for (size_t i = 0; i < list.size(); i++) {
			if (LocaleLanguage(list[i].name) == lang) return list[i].name;
		}
	}

	for (size_t i = 0; i < list.size(); i++) {
		if (NormalizeLocale(list[i].name) == NormalizeLocale(kFallbackLocale)) {
			return list[i].name;
		}
	}
	return list[0].name;
}

bool HasMsg(const char *key) {
	if (key == 0) return false;
	Catalog &cat = Cat();
	return cat.values->find(key) != cat.values->end();
}

const char *Msg(const char *key) {
	if (key == 0) return "";
	Catalog &cat = Cat();
	Values::const_iterator it = cat.values->find(key);
	if (it != cat.values->end()) return it->second.c_str();

	// 無いキーはキー名をそのまま出す。カタログへ入れておくのは、
	// 返した文字列を呼び出し側が持ち続けても大丈夫にするため。
	printf("warning  : message not found: %s\n", key);
	return cat.values->insert(std::make_pair(std::string(key), std::string(key)))
	    .first->second.c_str();
}

namespace {

std::string Replace(const std::string &src, const char *mark, const std::string &value) {
	std::string out = src;
	const std::string tag = mark;
	size_t pos = 0;
	while ((pos = out.find(tag, pos)) != std::string::npos) {
		out.replace(pos, tag.size(), value);
		pos += value.size();
	}
	return out;
}

}  // namespace

std::string MsgF(const char *key, const std::string &a0) {
	return Replace(Msg(key), "{0}", a0);
}

std::string MsgF(const char *key, const std::string &a0, const std::string &a1) {
	return Replace(Replace(Msg(key), "{0}", a0), "{1}", a1);
}

std::string MsgF(const char *key, const std::string &a0, const std::string &a1,
                 const std::string &a2) {
	return Replace(Replace(Replace(Msg(key), "{0}", a0), "{1}", a1), "{2}", a2);
}

std::string MsgF(const char *key, const std::string &a0, const std::string &a1,
                 const std::string &a2, const std::string &a3) {
	return Replace(Replace(Replace(Replace(Msg(key), "{0}", a0), "{1}", a1), "{2}", a2),
	               "{3}", a3);
}

std::string MsgFill(const std::string &text, const std::string &a0, const std::string &a1) {
	return Replace(Replace(text, "{0}", a0), "{1}", a1);
}

std::string MsgNum(const char *fmt, int v) {
	char buf[64];
	snprintf(buf, sizeof(buf), fmt, v);
	return buf;
}

std::string MsgNum(const char *fmt, double v) {
	char buf[64];
	snprintf(buf, sizeof(buf), fmt, v);
	return buf;
}

const std::vector<MsgRow> &MsgList(const char *section) {
	static const std::vector<MsgRow> kEmpty;
	Lists::const_iterator it = Cat().lists->find(section ? section : "");
	return (it == Cat().lists->end()) ? kEmpty : it->second;
}

int MsgDisplayWidth(const std::string &s) {
	int w = 0;
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = (unsigned char)s[i];
		if (c < 0x80) {
			i += 1;
			w += 1;
		} else if (c < 0xe0) {
			i += 2;
			w += 1;  // ラテン文字の類。半角として数える
		} else if (c < 0xf0) {
			i += 3;
			w += 2;  // 日本語はここ
		} else {
			i += 4;
			w += 2;
		}
	}
	return w;
}

}  // namespace mxv2
