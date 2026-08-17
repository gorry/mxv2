// mxv2 - 文字コード変換

#include "text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace mxv2 {

std::string SjisToUtf8(const std::string &sjis) {
	if (sjis.empty()) return std::string();

#ifdef _WIN32
	const UINT kCodePageShiftJis = 932;
	int wlen = MultiByteToWideChar(kCodePageShiftJis, 0, sjis.c_str(), (int)sjis.size(), NULL, 0);
	if (wlen <= 0) return sjis;
	std::wstring w((size_t)wlen, L'\0');
	MultiByteToWideChar(kCodePageShiftJis, 0, sjis.c_str(), (int)sjis.size(), &w[0], wlen);

	int ulen = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, NULL, 0, NULL, NULL);
	if (ulen <= 0) return sjis;
	std::string u((size_t)ulen, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wlen, &u[0], ulen, NULL, NULL);
	return u;
#else
	// TODO(Phase 7): 変換テーブルを持たせる。それまでは ASCII のみ通し、
	//   2 バイト文字は U+FFFD (REPLACEMENT CHARACTER) に潰す。
	//   文字の「描画」は OS 非依存になったが、MDX タイトルの Shift-JIS →
	//   UTF-8 変換だけはまだ Windows の CP932 に頼っている。Android /
	//   Emscripten では曲名が全て豆腐になるので、ここを埋めるのが先決。
	std::string out;
	out.reserve(sjis.size());
	for (size_t i = 0; i < sjis.size(); i++) {
		unsigned char c = (unsigned char)sjis[i];
		if (c < 0x80) {
			out += (char)c;
			continue;
		}
		bool lead = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
		if (lead && i + 1 < sjis.size()) i++;
		out += "\xEF\xBF\xBD";
	}
	return out;
#endif
}

std::string TrimTrailingControl(const std::string &s) {
	size_t end = s.size();
	while (end > 0) {
		unsigned char c = (unsigned char)s[end - 1];
		if (c > 0x20) break;
		end--;
	}
	return s.substr(0, end);
}

}  // namespace mxv2
