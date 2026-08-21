// mxv2 - 素材の置き場所（同梱ぶんとユーザーぶん）

#include "assetpath.h"

#include <algorithm>

#include "fileutil.h"

namespace mxv2 {

const char kBundledSkinPrefix[] = "assets:";

namespace {

const char *kSkinSubDir = "skin";
const size_t kPrefixLen = sizeof(kBundledSkinPrefix) - 1;

bool LessNoCase(const std::string &a, const std::string &b) {
	return CompareNoCase(a, b) < 0;
}

}  // namespace

bool IsBundledSkinRef(const std::string &ref) {
	return ref.size() > kPrefixLen &&
	       CompareNoCase(ref.substr(0, kPrefixLen), kBundledSkinPrefix) == 0;
}

std::string SkinRefName(const std::string &ref) {
	return IsBundledSkinRef(ref) ? ref.substr(kPrefixLen) : ref;
}

std::string MakeBundledSkinRef(const std::string &name) {
	return std::string(kBundledSkinPrefix) + name;
}

std::vector<std::string> AssetPaths::Roots() const {
	std::vector<std::string> roots;
	if (!userDir.empty()) roots.push_back(userDir);
	if (!bundledDir.empty()) roots.push_back(bundledDir);
	return roots;
}

std::string AssetPaths::Find(const std::string &relative) const {
	const std::vector<std::string> roots = Roots();
	for (size_t i = 0; i < roots.size(); i++) {
		const std::string path = JoinPath(roots[i], relative);
		if (FileExists(path)) return path;
	}
	return std::string();
}

std::string AssetPaths::SkinDir(const std::string &ref) const {
	const std::string name = SkinRefName(ref);
	if (name.empty()) return std::string();

	if (!IsBundledSkinRef(ref)) {
		const std::string own = UserSkinDir(name);
		if (!userDir.empty() && IsDirectory(own)) return own;
		// 接頭辞なしの指定は、ユーザーフォルダに無ければ同梱ぶんを指す
		// （まだ何も足していないときや、古い ini のため）。
	}
	if (bundledDir.empty()) return std::string();
	const std::string bundled = JoinPath(JoinPath(bundledDir, kSkinSubDir), name);
	return IsDirectory(bundled) ? bundled : std::string();
}

std::string AssetPaths::UserSkinDir(const std::string &name) const {
	return JoinPath(JoinPath(userDir, kSkinSubDir), SkinRefName(name));
}

bool AssetPaths::UserSkinExists(const std::string &name) const {
	if (userDir.empty() || name.empty()) return false;
	return IsDirectory(UserSkinDir(name));
}

std::string AssetPaths::CanonicalSkinRef(const std::string &ref) const {
	const std::string name = SkinRefName(ref);
	if (name.empty()) return ref;
	if (!IsBundledSkinRef(ref) && UserSkinExists(name)) return name;
	return MakeBundledSkinRef(name);
}

void AssetPaths::ListSkinRefs(std::vector<std::string> *out) const {
	out->clear();

	// ユーザーぶん -> 同梱ぶん の順に、それぞれ名前順で並べる。
	for (int pass = 0; pass < 2; pass++) {
		const bool bundled = (pass == 1);
		const std::string root = bundled ? bundledDir : userDir;
		if (root.empty()) continue;

		std::vector<DirEntry> entries;
		if (!ListDirectory(JoinPath(root, kSkinSubDir), &entries)) continue;

		std::vector<std::string> refs;
		for (size_t j = 0; j < entries.size(); j++) {
			if (!entries[j].isDir) continue;
			refs.push_back(bundled ? MakeBundledSkinRef(entries[j].name) : entries[j].name);
		}
		std::sort(refs.begin(), refs.end(), LessNoCase);
		out->insert(out->end(), refs.begin(), refs.end());
	}
}

}  // namespace mxv2
