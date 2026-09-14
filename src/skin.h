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

// ファイラーを画面のどの辺に置くか。
//
// 画面はここで 2 つに分かれる。「ファイラー側」と「ファイラー以外側」で、
// キャンバスが伸び縮みしてもファイラー以外側の厚みは宣言サイズのまま、
// 増減はすべてファイラー側が吸収する（fullscreen.md）。
// 上下に置けば縦に、左右に置けば横に伸びる。
enum FilerSide {
	kFilerSideBottom = 0,
	kFilerSideTop,
	kFilerSideLeft,
	kFilerSideRight,
	kNumFilerSides
};

// layout.ini での名前 ("Bottom" など) との変換。綴りは大文字小文字を問わない。
const char *FilerSideName(int side);
int FilerSideFromName(const std::string &name, int fallback);

// 伸縮する向きが縦か（上下配置）。
inline bool FilerSideVertical(int side) {
	return side == kFilerSideBottom || side == kFilerSideTop;
}

// ステータス欄に並ぶ項目。1 段（1 チャンネル）の中でどこに出すかを
// layout.ini の [Status] Pos<名前> で指定する（左上からの相対で、
// [Status] Pos と [Keyboard] ChannelY が足される）。
// FM の 16 項目は 8 段ぶんそのまま繰り返され、PCM の 2 項目は
// [Status] PcmX / PcmY の 8 スロットぶん繰り返される。
enum StatusItem {
	kStatusVolume = 0,
	kStatusLevelMeter,
	kStatusPanpot,
	kStatusDetune,
	kStatusVoice,
	kStatusQ,
	kStatusPtr,
	kStatusLFOPitch,
	kStatusLFOPitch1,
	kStatusLFOPitch2,
	kStatusLFOPitch3,
	kStatusLFOPitch4,
	kStatusLFOVolume,
	kStatusLFOVolume1,
	kStatusLFOVolume2,
	kStatusLFOVolume3,
	kStatusPcmVolume,
	kStatusPcmPtr,
	// ---- 音色データ表示（ステータス欄の長押しで切り替える。tonedata.md） ----
	// FM の 13 項目は 1 段（1 チャンネル）の中の位置。オペレータごとの項目
	// （AR〜AMSEnable）は、さらに [Status] OPMOperatorY のスロットぶんが足される。
	kStatusOPMAlgorithm,
	kStatusOPMFeedback,
	kStatusOPMAttackRate,
	kStatusOPMDecayRate,
	kStatusOPMSustainRate,
	kStatusOPMReleaseRate,
	kStatusOPMSustainLevel,
	kStatusOPMTotalLevel,
	kStatusOPMKeyScaling,
	kStatusOPMMultiple,
	kStatusOPMDetune1,
	kStatusOPMDetune2,
	kStatusOPMAMSEnable,
	// PCM の段に出す OPM 全体の値（Rect の左上 + [Keyboard] ChannelY[8] からの相対）。
	kStatusOPMNoise,
	kStatusOPMClockB,
	kStatusOPMLFOFreq,
	kStatusOPMLFOPMD,
	kStatusOPMLFOAMD,
	kStatusOPMLFOWave,
	kNumStatusItems
};

// 音色データ表示の項目の分類。
inline bool IsStatusOpmOperatorItem(StatusItem item) {
	return item >= kStatusOPMAttackRate && item <= kStatusOPMAMSEnable;
}
inline bool IsStatusOpmGlobalItem(StatusItem item) {
	return item >= kStatusOPMNoise && item <= kStatusOPMLFOWave;
}

// layout.ini でのキー名（"PosVolume" など）。並びは StatusItem と同じ。
extern const char *const kStatusItemKeys[kNumStatusItems];

// 配色ファイルの名前。旧 mxv の <テーマ名>.mxv をそのまま持ってきていたのを
// 2026-08-21 に layout.ini と揃えた。**書くのは新しい名前だけ**で、
// 旧い名前は読むためだけに残してある。
extern const char kColorsFile[];        // "colors.ini"
extern const char kLegacyColorsFile[];  // "theme.mxv"

struct Skin {
	// ---- 画面 --------------------------------------------------------
	// 「宣言サイズ」。キャンバスが伸びる前の大きさで、部品の座標も配色も
	// この大きさを前提に書く。実際のキャンバスはこれ以上に伸びうる
	// （PlacedFor / CanvasSizeFor と fullscreen.md）。
	int screenW, screenH;

	// ---- 画面の 2 分割 ------------------------------------------------
	// ファイラーを置く辺と、その辺からファイラー側の矩形が何ピクセル
	// あるか（宣言サイズでの値）。キャンバスが伸びたぶんはすべて
	// ファイラー側が受け取り、ファイラー以外側の厚みは変わらない。
	int filerSide;    // FilerSide
	int filerExtent;  // ファイラー側の矩形の厚み (px)

	// ---- 鍵盤 --------------------------------------------------------
	int kbX, kbY;
	int kbXOffset[13];  // 1 オクターブ分の鍵の x。[12] は 1 オクターブの幅
	int kbYOffset;
	int chYOffset[9];  // 各チャンネル行の y
	int keyOffset;     // 鍵の描画原点の補正

	// ---- ミニフォント（ステータス欄などのビットマップ文字） --------------
	// 画面に置くときの送り幅と行の高さ（字間・行間を含む）。
	// 素材の中の 1 文字の大きさは素材の大きさから決まるので、ここには無い
	// （drawscreen.cpp の kMiniFont* を見ること）。
	int miniFontW, miniFontH;

	// ---- ステータス ---------------------------------------------------
	// layout.ini では [Status] Rect に "x,y,w,h" でまとめて書く。x,y は
	// 9 段全体の左上で、**w,h は 1 段ぶんの背景の大きさ**（段は chYOffset
	// ごとに下へ並ぶ）。2026-09-04 に Pos / BackWidth / BackHeight を
	// 1 つにまとめた。
	int statusX, statusY, statusW, statusH;
	int pcmXOffset[8], pcmYOffset[8];  // PCM 8ch のステータス欄の並び
	// 項目ごとの位置。StatusItem の並びで [x, y]。旧 mxv は draw.cpp の
	// 関数ごとに #define CX_D / CY_D で埋め込んでいたもの。
	int statusPos[kNumStatusItems][2];
	// 音色データ表示で、オペレータごとの項目を置く段の y（[Status]
	// OPMOperatorY）。並びは OPM のスロット順（M1, M2, C1, C2）。既定の
	// 0,18,9,27 で上から M1 / C1 / M2 / C2（MML の OP1〜OP4 の順）に並ぶ。
	int opmOperatorY[4];

	// ---- レベルメータ -------------------------------------------------
	int levelMeterPalOfs;      // levelmeter.bmp のパレット開始番号
	int levelMeterWidthCells;  // セル数
	// 素材の左端を何画素捨てるか。levelmeter.bmp は 1 段ぶんの幅で
	// 作ってあり、音量表示と重なる左端を切ってから描く。描く幅は
	// 「素材の幅 - この値」。
	int levelMeterSrcX;

	// ---- バナー -------------------------------------------------------
	int bannerX, bannerY, bannerW, bannerH;

	// ---- 曲名 ---------------------------------------------------------
	int titleX, titleY, titleW, titleH;
	// 横スクロールの速さ (%)。1..1000。100 で「文字の高さ × 40/24 px/秒」
	// （drawscreen.cpp の kScrollBaseHeightsPerSec）。曲名欄とファイラーで
	// 計算式は同じで、それぞれの文字の高さ（titleH / ItemHeight）に掛かる。
	int titleScrollSpeed;

	// ---- ファイラー -----------------------------------------------
	// layout.ini に書くのは「ファイラー側の矩形からの内側マージン」だけ
	// （[FileList] Margin に "左,上,右,下"）。矩形はキャンバスの大きさで
	// 変わるので、絶対座標では書けない。
	int fileListMargin[4];
	// [0] = 小さい文字 / [1] = 大きい文字。layout.ini では「小,大」と書き、
	// 1 つだけ書けば両方に効く。
	int fileListItemH[2];  // 1 行の高さ
	// 曲名の横スクロールの速さ (%)。小・大の別は無く 1 つ。式は曲名欄と同じ。
	int fileListScrollSpeed;
	// 行の左端からのピクセル位置と、描画に使う幅（ピクセル）。
	// 旧 mxv は等幅フォント前提で「文字数」だったが、プロポーショナル
	// フォントでは意味を持たないのでピクセルに変えてある。
	int fileListBaseNameX[2], fileListBaseNameW[2];
	int fileListTitleX[2];

	// ---- ファイラー（導出値） -------------------------------------------
	// **layout.ini からは読まない。** PlacedFor() が今のキャンバスの
	// 大きさから求めて埋める。描画と当たり判定はこちらを見ること。
	int fileListX, fileListY, fileListW, fileListH;
	int fileListRows[2];    // = fileListH / fileListItemH（切り捨て）
	int fileListTitleW[2];  // = fileListW - fileListTitleX

	// ---- スクロールバー -----------------------------------------------
	// スクロールバーはファイラーの矩形の右端を分け合う。layout.ini に書くのは
	// その幅だけで、位置はファイラーの矩形から決まる。
	//   Width    描画に使う幅
	//   HitWidth 当たり判定の幅（描画幅以上。左へ広げると、細いバーでも
	//            指で掴めるようになる。Phone がそうしている）
	int scrollWidth;
	int scrollHitWidth;
	// 素材の中の位置と大きさ。矢印は上端・下端に貼り付き、溝 (SrcBar) は
	// 残りの高さぶん上から繰り返して敷かれる。押下中の矢印は通常の矢印と
	// 同じ場所へ描く。
	Xywh scrollSrcThumb;
	Xywh scrollSrcUpArrowPress;
	Xywh scrollSrcDownArrowPress;
	Xywh scrollSrcUpArrow;
	Xywh scrollSrcBar;
	Xywh scrollSrcDownArrow;

	// ---- スクロールバー（導出値） ---------------------------------------
	// **layout.ini からは読まない。** PlacedFor() が埋める。
	int scrollX, scrollY, scrollW, scrollH;  // 描画の矩形
	int scrollHitX, scrollHitW;              // 当たり判定の矩形（y は描画と同じ）
	int scrollGrooveH;                       // 溝を描く高さ
	int scrollPosUpArrow[2];
	int scrollPosBar[2];
	int scrollPosDownArrow[2];

	// ---- プログレスバー -----------------------------------------------
	// 時刻表示は Rect の左上からの相対位置。layout.ini では [ProgressBar]
	// TimePos に "x,y" で書く（2026-09-04 に TimeX / TimeY をまとめた）。
	int progX, progY, progW, progH;
	int progTimePos[2];
	// 素材の中の位置と大きさ。音量バーと同じ分け方（つまみが無いだけ）で、
	// 左端 / 中央 / 右端 の 3 つ（2026-09-12。それまでは Rect と同じ幅の
	// 絵をそのまま貼っていた）。左端は Rect の左端、右端は Rect の右端に
	// 貼り付き、中央 (SrcBar) は残りの幅ぶん**左から繰り返して**敷かれる。
	// **矩形は上段（未再生）の位置**で、下段（再生済み）は同じ矩形を
	// その高さぶん下へずらした位置にある。再生位置より左は下段から、
	// 右は上段から取る。同梱素材の並びは左から 左端 / 右端 / 中央（残り全部）
	// で、左端と右端は同じ大きさ。
	Xywh progSrcBarLeft;
	Xywh progSrcBarRight;
	Xywh progSrcBar;

	// ---- 音量バー -----------------------------------------------------
	// VolumePos は音量値の表示位置（Rect の左上からの相対）。プログレスバーの
	// TimePos と同じ形。2026-09-12 に TimePos から改名した（原典由来の名前で、
	// 出るのは時刻ではなく音量値だったため）。
	int volX, volY, volW, volH;
	int volVolumePos[2];
	// 素材の中の位置と大きさ。スクロールバーと同じ分け方で、バーは
	// 左端 / 中央 / 右端 の 3 つに分かれる（2026-09-12。それまでは
	// つまみとスライド 1 枚だった）。左端は Rect の左端、右端は Rect の
	// 右端に貼り付き、中央 (SrcBar) は残りの幅ぶん**左から繰り返して**
	// 敷かれる（素材より長ければ繰り返し、短ければ途中で切る）。つまみは
	// 音量に応じて Rect の中を左右に動く（動ける幅は volBarMovement）。
	// 同梱素材の並びは左から つまみ / 左端 / 右端 / 中央（残り全部）で、
	// つまみ・左端・右端は同じ大きさ。
	Xywh volSrcThumb;
	Xywh volSrcBarLeft;
	Xywh volSrcBarRight;
	Xywh volSrcBar;

	// ---- 操作ボタン -----------------------------------------------------
	// layout.ini では [PlayKey] Rect に "x,y,w,h" で書く（2026-09-04 に Pos
	// から変えた）。x,y は Pos<n> の原点。**w,h は使うボタン全体を覆う
	// 大きさ**で、描画には使わない（ボタンは 1 つずつ Src<n>/Pos<n> で
	// 描く）。スキンエディタが「操作ボタンのまとまり」を掴む範囲に使う。
	int playKeyX, playKeyY, playKeyW, playKeyH;
	int numPlayKeys;         // 使うボタンの数（SHUFFLE を含めるなら 9）
	Xywh playKeyRect[9];     // playkey.bmp 内の位置と大きさ
	int playKeyPos[9][2];    // 画面上の配置（playKeyX/Y からの相対）

	// playkey.bmp のパレット番号。palRed 以降は LED の点灯色に使う色玉で、
	// 黄 (Yellow) と青 (Blue) は 2026-09-04 に足した（素材のパレットには
	// 色が入っているが、今どの LED にも割り当てていないので描画では使わない）。
	int palPlayKeyKey, palPlayLed, palPauseLed, palContLed, palRepeatLed;
	int palDark, palRed, palGreen, palYellow, palBlue;

	// ---- 素材のファイル名 ---------------------------------------------
	// layout.ini では表示部品ごとのセクションに Img<名前> のキーで書く
	// （背景なら [Screen] ImgBack、バナーなら [Banner] ImgBanner）。
	// スキンのフォルダに無ければ Base のフォルダから読む (FindFile)。
	// 差し替えたいときはファイル名ごと変えられる。
	std::string backBitmap;         // 背景
	std::string kb0Bitmap;          // 鍵盤の下地
	std::string kb1Bitmap;          // 白鍵側の鍵
	std::string kb2Bitmap;          // 黒鍵側の鍵
	std::string miniFontBitmap;     // ミニフォント（ビットマップ文字）
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

	// ---- キャンバスの大きさに合わせる ---------------------------------
	// 出力 (outW x outH) に収まるキャンバスの大きさを求める。伸びるのは
	// ファイラーを置いた向きだけで、もう一方は宣言サイズのまま
	// （残りはレターボックスになる）。
	// stretchLimit は伸ばす方向の上限 (px)。0 なら上限なし（それでも
	// kMaxCanvasStretch 倍で頭打ちにする）。
	void CanvasSizeFor(int outW, int outH, int stretchLimit, int *cw, int *ch) const;

	// 伸ばす方向の上限をどれだけ大きくしても、宣言サイズのこの倍率で
	// 打ち切る。背景を使わないスキンで窓を極端に細長くしたときに、
	// 途方もない大きさのバッファを作らないための保険。
	static const int kMaxCanvasStretch = 8;

	// キャンバスの大きさ (canvasW x canvasH) に合わせて導出値を埋めた
	// コピーを返す。ファイラー以外側の部品の座標には、その矩形の原点が
	// 足される（ファイラーを上や左に置いたときにずれるぶん）。
	// **描画と当たり判定はこのコピーを見ること。**
	Skin PlacedFor(int canvasW, int canvasH) const;

	// PlacedFor() が埋める。ファイラー以外側の矩形の原点と、そのときの
	// キャンバスの大きさ。背景ビットマップを貼る位置を決めるのに使う。
	int placedCanvasW, placedCanvasH;
	int placedOtherX, placedOtherY;

	// ---- 導出値 -------------------------------------------------------
	int fileListMaxItemH() const {
		return (fileListItemH[0] > fileListItemH[1]) ? fileListItemH[0] : fileListItemH[1];
	}
	// つまみが動ける幅。旧 mxv の MX_CH_SCROLLBARMOVEMENT / MX_CW_TOTALVOLBARMOVEMENT。
	// つまみは溝の中を動くので、**描く溝**の高さからつまみの高さを引いたもの
	// （素材の溝の高さではない。溝は繰り返して敷くので伸び縮みする）。
	int scrollBarMovement() const { return scrollGrooveH - scrollSrcThumb.h; }
	int volBarMovement() const { return volW - volSrcThumb.w; }

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
