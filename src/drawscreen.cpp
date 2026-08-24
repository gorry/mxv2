// mxv2 - 描画コア（旧 mxv/draw.cpp の移植）

#include "drawscreen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bmpfile.h"
#include "fileutil.h"

namespace mxv2 {

namespace {

// レイアウト定数はスキン (skin.h) が持つ。既定値は旧 mxv/draw.cpp と同じ。

int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }

// 5x7 での退避描画に流す最大文字数。はみ出した分はブリッタ側でクリップされる
// ので、画面幅を越えられる長さがあれば十分。
const size_t kMaxAsciiChars = 128;

// マスクしているチャンネルの鍵盤に乗せる灰色と、その濃さ (0..100)。
// 押している鍵の色がうっすら透けるくらいにしてある。
const int kMaskGray = 96;
const int kMaskAlpha = 60;

// 音量の「まだ一度も描いていない」印。-100..+100 のどれとも重ならない値。
const int kVolumeNever = -1000;

// 操作ボタンの「まだ一度も描いていない」印。
// **0 を使ってはいけない**。全ボタンが上がっていて LED も消えている状態が
// ちょうど 0 なので、背景を描き直した直後（演奏前など）に PutPlayKey を
// 呼んでも「前回と同じ」と見なされ、ボタンが消えたままになる。
const uint32_t kPlayKeyStatusNever = 0xffffffffu;

// スクロールバーの部品の当たり判定。pos は {x, y}、src の w/h を大きさに使う。
bool InRect(int x, int y, const int pos[2], const Xywh &src) {
	return x >= pos[0] && x < pos[0] + src.w && y >= pos[1] && y < pos[1] + src.h;
}

// 5x7 フォントで描ける形（ASCII 大文字）に落とす。文字描画が使えない
// プラットフォーム向けの退避用。
std::string ToAscii(const std::string &utf8, size_t maxLen) {
	std::string out;
	out.reserve(utf8.size());
	for (size_t i = 0; i < utf8.size() && out.size() < maxLen; i++) {
		unsigned char c = (unsigned char)utf8[i];
		if (c >= 0x80) {
			if ((c & 0xc0) == 0x80) continue;  // UTF-8 の後続バイト
			out += '?';
			continue;
		}
		if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
		if (c < 0x20) c = ' ';
		out += (char)c;
	}
	return out;
}

}  // namespace

// ---------------------------------------------------------------------------

DrawScreen::DrawScreen()
    : skin_(0),
      textLayer_(0),
      fileListFontSize_(0),
      channelMask_(0),
      scrollBarFlags_(0),
      scrollBarThumb_(0),
      playKeyStatusLast_(kPlayKeyStatusNever),
      progressBarLenLast_(-1),
      progressNowSecLast_(-1),
      totalVolBarLast_(kVolumeNever),
      fileListCursorLast_(-1),
      fileListOffsetLast_(0) {
	memset(kbPalette_, 0, sizeof(kbPalette_));
	memset(palLevelMeter_, 0, sizeof(palLevelMeter_));
}

DrawScreen::~DrawScreen() {}

bool DrawScreen::Init(const Skin *skin, std::string *err) {
	InitBlendTables();

	if (skin == 0) {
		*err = "スキンが指定されていません。";
		return false;
	}
	skin_ = skin;

	if (!screen_.Create(width(), height(), 24) || !back_.Create(width(), height(), 24) ||
	    !backBitmap_.Create(width(), height(), 24)) {
		*err = "画面バッファを確保できません。";
		return false;
	}

	colors_ = Colors();
	colors_.Load(skin_->FindColorsFile());

	if (!LoadAssets(err)) return false;

	Reload();
	return true;
}

// ---------------------------------------------------------------------------
// 素材の読み込み
// ---------------------------------------------------------------------------

bool DrawScreen::LoadAssets(std::string *err) {
	Bitmap kb1, kb2;

	// ファイル名はスキンが持つ（layout.ini の各セクションの Img* キー）。
	struct Item {
		const std::string *name;
		Bitmap *dst;
	};
	const Item items[] = {
		{ &skin_->kb0Bitmap, &kb0_ },
		{ &skin_->kb1Bitmap, &kb1 },
		{ &skin_->kb2Bitmap, &kb2 },
		{ &skin_->font5x7Bitmap, &font_ },
		{ &skin_->levelMeterBitmap, &levelMeter_ },
		{ &skin_->bannerBitmap, &banner_ },
		{ &skin_->playKeyBitmap, &playKey_ },
		{ &skin_->progressBarBitmap, &progressBarBase_ },
		{ &skin_->volBarBitmap, &totalVolBarBase_ },
		{ &skin_->scrollBarBitmap, &scrollBarBase_ },
	};
	for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		// スキンのフォルダ -> 土台のフォルダ の順に探す。
		if (!LoadBmpFile(skin_->FindFile(*items[i].name), items[i].dst, err)) return false;
	}

	// 鍵ビットマップの切り出し。奇数番の音は kb2 (黒鍵) から取る。
	// パレット 0x11 を 1 (影)、0x12+n を 2 (点灯色) へ寄せる。
	static const int kUseKb2[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	for (int i = 0; i < 12; i++) {
		const Bitmap &src = kUseKb2[i] ? kb2 : kb1;
		CutKeyboardBitmap(&keyboard_[i], src, skin_->kbXOffset[i], 7, 0x11, 0x12 + i);
	}
	for (int i = 0; i < 12; i++) {
		kbPalette_[i] = kb1.palette()[i + 0x12];
	}

	// レベルメータの点灯色 / 消灯色
	for (int i = 0; i < skin_->levelMeterWidthCells * 2; i++) {
		palLevelMeter_[i] = levelMeter_.palette()[i + skin_->levelMeterPalOfs];
	}

	// 組み立て用のバッファ
	if (!progressBar_.Create(skin_->progW, skin_->progH, 8)) {
		*err = "プログレスバーのバッファを確保できません。";
		return false;
	}
	memcpy(progressBar_.palette(), progressBarBase_.palette(), sizeof(Rgb) * 256);

	if (!totalVolBar_.Create(skin_->volW, skin_->volH, 8)) {
		*err = "音量バーのバッファを確保できません。";
		return false;
	}
	memcpy(totalVolBar_.palette(), totalVolBarBase_.palette(), sizeof(Rgb) * 256);

	if (!scrollBar_.Create(skin_->scrollW, skin_->scrollH, 8)) {
		*err = "スクロールバーのバッファを確保できません。";
		return false;
	}
	memcpy(scrollBar_.palette(), scrollBarBase_.palette(), sizeof(Rgb) * 256);

	// 操作ボタンの LED は消灯状態から始める
	playKey_.SetPalette(skin_->palPlayLed, 25, 25, 25);
	playKey_.SetPalette(skin_->palPauseLed, 25, 25, 25);
	playKey_.SetPalette(skin_->palContLed, 25, 25, 25);
	playKey_.SetPalette(skin_->palRepeatLed, 25, 25, 25);

	return true;
}

void DrawScreen::CutKeyboardBitmap(Bitmap *out, const Bitmap &src, int xsrc, int cutWidth,
                                   int pal1, int pal2) {
	out->Create(cutWidth, src.height(), 8);
	for (int y = 0; y < src.height(); y++) {
		const uint8_t *p = src.RowFromTop(y) + xsrc;
		uint8_t *q = out->RowFromTop(y);
		for (int x = 0; x < cutWidth; x++) {
			int c = p[x];
			q[x] = (uint8_t)((c == pal1) ? 1 : (c == pal2) ? 2 : 0);
		}
	}
}

// ---------------------------------------------------------------------------
// 背景合成
// ---------------------------------------------------------------------------

void DrawScreen::LoadBackBitmap() {
	BmpFill(&backBitmap_, 0, 0, width(), height(), 0, 0, 0, 100);
	if (!colors_.back.bitmap) return;

	Bitmap image;
	std::string err;
	if (!LoadBmpFile(skin_->FindFile(skin_->backBitmap), &image, &err)) return;
	BmpCopy(&backBitmap_, 0, 0, width(), height(), &image, 0, 0, 100);
}

void DrawScreen::CompositeBanner() {
	BmpCopy(&back_, skin_->bannerX, skin_->bannerY, skin_->bannerW, skin_->bannerH, &banner_, 0, 0, kBlendMul);
}

void DrawScreen::CompositeStatusBack() {
	for (int i = 0; i < 9; i++) {
		int x = 0, y = 0, w = 0, h = 0;
		if (!StatusRect(i, &x, &y, &w, &h)) continue;
		BmpFill(&back_, x, y, w, h, colors_.status.backColor.r, colors_.status.backColor.g,
		        colors_.status.backColor.b, colors_.status.backColorBright);
	}
}

// ステータス欄 1 段ぶんの矩形（0..7 = FM ch.1-8 / 8 = PCM）。
// 下地を敷く場所とクリックの当たり判定で同じものを使う。
bool DrawScreen::StatusRect(int row, int *x, int *y, int *w, int *h) const {
	if (row < 0 || row >= 9 || skin_ == 0) return false;
	*x = skin_->statusX;
	*y = skin_->statusY + skin_->chYOffset[row];
	*w = skin_->statusBackW;
	*h = skin_->statusBackH;
	return true;
}

void DrawScreen::CompositeFileList() {
	BmpFill(&back_, skin_->fileListX, skin_->fileListY, skin_->fileListW, skin_->fileListH, colors_.filer.backColor.r,
	        colors_.filer.backColor.g, colors_.filer.backColor.b, colors_.filer.backColorBright);
}

void DrawScreen::CompositeKeyboard() {
	kb0_.SetPalette(1, colors_.kb.blackBright, colors_.kb.blackBright, colors_.kb.blackBright);
	kb0_.SetPalette(2, colors_.kb.whiteBright, colors_.kb.whiteBright, colors_.kb.whiteBright);
	for (int i = 0; i < 9; i++) {
		BmpCopy(&back_, skin_->kbX, skin_->kbY + skin_->chYOffset[i] + skin_->kbYOffset, kb0_.width(), kb0_.height(),
		        &kb0_, 0, 0, kBlendMul);
	}
}

void DrawScreen::CompositeBack() {
	BmpFill(&back_, 0, 0, width(), height(), 0, 0, 0, 100);
	BmpCopy(&back_, 0, 0, width(), height(), &backBitmap_, 0, 0, colors_.back.bitmapBright);
	BmpFill(&back_, 0, 0, width(), height(), colors_.back.color.r, colors_.back.color.g,
	        colors_.back.color.b, colors_.back.colorBright);

	CompositeBanner();
	CompositeStatusBack();
	CompositeFileList();
	CompositeKeyboard();
}

void DrawScreen::Reload() {
	LoadBackBitmap();
	CompositeBack();

	BmpCopy(&screen_, 0, 0, width(), height(), &back_, 0, 0, 100);
	// 文字レイヤーも一度消す。行と曲名はこの後で描き直される。
	if (textLayer_ != 0) textLayer_->ClearAll();

	playKeyStatusLast_ = kPlayKeyStatusNever;
	progressBarLenLast_ = -1;
	progressNowSecLast_ = -1;
	totalVolBarLast_ = kVolumeNever;
	fileListLast_.clear();
	fileListCursorLast_ = -1;

	// 演奏中ならこの後ポーリングが本当の値を積み直す（Player::
	// RequestStatusRefresh）。始まる前と止まっている間は 0 のまま。
	PutStatusZero();
	PutMDXTitle(mdxTitle_);
}

// ---------------------------------------------------------------------------
// 転送
// ---------------------------------------------------------------------------

void DrawScreen::BlitTo(Screen *out) const {
	if (out == 0 || out->pixels() == 0) return;
	const int w = Min(width(), out->width());
	const int h = Min(height(), out->height());
	for (int y = 0; y < h; y++) {
		const uint8_t *p = screen_.RowFromTop(y);
		uint32_t *q = out->pixels() + (size_t)y * out->width();
		for (int x = 0; x < w; x++) {
			q[x] = 0xff000000u | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
			p += 3;
		}
	}
	OverlayChannelMask(out);
}

// チャンネル ch (0..15) の鍵盤の矩形。
//
// FM は 1 段まるごと。PCM は 8ch で 1 段を共有しているので、横に 8 等分して
// 左から ch.P..W に割り当てる。灰色を乗せる場所とクリックの当たり判定が
// 食い違わないよう、両方ここから取る。
bool DrawScreen::ChannelKeyRect(int ch, int *x0, int *y0, int *x1, int *y1) const {
	if (ch < 0 || ch >= 16 || skin_ == 0 || !kb0_.valid()) return false;

	const int kbW = kb0_.width();
	const int kbH = kb0_.height();
	const int row = (ch < 8) ? ch : 8;

	*x0 = skin_->kbX;
	*x1 = skin_->kbX + kbW;
	if (ch >= 8) {
		const int i = ch - 8;
		*x0 = skin_->kbX + kbW * i / 8;
		*x1 = skin_->kbX + kbW * (i + 1) / 8;
	}
	*y0 = skin_->kbY + skin_->chYOffset[row] + skin_->kbYOffset;
	*y1 = *y0 + kbH;
	return true;
}

// マスクしているチャンネルの鍵盤を灰色で伏せる。
//
// キャンバスではなく転送先へ乗せるので、キャンバスの中身（鍵盤の押下状態）は
// そのまま残る。マスクを外せば次のフレームから元どおり見える。
void DrawScreen::OverlayChannelMask(Screen *out) const {
	if (channelMask_ == 0) return;

	const int outW = out->width();
	const int outH = out->height();

	for (int ch = 0; ch < 16; ch++) {
		if ((channelMask_ & (1 << ch)) == 0) continue;

		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!ChannelKeyRect(ch, &x0, &y0, &x1, &y1)) continue;

		for (int y = Max(0, y0); y < Min(outH, y1); y++) {
			uint32_t *q = out->pixels() + (size_t)y * outW;
			for (int x = Max(0, x0); x < Min(outW, x1); x++) {
				const uint32_t c = q[x];
				const int r = (int)((c >> 16) & 0xff);
				const int g = (int)((c >> 8) & 0xff);
				const int b = (int)(c & 0xff);
				const int nr = (r * (100 - kMaskAlpha) + kMaskGray * kMaskAlpha) / 100;
				const int ng = (g * (100 - kMaskAlpha) + kMaskGray * kMaskAlpha) / 100;
				const int nb = (b * (100 - kMaskAlpha) + kMaskGray * kMaskAlpha) / 100;
				q[x] = 0xff000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) |
				       (uint32_t)nb;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// 文字描画 (5x7 フォント)
// ---------------------------------------------------------------------------

void DrawScreen::Print(int x, int y, const char *msg, const Rgb &color, int alpha) {
	font_.SetPalette(1, color.r, color.g, color.b);
	for (const char *p = msg; *p != '\0'; p++) {
		int c = (unsigned char)*p;
		if (c < 0x20) c = 0x20;
		if (c > 0x6f) c = 0x6f;
		c -= 0x20;
		BmpCopyTransparent(&screen_, x, y, 5, 7, &font_, (c & 0x0f) * 5, (c >> 4) * 7, alpha);
		x += skin_->fontW;
	}
}

void DrawScreen::PrintCompose(int x, int y, const char *msg, const Rgb &color, int alpha) {
	font_.SetPalette(1, color.r, color.g, color.b);
	for (const char *p = msg; *p != '\0'; p++) {
		int c = (unsigned char)*p;
		if (c < 0x20) c = 0x20;
		if (c > 0x6f) c = 0x6f;
		c -= 0x20;
		BmpCopyComposite(&screen_, x, y, 5, 7, &font_, (c & 0x0f) * 5, (c >> 4) * 7, &back_, x,
		                 y, alpha);
		x += skin_->fontW;
	}
}

void DrawScreen::PutStatusText(int x, int y, int cells, const char *text) {
	(void)cells;
	PrintCompose(x, y, text, colors_.status.color, colors_.status.colorBright);
}

// ---------------------------------------------------------------------------
// 鍵盤
// ---------------------------------------------------------------------------

void DrawScreen::PutNoteOn(int key, int row, int color, int bendMode) {
	if (row < 0 || row >= 9) return;
	key += skin_->keyOffset;
	if (key < 0) return;
	const int oct = key / 12;
	const int note = key % 12;
	Bitmap &b = keyboard_[note];
	if (!b.valid()) return;

	b.palette()[2] = kbPalette_[color % 12];

	const int x = skin_->kbX + skin_->kbXOffset[note] + skin_->kbXOffset[12] * oct - skin_->kbXOffset[skin_->keyOffset];
	const int y = skin_->kbY + skin_->chYOffset[row] + skin_->kbYOffset;
	BmpCopyTransparent(&screen_, x, y, b.width(), b.height(), &b, 0, 0,
	                   bendMode ? colors_.kb.bright / 2 : colors_.kb.bright);
}

void DrawScreen::PutNoteOff(int key, int row) {
	if (row < 0 || row >= 9) return;
	key += skin_->keyOffset;
	if (key < 0) return;
	const int oct = key / 12;
	const int note = key % 12;
	const Bitmap &b = keyboard_[note];
	if (!b.valid()) return;

	const int x = skin_->kbX + skin_->kbXOffset[note] + skin_->kbXOffset[12] * oct - skin_->kbXOffset[skin_->keyOffset];
	const int y = skin_->kbY + skin_->chYOffset[row] + skin_->kbYOffset;
	BmpCopy(&screen_, x, y, b.width(), b.height(), &back_, x, y, 100);
}

// ---------------------------------------------------------------------------
// ステータス表示 (FM 0..7)
// ---------------------------------------------------------------------------

void DrawScreen::PutVolume(int volume, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	if (volume >= 128) {
		snprintf(s, sizeof(s), "V%03d", 127 - (volume & 127));
	} else {
		snprintf(s, sizeof(s), "V%-3d", volume);
	}
	PutStatusText(2 + skin_->statusX, skin_->chYOffset[row] + 0 + skin_->statusY, 4, s);
}

void DrawScreen::PutPanpot(int panpot, int row) {
	if (row < 0 || row >= 9) return;
	char s[2];
	s[0] = "-LRC"[panpot & 3];
	s[1] = '\0';
	PutStatusText(122 + skin_->statusX, skin_->chYOffset[row] + 0 + skin_->statusY, 1, s);
}

void DrawScreen::PutDetune(int detune, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	const int v = (int16_t)detune;
	snprintf(s, sizeof(s), "D%c%04d", (v >= 0) ? '+' : '-', abs(v) % 10000);
	PutStatusText(2 + skin_->statusX, skin_->chYOffset[row] + 9 + skin_->statusY, 6, s);
}

void DrawScreen::PutVoice(int voice, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "@%03d", voice % 1000);
	PutStatusText(40 + skin_->statusX, skin_->chYOffset[row] + 9 + skin_->statusY, 4, s);
}

void DrawScreen::PutQ(int q, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "Q%03d", q % 1000);
	PutStatusText(66 + skin_->statusX, skin_->chYOffset[row] + 9 + skin_->statusY, 4, s);
}

void DrawScreen::PutPtr(int ptr, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "$%05X", (unsigned)ptr & 0xfffff);
	PutStatusText(92 + skin_->statusX, skin_->chYOffset[row] + 9 + skin_->statusY, 6, s);
}

void DrawScreen::PutLFOPitch(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	const int t = (int16_t)v;
	snprintf(s, sizeof(s), "P%c%04d", (t >= 0) ? '+' : '-', abs(t) % 10000);
	PutStatusText(2 + skin_->statusX, skin_->chYOffset[row] + 18 + skin_->statusY, 6, s);
}

void DrawScreen::PutLFOPitch1(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[2];
	s[0] = (char)(0x65 + v);
	s[1] = '\0';
	PutStatusText(40 + skin_->statusX, skin_->chYOffset[row] + 18 + skin_->statusY, 1, s);
}

void DrawScreen::PutLFOPitch2(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%04d", (uint16_t)v % 10000);
	PutStatusText(46 + skin_->statusX, skin_->chYOffset[row] + 18 + skin_->statusY, 4, s);
}

void DrawScreen::PutLFOPitch3(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%c%04d", (v >= 0) ? '+' : '-', abs(v) % 10000);
	PutStatusText(72 + skin_->statusX, skin_->chYOffset[row] + 18 + skin_->statusY, 5, s);
}

void DrawScreen::PutLFOPitch4(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "D%03d", (uint16_t)v % 1000);
	PutStatusText(104 + skin_->statusX, skin_->chYOffset[row] + 18 + skin_->statusY, 4, s);
}

void DrawScreen::PutLFOVolume(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	const int t = (int16_t)v;
	snprintf(s, sizeof(s), "A%c%04d", (t >= 0) ? '+' : '-', abs(t) % 10000);
	PutStatusText(2 + skin_->statusX, skin_->chYOffset[row] + 27 + skin_->statusY, 6, s);
}

void DrawScreen::PutLFOVolume1(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[2];
	s[0] = (char)(0x65 + v);
	s[1] = '\0';
	PutStatusText(40 + skin_->statusX, skin_->chYOffset[row] + 27 + skin_->statusY, 1, s);
}

void DrawScreen::PutLFOVolume2(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%04d", (uint16_t)v % 10000);
	PutStatusText(46 + skin_->statusX, skin_->chYOffset[row] + 27 + skin_->statusY, 4, s);
}

void DrawScreen::PutLFOVolume3(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%c%04d", (v >= 0) ? '+' : '-', abs(v) % 10000);
	PutStatusText(72 + skin_->statusX, skin_->chYOffset[row] + 27 + skin_->statusY, 5, s);
}

void DrawScreen::PutLevelMeter(const char *levelMeterInfo, int row) {
	if (row < 0 || row >= 9 || !levelMeter_.valid()) return;

	// 点灯しているセルだけ明るいパレットに差し替える。
	for (int i = 0; i < skin_->levelMeterWidthCells; i++) {
		levelMeter_.palette()[i + skin_->levelMeterPalOfs] =
		    levelMeterInfo[i] ? palLevelMeter_[i] : palLevelMeter_[i + skin_->levelMeterWidthCells];
	}

	const int skip = 24 + 6 + 2;
	const int w = levelMeter_.width() - skip;
	const int h = levelMeter_.height();
	const int x = 26 + skin_->statusX;
	const int y = skin_->chYOffset[row] + 0 + skin_->statusY;
	BmpCopyComposite(&screen_, x, y, w, h, &levelMeter_, skip, 0, &back_, x, y,
	                 colors_.status.colorBright);
}

// ---------------------------------------------------------------------------
// ステータス表示 (PCM 8..15)
// ---------------------------------------------------------------------------

void DrawScreen::PutPCMVolume(int volume, int row) {
	const int i = row - 8;
	if (i < 0 || i >= 8) return;
	char s[64];
	if (volume >= 128) {
		snprintf(s, sizeof(s), "V%03d", 127 - (volume & 127));
	} else {
		snprintf(s, sizeof(s), "V%-3d", volume);
	}
	PutStatusText(skin_->pcmXOffset[i] + 2 + skin_->statusX,
	              skin_->pcmYOffset[i] + skin_->chYOffset[8] + 0 + skin_->statusY, 4, s);
}

void DrawScreen::PutPCMPtr(int ptr, int row) {
	const int i = row - 8;
	if (i < 0 || i >= 8) return;
	char s[64];
	snprintf(s, sizeof(s), "$%05X", (unsigned)ptr & 0xfffff);
	PutStatusText(skin_->pcmXOffset[i] + 24 + skin_->statusX,
	              skin_->pcmYOffset[i] + skin_->chYOffset[8] + 0 + skin_->statusY, 6, s);
}

// 演奏を始める前のステータス欄。項目と並びを 1 か所で決めたいので、
// 実際に値が届いたときと同じ Put* を 0 で呼ぶ。
// 出す顔ぶれは StatusWatch::Poll が積むものに合わせてある（FM 8 段は全項目、
// PCM の段は 8 スロットの音量とポインタだけで、レベルメータは無い）。
void DrawScreen::PutStatusZero() {
	for (int row = 0; row < 8; row++) {
		PutVolume(0, row);
		PutPanpot(0, row);
		PutDetune(0, row);
		PutVoice(0, row);
		PutQ(0, row);
		PutPtr(0, row);
		PutLFOPitch(0, row);
		PutLFOPitch1(0, row);
		PutLFOPitch2(0, row);
		PutLFOPitch3(0, row);
		PutLFOPitch4(0, row);
		PutLFOVolume(0, row);
		PutLFOVolume1(0, row);
		PutLFOVolume2(0, row);
		PutLFOVolume3(0, row);

		// セル数はスキン持ち ([LevelMeter] Cells) なので、その数だけ消す。
		std::vector<char> meter(skin_->levelMeterWidthCells > 0
		                            ? (size_t)skin_->levelMeterWidthCells
		                            : 1,
		                        0);
		PutLevelMeter(&meter[0], row);
	}
	for (int i = 0; i < 8; i++) {
		PutPCMVolume(0, 8 + i);
		PutPCMPtr(0, 8 + i);
	}
}

// ---------------------------------------------------------------------------
// 曲名
// ---------------------------------------------------------------------------

void DrawScreen::PutMDXTitle(const std::string &titleUtf8) {
	mdxTitle_ = titleUtf8;

	// 背景を戻してからタイトル欄の下地を敷く
	BmpCopy(&screen_, skin_->titleX, skin_->titleY, skin_->titleW, skin_->titleH, &back_, skin_->titleX, skin_->titleY, 100);
	BmpFill(&screen_, skin_->titleX, skin_->titleY, skin_->titleW, skin_->titleH, colors_.mdxTitle.backColor.r,
	        colors_.mdxTitle.backColor.g, colors_.mdxTitle.backColor.b,
	        colors_.mdxTitle.backColorBright);

	if (titleUtf8.empty()) return;

	if (textLayer_ != 0 && textLayer_->available()) {
		// 文字はキャンバスではなく出力解像度のレイヤーへ描く。
		textLayer_->ClearRect(skin_->titleX, skin_->titleY, skin_->titleW, skin_->titleH);
		textLayer_->DrawText(skin_->titleX + 4, skin_->titleY, skin_->titleW - 8, skin_->titleH, titleUtf8,
		                     colors_.mdxTitle.color, colors_.mdxTitle.colorBright);
		return;
	}

	// フォントが読めなかったときの非常用。5x7 は本来ビジュアライザ用なので、
	// ここへ落ちている時点で assets の同梱フォントが失われている。
	Print(skin_->titleX + 4, skin_->titleY + 3, ToAscii(titleUtf8, kMaxAsciiChars).c_str(),
	      colors_.mdxTitle.color, colors_.mdxTitle.colorBright);
}

// ---------------------------------------------------------------------------
// ファイラー
// ---------------------------------------------------------------------------

int DrawScreen::fileListRows() const {
	return skin_->fileListRows[fileListFontSize_ & 1];
}

int DrawScreen::fileListItemH() const {
	return skin_->fileListItemH[fileListFontSize_ & 1];
}

void DrawScreen::SetFileListFontSize(int size) {
	fileListFontSize_ = (size & 1);
	fileListLast_.clear();
	fileListCursorLast_ = -1;
}

void DrawScreen::PutFileList(const Filer &filer, bool refresh) {
	const int rows = fileListRows();
	const int fs = fileListFontSize_ & 1;  // 0=小さい文字 / 1=大きい文字
	const int itemH = skin_->fileListItemH[fs];
	const int top = filer.top();
	const int cursor = filer.cursor();

	// スクロール位置の端数 (0..itemH-1)。ドラッグ中だけ 0 以外になる。
	// 端数があるぶん全体が上へずれるので、上下の端に半端な行が出る。
	// その 1 行ぶん多く回して、はみ出しは矩形を切って描く。
	const int offset = filer.topOffsetPx();
	const int drawRows = (offset > 0) ? rows + 1 : rows;
	const int listTop = skin_->fileListY;
	const int listBottom = skin_->fileListY + skin_->fileListH;

	// 端数が変わると行と画素の対応がまるごとずれるので、行ごとの差分は
	// 使えない。ドラッグ中は毎フレーム全部描き直す（文字はグリフを
	// キャッシュしてあるので、焼き直しではなく転送だけで済む）。
	if (fileListOffsetLast_ != offset) {
		fileListOffsetLast_ = offset;
		refresh = true;
	}
	if ((int)fileListLast_.size() != rows + 1) {
		fileListLast_.assign(rows + 1, FileItem());
		refresh = true;
	}

	for (int i = 0; i < drawRows; i++) {
		const int j = top + i;
		bool redraw = refresh;

		FileItem shown;
		if (j < filer.itemCount()) shown = filer.item(j);

		if (fileListLast_[i].baseName != shown.baseName ||
		    fileListLast_[i].title != shown.title || fileListLast_[i].type != shown.type) {
			fileListLast_[i] = shown;
			redraw = true;
		}
		// カーソルが出入りした行は描き直す
		if (fileListCursorLast_ != cursor && (j == fileListCursorLast_ || j == cursor)) {
			redraw = true;
		}
		if (!redraw) continue;

		const int x = skin_->fileListX;
		// 文字を置く基準は切る前の行の上辺。ここを動かすと字が縦に潰れる。
		const int rowY = listTop + i * itemH - offset;
		int y = rowY;
		int h = itemH;
		if (y < listTop) {
			h -= (listTop - y);
			y = listTop;
		}
		if (y + h > listBottom) h = listBottom - y;
		if (h <= 0) break;

		// 背景を戻す -> カーソル -> 文字
		// 背景とカーソルはキャンバス側、文字は出力解像度のレイヤー側。
		BmpCopy(&screen_, x, y, skin_->fileListW, h, &back_, x, y, 100);
		if (j == cursor && j < filer.itemCount()) {
			BmpFillMul(&screen_, x, y, skin_->fileListW, h, colors_.filer.cursorColor.r,
			           colors_.filer.cursorColor.g, colors_.filer.cursorColor.b,
			           colors_.filer.cursorColorBright);
		}
		if (textLayer_ != 0) textLayer_->ClearRect(x, y, skin_->fileListW, h);
		if (shown.baseName.empty() && shown.title.empty()) continue;

		// 種別で文字色を変える。"[Setting]" は MDX と同じ色。
		Rgb color = colors_.filer.color;
		if (shown.type & kFileItemFileSystem) {
			color = colors_.filer.fileSystemColor;
		} else if (shown.type & kFileItemDrive) {
			color = colors_.filer.driveColor;
		} else if (shown.type & kFileItemDir) {
			color = colors_.filer.folderColor;
		}

		if (textLayer_ != 0 && textLayer_->available()) {
			// 字は切る前の行位置 (rowY) に置き、はみ出しは y..y+h で切る。
			textLayer_->DrawText(x + skin_->fileListBaseNameX[fs], rowY,
			                     skin_->fileListBaseNameW[fs], itemH, shown.baseName, color,
			                     colors_.filer.colorBright, y, h);
			if (!shown.title.empty()) {
				textLayer_->DrawText(x + skin_->fileListTitleX[fs], rowY,
				                     skin_->fileListTitleW[fs], itemH, shown.title, color,
				                     colors_.filer.colorBright, y, h);
			}
		} else {
			// フォントが読めなかったときの非常用（5x7 は本来ビジュアライザ用）。
			// こちらは縦に切れないので、丸ごと入る行だけ描く。
			if (h >= itemH) {
				Print(x + skin_->fileListBaseNameX[fs], rowY + 1,
				      ToAscii(shown.baseName, kMaxAsciiChars).c_str(), color,
				      colors_.filer.colorBright);
				if (!shown.title.empty()) {
					Print(x + skin_->fileListTitleX[fs], rowY + 1,
					      ToAscii(shown.title, kMaxAsciiChars).c_str(), color,
					      colors_.filer.colorBright);
				}
			}
		}
	}

	// 最終行の余りを背景で埋める（行数 * 行高がぴったりでないスキン用）
	{
		const int y = listTop + drawRows * itemH - offset;
		int h = listBottom - y;
		if (h > 0) {
			BmpCopy(&screen_, skin_->fileListX, y, skin_->fileListW, h, &back_, skin_->fileListX, y, 100);
			if (textLayer_ != 0) textLayer_->ClearRect(skin_->fileListX, y, skin_->fileListW, h);
		}
	}

	fileListCursorLast_ = cursor;
}

// ---------------------------------------------------------------------------
// スクロールバー
// ---------------------------------------------------------------------------

void DrawScreen::SetScrollBarThumb(int y) {
	scrollBarThumb_ = Max(0, Min(scrollBarMovement(), y));
}

// topPx / maxTopPx は Filer のスクロール位置（画素）。行番号ではなく画素で
// 受けるので、ファイラーを画素単位で送るとつまみも同じだけ滑らかに動く。
void DrawScreen::PutScrollBar(int topPx, int maxTopPx) {
	if (!scrollBar_.valid()) return;

	// つまみ自体をドラッグしている間は、掴んだ位置のまま動かす。
	// 旧 mxv の MX_PUTSCROLLBAR_FLAG_DRAG。
	if ((scrollBarFlags_ & kScrollBarDrag) == 0) {
		SetScrollBarThumb((maxTopPx > 0) ? (scrollBarMovement() * topPx / maxTopPx) : 0);
	}

	// 部品の位置と切り出しはスキンが持つ（layout.ini の [ScrollBar] Src* / Pos*）。
	// 書かれない隙間はパレット 0 = 透明にしたいので、まず消す。
	BmpFill(&scrollBar_, 0, 0, skin_->scrollW, skin_->scrollH, 0, 0, 0, 100);

	const Xywh &up = skin_->scrollSrcUpArrow;
	const Xywh &bar = skin_->scrollSrcBar;
	const Xywh &down = skin_->scrollSrcDownArrow;
	const Xywh &thumb = skin_->scrollSrcThumb;

	BmpCopy(&scrollBar_, skin_->scrollPosUpArrow[0], skin_->scrollPosUpArrow[1], up.w, up.h,
	        &scrollBarBase_, up.x, up.y, 100);
	BmpCopy(&scrollBar_, skin_->scrollPosBar[0], skin_->scrollPosBar[1], bar.w, bar.h,
	        &scrollBarBase_, bar.x, bar.y, 100);
	BmpCopy(&scrollBar_, skin_->scrollPosDownArrow[0], skin_->scrollPosDownArrow[1], down.w, down.h,
	        &scrollBarBase_, down.x, down.y, 100);

	// つまみは溝の中を動く。
	BmpCopy(&scrollBar_, skin_->scrollPosBar[0], skin_->scrollPosBar[1] + scrollBarThumb_, thumb.w,
	        thumb.h, &scrollBarBase_, thumb.x, thumb.y, 100);

	// 矢印の押下表示は通常の矢印と同じ場所に差し替える。原典はここを
	// 高さ CH_D(110) で転送していてバー全体を潰していた。
	if (scrollBarFlags_ & kScrollBarUpArrowDown) {
		const Xywh &s = skin_->scrollSrcUpArrowPress;
		BmpCopy(&scrollBar_, skin_->scrollPosUpArrow[0], skin_->scrollPosUpArrow[1], s.w, s.h,
		        &scrollBarBase_, s.x, s.y, 100);
	}
	if (scrollBarFlags_ & kScrollBarDownArrowDown) {
		const Xywh &s = skin_->scrollSrcDownArrowPress;
		BmpCopy(&scrollBar_, skin_->scrollPosDownArrow[0], skin_->scrollPosDownArrow[1], s.w, s.h,
		        &scrollBarBase_, s.x, s.y, 100);
	}

	// ファイラーと重なっている列には触らない。
	//
	// BmpCopyComposite はパレット 0（透明）の画素に背景 (back_) をそのまま
	// 敷く。つまり「毎フレーム背景で塗り直してから絵を載せる」ので、
	// スクロールバーの矩形がファイラーへ食い込んでいると、
	// ファイラーが差分描画で置いたカーソルの右端がそこだけ消えてしまう
	// （Phone は指で掴みやすいよう、左側 12px を透明な当たり判定にしている。
	//  [FileList] は x 4..464、[ScrollBar] は x 452..476 で 12px 重なる）。
	// 重なりぶんは絵が無いので、描かずに残すのが正しい。
	const int sx = skin_->scrollX;
	const int sy = skin_->scrollY;
	const int sw = skin_->scrollW;
	const int sh = skin_->scrollH;
	const int listRight = skin_->fileListX + skin_->fileListW;
	const int iy0 = Max(sy, skin_->fileListY);
	const int iy1 = Min(sy + sh, skin_->fileListY + skin_->fileListH);

	if (sx >= listRight || iy0 >= iy1) {
		// 重なっていない。今までどおり一度に描く。
		CompositeScrollBar(sx, sy, sw, sh);
		return;
	}
	// 縦に重なっている帯だけ、ファイラーより右の列に限る。
	if (iy0 > sy) CompositeScrollBar(sx, sy, sw, iy0 - sy);
	const int cx = Min(listRight, sx + sw);
	if (cx < sx + sw) CompositeScrollBar(cx, iy0, sx + sw - cx, iy1 - iy0);
	if (iy1 < sy + sh) CompositeScrollBar(sx, iy1, sw, sy + sh - iy1);
}

// スクロールバーの一部分を画面へ合成する。x/y は画面座標。
void DrawScreen::CompositeScrollBar(int x, int y, int w, int h) {
	if (w <= 0 || h <= 0) return;
	BmpCopyComposite(&screen_, x, y, w, h, &scrollBar_, x - skin_->scrollX,
	                 y - skin_->scrollY, &back_, x, y, kBlendMul);
}

// ---------------------------------------------------------------------------
// プログレスバー
// ---------------------------------------------------------------------------

void DrawScreen::PutProgressBar(uint32_t nowTimeMs, uint32_t playTimeMs, bool refresh) {
	if (!progressBar_.valid()) return;

	uint32_t now = nowTimeMs;
	if (playTimeMs != 0 && now > playTimeMs) now = playTimeMs;

	int len = 0;
	if (playTimeMs != 0) len = (int)((int64_t)skin_->progW * now / playTimeMs);

	bool disp = refresh;
	if (progressBarLenLast_ != len) {
		progressBarLenLast_ = len;
		disp = true;
	}
	if (disp) {
		// 進んだ部分は素材の 2 段目、残りは 1 段目。
		BmpCopy(&progressBar_, 0, 0, len, skin_->progH, &progressBarBase_, 0, skin_->progH, 100);
		BmpCopy(&progressBar_, len, 0, skin_->progW - len, skin_->progH, &progressBarBase_, len, 0, 100);
		BmpCopyComposite(&screen_, skin_->progX, skin_->progY, skin_->progW, skin_->progH, &progressBar_, 0, 0, &back_,
		                 skin_->progX, skin_->progY, kBlendMul);
	}

	const int nowSec = (int)(nowTimeMs / 1000);
	disp = refresh;
	if (progressNowSecLast_ != nowSec) {
		progressNowSecLast_ = nowSec;
		disp = true;
	}
	if (disp) {
		int t = Min(nowSec, 99 * 60 + 59);
		int t2 = Min((int)(playTimeMs / 1000), 99 * 60 + 59);
		char s[128];
		snprintf(s, sizeof(s), "PLAY TIME: %02d:%02d / %02d:%02d", t / 60, t % 60, t2 / 60,
		         t2 % 60);
		PrintCompose(skin_->progX + skin_->progTimeXOfs, skin_->progY + skin_->progTimeYOfs, s, colors_.playKey.color,
		             colors_.playKey.colorBright);
	}
}

// ---------------------------------------------------------------------------
// 音量バー
// ---------------------------------------------------------------------------

// 音量 (-100..+100) をつまみの画素位置へ。バーの幅はスキン次第なので、
// ここで初めて画素に落とす。
int DrawScreen::TotalVolBarPosFromVolume(int volume) const {
	const int m = totalVolBarMovement();
	if (m <= 0) return 0;
	return Max(0, Min(m, (volume + 100) * m / 200));
}

// つまみを置いた画素位置から音量 (-100..+100) へ。上の逆。
int DrawScreen::VolumeFromX(int x) const {
	const int m = totalVolBarMovement();
	if (m <= 0) return 0;
	const int pos = Max(0, Min(m, x - (skin_->volX + skin_->volNobW / 2)));
	return pos * 200 / m - 100;
}

void DrawScreen::PutTotalVolBar(int volume, bool refresh) {
	if (!totalVolBar_.valid()) return;

	volume = Max(-100, Min(100, volume));
	bool disp = refresh || (totalVolBarLast_ != volume);
	if (!disp) return;
	totalVolBarLast_ = volume;

	const int barPos = TotalVolBarPosFromVolume(volume);
	BmpCopy(&totalVolBar_, 0, 0, skin_->volRect[1].w, skin_->volRect[1].h, &totalVolBarBase_,
	        skin_->volRect[1].x, skin_->volRect[1].y, 100);
	BmpCopy(&totalVolBar_, barPos, 0, skin_->volRect[0].w, skin_->volRect[1].h,
	        &totalVolBarBase_, skin_->volRect[0].x, skin_->volRect[1].y, 100);
	BmpCopyComposite(&screen_, skin_->volX, skin_->volY, skin_->volW, skin_->volH, &totalVolBar_, 0, 0, &back_, skin_->volX,
	                 skin_->volY, kBlendMul);

	// 桁数は固定にする。短い文字列を書くと前の表示の末尾が残る。
	char s[64];
	snprintf(s, sizeof(s), "%c%03d", (volume >= 0) ? '+' : '-', abs(volume));
	PrintCompose(skin_->volX + skin_->volTimeXOfs, skin_->volY + skin_->volTimeYOfs, s, colors_.playKey.color,
	             colors_.playKey.colorBright);
}

// ---------------------------------------------------------------------------
// 操作ボタン
// ---------------------------------------------------------------------------

void DrawScreen::PutPlayKey(uint32_t status, bool refresh) {
	if (!playKey_.valid()) return;

	playKey_.SetPalette(skin_->palPlayKeyKey, colors_.playKey.keyBright, colors_.playKey.keyBright,
	                    colors_.playKey.keyBright);

	struct LedMap {
		uint32_t bit;
		int pal;
		int onPal;
	};
	const LedMap leds[4] = {
		{ kPlayKeyPlayLed, skin_->palPlayLed, skin_->palGreen },
		{ kPlayKeyPauseLed, skin_->palPauseLed, skin_->palGreen },
		{ kPlayKeyContLed, skin_->palContLed, skin_->palRed },
		{ kPlayKeyRepeatLed, skin_->palRepeatLed, skin_->palRed },
	};

	bool ledChanged = false;
	for (int i = 0; i < 4; i++) {
		const uint32_t now = status & leds[i].bit;
		if (now == (playKeyStatusLast_ & leds[i].bit)) continue;
		playKey_.palette()[leds[i].pal] = playKey_.palette()[now ? leds[i].onPal : skin_->palDark];
		ledChanged = true;
	}

	for (int i = 0; i < skin_->numPlayKeys; i++) {
		const uint32_t sw = status & (1u << i);
		bool disp = refresh || ledChanged;
		if (sw != (playKeyStatusLast_ & (1u << i))) disp = true;
		if (!disp) continue;

		const int w = skin_->playKeyRect[i].w;
		const int h = skin_->playKeyRect[i].h;
		const int x = skin_->playKeyX + skin_->playKeyPos[i][0];
		const int y = skin_->playKeyY + skin_->playKeyPos[i][1];
		BmpCopyComposite(&screen_, x, y, w, h, &playKey_, skin_->playKeyRect[i].x,
		                 skin_->playKeyRect[i].y + (sw ? h : 0), &back_, x, y, kBlendMul);
	}

	playKeyStatusLast_ = status;
}

// ---------------------------------------------------------------------------
// ヒットチェック（旧 mxv の Screen_HitCheck_*）
// ---------------------------------------------------------------------------

int DrawScreen::HitCheckPlayKey(int x, int y) const {
	for (int i = 0; i < skin_->numPlayKeys; i++) {
		const int l = skin_->playKeyX + skin_->playKeyPos[i][0];
		const int t = skin_->playKeyY + skin_->playKeyPos[i][1];
		if (x >= l && x < l + skin_->playKeyRect[i].w && y >= t && y < t + skin_->playKeyRect[i].h) {
			return i + 1;
		}
	}
	return kHitPlayKeyNone;
}

int DrawScreen::HitCheckScrollBar(int x, int y) const {
	if (x < skin_->scrollX || x >= skin_->scrollX + skin_->scrollW) return kHitScrollBarNone;
	if (y < skin_->scrollY || y >= skin_->scrollY + skin_->scrollH) return kHitScrollBarNone;

	// 当たり判定も描いた場所（スキンの Pos* と Src* の大きさ）から決める。
	const int dx = x - skin_->scrollX;
	const int dy = y - skin_->scrollY;

	if (InRect(dx, dy, skin_->scrollPosUpArrow, skin_->scrollSrcUpArrow)) {
		return kHitScrollBarUpArrow;
	}
	if (InRect(dx, dy, skin_->scrollPosDownArrow, skin_->scrollSrcDownArrow)) {
		return kHitScrollBarDownArrow;
	}
	// 残りは溝の中。つまみの上下がページ送りになる。
	if (!InRect(dx, dy, skin_->scrollPosBar, skin_->scrollSrcBar)) return kHitScrollBarNone;

	const int thumbTop = skin_->scrollPosBar[1] + scrollBarThumb_;
	if (dy < thumbTop) return kHitScrollBarUpPage;
	if (dy < thumbTop + skin_->scrollSrcThumb.h) return kHitScrollBarThumb;
	return kHitScrollBarDownPage;
}

int DrawScreen::HitCheckFileList(int x, int y) const {
	if (x < skin_->fileListX || x >= skin_->fileListX + skin_->fileListW) return -1;
	if (y < skin_->fileListY || y >= skin_->fileListY + skin_->fileListH) return -1;

	// 画素単位でスクロールしていると全体が上へずれているので、その分を足して
	// から行に直す。ずれの量は最後に描いたときのものを使う（描いてあるものと
	// 当たり判定を必ず一致させるため）。戻り値は「上から数えて何行目か」で、
	// 呼び出し側が Filer::top() に足して項目を決める。
	const int itemH = skin_->fileListItemH[fileListFontSize_ & 1];
	const int row = (y - skin_->fileListY + fileListOffsetLast_) / itemH;
	// 大きい文字 (13px) では 110/13 = 8 行と半端が出る。原典は半端の帯でも
	// 行 8 を返していたが、そこには何も描かれていないので弾く。
	// ずれているときは半端な行が 1 つ増える。
	const int drawRows = fileListRows() + ((fileListOffsetLast_ > 0) ? 1 : 0);
	if (row >= drawRows) return -1;
	return row;
}

int DrawScreen::HitCheckProgressBar(int x, int y) const {
	if (y < skin_->progY || y >= skin_->progY + skin_->progH) return -1;
	if (x < skin_->progX || x >= skin_->progX + skin_->progW) return -1;
	return Max(0, Min(skin_->progW, x - skin_->progX));
}

bool DrawScreen::HitCheckBanner(int x, int y) const {
	if (x < skin_->bannerX || x >= skin_->bannerX + skin_->bannerW) return false;
	if (y < skin_->bannerY || y >= skin_->bannerY + skin_->bannerH) return false;
	return true;
}

int DrawScreen::HitCheckKeyboard(int x, int y) const {
	for (int ch = 0; ch < 16; ch++) {
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!ChannelKeyRect(ch, &x0, &y0, &x1, &y1)) continue;
		if (x >= x0 && x < x1 && y >= y0 && y < y1) return ch;
	}
	return -1;
}

int DrawScreen::HitCheckStatus(int x, int y) const {
	for (int row = 0; row < 9; row++) {
		int rx = 0, ry = 0, rw = 0, rh = 0;
		if (!StatusRect(row, &rx, &ry, &rw, &rh)) continue;
		if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) return row;
	}
	return -1;
}

// 音量は -100..+100 で、-1 も正しい値なので「当たらなかった」を戻り値では
// 表せない。真偽値で返して音量は out で渡す。
bool DrawScreen::HitCheckTotalVolBar(int x, int y, int *volume) const {
	if (y < skin_->volY || y >= skin_->volY + skin_->volH) return false;
	if (x < skin_->volX || x >= skin_->volX + skin_->volW) return false;
	if (volume != 0) *volume = VolumeFromX(x);
	return true;
}

}  // namespace mxv2
