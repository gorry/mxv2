// mxv2 - MDX / PDX の読み込み

#include "mdxsong.h"

#include <cstdio>
#include <cstring>

#include "fileutil.h"
#include "text.h"

#include <mdx_util.h>

namespace mxv2 {

namespace {

// PDX ファイル名は Shift_JIS。ASCII ならそのまま UTF-8 として扱えるので、
// 生バイト版と UTF-8 変換版の両方を候補にする。
void AppendNameVariants(const std::string &name, std::vector<std::string> *out) {
	out->push_back(name);
	std::string utf8 = SjisToUtf8(name);
	if (utf8 != name) out->push_back(utf8);
}

// 大文字小文字を区別するファイルシステム向けに、名前部 / 拡張子部の
// 大文字小文字を反転させた候補も作る（portable_mdx のサンプルと同じ方針）。
#ifndef _WIN32
std::string FlipCase(const std::string &s, bool stem, bool ext) {
	size_t dot = s.rfind('.');
	std::string out = s;
	for (size_t i = 0; i < out.size(); i++) {
		bool inExt = (dot != std::string::npos && i > dot);
		if (inExt ? !ext : !stem) continue;
		char c = out[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) out[i] = (char)(c ^ 0x20);
	}
	return out;
}
#endif

// PDX を探して読み込む。見つかった場所を foundPath に返す。
bool FindAndReadPdx(const std::string &pdxFileName,
                    const std::string &mdxDir,
                    const std::vector<std::string> &extraDirs,
                    std::vector<uint8_t> *out,
                    std::string *foundPath) {
	std::vector<std::string> names;
	AppendNameVariants(pdxFileName, &names);

	// 拡張子が付いていないケースがあるので ".pdx" 付きも候補にする。
	size_t base = names.size();
	for (size_t i = 0; i < base; i++) names.push_back(names[i] + ".pdx");

#ifndef _WIN32
	size_t n = names.size();
	for (size_t i = 0; i < n; i++) {
		names.push_back(FlipCase(names[i], true, false));
		names.push_back(FlipCase(names[i], false, true));
		names.push_back(FlipCase(names[i], true, true));
	}
#endif

	std::vector<std::string> dirs;
	dirs.push_back(mdxDir);
	for (size_t i = 0; i < extraDirs.size(); i++) dirs.push_back(extraDirs[i]);

	// 旧 mxv は「MDX と同じ場所 -> 同じ場所 + .pdx -> PDX パス -> PDX パス +
	// .pdx」の順で探す。ここではディレクトリを外側、名前候補を内側に回す。
	for (size_t d = 0; d < dirs.size(); d++) {
		for (size_t i = 0; i < names.size(); i++) {
			std::string path = JoinPath(dirs[d], names[i]);
			if (!FileExists(path)) continue;
			if (!ReadWholeFile(path, out)) continue;
			*foundPath = path;
			return true;
		}
	}
	return false;
}

}  // namespace

bool LoadMdxSong(const std::string &mdxPath,
                 const std::vector<std::string> &pdxSearchDirs,
                 MdxSong *out,
                 std::string *err) {
	*out = MdxSong();
	out->path = mdxPath;

	std::vector<uint8_t> mdxImage;
	if (!ReadWholeFile(mdxPath, &mdxImage) || mdxImage.empty()) {
		*err = "MDX ファイルを読み込めません: " + mdxPath;
		return false;
	}
	const uint32_t mdxImageSize = (uint32_t)mdxImage.size();

	// タイトル
	{
		char title[512];
		if (!MdxGetTitle(&mdxImage[0], mdxImageSize, title, sizeof(title))) {
			*err = "MDX のタイトルを取得できません（壊れている可能性があります）: " + mdxPath;
			return false;
		}
		out->titleSjis = TrimTrailingControl(std::string(title));
		out->title = SjisToUtf8(out->titleSjis);
	}

	// PDX を要求するか
	bool hasPdxName = false;
	if (!MdxHasPdxFileName(&mdxImage[0], mdxImageSize, &hasPdxName)) {
		*err = "MDX の PDX 情報を取得できません: " + mdxPath;
		return false;
	}
	out->requiresPdx = hasPdxName;

	// PDX 読み込み
	std::vector<uint8_t> pdxImage;
	if (hasPdxName) {
		char name[FILENAME_MAX];
		memset(name, 0, sizeof(name));
		if (!MdxGetPdxFileName(&mdxImage[0], mdxImageSize, name, sizeof(name))) {
			*err = "MDX の PDX ファイル名を取得できません: " + mdxPath;
			return false;
		}
		out->pdxFileName = std::string(name);
		if (!out->pdxFileName.empty()) {
			out->hasPdx = FindAndReadPdx(out->pdxFileName, DirNameOf(mdxPath), pdxSearchDirs,
			                             &pdxImage, &out->pdxPath);
		}
		if (!out->hasPdx) {
			// PDX が見つからなくても、MDX が PDX を要求している以上、MXDRV には
			// PDX バッファを渡さないと演奏が始まらない (MXDRV_SetData2 が
			// pdx == NULL のとき PDX 設定コマンドを送らないため)。
			// 空の PDX (96 エントリ全て長さ 0) を代わりに渡し、FM だけを鳴らす。
			const size_t kEmptyPdxSize = 96 * 8;
			pdxImage.assign(kEmptyPdxSize, 0);
		}
	}

	// MXDRV へ渡すバッファを組み立てる
	uint32_t mdxBufferSize = 0;
	uint32_t pdxBufferSize = 0;
	const uint32_t pdxImageSize = (uint32_t)pdxImage.size();
	if (!MdxGetRequiredBufferSize(&mdxImage[0], mdxImageSize, pdxImageSize,
	                              &mdxBufferSize, &pdxBufferSize)) {
		*err = "MDX のバッファサイズを算出できません: " + mdxPath;
		return false;
	}

	out->mdxBuffer.assign(mdxBufferSize, 0);
	if (out->requiresPdx && pdxBufferSize != 0) out->pdxBuffer.assign(pdxBufferSize, 0);

	if (!MdxUtilCreateMdxPdxBuffer(&mdxImage[0], mdxImageSize,
	                               pdxImage.empty() ? NULL : &pdxImage[0], pdxImageSize,
	                               out->mdxBuffer.empty() ? NULL : &out->mdxBuffer[0],
	                               (uint32_t)out->mdxBuffer.size(),
	                               out->pdxBuffer.empty() ? NULL : &out->pdxBuffer[0],
	                               (uint32_t)out->pdxBuffer.size())) {
		*err = "MDX バッファの構築に失敗しました: " + mdxPath;
		return false;
	}

	return true;
}

}  // namespace mxv2
