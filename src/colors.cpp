// mxv2 - 画面の配色（スキンの colors.ini）

#include "colors.h"

#include "ini.h"

namespace mxv2 {

namespace {

// COLORREF (0x00BBGGRR) を Rgb へ。旧 mxv は RGB() マクロの値を INI に
// そのまま書いているので、この並びで解釈する。
Rgb FromColorRef(int c) {
	return MakeRgb(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
}

int ToColorRef(const Rgb &c) {
	return (int)c.r | ((int)c.g << 8) | ((int)c.b << 16);
}

}  // namespace

Colors::Colors() {
	back.bitmap = 0;
	back.bitmapBright = 50;
	back.color = MakeRgb(0, 0, 255);
	back.colorBright = 50;

	kb.blackBright = 50;
	kb.whiteBright = 150;
	kb.bright = 100;

	status.color = MakeRgb(255, 255, 255);
	status.colorBright = 100;
	status.backColor = MakeRgb(0, 0, 0);
	status.backColorBright = 50;

	mdxTitle.color = MakeRgb(255, 255, 255);
	mdxTitle.colorBright = 100;
	mdxTitle.backColor = MakeRgb(0, 0, 0);
	mdxTitle.backColorBright = 50;

	filer.cursorBright = MakeRgb(50, 50, 200);
	filer.color = MakeRgb(255, 255, 255);
	filer.colorBright = 100;
	filer.folderColor = MakeRgb(128, 255, 128);
	filer.driveColor = MakeRgb(255, 128, 128);
	filer.backColor = MakeRgb(0, 0, 0);
	filer.backColorBright = 50;

	playKey.color = MakeRgb(255, 255, 255);
	playKey.colorBright = 100;
	playKey.keyBright = 150;
}

bool Colors::Load(const std::string &path) {
	Ini ini;
	if (!ini.Load(path)) return false;

	back.bitmap = ini.GetInt("Back", "Bitmap", back.bitmap);
	back.bitmapBright = ini.GetInt("Back", "BitmapBright", back.bitmapBright);
	back.color = FromColorRef(ini.GetInt("Back", "Color", ToColorRef(back.color)));
	back.colorBright = ini.GetInt("Back", "ColorBright", back.colorBright);

	kb.blackBright = ini.GetInt("KB", "BlackBright", kb.blackBright);
	kb.whiteBright = ini.GetInt("KB", "WhiteBright", kb.whiteBright);
	kb.bright = ini.GetInt("KB", "Bright", kb.bright);

	status.color = FromColorRef(ini.GetInt("Status", "Color", ToColorRef(status.color)));
	status.colorBright = ini.GetInt("Status", "ColorBright", status.colorBright);
	status.backColor =
	    FromColorRef(ini.GetInt("Status", "BackColor", ToColorRef(status.backColor)));
	status.backColorBright = ini.GetInt("Status", "BackColorBright", status.backColorBright);

	mdxTitle.color = FromColorRef(ini.GetInt("MDXTitle", "Color", ToColorRef(mdxTitle.color)));
	mdxTitle.colorBright = ini.GetInt("MDXTitle", "ColorBright", mdxTitle.colorBright);
	mdxTitle.backColor =
	    FromColorRef(ini.GetInt("MDXTitle", "BackColor", ToColorRef(mdxTitle.backColor)));
	mdxTitle.backColorBright =
	    ini.GetInt("MDXTitle", "BackColorBright", mdxTitle.backColorBright);

	filer.cursorBright =
	    FromColorRef(ini.GetInt("Filer", "CursorBright", ToColorRef(filer.cursorBright)));
	filer.color = FromColorRef(ini.GetInt("Filer", "Color", ToColorRef(filer.color)));
	filer.colorBright = ini.GetInt("Filer", "ColorBright", filer.colorBright);
	filer.folderColor =
	    FromColorRef(ini.GetInt("Filer", "FolderColor", ToColorRef(filer.folderColor)));
	filer.driveColor =
	    FromColorRef(ini.GetInt("Filer", "DriveColor", ToColorRef(filer.driveColor)));
	filer.backColor = FromColorRef(ini.GetInt("Filer", "BackColor", ToColorRef(filer.backColor)));
	filer.backColorBright = ini.GetInt("Filer", "BackColorBright", filer.backColorBright);

	playKey.color = FromColorRef(ini.GetInt("PlayKey", "Color", ToColorRef(playKey.color)));
	playKey.colorBright = ini.GetInt("PlayKey", "ColorBright", playKey.colorBright);
	playKey.keyBright = ini.GetInt("PlayKey", "KeyBright", playKey.keyBright);

	return true;
}

bool Colors::Save(const std::string &path) const {
	// 既存ファイルを読んでから上書きする。旧 mxv が書いていたキー以外
	// （コメントは落ちるが、知らないキーは残る）。
	Ini ini;
	ini.Load(path);

	ini.SetInt("Back", "Bitmap", back.bitmap);
	ini.SetInt("Back", "BitmapBright", back.bitmapBright);
	ini.SetInt("Back", "Color", ToColorRef(back.color));
	ini.SetInt("Back", "ColorBright", back.colorBright);

	ini.SetInt("KB", "BlackBright", kb.blackBright);
	ini.SetInt("KB", "WhiteBright", kb.whiteBright);
	ini.SetInt("KB", "Bright", kb.bright);

	ini.SetInt("Status", "Color", ToColorRef(status.color));
	ini.SetInt("Status", "ColorBright", status.colorBright);
	ini.SetInt("Status", "BackColor", ToColorRef(status.backColor));
	ini.SetInt("Status", "BackColorBright", status.backColorBright);

	ini.SetInt("MDXTitle", "Color", ToColorRef(mdxTitle.color));
	ini.SetInt("MDXTitle", "ColorBright", mdxTitle.colorBright);
	ini.SetInt("MDXTitle", "BackColor", ToColorRef(mdxTitle.backColor));
	ini.SetInt("MDXTitle", "BackColorBright", mdxTitle.backColorBright);

	ini.SetInt("Filer", "CursorBright", ToColorRef(filer.cursorBright));
	ini.SetInt("Filer", "Color", ToColorRef(filer.color));
	ini.SetInt("Filer", "ColorBright", filer.colorBright);
	ini.SetInt("Filer", "FolderColor", ToColorRef(filer.folderColor));
	ini.SetInt("Filer", "DriveColor", ToColorRef(filer.driveColor));
	ini.SetInt("Filer", "BackColor", ToColorRef(filer.backColor));
	ini.SetInt("Filer", "BackColorBright", filer.backColorBright);

	ini.SetInt("PlayKey", "Color", ToColorRef(playKey.color));
	ini.SetInt("PlayKey", "ColorBright", playKey.colorBright);
	ini.SetInt("PlayKey", "KeyBright", playKey.keyBright);

	return ini.Save(path);
}

}  // namespace mxv2
