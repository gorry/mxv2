// mxv2 - Dear ImGui 向けの、禁則つき折り返し

#include "kinsoku.h"

#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // ImTextCharFromUtf8

namespace mxv2 {

namespace {

// 折り返しの最小単位。ASCII の語は 1 つ、それ以外は 1 文字。
struct Atom {
	const char *begin;
	const char *end;
	unsigned first;  // 先頭の文字（行頭禁則の判定）
	unsigned last;   // 末尾の文字（行末禁則の判定）
	bool space;
	float width;
};

bool IsSpace(unsigned c) { return c == ' ' || c == '\t' || c == 0x3000; }

// 行頭に来てはいけない字。
bool ForbidLineStart(unsigned c) {
	switch (c) {
		case ',': case '.': case ';': case ':': case '!': case '?': case ')': case ']': case '}':
		case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0x30FB: case 0xFF1A: case 0xFF1B:
		case 0xFF1F: case 0xFF01: case 0x30FC: case 0x301C: case 0xFF5E: case 0x2026: case 0x2025:
		case 0xFF09: case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3009: case 0x300B:
		case 0xFF3D: case 0xFF5D: case 0x2019: case 0x201D:
		// 小書きの仮名と繰り返し記号
		case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083:
		case 0x3085: case 0x3087: case 0x308E: case 0x3095: case 0x3096:
		case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3:
		case 0x30E5: case 0x30E7: case 0x30EE: case 0x30F5: case 0x30F6: case 0x3005: case 0x303B:
			return true;
		default:
			return false;
	}
}

// 行末に来てはいけない字。
bool ForbidLineEnd(unsigned c) {
	switch (c) {
		case '(': case '[': case '{':
		case 0xFF08: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0x3008: case 0x300A:
		case 0xFF3B: case 0xFF5B: case 0x2018: case 0x201C:
			return true;
		default:
			return false;
	}
}

// atoms[j-1] と atoms[j] の間で折ってよいか。
bool LegalBreak(const std::vector<Atom> &atoms, size_t j) {
	if (atoms[j].space || atoms[j - 1].space) return true;
	return !ForbidLineEnd(atoms[j - 1].last) && !ForbidLineStart(atoms[j].first);
}

void SplitAtoms(const char *b, const char *e, std::vector<Atom> *out) {
	const char *s = b;
	while (s < e) {
		unsigned c = (unsigned char)*s;
		const char *next = s + 1;
		if (c >= 0x80) next = s + ImTextCharFromUtf8(&c, s, e);

		Atom a;
		a.begin = s;
		a.first = c;
		a.last = c;
		a.space = IsSpace(c);
		if (!a.space && c < 0x80) {
			// ASCII の語: 空白でも非 ASCII でもない字が続くかぎり伸ばす。
			while (next < e) {
				unsigned d = (unsigned char)*next;
				if (d >= 0x80 || IsSpace(d)) break;
				a.last = d;
				next++;
			}
		}
		a.end = next;
		a.width = ImGui::CalcTextSize(a.begin, a.end).x;
		out->push_back(a);
		s = next;
	}
}

void WrapParagraph(const char *b, const char *e, float wrapWidth, std::string *out) {
	std::vector<Atom> atoms;
	SplitAtoms(b, e, &atoms);
	const size_t n = atoms.size();
	size_t start = 0;
	bool firstLine = true;
	while (start < n) {
		while (start < n && atoms[start].space) start++;  // 行頭の空白は捨てる
		if (start >= n) break;

		// 収まるところまで詰める。
		float w = 0.0f;
		size_t k = start;
		while (k < n && w + atoms[k].width <= wrapWidth) {
			w += atoms[k].width;
			k++;
		}
		size_t j = n;
		if (k < n) {
			// atoms[k] が入らない。手前で、禁則に触れない折り目を探す。
			j = k;
			while (j > start && !LegalBreak(atoms, j)) j--;
			// 見つからなければ（行が空になるなら）やむを得ず k で切る。
			// k == start のときは 1 語だけでも置く（はみ出す）。
			if (j == start) j = (k > start) ? k : start + 1;
		}
		// 行末の空白は捨てる。
		size_t end = j;
		while (end > start && atoms[end - 1].space) end--;
		if (!firstLine) out->push_back('\n');
		firstLine = false;
		out->append(atoms[start].begin, atoms[end - 1].end);
		start = j;
	}
}

}  // namespace

std::string WrapTextKinsoku(const std::string &text, float wrapWidth) {
	std::string out;
	if (wrapWidth <= 0.0f) return text;
	const char *b = text.c_str();
	const char *e = b + text.size();
	const char *p = b;
	bool first = true;
	while (p <= e) {
		const char *nl = p;
		while (nl < e && *nl != '\n') nl++;
		if (!first) out.push_back('\n');
		first = false;
		WrapParagraph(p, nl, wrapWidth, &out);
		if (nl >= e) break;
		p = nl + 1;
	}
	return out;
}

void TextWrappedKinsoku(const char *text, float wrapWidth) {
	const std::string wrapped = WrapTextKinsoku(text != 0 ? text : "", wrapWidth);
	ImGui::TextUnformatted(wrapped.c_str());
}

void TextWrappedKinsoku(const char *text) {
	TextWrappedKinsoku(text, ImGui::GetContentRegionAvail().x);
}

}  // namespace mxv2
