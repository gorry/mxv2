// mxv2 - 描画コア（旧 mxv/draw.cpp の移植）

#include "drawscreen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bmpfile.h"
#include "fileutil.h"
#include "message.h"
#include "settings.h"

namespace mxv2 {

namespace {

// レイアウト定数はスキン (skin.h) が持つ。既定値は旧 mxv/draw.cpp と同じ。

int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }

// ミニフォントでの退避描画に流す最大文字数。はみ出した分はブリッタ側で
// クリップされるので、画面幅を越えられる長さがあれば十分。
const size_t kMaxAsciiChars = 128;

// 曲名の左右の余白 (px)。
const int kTitleMarginX = 4;

// 曲名が枠に収まらないときのスクロール。
//   先頭で止まる → 一定の速さで末尾まで送る → 末尾で止まる →
//   先頭へ戻る、の繰り返し。止まる長さは曲名欄とファイラーで別。
// 速さは**文字の高さの何倍を 1 秒に送るか**で持つ（2026-09-12、ユーザーの
// 指示）。画素で決めると解像度やスキンの字の大きさで読める速さが変わる
// ため。基準は、それまでの 40 論理px/秒を Pixel 7a の Phone スキンの
// 曲名欄（高さ 24px）で換算した 40/24 で、スキンの `[Title] ScrollSpeed` /
// `[FileList] ScrollSpeed` (%) がこれに掛かる（100% = 基準）。曲名欄と
// ファイラーで式は同じで、掛ける高さがそれぞれ titleH / ItemHeight。
const float kScrollBaseHeightsPerSec = 40.0f / 24.0f;
const uint32_t kTitleScrollHoldMs = 3000;     // 画面下の曲名欄
const uint32_t kFileListScrollHoldMs = 1000;  // ファイラーの曲名（短めに）
// 幅の測り方（切り上げ）と字の置き方の端数で、収まっているのに 1〜2px だけ
// はみ出したことになる場合がある。そのぶんで動き出すと、止まるたびにわずかに
// 震えて見えるので、これ以下のはみ出しは無視する。
const int kTitleScrollSlack = 2;

// 文字の高さ (論理px) とスキンの速さ (%) から、送る速さ (論理px/秒) へ。
float ScrollPxPerSec(int textHeight, int speedPercent) {
	return (float)textHeight * kScrollBaseHeightsPerSec * (float)speedPercent / 100.0f;
}

// 周期の先頭から t ミリ秒経ったときの送り量 (px)。pxPerSec は送る速さ
// （文字の高さ × k*HeightsPerSec で呼ぶ側が決める）。
//   0..hold          … 先頭のまま
//   hold..hold+移動  … 一定の速さで送る
//   その後 hold      … 末尾のまま
// を繰り返す。
float ScrollOffsetAt(uint32_t t, float maxPx, uint32_t holdMs, float pxPerSec) {
	if (maxPx <= 0.0f || pxPerSec <= 0.0f) return 0.0f;
	const uint32_t moveMs = (uint32_t)(maxPx * 1000.0f / pxPerSec + 0.5f);
	const uint32_t cycleMs = holdMs * 2 + moveMs;
	const uint32_t u = t % cycleMs;
	if (u < holdMs) return 0.0f;
	if (u >= holdMs + moveMs) return maxPx;
	const float off = (u - holdMs) * pxPerSec / 1000.0f;
	return (off > maxPx) ? maxPx : off;
}

// ミニフォントの素材は ASCII 0x20〜0x6F を 16 列 x 5 行に並べたグリフ表。
// **1 文字の大きさは素材の大きさ ÷ この並びで決まる**ので、素材を大きく
// 作れば大きい字になる（旧 mxv の 5x7 に縛られない）。画面に置くときの
// 送り幅と行の高さは別で、スキンの [MiniFont] Width / Height が決める。
const int kMiniFontFirstChar = 0x20;
const int kMiniFontLastChar = 0x6f;
const int kMiniFontCols = 16;
const int kMiniFontRows = 5;

// グリフ表の中でその文字が置かれている場所。範囲外の文字は端へ丸める。
void MiniGlyphSrc(int ch, int glyphW, int glyphH, int *sx, int *sy) {
	if (ch < kMiniFontFirstChar) ch = kMiniFontFirstChar;
	if (ch > kMiniFontLastChar) ch = kMiniFontLastChar;
	ch -= kMiniFontFirstChar;
	*sx = (ch % kMiniFontCols) * glyphW;
	*sy = (ch / kMiniFontCols) * glyphH;
}

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
// これは**ボタンの押下表示にだけ効く印**で、LED は使わない（LED はこの印を
// 「全部点いている」と読んでしまうため。PutPlayKey のコメントを見ること）。
const uint32_t kPlayKeyStatusNever = 0xffffffffu;

// ミニフォントで描ける形（ASCII 大文字）に落とす。文字描画が使えない
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
      canvasW_(0),
      canvasH_(0),
      backImageW_(0),
      backImageH_(0),
      textLayer_(0),
      fileListFontSize_(0),
      channelMask_(0),
      scrollBarFlags_(0),
      scrollBarThumb_(0),
      miniGlyphW_(0),
      miniGlyphH_(0),
      titleScrollMax_(0.0f),
      titleScrollShown_(0.0f),
      titleScrollBaseMs_(0),
      titleScrollStarted_(false),
      fileListScroll_(0),
      fileListPhaseTickMs_(0),
      fileListFsLast_(-1),
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
		*err = Msg("Error.NoSkin");
		return false;
	}
	skin_ = skin;
	canvasW_ = skin->screenW;
	canvasH_ = skin->screenH;
	layout_ = skin->PlacedFor(canvasW_, canvasH_);

	if (!CreateBuffers(err)) return false;

	colors_ = Colors();
	colors_.Load(layout_.FindColorsFile());

	if (!LoadAssets(err)) return false;

	Reload();
	return true;
}

// スクロールバーの組み立て用バッファ。大きさはファイラーの矩形から決まるので、
// キャンバスを変えるたびに作り直す。[ScrollBar] Width=0 の
// 「スクロールバーを出さない」スキンでは器を持たない（PutScrollBar は
// valid() を見て何もしない）。
bool DrawScreen::EnsureScrollBarBuffer(std::string *err) {
	if (layout_.scrollW <= 0 || layout_.scrollH <= 0) {
		scrollBar_.Destroy();
		return true;
	}
	if (!scrollBar_.Create(layout_.scrollW, layout_.scrollH, 8)) {
		*err = Msg("Error.ScrollBuffer");
		return false;
	}
	memcpy(scrollBar_.palette(), scrollBarBase_.palette(), sizeof(Rgb) * 256);
	return true;
}

bool DrawScreen::CreateBuffers(std::string *err) {
	if (!screen_.Create(width(), height(), 24) || !back_.Create(width(), height(), 24) ||
	    !backBitmap_.Create(width(), height(), 24)) {
		*err = Msg("Error.ScreenBuffer");
		return false;
	}
	return true;
}

// キャンバスの大きさを変える。バッファを作り直し、レイアウトを解決し直して
// 全面を描き直す。窓のリサイズと画面の回転から呼ばれる（fullscreen.md）。
bool DrawScreen::Resize(int canvasW, int canvasH, std::string *err) {
	if (skin_ == 0) {
		*err = Msg("Error.NoSkin");
		return false;
	}
	if (canvasW == canvasW_ && canvasH == canvasH_) return true;
	if (canvasW <= 0 || canvasH <= 0) {
		*err = Msg("Error.ScreenSize");
		return false;
	}

	canvasW_ = canvasW;
	canvasH_ = canvasH;
	layout_ = skin_->PlacedFor(canvasW_, canvasH_);

	if (!CreateBuffers(err)) return false;
	// スクロールバーの組み立て用だけはファイラーの矩形と同じ大きさなので
	// ここで作り直す（プログレスバーと音量バーはキャンバスに依らない）。
	if (!EnsureScrollBarBuffer(err)) return false;

	// 行数が変わっているので、差分描画の前回値は作り直させる。
	fileListLast_.clear();
	fileListScrollLast_.clear();
	fileListTitleW_.clear();
	fileListCursorLast_ = -1;

	Reload();
	return true;
}

int DrawScreen::stretchLimit() const {
	if (skin_ == 0) return 0;
	if (!colors_.back.bitmap) return 0;  // 背景を使わない設定なら上限なし
	const bool vertical = FilerSideVertical(skin_->filerSide);
	const int declared = vertical ? skin_->screenH : skin_->screenW;
	const int image = vertical ? backImageH_ : backImageW_;
	return (image > declared) ? image : declared;
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
		{ &layout_.kb0Bitmap, &kb0_ },
		{ &layout_.kb1Bitmap, &kb1 },
		{ &layout_.kb2Bitmap, &kb2 },
		{ &layout_.miniFontBitmap, &miniFont_ },
		{ &layout_.levelMeterBitmap, &levelMeter_ },
		{ &layout_.bannerBitmap, &banner_ },
		{ &layout_.playKeyBitmap, &playKey_ },
		{ &layout_.progressBarBitmap, &progressBarBase_ },
		{ &layout_.volBarBitmap, &totalVolBarBase_ },
		{ &layout_.scrollBarBitmap, &scrollBarBase_ },
	};
	for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		// スキンのフォルダ -> 土台のフォルダ の順に探す。
		if (!LoadBmpFile(layout_.FindFile(*items[i].name), items[i].dst, err)) return false;
	}

	// ミニフォントの 1 文字の大きさは素材から決まる（スキンが持つのは
	// 画面に置くときの送り幅と行の高さだけ）。
	miniGlyphW_ = miniFont_.width() / kMiniFontCols;
	miniGlyphH_ = miniFont_.height() / kMiniFontRows;

	// 鍵ビットマップの切り出し。奇数番の音は kb2 (黒鍵) から取る。
	// パレット 0x11 を 1 (影)、0x12+n を 2 (点灯色) へ寄せる。
	static const int kUseKb2[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	for (int i = 0; i < 12; i++) {
		const Bitmap &src = kUseKb2[i] ? kb2 : kb1;
		CutKeyboardBitmap(&keyboard_[i], src, layout_.kbXOffset[i], 7, 0x11, 0x12 + i);
	}
	for (int i = 0; i < 12; i++) {
		kbPalette_[i] = kb1.palette()[i + 0x12];
	}

	// レベルメータの点灯色 / 消灯色
	for (int i = 0; i < layout_.levelMeterWidthCells * 2; i++) {
		palLevelMeter_[i] = levelMeter_.palette()[i + layout_.levelMeterPalOfs];
	}

	// 組み立て用のバッファ
	if (!progressBar_.Create(layout_.progW, layout_.progH, 8)) {
		*err = Msg("Error.ProgressBuffer");
		return false;
	}
	memcpy(progressBar_.palette(), progressBarBase_.palette(), sizeof(Rgb) * 256);

	if (!totalVolBar_.Create(layout_.volW, layout_.volH, 8)) {
		*err = Msg("Error.VolumeBuffer");
		return false;
	}
	memcpy(totalVolBar_.palette(), totalVolBarBase_.palette(), sizeof(Rgb) * 256);

	if (!EnsureScrollBarBuffer(err)) return false;

	// 操作ボタンの LED は消灯状態から始める
	playKey_.SetPalette(layout_.palPlayLed, 25, 25, 25);
	playKey_.SetPalette(layout_.palPauseLed, 25, 25, 25);
	playKey_.SetPalette(layout_.palContLed, 25, 25, 25);
	playKey_.SetPalette(layout_.palRepeatLed, 25, 25, 25);

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
	backImageW_ = 0;
	backImageH_ = 0;
	if (!colors_.back.bitmap) return;

	Bitmap image;
	std::string err;
	if (!LoadBmpFile(layout_.FindFile(layout_.backBitmap), &image, &err)) return;
	backImageW_ = image.width();
	backImageH_ = image.height();

	// 背景を貼る原点は「ファイラー以外側」の外の角。ファイラーが伸びる向きの
	// 反対側に貼り付ける（下へ伸びるなら左上のまま、上へ伸びるなら左下、
	// 左へ伸びるなら右上）。素材が足りないところは黒のまま残り、このあとの
	// CompositeBack で配色の背景色が乗る。はみ出しは BmpCopy が切る。
	int dx = 0, dy = 0;
	if (layout_.filerSide == kFilerSideTop) dy = height() - image.height();
	if (layout_.filerSide == kFilerSideLeft) dx = width() - image.width();
	BmpCopy(&backBitmap_, dx, dy, image.width(), image.height(), &image, 0, 0, 100);
}

void DrawScreen::CompositeBanner() {
	BmpCopy(&back_, layout_.bannerX, layout_.bannerY, layout_.bannerW, layout_.bannerH, &banner_, 0, 0, kBlendMul);
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
	*x = layout_.statusX;
	*y = layout_.statusY + layout_.chYOffset[row];
	*w = layout_.statusW;
	*h = layout_.statusH;
	return true;
}

void DrawScreen::CompositeFileList() {
	BmpFill(&back_, layout_.fileListX, layout_.fileListY, layout_.fileListW, layout_.fileListH, colors_.filer.backColor.r,
	        colors_.filer.backColor.g, colors_.filer.backColor.b, colors_.filer.backColorBright);
}

void DrawScreen::CompositeKeyboard() {
	kb0_.SetPalette(1, colors_.kb.blackBright, colors_.kb.blackBright, colors_.kb.blackBright);
	kb0_.SetPalette(2, colors_.kb.whiteBright, colors_.kb.whiteBright, colors_.kb.whiteBright);
	for (int i = 0; i < 9; i++) {
		BmpCopy(&back_, layout_.kbX, layout_.kbY + layout_.chYOffset[i] + layout_.kbYOffset, kb0_.width(), kb0_.height(),
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
	// 曲名の幅はスキンの桁と文字の大きさで変わるので、測り直させる。
	fileListScrollLast_.clear();
	fileListTitleW_.clear();
	fileListFsLast_ = -1;

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

	*x0 = layout_.kbX;
	*x1 = layout_.kbX + kbW;
	if (ch >= 8) {
		const int i = ch - 8;
		*x0 = layout_.kbX + kbW * i / 8;
		*x1 = layout_.kbX + kbW * (i + 1) / 8;
	}
	*y0 = layout_.kbY + layout_.chYOffset[row] + layout_.kbYOffset;
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
// 文字描画 (ミニフォント)
// ---------------------------------------------------------------------------

void DrawScreen::PrintMini(int x, int y, const char *msg, const Rgb &color, int alpha) {
	miniFont_.SetPalette(1, color.r, color.g, color.b);
	for (const char *p = msg; *p != '\0'; p++) {
		int sx = 0, sy = 0;
		MiniGlyphSrc((unsigned char)*p, miniGlyphW_, miniGlyphH_, &sx, &sy);
		BmpCopyTransparent(&screen_, x, y, miniGlyphW_, miniGlyphH_, &miniFont_, sx, sy, alpha);
		x += layout_.miniFontW;
	}
}

void DrawScreen::PrintMiniCompose(int x, int y, const char *msg, const Rgb &color, int alpha) {
	miniFont_.SetPalette(1, color.r, color.g, color.b);
	for (const char *p = msg; *p != '\0'; p++) {
		int sx = 0, sy = 0;
		MiniGlyphSrc((unsigned char)*p, miniGlyphW_, miniGlyphH_, &sx, &sy);
		BmpCopyComposite(&screen_, x, y, miniGlyphW_, miniGlyphH_, &miniFont_, sx, sy, &back_, x,
		                 y, alpha);
		x += layout_.miniFontW;
	}
}

// ステータス欄の項目 1 つの左上。FM の項目は段 (row) の y をそのまま使い、
// PCM の 2 項目だけは PCM 行 (chYOffset[8]) の中の 8 スロットから選ぶ。
void DrawScreen::StatusItemPos(StatusItem item, int row, int *x, int *y) const {
	const int *pos = layout_.statusPos[item];
	if (item == kStatusPcmVolume || item == kStatusPcmPtr) {
		const int i = row - 8;
		*x = layout_.statusX + layout_.pcmXOffset[i] + pos[0];
		*y = layout_.statusY + layout_.chYOffset[8] + layout_.pcmYOffset[i] + pos[1];
		return;
	}
	*x = layout_.statusX + pos[0];
	*y = layout_.statusY + layout_.chYOffset[row] + pos[1];
}

void DrawScreen::PutStatusText(StatusItem item, int row, const char *text) {
	int x = 0, y = 0;
	StatusItemPos(item, row, &x, &y);
	PrintMiniCompose(x, y, text, colors_.status.color, colors_.status.colorBright);
}

// ---------------------------------------------------------------------------
// 鍵盤
// ---------------------------------------------------------------------------

void DrawScreen::PutNoteOn(int key, int row, int color, int bendMode) {
	if (row < 0 || row >= 9) return;
	key += layout_.keyOffset;
	if (key < 0) return;
	const int oct = key / 12;
	const int note = key % 12;
	Bitmap &b = keyboard_[note];
	if (!b.valid()) return;

	b.palette()[2] = kbPalette_[color % 12];

	const int x = layout_.kbX + layout_.kbXOffset[note] + layout_.kbXOffset[12] * oct - layout_.kbXOffset[layout_.keyOffset];
	const int y = layout_.kbY + layout_.chYOffset[row] + layout_.kbYOffset;
	BmpCopyTransparent(&screen_, x, y, b.width(), b.height(), &b, 0, 0,
	                   bendMode ? colors_.kb.bright / 2 : colors_.kb.bright);
}

void DrawScreen::PutNoteOff(int key, int row) {
	if (row < 0 || row >= 9) return;
	key += layout_.keyOffset;
	if (key < 0) return;
	const int oct = key / 12;
	const int note = key % 12;
	const Bitmap &b = keyboard_[note];
	if (!b.valid()) return;

	const int x = layout_.kbX + layout_.kbXOffset[note] + layout_.kbXOffset[12] * oct - layout_.kbXOffset[layout_.keyOffset];
	const int y = layout_.kbY + layout_.chYOffset[row] + layout_.kbYOffset;
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
	PutStatusText(kStatusVolume, row, s);
}

void DrawScreen::PutPanpot(int panpot, int row) {
	if (row < 0 || row >= 9) return;
	char s[2];
	s[0] = "-LRC"[panpot & 3];
	s[1] = '\0';
	PutStatusText(kStatusPanpot, row, s);
}

void DrawScreen::PutDetune(int detune, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	const int v = (int16_t)detune;
	snprintf(s, sizeof(s), "D%c%04d", (v >= 0) ? '+' : '-', abs(v) % 10000);
	PutStatusText(kStatusDetune, row, s);
}

void DrawScreen::PutVoice(int voice, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "@%03d", voice % 1000);
	PutStatusText(kStatusVoice, row, s);
}

void DrawScreen::PutQ(int q, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "Q%03d", q % 1000);
	PutStatusText(kStatusQ, row, s);
}

void DrawScreen::PutPtr(int ptr, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "$%05X", (unsigned)ptr & 0xfffff);
	PutStatusText(kStatusPtr, row, s);
}

void DrawScreen::PutLFOPitch(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	const int t = (int16_t)v;
	snprintf(s, sizeof(s), "P%c%04d", (t >= 0) ? '+' : '-', abs(t) % 10000);
	PutStatusText(kStatusLFOPitch, row, s);
}

void DrawScreen::PutLFOPitch1(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[2];
	s[0] = (char)(0x65 + v);
	s[1] = '\0';
	PutStatusText(kStatusLFOPitch1, row, s);
}

void DrawScreen::PutLFOPitch2(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%04d", (uint16_t)v % 10000);
	PutStatusText(kStatusLFOPitch2, row, s);
}

void DrawScreen::PutLFOPitch3(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%c%04d", (v >= 0) ? '+' : '-', abs(v) % 10000);
	PutStatusText(kStatusLFOPitch3, row, s);
}

void DrawScreen::PutLFOPitch4(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "D%03d", (uint16_t)v % 1000);
	PutStatusText(kStatusLFOPitch4, row, s);
}

void DrawScreen::PutLFOVolume(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	const int t = (int16_t)v;
	snprintf(s, sizeof(s), "A%c%04d", (t >= 0) ? '+' : '-', abs(t) % 10000);
	PutStatusText(kStatusLFOVolume, row, s);
}

void DrawScreen::PutLFOVolume1(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[2];
	s[0] = (char)(0x65 + v);
	s[1] = '\0';
	PutStatusText(kStatusLFOVolume1, row, s);
}

void DrawScreen::PutLFOVolume2(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%04d", (uint16_t)v % 10000);
	PutStatusText(kStatusLFOVolume2, row, s);
}

void DrawScreen::PutLFOVolume3(int v, int row) {
	if (row < 0 || row >= 9) return;
	char s[64];
	snprintf(s, sizeof(s), "%c%04d", (v >= 0) ? '+' : '-', abs(v) % 10000);
	PutStatusText(kStatusLFOVolume3, row, s);
}

void DrawScreen::PutLevelMeter(const char *levelMeterInfo, int row) {
	if (row < 0 || row >= 9 || !levelMeter_.valid()) return;

	// 点灯しているセルだけ明るいパレットに差し替える。
	for (int i = 0; i < layout_.levelMeterWidthCells; i++) {
		levelMeter_.palette()[i + layout_.levelMeterPalOfs] =
		    levelMeterInfo[i] ? palLevelMeter_[i] : palLevelMeter_[i + layout_.levelMeterWidthCells];
	}

	// 素材は 1 段ぶんの幅で作ってあり、音量表示と重なる左端 (SrcX) を
	// 切ってから描く。
	const int skip = layout_.levelMeterSrcX;
	const int w = levelMeter_.width() - skip;
	const int h = levelMeter_.height();
	int x = 0, y = 0;
	StatusItemPos(kStatusLevelMeter, row, &x, &y);
	BmpCopyComposite(&screen_, x, y, w, h, &levelMeter_, skip, 0, &back_, x, y,
	                 colors_.status.colorBright);
}

// ---------------------------------------------------------------------------
// ステータス表示 (PCM 8..15)
// ---------------------------------------------------------------------------

void DrawScreen::PutPCMVolume(int volume, int row) {
	if (row < 8 || row >= 16) return;
	char s[64];
	if (volume >= 128) {
		snprintf(s, sizeof(s), "V%03d", 127 - (volume & 127));
	} else {
		snprintf(s, sizeof(s), "V%-3d", volume);
	}
	PutStatusText(kStatusPcmVolume, row, s);
}

void DrawScreen::PutPCMPtr(int ptr, int row) {
	if (row < 8 || row >= 16) return;
	char s[64];
	snprintf(s, sizeof(s), "$%05X", (unsigned)ptr & 0xfffff);
	PutStatusText(kStatusPcmPtr, row, s);
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
		std::vector<char> meter(layout_.levelMeterWidthCells > 0
		                            ? (size_t)layout_.levelMeterWidthCells
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

	// 枠に収まらないぶんはスクロールで見せる。曲が変わるたびに測り直し、
	// 周期も先頭からやり直す。
	titleScrollMax_ = 0.0f;
	titleScrollShown_ = 0.0f;
	titleScrollStarted_ = false;
	if (!titleUtf8.empty() && textLayer_ != 0 && textLayer_->available()) {
		const int inner = layout_.titleW - kTitleMarginX * 2;
		const int need = textLayer_->MeasureWidth(layout_.titleH, titleUtf8);
		if (need > inner + kTitleScrollSlack) titleScrollMax_ = (float)(need - inner);
	}

	DrawMDXTitle(0.0f);
}

// 曲名を送り量 scrollX（論理px）で描く。**枠の下地から描き直す**ので、
// スクロール中は毎フレームここを通る。
void DrawScreen::DrawMDXTitle(float scrollX) {
	titleScrollShown_ = scrollX;

	// 背景を戻してからタイトル欄の下地を敷く
	BmpCopy(&screen_, layout_.titleX, layout_.titleY, layout_.titleW, layout_.titleH, &back_, layout_.titleX, layout_.titleY, 100);
	BmpFill(&screen_, layout_.titleX, layout_.titleY, layout_.titleW, layout_.titleH, colors_.mdxTitle.backColor.r,
	        colors_.mdxTitle.backColor.g, colors_.mdxTitle.backColor.b,
	        colors_.mdxTitle.backColorBright);

	if (mdxTitle_.empty()) return;

	if (textLayer_ != 0 && textLayer_->available()) {
		// 文字はキャンバスではなく出力解像度のレイヤーへ描く。
		// スクロールするときは端で字が切れてよい（切れる手前で止めると、
		// 送るたびに 1 文字ぶん飛んで見える）。
		textLayer_->ClearRect(layout_.titleX, layout_.titleY, layout_.titleW, layout_.titleH);
		textLayer_->DrawText(layout_.titleX + kTitleMarginX, layout_.titleY,
		                     layout_.titleW - kTitleMarginX * 2, layout_.titleH, mdxTitle_,
		                     colors_.mdxTitle.color, colors_.mdxTitle.colorBright, 0, 0,
		                     scrollX, true);
		return;
	}

	// フォントが読めなかったときの非常用。ミニフォントは本来ステータス欄などの
	// 数字用なので、ここへ落ちている時点で assets の同梱フォントが失われている。
	// こちらはスクロールしない（非常用なので凝らない）。
	PrintMini(layout_.titleX + kTitleMarginX, layout_.titleY + 3,
	          ToAscii(mdxTitle_, kMaxAsciiChars).c_str(), colors_.mdxTitle.color,
	          colors_.mdxTitle.colorBright);
}

// 曲名のスクロール。左端で止まる → 右端まで送る → 右端で止まる → 先頭へ戻る、
// の繰り返し。Phone のような横幅の狭いスキンでも曲名を最後まで読めるようにする。
void DrawScreen::UpdateTitleScroll(uint32_t nowMs) {
	if (titleScrollMax_ <= 0.0f) return;  // 枠に収まっている

	if (!titleScrollStarted_) {
		titleScrollStarted_ = true;
		titleScrollBaseMs_ = nowMs;
	}

	const float scrollX =
	    ScrollOffsetAt(nowMs - titleScrollBaseMs_, titleScrollMax_, kTitleScrollHoldMs,
	                   ScrollPxPerSec(layout_.titleH, layout_.titleScrollSpeed));

	// 変わっていなければ描き直さない（止まっている間は何もしない）。
	const float diff = scrollX - titleScrollShown_;
	if (diff > -0.25f && diff < 0.25f) return;
	DrawMDXTitle(scrollX);
}

// ---------------------------------------------------------------------------
// ファイラー
// ---------------------------------------------------------------------------

int DrawScreen::fileListRows() const {
	return layout_.fileListRows[fileListFontSize_ & 1];
}

int DrawScreen::fileListItemH() const {
	return layout_.fileListItemH[fileListFontSize_ & 1];
}

void DrawScreen::SetFileListFontSize(int size) {
	fileListFontSize_ = (size & 1);
	fileListLast_.clear();
	fileListCursorLast_ = -1;
}

void DrawScreen::PutFileList(const Filer &filer, bool refresh, uint32_t nowMs) {
	const int rows = fileListRows();
	const int fs = fileListFontSize_ & 1;  // 0=小さい文字 / 1=大きい文字
	const int itemH = layout_.fileListItemH[fs];
	const int top = filer.top();
	const int cursor = filer.cursor();

	// スクロール位置の端数 (0..itemH-1)。ドラッグ中だけ 0 以外になる。
	// 端数があるぶん全体が上へずれるので、上下の端に半端な行が出る。
	// その 1 行ぶん多く回して、はみ出しは矩形を切って描く。
	const int offset = filer.topOffsetPx();
	const int drawRows = (offset > 0) ? rows + 1 : rows;
	const int listTop = layout_.fileListY;
	const int listBottom = layout_.fileListY + layout_.fileListH;

	// 端数が変わると行と画素の対応がまるごとずれるので、行ごとの差分は
	// 使えない。ドラッグ中は毎フレーム全部描き直す（文字はグリフを
	// キャッシュしてあるので、焼き直しではなく転送だけで済む）。
	if (fileListOffsetLast_ != offset) {
		fileListOffsetLast_ = offset;
		refresh = true;
	}
	if (fileListFsLast_ != fs) {
		fileListFsLast_ = fs;
		// 幅は文字の大きさで変わる。測り直させる。
		fileListTitleW_.clear();
		refresh = true;
	}
	if ((int)fileListLast_.size() != rows + 1) {
		fileListLast_.assign(rows + 1, FileItem());
		refresh = true;
	}
	if ((int)fileListScrollLast_.size() != rows + 1 ||
	    (int)fileListTitleW_.size() != rows + 1) {
		fileListScrollLast_.assign(rows + 1, 0.0f);
		fileListTitleW_.assign(rows + 1, -1);
	}

	// 横スクロールの進み具合は**項目ごと**に持つ。フォルダが変わったら作り直す
	// （中身が総入れ替えになるので、前の値には意味が無い）。
	const int itemCount = filer.itemCount();
	if (fileListRefLast_ != filer.currentRef() || (int)fileListPhaseMs_.size() != itemCount) {
		fileListRefLast_ = filer.currentRef();
		fileListPhaseMs_.assign(itemCount, 0);
	}

	// **見えている行だけ**時間を進める。画面から外れている行は、そのときの
	// 位置のまま待つ（縦にスクロールしても横位置が飛ばない）。
	{
		uint32_t dt = nowMs - fileListPhaseTickMs_;
		fileListPhaseTickMs_ = nowMs;
		// 長く描いていなかったとき（バックグラウンドなど）は進めない。
		if (dt > 500) dt = 0;
		if (dt > 0) {
			for (int i = 0; i < drawRows; i++) {
				const int j = top + i;
				if (j >= 0 && j < itemCount) fileListPhaseMs_[j] += dt;
			}
		}
	}

	// カーソルが来た行だけ先頭へ戻す。
	if (cursor != fileListCursorLast_ && cursor >= 0 && cursor < itemCount) {
		fileListPhaseMs_[cursor] = 0;
	}

	for (int i = 0; i < drawRows; i++) {
		const int j = top + i;
		bool redraw = refresh;

		FileItem shown;
		if (j < filer.itemCount()) shown = filer.item(j);

		if (fileListLast_[i].baseName != shown.baseName ||
		    fileListLast_[i].title != shown.title || fileListLast_[i].type != shown.type) {
			fileListLast_[i] = shown;
			fileListTitleW_[i] = -1;  // 幅は測り直す
			redraw = true;
		}
		// カーソルが出入りした行は描き直す
		if (fileListCursorLast_ != cursor && (j == fileListCursorLast_ || j == cursor)) {
			redraw = true;
		}

		// 曲名が桁に収まらない行は横へ送る。対象は設定しだいで
		// 「カーソル行だけ」か「全て」。送り量が変わった行は描き直す。
		float scrollX = 0.0f;
		bool titlePartial = false;
		const bool scrollRow =
		    (fileListScroll_ == Settings::kScrollAll) ||
		    (fileListScroll_ == Settings::kScrollCursor && j == cursor);
		if (scrollRow && !shown.title.empty() && textLayer_ != 0 && textLayer_->available()) {
			if (fileListTitleW_[i] < 0) {
				fileListTitleW_[i] = textLayer_->MeasureWidth(itemH, shown.title);
			}
			const int inner = layout_.fileListTitleW[fs];
			if (fileListTitleW_[i] > inner + kTitleScrollSlack) {
				titlePartial = true;
				const uint32_t phase =
				    (j >= 0 && j < itemCount) ? fileListPhaseMs_[j] : 0;
				scrollX = ScrollOffsetAt(phase, (float)(fileListTitleW_[i] - inner),
				                         kFileListScrollHoldMs,
				                         ScrollPxPerSec(itemH, layout_.fileListScrollSpeed));
			}
		}
		if (!redraw) {
			const float diff = scrollX - fileListScrollLast_[i];
			if (diff > 0.25f || diff < -0.25f) redraw = true;
		}
		if (!redraw) continue;
		fileListScrollLast_[i] = scrollX;

		const int x = layout_.fileListX;
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
		BmpCopy(&screen_, x, y, layout_.fileListW, h, &back_, x, y, 100);
		if (j == cursor && j < filer.itemCount()) {
			BmpFillMul(&screen_, x, y, layout_.fileListW, h, colors_.filer.cursorColor.r,
			           colors_.filer.cursorColor.g, colors_.filer.cursorColor.b,
			           colors_.filer.cursorColorBright);
		}
		if (textLayer_ != 0) textLayer_->ClearRect(x, y, layout_.fileListW, h);
		if (shown.baseName.empty() && shown.title.empty()) continue;

		// 種別で文字色を変える。"[Setting]" は MDX と同じ色。
		// ブックマークの行はフォルダへ移るものなのでフォルダの色。
		Rgb color = colors_.filer.color;
		if (shown.type & kFileItemFileSystem) {
			color = colors_.filer.fileSystemColor;
		} else if (shown.type & kFileItemDrive) {
			color = colors_.filer.driveColor;
		} else if (shown.type & (kFileItemDir | kFileItemBookmark)) {
			color = colors_.filer.folderColor;
		}

		if (textLayer_ != 0 && textLayer_->available()) {
			// 字は切る前の行位置 (rowY) に置き、はみ出しは y..y+h で切る。
			textLayer_->DrawText(x + layout_.fileListBaseNameX[fs], rowY,
			                     layout_.fileListBaseNameW[fs], itemH, shown.baseName, color,
			                     colors_.filer.colorBright, y, h);
			if (!shown.title.empty()) {
				textLayer_->DrawText(x + layout_.fileListTitleX[fs], rowY,
				                     layout_.fileListTitleW[fs], itemH, shown.title, color,
				                     colors_.filer.colorBright, y, h, scrollX, titlePartial);
			}
		} else {
			// フォントが読めなかったときの非常用（ミニフォントは本来
			// ステータス欄などの数字用）。こちらは縦に切れないので、
			// 丸ごと入る行だけ描く。
			if (h >= itemH) {
				PrintMini(x + layout_.fileListBaseNameX[fs], rowY + 1,
				          ToAscii(shown.baseName, kMaxAsciiChars).c_str(), color,
				          colors_.filer.colorBright);
				if (!shown.title.empty()) {
					PrintMini(x + layout_.fileListTitleX[fs], rowY + 1,
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
			BmpCopy(&screen_, layout_.fileListX, y, layout_.fileListW, h, &back_, layout_.fileListX, y, 100);
			if (textLayer_ != 0) textLayer_->ClearRect(layout_.fileListX, y, layout_.fileListW, h);
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

	// 切り出しはスキンが持つ（layout.ini の [ScrollBar] Src*）。置く位置は
	// PlacedFor が決めた導出値で、矢印は上端・下端に貼り付き、間が溝になる。
	// 書かれない隙間はパレット 0 = 透明にしたいので、まず消す。
	BmpFill(&scrollBar_, 0, 0, layout_.scrollW, layout_.scrollH, 0, 0, 0, 100);

	const Xywh &up = layout_.scrollSrcUpArrow;
	const Xywh &bar = layout_.scrollSrcBar;
	const Xywh &down = layout_.scrollSrcDownArrow;
	const Xywh &thumb = layout_.scrollSrcThumb;

	BmpCopy(&scrollBar_, layout_.scrollPosUpArrow[0], layout_.scrollPosUpArrow[1], up.w, up.h,
	        &scrollBarBase_, up.x, up.y, 100);
	// 溝は素材を上端から繰り返して敷く。足りない最後の 1 枚は途中で切る。
	// 逆に素材のほうが長ければ、上から必要なぶんだけ使う。
	if (bar.h > 0) {
		for (int y = 0; y < layout_.scrollGrooveH; y += bar.h) {
			int h = layout_.scrollGrooveH - y;
			if (h > bar.h) h = bar.h;
			BmpCopy(&scrollBar_, layout_.scrollPosBar[0], layout_.scrollPosBar[1] + y, bar.w,
			        h, &scrollBarBase_, bar.x, bar.y, 100);
		}
	}
	BmpCopy(&scrollBar_, layout_.scrollPosDownArrow[0], layout_.scrollPosDownArrow[1], down.w, down.h,
	        &scrollBarBase_, down.x, down.y, 100);

	// つまみは溝の中を動く。
	BmpCopy(&scrollBar_, layout_.scrollPosBar[0], layout_.scrollPosBar[1] + scrollBarThumb_, thumb.w,
	        thumb.h, &scrollBarBase_, thumb.x, thumb.y, 100);

	// 矢印の押下表示は通常の矢印と同じ場所に差し替える。原典はここを
	// 高さ CH_D(110) で転送していてバー全体を潰していた。
	if (scrollBarFlags_ & kScrollBarUpArrowDown) {
		const Xywh &s = layout_.scrollSrcUpArrowPress;
		BmpCopy(&scrollBar_, layout_.scrollPosUpArrow[0], layout_.scrollPosUpArrow[1], s.w, s.h,
		        &scrollBarBase_, s.x, s.y, 100);
	}
	if (scrollBarFlags_ & kScrollBarDownArrowDown) {
		const Xywh &s = layout_.scrollSrcDownArrowPress;
		BmpCopy(&scrollBar_, layout_.scrollPosDownArrow[0], layout_.scrollPosDownArrow[1], s.w, s.h,
		        &scrollBarBase_, s.x, s.y, 100);
	}

	// スクロールバーの**描画**の矩形は、一覧の矩形と必ず隣り合う
	// （PlacedFor がファイラーの矩形を分けて決めている）。重なることは
	// ないので、一度に描いてよい。
	//
	// ※ 指で掴みやすくするための「見た目より広い当たり判定」は
	//   [ScrollBar] HitWidth のほうで、こちらは一覧に食い込む。描画は
	//   食い込まないので、一覧が差分描画で置いたカーソルの右端が
	//   消えることはない。
	CompositeScrollBar(layout_.scrollX, layout_.scrollY, layout_.scrollW, layout_.scrollH);
}

// スクロールバーの一部分を画面へ合成する。x/y は画面座標。
void DrawScreen::CompositeScrollBar(int x, int y, int w, int h) {
	if (w <= 0 || h <= 0) return;
	BmpCopyComposite(&screen_, x, y, w, h, &scrollBar_, x - layout_.scrollX,
	                 y - layout_.scrollY, &back_, x, y, kBlendMul);
}

// ---------------------------------------------------------------------------
// プログレスバー
// ---------------------------------------------------------------------------

// プログレスバーの部品 1 つを、横の範囲 [xFrom, xTo) に掛かるぶんだけ写す。
// piece は素材内の矩形（上段）、dx は器の中での置き場所、srcYOfs は下段
// （再生済み）を取るときの縦のずらし。
static void PutBarPiece(Bitmap *dst, const Bitmap *src, const Xywh &piece, int dx, int pieceW,
                        int xFrom, int xTo, int srcYOfs) {
	const int start = Max(dx, xFrom);
	const int end = Min(dx + pieceW, xTo);
	if (end <= start) return;
	BmpCopy(dst, start, 0, end - start, piece.h, src, piece.x + (start - dx), piece.y + srcYOfs,
	        100);
}

void DrawScreen::PutProgressBar(uint32_t nowTimeMs, uint32_t playTimeMs, bool refresh) {
	if (!progressBar_.valid()) return;

	uint32_t now = nowTimeMs;
	if (playTimeMs != 0 && now > playTimeMs) now = playTimeMs;

	int len = 0;
	if (playTimeMs != 0) len = (int)((int64_t)layout_.progW * now / playTimeMs);

	bool disp = refresh;
	if (progressBarLenLast_ != len) {
		progressBarLenLast_ = len;
		disp = true;
	}
	if (disp) {
		// バーは 左端 / 中央の繰り返し / 右端 の 3 つで敷く（音量バーと同じ。
		// つまみが無いだけ）。進んだ部分 [0,len) は素材の下段、残りは上段。
		// 下段は同じ矩形をその高さぶん下へずらした位置。
		BmpFill(&progressBar_, 0, 0, layout_.progW, layout_.progH, 0, 0, 0, 100);
		const Xywh &left = layout_.progSrcBarLeft;
		const Xywh &right = layout_.progSrcBarRight;
		const Xywh &bar = layout_.progSrcBar;
		const int middleW = layout_.progW - left.w - right.w;
		for (int pass = 0; pass < 2; pass++) {
			const bool played = (pass == 0);
			const int xFrom = played ? 0 : len;
			const int xTo = played ? len : layout_.progW;
			PutBarPiece(&progressBar_, &progressBarBase_, left, 0, left.w, xFrom, xTo,
			            played ? left.h : 0);
			if (bar.w > 0 && bar.h > 0) {
				for (int x = 0; x < middleW; x += bar.w) {
					int w = middleW - x;
					if (w > bar.w) w = bar.w;
					PutBarPiece(&progressBar_, &progressBarBase_, bar, left.w + x, w, xFrom, xTo,
					            played ? bar.h : 0);
				}
			}
			PutBarPiece(&progressBar_, &progressBarBase_, right, layout_.progW - right.w, right.w,
			            xFrom, xTo, played ? right.h : 0);
		}
		BmpCopyComposite(&screen_, layout_.progX, layout_.progY, layout_.progW, layout_.progH, &progressBar_, 0, 0, &back_,
		                 layout_.progX, layout_.progY, kBlendMul);
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
		PrintMiniCompose(layout_.progX + layout_.progTimePos[0], layout_.progY + layout_.progTimePos[1], s,
		                 colors_.playKey.color, colors_.playKey.colorBright);
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
	const int pos = Max(0, Min(m, x - (layout_.volX + layout_.volSrcThumb.w / 2)));
	return pos * 200 / m - 100;
}

void DrawScreen::PutTotalVolBar(int volume, bool refresh) {
	if (!totalVolBar_.valid()) return;

	volume = Max(-100, Min(100, volume));
	bool disp = refresh || (totalVolBarLast_ != volume);
	if (!disp) return;
	totalVolBarLast_ = volume;

	const int barPos = TotalVolBarPosFromVolume(volume);

	// バーは 左端 / 中央の繰り返し / 右端 の 3 つで敷く（スクロールバーの溝と
	// 同じ作法。あちらは縦、こちらは横）。書かれない隙間はパレット 0 に
	// したいので、まず消す。
	BmpFill(&totalVolBar_, 0, 0, layout_.volW, layout_.volH, 0, 0, 0, 100);

	const Xywh &left = layout_.volSrcBarLeft;
	const Xywh &right = layout_.volSrcBarRight;
	const Xywh &bar = layout_.volSrcBar;
	const Xywh &thumb = layout_.volSrcThumb;
	BmpCopy(&totalVolBar_, 0, 0, left.w, left.h, &totalVolBarBase_, left.x, left.y, 100);
	const int middleW = layout_.volW - left.w - right.w;
	if (bar.w > 0 && bar.h > 0) {
		for (int x = 0; x < middleW; x += bar.w) {
			int w = middleW - x;
			if (w > bar.w) w = bar.w;
			BmpCopy(&totalVolBar_, left.w + x, 0, w, bar.h, &totalVolBarBase_, bar.x, bar.y, 100);
		}
	}
	BmpCopy(&totalVolBar_, layout_.volW - right.w, 0, right.w, right.h, &totalVolBarBase_, right.x,
	        right.y, 100);
	BmpCopy(&totalVolBar_, barPos, 0, thumb.w, thumb.h, &totalVolBarBase_, thumb.x, thumb.y, 100);
	BmpCopyComposite(&screen_, layout_.volX, layout_.volY, layout_.volW, layout_.volH, &totalVolBar_, 0, 0, &back_, layout_.volX,
	                 layout_.volY, kBlendMul);

	// 桁数は固定にする。短い文字列を書くと前の表示の末尾が残る。
	char s[64];
	snprintf(s, sizeof(s), "%c%03d", (volume >= 0) ? '+' : '-', abs(volume));
	PrintMiniCompose(layout_.volX + layout_.volVolumePos[0], layout_.volY + layout_.volVolumePos[1], s,
	                 colors_.playKey.color, colors_.playKey.colorBright);
}

// ---------------------------------------------------------------------------
// 操作ボタン
// ---------------------------------------------------------------------------

void DrawScreen::PutPlayKey(uint32_t status, bool refresh) {
	if (!playKey_.valid()) return;

	playKey_.SetPalette(layout_.palPlayKeyKey, colors_.playKey.keyBright, colors_.playKey.keyBright,
	                    colors_.playKey.keyBright);

	struct LedMap {
		uint32_t bit;
		int pal;
		int onPal;
	};
	// PAUSE だけ黄（2026-09-04、ユーザー指示。素材のパレットでも PAUSE の
	// 玉 (palPauseLed) は元から黄色なので、点灯色と素材の色が揃った）。
	const LedMap leds[4] = {
		{ kPlayKeyPlayLed, layout_.palPlayLed, layout_.palGreen },
		{ kPlayKeyPauseLed, layout_.palPauseLed, layout_.palYellow },
		{ kPlayKeyContLed, layout_.palContLed, layout_.palRed },
		{ kPlayKeyRepeatLed, layout_.palRepeatLed, layout_.palRed },
	};

	// LED は「前回の status」ではなく **今パレットに入っている色**と見比べる。
	// playKeyStatusLast_ の「まだ描いていない」印 (0xffffffff) は全 LED が
	// 点いている状態とちょうど同じ値なので、**点いた状態で始まる LED を
	// 「前回と同じ」と見なして塗り替えそこねる**。曲を始めると PollSong が
	// PlaySong の直後に Reload()（＝この印に戻す）を呼ぶので、PLAY の LED は
	// 演奏中ずっと消灯色のままだった。ほかに起動直後から演奏している場合や、
	// LoadAssets がパレットを消灯色へ戻すスキン切り替えでも同じことが起きる。
	// パレットと見比べれば、印が何であっても今の状態に合う色へ必ず直る。
	bool ledChanged = false;
	for (int i = 0; i < 4; i++) {
		const uint32_t now = status & leds[i].bit;
		const Rgb want = playKey_.palette()[now ? leds[i].onPal : layout_.palDark];
		Rgb &cur = playKey_.palette()[leds[i].pal];
		if (cur.r == want.r && cur.g == want.g && cur.b == want.b) continue;
		cur = want;
		ledChanged = true;
	}

	for (int i = 0; i < layout_.numPlayKeys; i++) {
		const uint32_t sw = status & (1u << i);
		bool disp = refresh || ledChanged;
		if (sw != (playKeyStatusLast_ & (1u << i))) disp = true;
		if (!disp) continue;

		const int w = layout_.playKeyRect[i].w;
		const int h = layout_.playKeyRect[i].h;
		const int x = layout_.playKeyX + layout_.playKeyPos[i][0];
		const int y = layout_.playKeyY + layout_.playKeyPos[i][1];
		BmpCopyComposite(&screen_, x, y, w, h, &playKey_, layout_.playKeyRect[i].x,
		                 layout_.playKeyRect[i].y + (sw ? h : 0), &back_, x, y, kBlendMul);
	}

	playKeyStatusLast_ = status;
}

// ---------------------------------------------------------------------------
// ヒットチェック（旧 mxv の Screen_HitCheck_*）
// ---------------------------------------------------------------------------

int DrawScreen::HitCheckPlayKey(int x, int y) const {
	for (int i = 0; i < layout_.numPlayKeys; i++) {
		const int l = layout_.playKeyX + layout_.playKeyPos[i][0];
		const int t = layout_.playKeyY + layout_.playKeyPos[i][1];
		if (x >= l && x < l + layout_.playKeyRect[i].w && y >= t && y < t + layout_.playKeyRect[i].h) {
			return i + 1;
		}
	}
	return kHitPlayKeyNone;
}

int DrawScreen::HitCheckScrollBar(int x, int y) const {
	// 横は当たり判定の矩形 ([ScrollBar] HitWidth) で見る。描画より広く
	// できるので、細いバーでも指で掴める（Phone がそうしている）。
	if (x < layout_.scrollHitX || x >= layout_.scrollHitX + layout_.scrollHitW) {
		return kHitScrollBarNone;
	}
	if (y < layout_.scrollY || y >= layout_.scrollY + layout_.scrollH) return kHitScrollBarNone;

	// 縦は描いたとおりに分ける。矢印は上端・下端に貼り付き、間が溝。
	const int dy = y - layout_.scrollY;
	if (dy < layout_.scrollPosUpArrow[1] + layout_.scrollSrcUpArrow.h) {
		return kHitScrollBarUpArrow;
	}
	if (dy >= layout_.scrollPosDownArrow[1]) return kHitScrollBarDownArrow;

	// 残りは溝の中。つまみの上下がページ送りになる。
	const int thumbTop = layout_.scrollPosBar[1] + scrollBarThumb_;
	if (dy < thumbTop) return kHitScrollBarUpPage;
	if (dy < thumbTop + layout_.scrollSrcThumb.h) return kHitScrollBarThumb;
	return kHitScrollBarDownPage;
}

int DrawScreen::HitCheckFileList(int x, int y) const {
	if (x < layout_.fileListX || x >= layout_.fileListX + layout_.fileListW) return -1;
	if (y < layout_.fileListY || y >= layout_.fileListY + layout_.fileListH) return -1;

	// 画素単位でスクロールしていると全体が上へずれているので、その分を足して
	// から行に直す。ずれの量は最後に描いたときのものを使う（描いてあるものと
	// 当たり判定を必ず一致させるため）。戻り値は「上から数えて何行目か」で、
	// 呼び出し側が Filer::top() に足して項目を決める。
	const int itemH = layout_.fileListItemH[fileListFontSize_ & 1];
	const int row = (y - layout_.fileListY + fileListOffsetLast_) / itemH;
	// 大きい文字 (13px) では 110/13 = 8 行と半端が出る。原典は半端の帯でも
	// 行 8 を返していたが、そこには何も描かれていないので弾く。
	// ずれているときは半端な行が 1 つ増える。
	const int drawRows = fileListRows() + ((fileListOffsetLast_ > 0) ? 1 : 0);
	if (row >= drawRows) return -1;
	return row;
}

int DrawScreen::HitCheckProgressBar(int x, int y) const {
	if (y < layout_.progY || y >= layout_.progY + layout_.progH) return -1;
	if (x < layout_.progX || x >= layout_.progX + layout_.progW) return -1;
	return Max(0, Min(layout_.progW, x - layout_.progX));
}

// バーの外へ指が出ても端に丸める。上の HitCheckProgressBar は「当たったか」を
// 見るので y も見るが、掴んだあとは x だけで決める。
int DrawScreen::ProgressPosFromX(int x) const {
	return Max(0, Min(layout_.progW, x - layout_.progX));
}

bool DrawScreen::HitCheckBanner(int x, int y) const {
	if (x < layout_.bannerX || x >= layout_.bannerX + layout_.bannerW) return false;
	if (y < layout_.bannerY || y >= layout_.bannerY + layout_.bannerH) return false;
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
	if (y < layout_.volY || y >= layout_.volY + layout_.volH) return false;
	if (x < layout_.volX || x >= layout_.volX + layout_.volW) return false;
	if (volume != 0) *volume = VolumeFromX(x);
	return true;
}

}  // namespace mxv2
