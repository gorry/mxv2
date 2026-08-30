// mxv2 - CP932 変換表の生成ツール（Windows 専用・手動実行）
//
// src/cp932table.h を作り直すときだけ使う。Windows の
// MultiByteToWideChar(932) が返す対応をそのまま書き出すので、
// 生成した表を使う限り Android / Web でも Windows と同じ結果になる。
//
// 使い方（VS の開発者コマンドプロンプトで）:
//   cl /nologo /EHsc /utf-8 tools\gen_cp932.cpp /Fe:gen_cp932.exe
//   gen_cp932.exe > src\cp932table.h
//
// CMake には登録していない。1 度作れば済むものなので、普段のビルドでは
// 生成済みの src/cp932table.h をそのまま使う。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

namespace {

const int kLeadLo1 = 0x81, kLeadHi1 = 0x9F;
const int kLeadLo2 = 0xE0, kLeadHi2 = 0xFC;
const int kTrailLo = 0x40, kTrailHi = 0xFC;
const unsigned short kInvalid = 0x30FB;  // CP932 の既定文字 (・)

bool IsLead(int b) {
	return (b >= kLeadLo1 && b <= kLeadHi1) || (b >= kLeadLo2 && b <= kLeadHi2);
}

// 1 文字ぶんの Shift_JIS を Unicode へ。変換できなければ 0。
unsigned Convert(const char *s, int n) {
	wchar_t w[8];
	int wlen = MultiByteToWideChar(932, 0, s, n, w, 8);
	if (wlen != 1) return 0;
	return (unsigned)w[0];
}

// 生成前に、実装側 (src/text.cpp) が前提にしている規則を確かめる。
// 崩れていたら表だけ直しても足りないので、その場で止める。
void CheckAssumptions() {
	int bad = 0;

	// 単バイト: ASCII はそのまま。半角カナは U+FF61 から並ぶ。
	// 未定義の 0x80 / 0xA0 / 0xFD-0xFF は Windows 独自の割り当てがある。
	for (int b = 0x00; b <= 0x7F; b++) {
		char s[1] = { (char)b };
		if (Convert(s, 1) != (unsigned)b) { printf("// NG single %02X\n", b); bad++; }
	}
	for (int b = 0xA1; b <= 0xDF; b++) {
		char s[1] = { (char)b };
		if (Convert(s, 1) != (unsigned)(0xFF61 + b - 0xA1)) { printf("// NG kana %02X\n", b); bad++; }
	}
	struct { int b; unsigned u; } odd[] = {
		{ 0x80, 0x0080 }, { 0xA0, 0xF8F0 }, { 0xFD, 0xF8F1 }, { 0xFE, 0xF8F2 }, { 0xFF, 0xF8F3 },
	};
	for (size_t i = 0; i < sizeof(odd) / sizeof(odd[0]); i++) {
		char s[1] = { (char)odd[i].b };
		if (Convert(s, 1) != odd[i].u) { printf("// NG odd %02X\n", odd[i].b); bad++; }
	}
	// 単独の先行バイトは既定文字になる。
	for (int b = 0x00; b <= 0xFF; b++) {
		if (!IsLead(b)) continue;
		char s[1] = { (char)b };
		if (Convert(s, 1) != kInvalid) { printf("// NG lonelead %02X\n", b); bad++; }
	}

	// 2 バイト: 対応があるのは後続 0x40-0xFC の中だけ。
	for (int lead = 0x00; lead <= 0xFF; lead++) {
		if (!IsLead(lead)) continue;
		for (int trail = 0x00; trail <= 0xFF; trail++) {
			if (trail >= kTrailLo && trail <= kTrailHi) continue;
			char s[2] = { (char)lead, (char)trail };
			const unsigned u = Convert(s, 2);
			if (u != 0 && u != kInvalid) {
				printf("// NG trail range %02X %02X -> U+%04X\n", lead, trail, u);
				bad++;
			}
		}
	}

	// 後続バイトが 0x00 のときだけ先行バイト 1 つで打ち切られ、
	// それ以外の無効な組では 2 バイトまとめて既定文字になる。
	for (int lead = 0x00; lead <= 0xFF; lead++) {
		if (!IsLead(lead)) continue;
		for (int trail = 0x00; trail <= 0xFF; trail++) {
			char s[3] = { (char)lead, (char)trail, 'A' };
			wchar_t w[8];
			const int wlen = MultiByteToWideChar(932, 0, s, 3, w, 8);
			const int want = (trail == 0x00) ? 3 : 2;
			if (wlen != want) {
				printf("// NG eat %02X %02X wlen=%d want=%d\n", lead, trail, wlen, want);
				bad++;
			}
		}
	}

	if (bad != 0) {
		fprintf(stderr, "assumptions broken: %d\n", bad);
		exit(1);
	}
}

}  // namespace

int main() {
	CheckAssumptions();

	const int trailCount = kTrailHi - kTrailLo + 1;
	int leadCount = 0;
	for (int b = 0; b <= 0xFF; b++) {
		if (IsLead(b)) leadCount++;
	}

	printf("// mxv2 - CP932 (Shift_JIS) -> Unicode の対応表\n");
	printf("//\n");
	printf("// **生成物。手で書き換えないこと。** tools/gen_cp932.cpp が\n");
	printf("// Windows の MultiByteToWideChar(932) から書き出したもの。\n");
	printf("// src/text.cpp だけが読む。\n");
	printf("//\n");
	printf("// 表に載るのは 2 バイト文字だけ。単バイト (ASCII / 半角カナ /\n");
	printf("// Windows 独自の 0x80・0xA0・0xFD-0xFF) は規則で書けるので\n");
	printf("// text.cpp 側で直に計算している。\n");
	printf("\n");
	printf("#ifndef MXV2_CP932TABLE_H\n");
	printf("#define MXV2_CP932TABLE_H\n");
	printf("\n");
	printf("namespace mxv2 {\n");
	printf("namespace cp932 {\n");
	printf("\n");
	printf("// 先行バイトは 0x%02X-0x%02X と 0x%02X-0x%02X の %d 種類、\n",
	       kLeadLo1, kLeadHi1, kLeadLo2, kLeadHi2, leadCount);
	printf("// 後続バイトは 0x%02X-0x%02X の %d 種類。\n", kTrailLo, kTrailHi, trailCount);
	printf("const int kLeadLo1 = 0x%02X;\n", kLeadLo1);
	printf("const int kLeadHi1 = 0x%02X;\n", kLeadHi1);
	printf("const int kLeadLo2 = 0x%02X;\n", kLeadLo2);
	printf("const int kLeadHi2 = 0x%02X;\n", kLeadHi2);
	printf("const int kTrailLo = 0x%02X;\n", kTrailLo);
	printf("const int kTrailHi = 0x%02X;\n", kTrailHi);
	printf("const int kLeadCount = %d;\n", leadCount);
	printf("const int kTrailCount = %d;\n", trailCount);
	printf("\n");
	printf("// 対応が無いところは 0。text.cpp が既定文字 U+30FB (・) に読み替える。\n");
	printf("static const unsigned short kDoubleByte[kLeadCount * kTrailCount] = {\n");

	int mapped = 0;
	for (int lead = 0x00; lead <= 0xFF; lead++) {
		if (!IsLead(lead)) continue;
		printf("\t// 0x%02X\n", lead);
		for (int trail = kTrailLo; trail <= kTrailHi; trail++) {
			char s[2] = { (char)lead, (char)trail };
			unsigned u = Convert(s, 2);
			if (u == kInvalid) u = 0;  // 既定文字は「対応なし」と同じ
			if (u != 0) mapped++;
			if ((trail - kTrailLo) % 12 == 0) printf("\t");
			printf("0x%04X,", u);
			if ((trail - kTrailLo) % 12 == 11 || trail == kTrailHi) printf("\n");
			else printf(" ");
		}
	}

	printf("};\n");
	printf("\n");
	printf("}  // namespace cp932\n");
	printf("}  // namespace mxv2\n");
	printf("\n");
	printf("#endif  // MXV2_CP932TABLE_H\n");

	fprintf(stderr, "mapped double-byte characters: %d\n", mapped);
	return 0;
}
