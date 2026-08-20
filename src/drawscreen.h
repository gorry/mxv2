// mxv2 - 描画コア（旧 mxv/draw.cpp の移植）
//
// 640x480 の 24bpp オフスクリーン (screen_) に、旧 mxv とまったく同じ手順で
// 描く。背景 (back_) を一度合成しておき、各表示要素は「背景から消す」→
// 「背景と合成して描く」という差分更新をする。
//
// 旧 mxv からの変更点:
//   - BITMAPINFO を Bitmap (bitmap.h) に置き換えた。メモリレイアウトは同じ。
//   - 素材は .rc のリソースではなく assets/*.bmp から読む。
//   - 曲名は GDI の DrawText ではなく 5x7 フォントで描く（Phase 4 で差し替え）。
//   - 無効矩形リスト (RectInvalidate) は不要になったので持たない。
//     SDL では毎フレーム全面を転送するほうが速い。

#ifndef MXV2_DRAWSCREEN_H
#define MXV2_DRAWSCREEN_H

#include <string>
#include <vector>

#include "bitmap.h"
#include "filer.h"
#include "screen.h"
#include "skin.h"
#include "textlayer.h"
#include "theme.h"

namespace mxv2 {

class DrawScreen {
public:
	DrawScreen();
	~DrawScreen();

	// スキン (レイアウトと素材の置き場所) を受け取り、素材を読んで
	// テーマを適用し、背景を合成する。skin は呼び出し側が保持し続けること。
	bool Init(const Skin *skin, std::string *err);

	int width() const { return skin_->screenW; }
	int height() const { return skin_->screenH; }
	const Skin &skin() const { return *skin_; }

	// 背景を作り直して全面を描き直す。
	void Reload();

	Theme &theme() { return theme_; }
	const Bitmap &screen() const { return screen_; }

	// ファイラと曲名の文字は、キャンバスではなく出力解像度のレイヤーへ描く。
	// 設定しない場合は 5x7 フォントの ASCII 表示へ退避する。
	void SetTextLayer(TextLayer *layer) { textLayer_ = layer; }

	// 24bpp オフスクリーンを Screen の ARGB8888 バッファへ転送する。
	void BlitTo(Screen *out) const;

	// ---- 鍵盤 -------------------------------------------------------
	void PutNoteOn(int key, int row, int color, int bendMode);
	void PutNoteOff(int key, int row);

	// ---- ステータス (FM 0..7) ---------------------------------------
	void PutVolume(int volume, int row);
	void PutPanpot(int panpot, int row);
	void PutDetune(int detune, int row);
	void PutVoice(int voice, int row);
	void PutQ(int q, int row);
	void PutPtr(int ptr, int row);
	void PutLFOPitch(int v, int row);
	void PutLFOPitch1(int v, int row);
	void PutLFOPitch2(int v, int row);
	void PutLFOPitch3(int v, int row);
	void PutLFOPitch4(int v, int row);
	void PutLFOVolume(int v, int row);
	void PutLFOVolume1(int v, int row);
	void PutLFOVolume2(int v, int row);
	void PutLFOVolume3(int v, int row);
	void PutLevelMeter(const char *levelMeterInfo, int row);

	// ---- ステータス (PCM 8..15) -------------------------------------
	void PutPCMVolume(int volume, int row);
	void PutPCMPtr(int ptr, int row);

	// ---- 画面下部 ---------------------------------------------------
	void PutMDXTitle(const std::string &titleUtf8);
	void PutProgressBar(uint32_t nowTimeMs, uint32_t playTimeMs, bool refresh);
	// 音量は -100..+100（0 が中央）。つまみの画素位置は中で計算する。
	void PutTotalVolBar(int volume, bool refresh);
	void PutPlayKey(uint32_t status, bool refresh);

	// ---- ファイラ ---------------------------------------------------
	// 画面に収まる行数。Filer::SetViewMetrics へ渡す。
	int fileListRows() const;
	// 1 行の高さ (px)。Filer::SetViewMetrics へ渡す（画素スクロールに要る）。
	int fileListItemH() const;
	void PutFileList(const Filer &filer, bool refresh);
	void PutScrollBar(int topPx, int maxTopPx);

	// ---- ヒットチェック (旧 mxv の Screen_HitCheck_*) ----------------
	// 操作キー。戻り値は旧 mxv の MX_HITCODE_PLAYKEY_* と同じ並びで、
	// 押下ビットは (1 << (hit-1)) = PlayKeyStatus の下位ビットに対応する。
	enum PlayKeyHit {
		kHitPlayKeyNone = 0,
		kHitPlayKeyPrev,
		kHitPlayKeyStop,
		kHitPlayKeyPlay,
		kHitPlayKeyFastPlay,
		kHitPlayKeyPause,
		kHitPlayKeyNext,
		kHitPlayKeyCont,
		kHitPlayKeyRepeat,
	};
	enum ScrollBarHit {
		kHitScrollBarNone = 0,
		kHitScrollBarUpArrow,
		kHitScrollBarUpPage,
		kHitScrollBarThumb,
		kHitScrollBarDownPage,
		kHitScrollBarDownArrow,
	};

	int HitCheckPlayKey(int x, int y) const;
	int HitCheckScrollBar(int x, int y) const;
	// 表示行 (0..fileListRows()-1)。当たらなければ -1。
	int HitCheckFileList(int x, int y) const;
	// バー内の x 位置 (0..kProgressBarWidth)。当たらなければ -1。
	int HitCheckProgressBar(int x, int y) const;
	// バナー。ここを押すとコンテキストメニューを出す（右クリックできない
	// 環境やスマートフォン向け）。
	bool HitCheckBanner(int x, int y) const;
	// 音量バーに当たったら true を返し、*volume に -100..+100 を入れる。
	// (-1 も正しい音量なので、戻り値では当たり外れを表せない)
	bool HitCheckTotalVolBar(int x, int y, int *volume) const;
	// x を音量へ写す (範囲外でも端に丸める)。ドラッグ中に使う。
	int VolumeFromX(int x) const;
	// 音量をつまみの画素位置へ写す。
	int TotalVolBarPosFromVolume(int volume) const;

	// ---- スクロールバーの見た目の状態 -------------------------------
	enum ScrollBarFlag {
		kScrollBarDrag = 1 << 0,           // つまみをドラッグ中 (位置を固定する)
		kScrollBarUpArrowDown = 1 << 1,    // 上矢印の押下表示
		kScrollBarDownArrowDown = 1 << 2,  // 下矢印の押下表示
	};
	void SetScrollBarFlags(int flags) { scrollBarFlags_ = flags; }
	int scrollBarFlags() const { return scrollBarFlags_; }
	void SetScrollBarThumb(int y);
	int scrollBarThumb() const { return scrollBarThumb_; }

	// 文字サイズ 0 = 小 (11 行) / 1 = 大 (8 行)。旧 mxv の MX_SetFontSize。
	void SetFileListFontSize(int size);
	int fileListFontSize() const { return fileListFontSize_; }

	// 操作キーの状態ビット。旧 mxv の MX_PUTPLAYKEY_STATUS_* と同じ。
	enum PlayKeyStatus {
		kPlayKeyPrev = 1 << 0,
		kPlayKeyStop = 1 << 1,
		kPlayKeyPlay = 1 << 2,
		kPlayKeyFastPlay = 1 << 3,
		kPlayKeyPause = 1 << 4,
		kPlayKeyNext = 1 << 5,
		kPlayKeyCont = 1 << 6,
		kPlayKeyRepeat = 1 << 7,
		kPlayKeyPlayLed = 1 << 16,
		kPlayKeyPauseLed = 1 << 17,
		kPlayKeyContLed = 1 << 18,
		kPlayKeyRepeatLed = 1 << 19,
	};

	// つまみが動ける幅。旧 mxv の MX_CW_TOTALVOLBARMOVEMENT /
	// MX_CH_SCROLLBARMOVEMENT / MX_CW_PROGRESSBAR。スキンで変わる。
	int totalVolBarMovement() const { return skin_->volBarMovement(); }
	int progressBarWidth() const { return skin_->progW; }
	int scrollBarMovement() const { return skin_->scrollBarMovement(); }

private:
	bool LoadAssets(std::string *err);
	void LoadBackBitmap();
	void CompositeBack();
	void CompositeBanner();
	void CompositeStatusBack();
	void CompositeFileList();
	void CompositeKeyboard();
	void CutKeyboardBitmap(Bitmap *out, const Bitmap &src, int xsrc, int cutWidth,
	                       int pal1, int pal2);

	// 5x7 フォントでの文字列描画。
	void Print(int x, int y, const char *msg, const Rgb &color, int alpha);
	void PrintCompose(int x, int y, const char *msg, const Rgb &color, int alpha);

	// ステータス欄の共通処理: 背景を戻してから文字を合成する。
	void PutStatusText(int x, int y, int cells, const char *text);

	const Skin *skin_;  // 呼び出し側の持ち物。寿命は DrawScreen より長いこと
	Theme theme_;

	Bitmap screen_;      // 表示用 24bpp
	Bitmap back_;        // 合成済み背景 24bpp
	Bitmap backBitmap_;  // テーマの背景画像 24bpp

	Bitmap kb0_;              // 鍵盤の下地
	Bitmap keyboard_[12];     // 各音の鍵
	Bitmap font_;             // 5x7 フォント
	Bitmap levelMeter_;       // レベルメータ
	Bitmap banner_;           // バナー
	Bitmap playKey_;          // 操作キー
	Bitmap progressBarBase_;  // プログレスバー素材
	Bitmap progressBar_;      // プログレスバー組み立て用
	Bitmap totalVolBarBase_;
	Bitmap totalVolBar_;
	Bitmap scrollBarBase_;
	Bitmap scrollBar_;

	TextLayer *textLayer_;
	int fileListFontSize_;

	int scrollBarFlags_;
	int scrollBarThumb_;  // 溝の中のつまみ位置 0..kScrollBarMovement

	Rgb kbPalette_[12];
	Rgb palLevelMeter_[128];

	std::string mdxTitle_;

	// 差分更新用の前回値
	uint32_t playKeyStatusLast_;
	int progressBarLenLast_;
	int progressNowSecLast_;
	int totalVolBarLast_;
	std::vector<FileItem> fileListLast_;
	int fileListCursorLast_;
	int fileListOffsetLast_;  // 前回のスクロール端数 (px)

	DrawScreen(const DrawScreen &);
	DrawScreen &operator=(const DrawScreen &);
};

}  // namespace mxv2

#endif  // MXV2_DRAWSCREEN_H
