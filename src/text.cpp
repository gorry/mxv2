// mxv2 - 文字コード変換

#include "text.h"

#include "cp932table.h"

namespace mxv2 {

namespace {

// CP932 の既定文字 (・)。対応の無いバイト列はここへ落ちる。
// Windows の MultiByteToWideChar(932) がそうしているので、それに合わせた。
const unsigned kDefaultChar = 0x30FB;

// Unicode 1 文字を UTF-8 で足す。CP932 の対応先は BMP に収まるので
// 3 バイトまでしか出ない。
void AppendUtf8(std::string *out, unsigned cp) {
	if (cp < 0x80) {
		*out += (char)cp;
	} else if (cp < 0x800) {
		*out += (char)(0xC0 | (cp >> 6));
		*out += (char)(0x80 | (cp & 0x3F));
	} else {
		*out += (char)(0xE0 | (cp >> 12));
		*out += (char)(0x80 | ((cp >> 6) & 0x3F));
		*out += (char)(0x80 | (cp & 0x3F));
	}
}

bool IsLeadByte(unsigned char b) {
	return (b >= cp932::kLeadLo1 && b <= cp932::kLeadHi1) ||
	       (b >= cp932::kLeadLo2 && b <= cp932::kLeadHi2);
}

int LeadIndex(unsigned char b) {
	if (b <= cp932::kLeadHi1) return (int)b - cp932::kLeadLo1;
	return (cp932::kLeadHi1 - cp932::kLeadLo1 + 1) + ((int)b - cp932::kLeadLo2);
}

// 単バイト文字。ASCII と半角カナのほかに、CP932 だけが持つ割り当てが
// 4 つある (0xA0 / 0xFD / 0xFE / 0xFF → U+F8F0-U+F8F3 の私用領域)。
// 相方の無い先行バイトもここへ来るので、その場合は既定文字。
unsigned SingleByteChar(unsigned char b) {
	if (b < 0x80) return b;
	if (b >= 0xA1 && b <= 0xDF) return 0xFF61 + ((unsigned)b - 0xA1);
	switch (b) {
		case 0x80: return 0x0080;
		case 0xA0: return 0xF8F0;
		case 0xFD: return 0xF8F1;
		case 0xFE: return 0xF8F2;
		case 0xFF: return 0xF8F3;
		default: return kDefaultChar;
	}
}

}  // namespace

std::string SjisToUtf8(const std::string &sjis) {
	std::string out;
	out.reserve(sjis.size() + sjis.size() / 2);

	size_t i = 0;
	while (i < sjis.size()) {
		const unsigned char c = (unsigned char)sjis[i];
		if (!IsLeadByte(c)) {
			AppendUtf8(&out, SingleByteChar(c));
			i++;
			continue;
		}

		// 先行バイト。後続が無いとき、および後続が 0x00 のときは 1 バイトで
		// 打ち切る。それ以外は対応が無くても 2 バイトまとめて食う
		// （どちらも Windows の CP932 変換の振る舞いに合わせたもの）。
		const unsigned char t = (i + 1 < sjis.size()) ? (unsigned char)sjis[i + 1] : 0x00;
		if (t == 0x00) {
			AppendUtf8(&out, kDefaultChar);
			i++;
			continue;
		}

		unsigned u = 0;
		if (t >= cp932::kTrailLo && t <= cp932::kTrailHi) {
			u = cp932::kDoubleByte[LeadIndex(c) * cp932::kTrailCount + ((int)t - cp932::kTrailLo)];
		}
		AppendUtf8(&out, (u != 0) ? u : kDefaultChar);
		i += 2;
	}
	return out;
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
