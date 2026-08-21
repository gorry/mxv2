// mxv2 - スキン（画面レイアウトと素材の置き場所）
//
// 旧 mxv は画面が 640x480 固定で、各部品の座標を draw.cpp に #define で
// 埋め込んでいた。mxv2 ではそれをフォルダ単位の「スキン」へ出し、
// 差し替えられるようにする。
//
//   skin/<名前>/
//       layout.ini     画面サイズと各部品の座標（このファイルが読むもの）
//       colors.ini     配色（colors.* が読む。[配色設定] で編集・保存できる）
//       font.ttf       文字描画に使うフォント（省略可）
//       *.bmp          素材ビットマップ
//
// skin/ は同梱ぶん (assets/) とユーザーフォルダの両方にある。1 つのスキンは
// どちらか一方のフォルダのもので、指定は "<名前>"（ユーザーフォルダ。
// 無ければ同梱）か "assets:<名前>"（同梱を名指し）。assetpath.h を見ること。
//
// layout.ini に `[Skin] Base=<名前>` を書くと、そのスキンを土台にできる。
// 土台の layout.ini を読んでから自分の値で上書きし、ファイル（.bmp や
// font.ttf）も自分のフォルダに無ければ土台のフォルダを見に行く。
// 配色だけ変えたスキンなら 2 ファイルで済む。土台の土台も辿る
// （「名前を付けて保存」した配色が、土台つきのスキンからでも作れるように）。
// 循環したところで打ち切る。

#ifndef MXV2_SKIN_H
#define MXV2_SKIN_H

#include <string>
#include <vector>

#include "assetpath.h"

namespace mxv2 {

struct Xywh {
	int x, y, w, h;
};

// 配色ファイルの名前。旧 mxv の <テーマ名>.mxv をそのまま持ってきていたのを
// 2026-08-21 に layout.ini と揃えた。**書くのは新しい名前だけ**で、
// 旧い名前は読むためだけに残してある。
extern const char kColorsFile[];        // "colors.ini"
extern const char kLegacyColorsFile[];  // "theme.mxv"

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

	// ---- ファイラー -----------------------------------------------
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
	// 部品は 6 つ。Src* は素材の中の位置と大きさ、Pos* は Rect の左上からの
	// 相対位置（[PlayKey] の Src<n> / Pos<n> と同じ書き方）。
	// つまみは溝の中を動くので位置が計算で決まり、押下中の矢印は通常の矢印と
	// 同じ場所へ描くので、この 3 つは Pos を持たない。
	int scrollX, scrollY, scrollW, scrollH;
	Xywh scrollSrcThumb;
	Xywh scrollSrcUpArrowPress;
	Xywh scrollSrcDownArrowPress;
	Xywh scrollSrcUpArrow;
	Xywh scrollSrcBar;
	Xywh scrollSrcDownArrow;
	int scrollPosUpArrow[2];
	int scrollPosBar[2];
	int scrollPosDownArrow[2];

	// ---- プログレスバー -----------------------------------------------
	int progX, progY, progW, progH;
	int progTimeXOfs, progTimeYOfs;

	// ---- 音量バー -----------------------------------------------------
	int volX, volY, volW, volH;
	int volTimeXOfs, volTimeYOfs;
	int volNobW;
	Xywh volRect[2];  // volbar.bmp 内の つまみ / スライド

	// ---- 操作ボタン -----------------------------------------------------
	int playKeyX, playKeyY;
	int numPlayKeys;         // 使うボタンの数（SHUFFLE を含めるなら 9）
	Xywh playKeyRect[9];     // playkey.bmp 内の位置と大きさ
	int playKeyPos[9][2];    // 画面上の配置（playKeyX/Y からの相対）

	// playkey.bmp のパレット番号
	int palPlayKeyKey, palPlayLed, palPauseLed, palContLed, palRepeatLed;
	int palDark, palRed, palGreen;

	// ---- 素材のファイル名 ---------------------------------------------
	// layout.ini では表示部品ごとのセクションに Img<名前> のキーで書く
	// （背景なら [Screen] ImgBack、バナーなら [Banner] ImgBanner）。
	// スキンのフォルダに無ければ Base のフォルダから読む (FindFile)。
	// 差し替えたいときはファイル名ごと変えられる。
	std::string backBitmap;         // 背景
	std::string kb0Bitmap;          // 鍵盤の下地
	std::string kb1Bitmap;          // 白鍵側の鍵
	std::string kb2Bitmap;          // 黒鍵側の鍵
	std::string font5x7Bitmap;      // ビジュアライザ用の 5x7 フォント
	std::string levelMeterBitmap;   // レベルメータ
	std::string bannerBitmap;       // バナー
	std::string playKeyBitmap;      // 操作ボタン
	std::string progressBarBitmap;  // プログレスバー
	std::string volBarBitmap;       // 音量バー
	std::string scrollBarBitmap;    // スクロールバー

	Skin();

	// スキンを読む。ref は "<名前>" か "assets:<名前>" (assetpath.h)。
	// 既定値から組み立て直すので、使い回してよい。layout.ini が無くても、
	// フォルダさえあれば既定レイアウトのまま true を返す（配色だけの
	// スキン用）。フォルダが無ければ false。
	bool Load(const AssetPaths &paths, const std::string &ref, std::string *err);

	// 素材を探す。dirs() の並び順（自分 -> 土台 -> その土台 …）。
	// 見つからなければ先頭のフォルダのパスを返す（呼び出し側でエラーにする）。
	std::string FindFile(const std::string &name) const;

	// 配色ファイルを探す。フォルダごとに colors.ini -> theme.mxv (旧名) の順に
	// 見るので、土台に新しい名前があっても自分の旧い名前が勝つ。
	// どこにも無ければ空文字列。
	std::string FindColorsFile() const;

	const std::string &ref() const { return ref_; }
	// layout.ini の [Skin] Base に書いてあったもの。無ければ空。
	const std::string &baseRef() const { return baseRef_; }
	// このスキンのファイルを探す場所。優先度の高い順。
	const std::vector<std::string> &dirs() const { return dirs_; }

	// ---- 導出値 -------------------------------------------------------
	int fileListMaxItemH() const {
		return (fileListItemH[0] > fileListItemH[1]) ? fileListItemH[0] : fileListItemH[1];
	}
	// つまみが動ける幅。旧 mxv の MX_CH_SCROLLBARMOVEMENT / MX_CW_TOTALVOLBARMOVEMENT。
	// つまみは溝の中を動くので、溝の高さからつまみの高さを引いたもの。
	int scrollBarMovement() const { return scrollSrcBar.h - scrollSrcThumb.h; }
	int volBarMovement() const { return volW - volNobW; }

private:
	// skinDir/layout.ini を今の値の上に重ねる。無ければ何もしない。
	void ApplyLayout(const std::string &skinDir);

	std::string ref_;
	std::string baseRef_;
	std::vector<std::string> dirs_;
};

// フォントを探す場所の並び。スキンのフォルダ -> 素材のルート。
std::vector<std::string> FontSearchDirs(const Skin &skin, const AssetPaths &paths);

}  // namespace mxv2

#endif  // MXV2_SKIN_H
