// mxv2 - メッセージカタログ

#include "message.h"

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
// 読むのは起動時の 1 度だけで、あとは参照しかしない。
struct Catalog {
	Values values;   // "<セクション>.<キー>" -> 文言
	Lists lists;     // セクション -> 並び順つきの中身
	std::string locale;
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
bool Merge(const std::string &path, bool replaceLists) {
	Ini ini;
	if (!ini.Load(path)) return false;

	Catalog &cat = Cat();
	const std::vector<std::string> sections = ini.Sections();
	for (size_t i = 0; i < sections.size(); i++) {
		const std::vector<std::string> keys = ini.Keys(sections[i]);
		std::vector<MsgRow> &list = cat.lists[sections[i]];
		if (replaceLists && !keys.empty()) list.clear();
		for (size_t j = 0; j < keys.size(); j++) {
			const std::string value = ini.GetString(sections[i], keys[j], std::string());
			cat.values[sections[i] + "." + keys[j]] = value;

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
	cat.values.clear();
	cat.lists.clear();
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

const char *Msg(const char *key) {
	if (key == 0) return "";
	Catalog &cat = Cat();
	Values::const_iterator it = cat.values.find(key);
	if (it != cat.values.end()) return it->second.c_str();

	// 無いキーはキー名をそのまま出す。カタログへ入れておくのは、
	// 返した文字列を呼び出し側が持ち続けても大丈夫にするため。
	printf("warning  : message not found: %s\n", key);
	return cat.values.insert(std::make_pair(std::string(key), std::string(key)))
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
	Lists::const_iterator it = Cat().lists.find(section ? section : "");
	return (it == Cat().lists.end()) ? kEmpty : it->second;
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
