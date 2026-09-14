// mxv2 - 描画コア（旧 mxv/draw.cpp の移植）
//
// 640x480 の 24bpp オフスクリーン (screen_) に、旧 mxv とまったく同じ手順で
// 描く。背景 (back_) を一度合成しておき、各表示要素は「背景から消す」→
// 「背景と合成して描く」という差分更新をする。
//
// 旧 mxv からの変更点:
//   - BITMAPINFO を Bitmap (bitmap.h) に置き換えた。メモリレイアウトは同じ。
//   - 素材は .rc のリソースではなくスキンのフォルダの *.bmp から読む。
//   - 曲名は GDI の DrawText ではなくミニフォントで描く（Phase 4 で差し替え）。
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
#include "colors.h"
#include "dispqueue.h"
#include "textlayer.h"

namespace mxv2 {

class DrawScreen {
public:
	DrawScreen();
	~DrawScreen();

	// スキン (レイアウトと素材の置き場所) を受け取り、素材を読んで
	// 配色を適用し、背景を合成する。skin は呼び出し側が保持し続けること。
	// キャンバスはスキンの宣言サイズで始まる（伸ばすのは Resize）。
	bool Init(const Skin *skin, std::string *err);

	// キャンバスの大きさを変える（窓のリサイズ、画面の回転）。
	// バッファを作り直し、レイアウトを解決し直して、全面を描き直す。
	// 大きさが同じなら何もしないで true。
	bool Resize(int canvasW, int canvasH, std::string *err);

	int width() const { return canvasW_; }
	int height() const { return canvasH_; }
	const Skin &skin() const { return *skin_; }
	// 今のキャンバスに合わせて解決したレイアウト（Skin::PlacedFor）。
	const Skin &layout() const { return layout_; }

	// キャンバスを伸ばせる上限 (px)。ファイラーを置いた向きの長さで、
	// 背景ビットマップの大きさ（宣言サイズより小さければ宣言サイズ）で
	// 決まる。「背景画像を使わない」設定なら 0 = 上限なし。
	// Skin::CanvasSizeFor へ渡す。
	int stretchLimit() const;

	// 背景を作り直して全面を描き直す。
	void Reload();

	Colors &colors() { return colors_; }
	const Bitmap &screen() const { return screen_; }

	// ファイラーと曲名の文字は、キャンバスではなく出力解像度のレイヤーへ描く。
	// 設定しない場合はミニフォントの ASCII 表示へ退避する。
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

	// ---- ステータス欄の表示モード（tonedata.md） ----------------------
	// ステータス欄の長押しで「チャンネルステータス」と「音色データ」を
	// 切り替える。ini には保存しない（常にチャンネルステータスで始まる）。
	// 上の Put*（チャンネルステータス）と下の PutOPM*（音色データ）は、
	// それぞれ自分のモードのときだけ描く。生成側は両方を積んでくるので、
	// 消費側 (Visualizer) はモードを気にしなくてよい。
	enum StatusMode {
		kStatusModeChannel = 0,
		kStatusModeTone,
	};
	int statusMode() const { return statusMode_; }
	// 切り替える。欄を下地で消して、新しいモードの 0 表示を敷く。呼んだ側は
	// Player::RequestStatusRefresh で値を積み直させること。
	void SetStatusMode(int mode);
	void ToggleStatusMode() {
		SetStatusMode(statusMode_ == kStatusModeChannel ? kStatusModeTone : kStatusModeChannel);
	}

	// ---- 音色データ (FM 0..7 / OPM 全体) ------------------------------
	// reg20 は OPM $20+ch の値（bit0-2 アルゴリズム、bit3-5 フィードバック）。
	void PutOPMChannel(int reg20, int row);
	// slot は OPM のスロット (0=M1 1=M2 2=C1 3=C2)、group は OpmOpGroup、
	// value はそのレジスタ（TL は音色データ）のバイト。
	void PutOPMOperator(int row, int slot, int group, int value);
	// kind は OpmGlobalKind。valid が false なら "--"。
	void PutOPMGlobal(int kind, int value, bool valid);

	// マスクしているチャンネルの鍵盤に半透明のグレーを乗せる。
	// bit0..7 = FM ch.1-8（その段の鍵盤全体）、bit8..15 = PCM ch.P-W
	// （PCM は鍵盤 1 段を共有するので、横に 8 等分して左から順に割り当てる）。
	// Player::channelMask() をそのまま渡してよい。
	//
	// 実際に乗せるのは BlitTo のとき。キャンバスに焼くと、重ねるたびに
	// 濃くなるうえ、鍵盤の差分描画とぶつかる（マスク中は処理ごと止まるので
	// その段は描き直されない）。
	void SetChannelMask(uint16_t mask) { channelMask_ = mask; }

	// ステータス欄を全部 0 で埋める。
	//
	// mxv2 はステータスをデコードスレッドのポーリングから積むので、演奏を
	// 始めるまで 1 つも積まれず、欄が空のままになる（旧 mxv は WM_TIMER が
	// 起動直後から回っていて、値が 0 のワークをそのまま描いていた）。
	// 画面を作り直したところで呼んで、旧 mxv と同じ「全部 0」の状態にする。
	void PutStatusZero();

	// ---- 画面下部 ---------------------------------------------------
	void PutMDXTitle(const std::string &titleUtf8);
	// 今出している曲名。読み込み中の知らせを出す前に控えておくのに使う。
	const std::string &mdxTitle() const { return mdxTitle_; }
	// 曲名が枠に収まらないときだけ横へスクロールさせる。**毎フレーム呼ぶこと**
	// （収まっているときと、動きが無いときは何もしない）。
	// nowMs は SDL_GetTicks の値。
	void UpdateTitleScroll(uint32_t nowMs);

	// ファイラーの曲名スクロール。値は Settings::FileListScroll
	// （0=しない / 1=カーソル行だけ / 2=全て）。
	void SetFileListScroll(int mode) { fileListScroll_ = mode; }
	void PutProgressBar(uint32_t nowTimeMs, uint32_t playTimeMs, bool refresh);
	// 音量は -100..+100（0 が中央）。つまみの画素位置は中で計算する。
	void PutTotalVolBar(int volume, bool refresh);
	void PutPlayKey(uint32_t status, bool refresh);

	// ---- ファイラー ---------------------------------------------------
	// 画面に収まる行数。Filer::SetViewMetrics へ渡す。
	int fileListRows() const;
	// 1 行の高さ (px)。Filer::SetViewMetrics へ渡す（画素スクロールに要る）。
	int fileListItemH() const;
	// nowMs は SDL_GetTicks の値（曲名スクロールに使う）。
	void PutFileList(const Filer &filer, bool refresh, uint32_t nowMs);
	void PutScrollBar(int topPx, int maxTopPx);

	// ---- ヒットチェック (旧 mxv の Screen_HitCheck_*) ----------------
	// 操作ボタン。戻り値は旧 mxv の MX_HITCODE_PLAYKEY_* と同じ並びで、
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
	// x をバー内の位置へ写す (範囲外でも端に丸める)。ドラッグ中に使う。
	int ProgressPosFromX(int x) const;
	// バナー。ここを押すとコンテキストメニューを出す（右クリックできない
	// 環境やスマートフォン向け）。
	bool HitCheckBanner(int x, int y) const;
	// 音量バーに当たったら true を返し、*volume に -100..+100 を入れる。
	// (-1 も正しい音量なので、戻り値では当たり外れを表せない)
	bool HitCheckTotalVolBar(int x, int y, int *volume) const;
	// x を音量へ写す (範囲外でも端に丸める)。ドラッグ中に使う。
	int VolumeFromX(int x) const;
	// 鍵盤。当たったチャンネル (0..7 = FM ch.1-8 / 8..15 = PCM ch.P-W) を
	// 返す。当たらなければ -1。区切りは灰色を乗せる場所と同じで、PCM の段は
	// 横に 8 等分して左から順に割り当てる（SetChannelMask のコメント）。
	int HitCheckKeyboard(int x, int y) const;
	// ステータス欄。当たった段 (0..7 = FM ch.1-8 / 8 = PCM) を返す。
	// 当たらなければ -1。鍵盤と違って段の中は割らない（呼び出し側は FM か
	// PCM かの一括操作に使う）。
	int HitCheckStatus(int x, int y) const;

	// チャンネル ch (0..15) の鍵盤の矩形。灰色を乗せる場所とクリックの
	// 当たり判定で同じものを使う。鍵盤が無ければ false。
	// チュートリアルのスポットライト (tutorial.cpp) も見る。
	bool ChannelKeyRect(int ch, int *x0, int *y0, int *x1, int *y1) const;
	// ステータス欄 1 段ぶんの矩形。下地を敷く場所と当たり判定で共用する。
	bool StatusRect(int row, int *x, int *y, int *w, int *h) const;
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

	// 操作ボタンの状態ビット。旧 mxv の MX_PUTPLAYKEY_STATUS_* と同じ。
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

	// ミニフォント（素材のビットマップ文字）での文字列描画。
	void PrintMini(int x, int y, const char *msg, const Rgb &color, int alpha);
	void PrintMiniCompose(int x, int y, const char *msg, const Rgb &color, int alpha);

	// ステータス欄の項目 1 つの左上（row 段目）。位置はスキン持ち。
	void StatusItemPos(StatusItem item, int row, int *x, int *y) const;

	// ステータス欄の共通処理: 項目の位置に文字を合成する。
	// チャンネルステータスのモードでないときは何もしない。
	void PutStatusText(StatusItem item, int row, const char *text);

	// 音色データの項目。row は FM の段 (0..7) か PCM の段 (8)、slot は
	// オペレータごとの項目のときの OPM スロット（それ以外は 0）。
	// 音色データのモードでないときは何もしない。
	void PutToneText(StatusItem item, int row, int slot, const char *text);
	// ステータス欄を下地で消す（モードの切り替え）。
	void ClearStatusArea();

	// スクロールバーの一部分を画面へ合成する（ファイラーと重なる列を
	// 避けて描くために分割して呼ぶ）。
	void CompositeScrollBar(int x, int y, int w, int h);

	const Skin *skin_;  // 呼び出し側の持ち物。寿命は DrawScreen より長いこと

	// skin_ を今のキャンバスの大きさに合わせて解決したもの
	// (Skin::PlacedFor)。ファイラー / 一覧 / スクロールバーの矩形、行数、
	// 曲名の幅、それにファイラー以外側の部品の座標が入っている。
	// **描画と当たり判定はすべてこちらを見る。** skin_ を直に見てよいのは
	// 「スキンそのもの」を返す skin() だけ。
	Skin layout_;
	int canvasW_, canvasH_;

	// 背景ビットマップの大きさ（読めなければ 0）。伸ばせる上限に使う。
	int backImageW_, backImageH_;

	bool CreateBuffers(std::string *err);
	bool EnsureScrollBarBuffer(std::string *err);

	Colors colors_;

	Bitmap screen_;      // 表示用 24bpp
	Bitmap back_;        // 合成済み背景 24bpp
	Bitmap backBitmap_;  // 配色設定の背景画像 24bpp

	Bitmap kb0_;              // 鍵盤の下地
	Bitmap keyboard_[12];     // 各音の鍵
	Bitmap miniFont_;         // ミニフォント（ビットマップ文字のグリフ表）
	Bitmap levelMeter_;       // レベルメータ
	Bitmap banner_;           // バナー
	Bitmap playKey_;          // 操作ボタン
	Bitmap progressBarBase_;  // プログレスバー素材
	Bitmap progressBar_;      // プログレスバー組み立て用
	Bitmap totalVolBarBase_;
	Bitmap totalVolBar_;
	Bitmap scrollBarBase_;
	Bitmap scrollBar_;

	TextLayer *textLayer_;
	int fileListFontSize_;
	int statusMode_;  // StatusMode

	// マスクしているチャンネル（SetChannelMask）。BlitTo で灰色を乗せる。
	uint16_t channelMask_;
	void OverlayChannelMask(Screen *out) const;

	int scrollBarFlags_;
	int scrollBarThumb_;  // 溝の中のつまみ位置 0..kScrollBarMovement

	Rgb kbPalette_[12];
	Rgb palLevelMeter_[128];

	// ミニフォントの 1 文字の大きさ。素材の大きさ ÷ グリフ表の並び
	// （drawscreen.cpp の kMiniFontCols / kMiniFontRows）で決まるので、
	// 5x7 のような特定の大きさには縛られない。
	int miniGlyphW_, miniGlyphH_;

	std::string mdxTitle_;

	// 曲名の横スクロール。枠に収まらない曲名だけが対象。
	void DrawMDXTitle(float scrollX);
	float titleScrollMax_;       // 右端まで送る量（論理px）。0 なら収まっている
	float titleScrollShown_;     // いま描いてある送り量
	uint32_t titleScrollBaseMs_; // 今の周期が始まった時刻
	bool titleScrollStarted_;    // 起点を決めたか（最初の UpdateTitleScroll で決まる）

	// 差分更新用の前回値
	uint32_t playKeyStatusLast_;
	int progressBarLenLast_;
	int progressNowSecLast_;
	int totalVolBarLast_;
	std::vector<FileItem> fileListLast_;
	int fileListCursorLast_;
	int fileListOffsetLast_;  // 前回のスクロール端数 (px)

	// ファイラーの曲名スクロール（Settings::FileListScroll）。
	int fileListScroll_;
	std::vector<float> fileListScrollLast_;  // 行ごとの、いま描いてある送り量
	std::vector<int> fileListTitleW_;        // 行ごとの曲名の幅（-1 は未計測）
	// 横スクロールの進み具合 (ms)。**一覧の項目ごとに、並び順で持つ**。
	// 縦にスクロールしても値を見失わないようにするため（行の位置ではなく
	// 項目に付いている）。**進めるのは見えている行だけ**で、画面から外れた
	// 行はその場の位置で止まったまま待つ。カーソルが来た行だけ 0 へ戻す。
	std::vector<uint32_t> fileListPhaseMs_;
	std::string fileListRefLast_;      // 前回のフォルダ（変わったら作り直す）
	uint32_t fileListPhaseTickMs_;     // 前回進めた時刻
	int fileListFsLast_;               // 前回の文字の大きさ（幅を測り直す印）

	DrawScreen(const DrawScreen &);
	DrawScreen &operator=(const DrawScreen &);
};

}  // namespace mxv2

#endif  // MXV2_DRAWSCREEN_H
