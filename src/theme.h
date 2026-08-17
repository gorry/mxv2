// mxv2 - テーマ（画面配色）
//
// 旧 mxv の SCREENPROPERTY と、<テーマ名>.mxv (INI 形式) の読み込みに相当する。
// 既定値は旧 mxv の Screen_LoadProp() と同じ。
//
// Bright 系の値は「合成の強さ」で、0..100 が alpha、それ以上は
// AlphaMul（100 未満で暗く、100 より大きいと明るく）として使われる。

#ifndef MXV2_THEME_H
#define MXV2_THEME_H

#include <string>

#include "bitmap.h"

namespace mxv2 {

struct Theme {
	struct {
		int bitmap;        // 背景ビットマップを使うか
		int bitmapBright;
		Rgb color;
		int colorBright;
	} back;

	struct {
		int blackBright;
		int whiteBright;
		int bright;
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
		Rgb cursorBright;
		Rgb color;
		int colorBright;
		Rgb folderColor;
		Rgb driveColor;
		Rgb backColor;
		int backColorBright;
	} filer;

	struct {
		Rgb color;
		int colorBright;
		int keyBright;
	} playKey;

	Theme();

	// <テーマ名>.mxv を読む。ファイルが無ければ既定値のまま false を返す。
	bool Load(const std::string &path);

	// <テーマ名>.mxv へ書き出す。
	bool Save(const std::string &path) const;
};

}  // namespace mxv2

#endif  // MXV2_THEME_H
