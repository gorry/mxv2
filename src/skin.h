// mxv2 - スキン（画面レイアウトと素材の置き場所）
//
// 旧 mxv は画面が 640x480 固定で、各部品の座標を draw.cpp に #define で
// 埋め込んでいた。mxv2 ではそれをフォルダ単位の「スキン」へ出し、
// 差し替えられるようにする。
//
//   assets/skin/<名前>/
//       layout.ini     画面サイズと各部品の座標（このファイルが読むもの）
//       theme.mxv      配色（theme.* が読む。設定ウィンドウで編集・保存できる）
//       font.ttf       文字描画に使うフォント（省略可）
//       *.bmp          素材ビットマップ
//
// layout.ini に `[Skin] Base=<名前>` を書くと、そのスキンを土台にできる。
// 土台の layout.ini を読んでから自分の値で上書きし、ファイル（.bmp や
// font.ttf）も自分のフォルダに無ければ土台のフォルダを見に行く。
// 配色だけ変えたスキンなら 2 ファイルで済む。

#ifndef MXV2_SKIN_H
#define MXV2_SKIN_H

#include <string>
#include <vector>

namespace mxv2 {

struct Xywh {
	int x, y, w, h;
};

struct Skin {
	// ---- 画面 --------------------------------------------------------
	int screenW, screenH;

	// ---- 鍵盤 --------------------------------------------------------
	int kbX, kbY;
	int kbXOffset[13];  // 1 オクターブ分の鍵の x。[12] は 1 オクターブの幅
	int kbYOffset;
	int chYOffset[9];  // 各チャンネル行の y
	int keyOffset;     // 鍵の描画原点の補正

	// ---- 5x7 フォント（ビジュアライザ用） ------------------------------
	int fontW, fontH;

	// ---- ステータス ---------------------------------------------------
	int statusX, statusY;
	int statusBackW, statusBackH;
	int pcmXOffset[8], pcmYOffset[8];  // PCM 8ch のステータス欄の並び

	// ---- レベルメータ -------------------------------------------------
	int levelMeterPalOfs;      // levelmeter.bmp のパレット開始番号
	int levelMeterWidthCells;  // セル数

	// ---- バナー -------------------------------------------------------
	int bannerX, bannerY, bannerW, bannerH;

	// ---- 曲名 ---------------------------------------------------------
	int titleX, titleY, titleW, titleH;

	// ---- ファイルリスト -----------------------------------------------
	int fileListX, fileListY, fileListW, fileListH;
	// [0] = 小さい文字 / [1] = 大きい文字。layout.ini では「小,大」と書き、
	// 1 つだけ書けば両方に効く。
	int fileListRows[2];   // 行数
	int fileListItemH[2];  // 1 行の高さ
	// 以下は行の左端からのピクセル位置と、描画に使う幅（ピクセル）。
	// 旧 mxv は等幅フォント前提で「文字数」だったが、プロポーショナル
	// フォントでは意味を持たないのでピクセルに変えてある。
	int fileListBaseNameX[2], fileListBaseNameW[2];
	int fileListTitleX[2], fileListTitleW[2];

	// ---- スクロールバー -----------------------------------------------
	int scrollX, scrollY, scrollW, scrollH;
	int scrollButtonH;

	// ---- プログレスバー -----------------------------------------------
	int progX, progY, progW, progH;
	int progTimeXOfs, progTimeYOfs;

	// ---- 音量バー -----------------------------------------------------
	int volX, volY, volW, volH;
	int volTimeXOfs, volTimeYOfs;
	int volNobW;
	Xywh volRect[2];  // volbar.bmp 内の つまみ / スライド

	// ---- 操作キー -----------------------------------------------------
	int playKeyX, playKeyY;
	int numPlayKeys;         // 使うキーの数（SHUFFLE を含めるなら 9）
	Xywh playKeyRect[9];     // playkey.bmp 内の位置と大きさ
	int playKeyPos[9][2];    // 画面上の配置（playKeyX/Y からの相対）

	// playkey.bmp のパレット番号
	int palPlayKeyKey, palPlayLed, palPauseLed, palContLed, palRepeatLed;
	int palDark, palRed, palGreen;

	// ---- 素材のファイル名 ---------------------------------------------
	// layout.ini の [Assets]。スキンのフォルダに無ければ Base のフォルダから
	// 読む (FindFile)。差し替えたいときはファイル名ごと変えられる。
	std::string backBitmap;         // 背景
	std::string kb0Bitmap;          // 鍵盤の下地
	std::string kb1Bitmap;          // 白鍵側の鍵
	std::string kb2Bitmap;          // 黒鍵側の鍵
	std::string font5x7Bitmap;      // ビジュアライザ用の 5x7 フォント
	std::string levelMeterBitmap;   // レベルメータ
	std::string bannerBitmap;       // バナー
	std::string playKeyBitmap;      // 操作キー
	std::string progressBarBitmap;  // プログレスバー
	std::string volBarBitmap;       // 音量バー
	std::string scrollBarBitmap;    // スクロールバー

	Skin();

	// skinDir の layout.ini を読む。Base 指定があれば先に土台を読む。
	// layout.ini が無くても既定値のまま true を返す（配色だけのスキン用）。
	bool Load(const std::string &skinDir, std::string *err);

	// 素材を探す。自分のフォルダ -> 土台のフォルダ の順。
	// 見つからなければ自分のフォルダのパスを返す（呼び出し側でエラーにする）。
	std::string FindFile(const std::string &name) const;

	const std::string &dir() const { return dir_; }
	const std::string &baseDir() const { return baseDir_; }

	// ---- 導出値 -------------------------------------------------------
	int fileListMaxItemH() const {
		return (fileListItemH[0] > fileListItemH[1]) ? fileListItemH[0] : fileListItemH[1];
	}
	// つまみが動ける幅。旧 mxv の MX_CH_SCROLLBARMOVEMENT / MX_CW_TOTALVOLBARMOVEMENT。
	int scrollBarMovement() const { return scrollH - scrollButtonH * 3; }
	int volBarMovement() const { return volW - volNobW; }

private:
	bool LoadInto(const std::string &skinDir, std::string *err, int depth);

	std::string dir_;
	std::string baseDir_;
};

// assets/skin の下にあるスキン名を並べる。
void ListSkins(const std::string &skinRootDir, std::vector<std::string> *out);

}  // namespace mxv2

#endif  // MXV2_SKIN_H
