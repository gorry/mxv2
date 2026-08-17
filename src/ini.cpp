// mxv2 - INI ファイルの読み書き

#include "ini.h"

#include <cstdio>
#include <cstdlib>

#include "fileutil.h"

namespace mxv2 {

namespace {

std::string Trim(const std::string &s) {
	size_t b = 0;
	size_t e = s.size();
	while (b < e && (unsigned char)s[b] <= 0x20) b++;
	while (e > b && (unsigned char)s[e - 1] <= 0x20) e--;
	return s.substr(b, e - b);
}

}  // namespace

bool Ini::Load(const std::string &path) {
	std::vector<uint8_t> data;
	if (!ReadWholeFile(path, &data)) return false;

	std::string text(data.begin(), data.end());
	std::string section;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string line =
		    Trim(text.substr(pos, (nl == std::string::npos) ? std::string::npos : nl - pos));
		pos = (nl == std::string::npos) ? (text.size() + 1) : (nl + 1);

		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line[0] == '[') {
			const size_t close = line.find(']');
			if (close != std::string::npos) {
				section = Trim(line.substr(1, close - 1));
				FindOrAdd(section);
			}
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		SetString(section, Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
	}
	return true;
}

bool Ini::Save(const std::string &path) const {
	std::string text;
	for (size_t i = 0; i < sections_.size(); i++) {
		const SectionData &s = sections_[i];
		if (s.values.empty()) continue;
		if (!text.empty()) text += "\n";
		if (!s.name.empty()) text += "[" + s.name + "]\n";
		for (size_t j = 0; j < s.order.size(); j++) {
			Values::const_iterator v = s.values.find(s.order[j]);
			if (v == s.values.end()) continue;
			text += s.order[j] + "=" + v->second + "\n";
		}
	}

	std::vector<uint8_t> data(text.begin(), text.end());
	return WriteWholeFile(path, data);
}

Ini::SectionData *Ini::Find(const std::string &name) {
	for (size_t i = 0; i < sections_.size(); i++) {
		if (sections_[i].name == name) return &sections_[i];
	}
	return 0;
}

const Ini::SectionData *Ini::Find(const std::string &name) const {
	for (size_t i = 0; i < sections_.size(); i++) {
		if (sections_[i].name == name) return &sections_[i];
	}
	return 0;
}

Ini::SectionData *Ini::FindOrAdd(const std::string &name) {
	SectionData *s = Find(name);
	if (s != 0) return s;
	SectionData add;
	add.name = name;
	sections_.push_back(add);
	return &sections_[sections_.size() - 1];
}

bool Ini::Has(const std::string &section, const std::string &key) const {
	const SectionData *s = Find(section);
	if (s == 0) return false;
	return s->values.find(key) != s->values.end();
}

int Ini::GetInt(const std::string &section, const std::string &key, int fallback) const {
	const SectionData *s = Find(section);
	if (s == 0) return fallback;
	Values::const_iterator v = s->values.find(key);
	if (v == s->values.end()) return fallback;
	return atoi(v->second.c_str());
}

std::string Ini::GetString(const std::string &section, const std::string &key,
                           const std::string &fallback) const {
	const SectionData *s = Find(section);
	if (s == 0) return fallback;
	Values::const_iterator v = s->values.find(key);
	if (v == s->values.end()) return fallback;
	return v->second;
}

void Ini::SetInt(const std::string &section, const std::string &key, int value) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", value);
	SetString(section, key, buf);
}

void Ini::SetString(const std::string &section, const std::string &key,
                    const std::string &value) {
	SectionData *s = FindOrAdd(section);
	if (s->values.find(key) == s->values.end()) s->order.push_back(key);
	s->values[key] = value;
}

}  // namespace mxv2
