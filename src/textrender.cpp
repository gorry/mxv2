// mxv2 - 日本語を含む文字列の描画（stb_truetype）

#include "textrender.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "fileutil.h"

// stb_truetype は Dear ImGui の同梱物を借りる。imgui 側も STBTT_STATIC 付きで
// 実装を持っているが、どちらも内部リンケージなので衝突しない。
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace mxv2 {

namespace {

// 同梱フォント。設定 UI と同じものを使う。スキンの font.ttf が最優先。
const char *kUserFont = "font.ttf";
const char *kBundledFont = "MPLUS1p-Regular.ttf";

// stb_truetype はヒンティングもステム・ダークニングもしないので、10px 前後だと
// 線が細く沈む。カバレッジにガンマを掛けて太らせる（GDI や FreeType が
// 内部でやっているのと同じ狙い）。1.0 で素のまま、小さいほど太くなる。
const float kCoverageGamma = 0.72f;

// 渡された順にディレクトリを見て、最初に見つかったものを返す。
std::string FindFirst(const std::vector<std::string> &dirs, const char *name) {
	for (size_t i = 0; i < dirs.size(); i++) {
		if (dirs[i].empty()) continue;
		const std::string path = JoinPath(dirs[i], name);
		if (FileExists(path)) return path;
	}
	return std::string();
}

const uint8_t *CoverageTable() {
	static uint8_t table[256];
	static bool ready = false;
	if (!ready) {
		for (int i = 0; i < 256; i++) {
			const float v = powf(i / 255.0f, kCoverageGamma);
			int n = (int)(v * 255.0f + 0.5f);
			if (n < 0) n = 0;
			if (n > 255) n = 255;
			table[i] = (uint8_t)n;
		}
		ready = true;
	}
	return table;
}

class NullTextRenderer : public TextRenderer {
public:
	virtual bool available() const { return false; }
	virtual void Draw(Bitmap *, int, int, int, float, const std::string &, int, bool) {}
	virtual int Measure(float, const std::string &) { return 0; }
};

// UTF-8 を 1 文字取り出す。壊れていたら U+FFFD を返して 1 バイト進める。
uint32_t NextCodepoint(const std::string &s, size_t *pos) {
	const size_t n = s.size();
	size_t i = *pos;
	const uint8_t c = (uint8_t)s[i];

	int extra = 0;
	uint32_t cp = 0;
	if (c < 0x80) {
		*pos = i + 1;
		return c;
	} else if ((c & 0xe0) == 0xc0) {
		extra = 1;
		cp = c & 0x1f;
	} else if ((c & 0xf0) == 0xe0) {
		extra = 2;
		cp = c & 0x0f;
	} else if ((c & 0xf8) == 0xf0) {
		extra = 3;
		cp = c & 0x07;
	} else {
		*pos = i + 1;
		return 0xfffd;
	}

	if (i + extra >= n) {
		*pos = n;
		return 0xfffd;
	}
	for (int k = 1; k <= extra; k++) {
		const uint8_t b = (uint8_t)s[i + k];
		if ((b & 0xc0) != 0x80) {
			*pos = i + k;
			return 0xfffd;
		}
		cp = (cp << 6) | (b & 0x3f);
	}
	*pos = i + extra + 1;
	return cp;
}

class StbTextRenderer : public TextRenderer {
public:
	StbTextRenderer()
	    : ready_(false), sizeKey_(-1), scale_(0.0f), baseline_(0), current_(0) {
		memset(&font_, 0, sizeof(font_));
	}

	bool Load(const std::vector<std::string> &searchDirs) {
		std::string path = FindFirst(searchDirs, kUserFont);
		if (path.empty()) path = FindFirst(searchDirs, kBundledFont);
		if (path.empty()) return false;
		if (!ReadWholeFile(path, &data_) || data_.empty()) return false;

		const int offset = stbtt_GetFontOffsetForIndex(&data_[0], 0);
		if (offset < 0) return false;
		if (!stbtt_InitFont(&font_, &data_[0], offset)) return false;

		stbtt_GetFontVMetrics(&font_, &ascent_, &descent_, &lineGap_);
		ready_ = true;
		return true;
	}

	virtual bool available() const { return ready_; }

	virtual void Draw(Bitmap *dst, int x, int y, int maxWidth, float cellHeight,
	                  const std::string &utf8, int offsetX, bool allowPartial) {
		if (!ready_ || dst == 0 || !dst->valid() || dst->bitCount() != 8) return;
		if (utf8.empty() || cellHeight <= 0.0f) return;

		SetSize(cellHeight);

		int limit = dst->width() - x;
		if (maxWidth > 0 && maxWidth < limit) limit = maxWidth;
		if (limit <= 0) return;

		// 左へずらすぶん、ペンは枠の外から始まる。
		int penX = -offsetX;
		size_t pos = 0;
		while (pos < utf8.size()) {
			const uint32_t cp = NextCodepoint(utf8, &pos);
			const Glyph *g = GetGlyph(cp);
			if (g == 0) continue;
			if (penX >= limit) break;
			// まだ枠の左側にいる字は飛ばす（スクロールしたぶん）。
			if (penX + g->advance <= 0) {
				penX += g->advance;
				continue;
			}
			// 入りきらない字を出さない指定のときは、そこで打ち切る。
			if (!allowPartial && penX + g->advance > limit && penX > 0) break;
			BlitGlyph(dst, x + penX, y, limit - penX, *g);
			penX += g->advance;
		}
	}

	virtual int Measure(float cellHeight, const std::string &utf8) {
		if (!ready_ || utf8.empty() || cellHeight <= 0.0f) return 0;
		SetSize(cellHeight);

		int w = 0;
		size_t pos = 0;
		while (pos < utf8.size()) {
			const uint32_t cp = NextCodepoint(utf8, &pos);
			const Glyph *g = GetGlyph(cp);
			if (g == 0) continue;
			w += g->advance;
		}
		return w;
	}

private:
	struct Glyph {
		std::vector<uint8_t> bits;  // w*h のカバレッジ
		int w, h;
		int xoff, yoff;  // ベースラインからの位置
		int advance;

		Glyph() : w(0), h(0), xoff(0), yoff(0), advance(0) {}
	};

	// 焼いたグリフは使い回す。曲名 (14px) とファイラーの行 (10/13px) が
	// 交互に来るので、サイズごとに持たないと焼き直しになる。
	typedef std::map<uint32_t, Glyph> GlyphMap;

	struct SizeCache {
		float scale;
		int baseline;
		GlyphMap glyphs;

		SizeCache() : scale(0.0f), baseline(0) {}
	};

	void SetSize(float cellHeight) {
		// 出力解像度に合わせると 17.5px のような半端な値になるので、
		// 1/4 px の粒度でキャッシュを引く。
		const int key = (int)(cellHeight * 4.0f + 0.5f);
		if (key == sizeKey_ && current_ != 0) return;
		sizeKey_ = key;

		SizeCache &sc = sizes_[key];
		if (sc.scale == 0.0f) {
			// 行の高さ (ascent - descent) が cellHeight に収まる倍率。
			// GDI の lfHeight が「セル高」だったのと同じ考え方。
			sc.scale = stbtt_ScaleForPixelHeight(&font_, key / 4.0f);
			sc.baseline = (int)(ascent_ * sc.scale + 0.5f);
		}
		current_ = &sc;
		scale_ = sc.scale;
		baseline_ = sc.baseline;
	}

	const Glyph *GetGlyph(uint32_t cp) {
		if (cp == '\r' || cp == '\n' || cp == '\t') cp = ' ';
		GlyphMap &glyphs = current_->glyphs;
		GlyphMap::iterator it = glyphs.find(cp);
		if (it != glyphs.end()) return &it->second;

		Glyph g;
		int adv = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&font_, (int)cp, &adv, &lsb);
		g.advance = (int)(adv * scale_ + 0.5f);

		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		stbtt_GetCodepointBitmapBox(&font_, (int)cp, scale_, scale_, &x0, &y0, &x1, &y1);
		g.w = x1 - x0;
		g.h = y1 - y0;
		g.xoff = x0;
		g.yoff = y0;
		if (g.w > 0 && g.h > 0) {
			g.bits.assign((size_t)g.w * g.h, 0);
			stbtt_MakeCodepointBitmap(&font_, &g.bits[0], g.w, g.h, g.w, scale_, scale_,
			                          (int)cp);
			const uint8_t *gamma = CoverageTable();
			for (size_t i = 0; i < g.bits.size(); i++) g.bits[i] = gamma[g.bits[i]];
		}

		glyphs[cp] = g;
		return &glyphs[cp];
	}

	void BlitGlyph(Bitmap *dst, int x, int y, int limit, const Glyph &g) {
		if (g.w <= 0 || g.h <= 0) return;

		for (int gy = 0; gy < g.h; gy++) {
			const int py = y + baseline_ + g.yoff + gy;
			if (py < 0 || py >= dst->height()) continue;
			uint8_t *q = dst->RowFromTop(py);
			const uint8_t *p = &g.bits[(size_t)gy * g.w];
			for (int gx = 0; gx < g.w; gx++) {
				const int px = x + g.xoff + gx;
				if (px < 0 || px >= dst->width()) continue;
				if (g.xoff + gx >= limit) break;
				if (p[gx] > q[px]) q[px] = p[gx];
			}
		}
	}

	bool ready_;
	std::vector<uint8_t> data_;
	stbtt_fontinfo font_;
	int ascent_, descent_, lineGap_;

	int sizeKey_;  // cellHeight を 1/4 px 単位にしたもの
	float scale_;
	int baseline_;
	std::map<int, SizeCache> sizes_;
	SizeCache *current_;

	StbTextRenderer(const StbTextRenderer &);
	StbTextRenderer &operator=(const StbTextRenderer &);
};

}  // namespace

TextRenderer *CreateTextRenderer(const std::vector<std::string> &searchDirs) {
	StbTextRenderer *r = new StbTextRenderer();
	if (r->Load(searchDirs)) return r;
	delete r;
	return new NullTextRenderer();
}

}  // namespace mxv2
