// mxv2 - apk の中の同梱素材を内部ストレージへ展開する（Android 専用）

#include "androidassets.h"

#include <cstdio>
#include <map>

#include <SDL.h>

#include "fileutil.h"

namespace mxv2 {

namespace {

// 索引ファイル。apk の assets の根に置く（展開先にも同じ名前で残して、
// 次の起動で突き合わせる）。
const char kIndexName[] = "assetindex.txt";

// パス -> 目印（"<crc32> <バイト数>"）。索引の 1 行を割ったもの。
typedef std::map<std::string, std::string> IndexMap;

// apk の中のファイルを読む。相対パスを渡すと SDL が assets を見に行く。
bool ReadApkFile(const std::string &relative, std::vector<uint8_t> *out) {
	SDL_RWops *rw = SDL_RWFromFile(relative.c_str(), "rb");
	if (rw == NULL) return false;

	out->clear();
	const Sint64 size = SDL_RWsize(rw);
	if (size > 0) out->reserve((size_t)size);

	uint8_t buf[64 * 1024];
	for (;;) {
		const size_t n = SDL_RWread(rw, buf, 1, sizeof(buf));
		if (n == 0) break;
		out->insert(out->end(), buf, buf + n);
	}
	SDL_RWclose(rw);
	return true;
}

std::string ToString(const std::vector<uint8_t> &data) {
	if (data.empty()) return std::string();
	return std::string((const char *)&data[0], data.size());
}

// 索引を読む。"<crc32> <サイズ> <パス>" の 3 つ目は空白を含みうるので、
// 頭から 2 つだけ切って残りをパスにする。
void ParseIndex(const std::string &text, IndexMap *out) {
	size_t pos = 0;
	while (pos < text.size()) {
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos) eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if (line.empty() || line[0] == '#') continue;

		const size_t sp1 = line.find(' ');
		if (sp1 == std::string::npos) continue;
		const size_t sp2 = line.find(' ', sp1 + 1);
		if (sp2 == std::string::npos) continue;
		const std::string path = line.substr(sp2 + 1);
		if (path.empty()) continue;
		(*out)[path] = line.substr(0, sp2);
	}
}

void Warn(std::vector<std::string> *warnings, const std::string &text) {
	printf("warning  : %s\n", text.c_str());
	if (warnings != 0) warnings->push_back(text);
}

}  // namespace

bool ExtractBundledAssets(const std::string &destDir, std::vector<std::string> *warnings) {
	std::vector<uint8_t> raw;
	if (!ReadApkFile(kIndexName, &raw)) {
		Warn(warnings, std::string("asset index not found in apk: ") + kIndexName);
		return false;
	}
	const std::string newText = ToString(raw);

	// 展開済みのものと同じなら触らない（毎回の起動はここで終わる）。
	const std::string stampPath = JoinPath(destDir, kIndexName);
	{
		std::vector<uint8_t> old;
		if (ReadWholeFile(stampPath, &old) && ToString(old) == newText) return true;
	}

	IndexMap wanted;
	ParseIndex(newText, &wanted);
	if (wanted.empty()) {
		Warn(warnings, std::string("asset index is empty: ") + kIndexName);
		return false;
	}

	IndexMap have;
	{
		std::vector<uint8_t> old;
		if (ReadWholeFile(stampPath, &old)) ParseIndex(ToString(old), &have);
	}

	if (!MakeDirectories(destDir)) {
		Warn(warnings, "cannot create asset directory: " + destDir);
		return false;
	}

	bool ok = true;
	int written = 0;
	for (IndexMap::const_iterator it = wanted.begin(); it != wanted.end(); ++it) {
		const std::string &rel = it->first;
		const std::string path = JoinPath(destDir, rel);

		// 目印が同じで実物もあるなら、書き直す必要はない。
		const IndexMap::const_iterator old = have.find(rel);
		if (old != have.end() && old->second == it->second && FileExists(path)) continue;

		std::vector<uint8_t> data;
		if (!ReadApkFile(rel, &data)) {
			Warn(warnings, "cannot read from apk: " + rel);
			ok = false;
			continue;
		}
		const std::string dir = DirNameOf(path);
		if (!dir.empty() && !MakeDirectories(dir)) {
			Warn(warnings, "cannot create directory: " + dir);
			ok = false;
			continue;
		}
		if (!WriteWholeFile(path, data)) {
			Warn(warnings, "cannot write: " + path);
			ok = false;
			continue;
		}
		written++;
	}

	// 索引から消えたものは、展開先からも消す。
	int removed = 0;
	for (IndexMap::const_iterator it = have.begin(); it != have.end(); ++it) {
		if (wanted.find(it->first) != wanted.end()) continue;
		if (RemoveFile(JoinPath(destDir, it->first))) removed++;
	}

	// 索引は最後に置く。途中で落ちたときは次の起動でやり直しになる。
	if (ok) {
		std::vector<uint8_t> data(newText.begin(), newText.end());
		if (!WriteWholeFile(stampPath, data)) {
			Warn(warnings, "cannot write: " + stampPath);
			ok = false;
		}
	}

	printf("assets   : extracted %d file(s), removed %d, into %s\n", written, removed,
	       destDir.c_str());
	return ok;
}

}  // namespace mxv2
