// mxv2 - 画面の配色（スキンの colors.ini）
//
// 旧 mxv の SCREENPROPERTY と、<テーマ名>.mxv (INI 形式) の読み込みに相当する。
// 既定値は旧 mxv の Screen_LoadProp() と同じ。中身のセクション名とキーは
// 旧 mxv のまま（ファイル名だけ colors.ini にした。旧名 theme.mxv も読める）。
//
// Bright 系の値は「合成の強さ」で、0..100 が alpha、それ以上は
// AlphaMul（100 未満で暗く、100 より大きいと明るく）として使われる。
//
// [配色設定] のスライダは、**同じ Bright でも 2 通りに分けてある**。
//   ・文字と背景の濃さ (colorBright / backColorBright / back の 2 つ) は
//     alpha なので **0..100**
//   ・素材へ掛けるゲイン (kb の 3 つ / playKey.keyBright) は 100 が素通しで、
//     上へ振ると明るくなるので **0..200**
// ファイルの値そのものはどちらも同じ型なので、手で書けば範囲外も入る。

#ifndef MXV2_COLORS_H
#define MXV2_COLORS_H

#include <string>

#include "bitmap.h"

namespace mxv2 {

struct Colors {
	struct {
		int bitmap;        // 背景ビットマップを使うか
		int bitmapBright;
		Rgb color;
		int colorBright;
	} back;

	struct {
		int blackBright;  // 鍵盤の下地（黒鍵側）
		int whiteBright;  // 鍵盤の下地（白鍵側）
		int bright;       // 押している鍵。ベンド中は半分になる
	} kb;

	struct {
		Rgb color;
		int colorBright;
		Rgb backColor;
		int backColorBright;
	} status;

	struct {
		Rgb color;
		int colorBright;
		Rgb backColor;
		int backColorBright;
	} mdxTitle;

	struct {
		// カーソルは背景に対する**ゲイン**（100 が素通し。既定は青へ寄せる
		// (50,50,200)）。cursorColorBright はそれをどれだけ効かせるかで、
		// 100 なら素の乗算、0 ならカーソルが出ない。
		Rgb cursorColor;
		int cursorColorBright;
		Rgb color;
		int colorBright;
		Rgb folderColor;
		Rgb driveColor;
		// ファイルシステム（選択画面の行と、ルートの "[FS]"）。
		// "[Setting]" は MDX と同じ color を使う。
		Rgb fileSystemColor;
		Rgb backColor;
		int backColorBright;
	} filer;

	struct {
		Rgb color;
		int colorBright;
		int keyBright;
	} playKey;

	Colors();

	// 読む。ファイルが無ければ既定値のまま false を返す。
	bool Load(const std::string &path);

	// 書き出す。
	bool Save(const std::string &path) const;
};

}  // namespace mxv2

#endif  // MXV2_COLORS_H
