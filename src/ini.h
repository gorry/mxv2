// mxv2 - INI ファイルの読み書き
//
// 旧 mxv は GetPrivateProfileInt/WritePrivateProfileString で mxv.ini と
// <テーマ名>.mxv を扱っていた。その置き換え。Windows の API と違い、
// 書き込みは「全体を組み立てて一度に保存する」方式にしてある。
//
// 書式は素朴な INI。行頭の ; と # はコメント。セクション名・キーは
// 大文字小文字を区別する（旧 mxv の .mxv がそう書かれているため）。

#ifndef MXV2_INI_H
#define MXV2_INI_H

#include <map>
#include <string>
#include <vector>

namespace mxv2 {

class Ini {
public:
	Ini() {}

	// 読み込む。ファイルが無ければ false（中身は空のまま）。
	bool Load(const std::string &path);
	// 保存する。セクションは追加した順に並ぶ。
	bool Save(const std::string &path) const;

	bool Has(const std::string &section, const std::string &key) const;
	int GetInt(const std::string &section, const std::string &key, int fallback) const;
	std::string GetString(const std::string &section, const std::string &key,
	                      const std::string &fallback) const;

	void SetInt(const std::string &section, const std::string &key, int value);
	void SetString(const std::string &section, const std::string &key,
	               const std::string &value);

private:
	typedef std::map<std::string, std::string> Values;

	struct SectionData {
		std::string name;
		std::vector<std::string> order;  // キーの出現順
		Values values;
	};

	SectionData *Find(const std::string &name);
	const SectionData *Find(const std::string &name) const;
	SectionData *FindOrAdd(const std::string &name);

	std::vector<SectionData> sections_;
};

}  // namespace mxv2

#endif  // MXV2_INI_H
