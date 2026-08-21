// mxv2 - スキン（画面レイアウトと素材の置き場所）

#include "skin.h"

#include <cstdio>
#include <cstdlib>

#include "fileutil.h"
#include "ini.h"

namespace mxv2 {

namespace {

// "0,3,6,9" のような並びを読む。足りない分は既定値のまま残す。
// 戻り値は実際に読めた個数。
int GetIntList(const Ini &ini, const char *section, const char *key, int *out, int count) {
	const std::string s = ini.GetString(section, key, std::string());
	if (s.empty()) return 0;

	int n = 0;
	size_t pos = 0;
	while (pos <= s.size() && n < count) {
		const size_t comma = s.find(',', pos);
		const std::string item =
		    s.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
		if (!item.empty()) out[n++] = atoi(item.c_str());
		if (comma == std::string::npos) break;
		pos = comma + 1;
	}
	return n;
}

// ファイルリストの「小さい文字, 大きい文字」の 2 つ組を読む。
// 1 つしか書かれていなければ、その値が両方に効く。
void GetFontSizePair(const Ini &ini, const char *section, const char *key, int *out) {
	int v[2] = { out[0], out[1] };
	const int n = GetIntList(ini, section, key, v, 2);
	if (n == 0) return;
	out[0] = v[0];
	out[1] = (n >= 2) ? v[1] : v[0];
}

// "x,y,w,h" を読む。
void GetXywh(const Ini &ini, const char *section, const char *key, Xywh *out) {
	int v[4] = { out->x, out->y, out->w, out->h };
	GetIntList(ini, section, key, v, 4);
	out->x = v[0];
	out->y = v[1];
	out->w = v[2];
	out->h = v[3];
}

// "x,y" を読む。メンバが隣接している保証は無いので、値渡しで受ける。
void GetXy(const Ini &ini, const char *section, const char *key, int *x, int *y) {
	int v[2] = { *x, *y };
	GetIntList(ini, section, key, v, 2);
	*x = v[0];
	*y = v[1];
}

std::string IndexedKey(const char *prefix, int i) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%s%d", prefix, i);
	return buf;
}

// 土台を辿る深さの上限（自分を含む）。循環は名前で弾くので、これは
// 「異様に長い連鎖で時間を食わない」ための保険。
const int kMaxSkinDepth = 8;

// skinDir/layout.ini の [Skin] Base を読む。
std::string ReadBaseRef(const std::string &skinDir) {
	Ini ini;
	if (!ini.Load(JoinPath(skinDir, "layout.ini"))) return std::string();
	return ini.GetString("Skin", "Base", std::string());
}

}  // namespace

Skin::Skin() {
	// 既定値は旧 mxv/draw.cpp と同じ 640x480 のレイアウト。
	screenW = 640;
	screenH = 480;

	kbX = 4;
	kbY = 4;
	{
		static const int kDefault[13] = { 0, 3, 6, 9, 12, 18, 21, 24, 27, 30, 33, 36, 42 };
		for (int i = 0; i < 13; i++) kbXOffset[i] = kDefault[i];
	}
	kbYOffset = 0;
	{
		static const int kDefault[9] = { 0, 38, 76, 114, 152, 190, 228, 266, 304 };
		for (int i = 0; i < 9; i++) chYOffset[i] = kDefault[i];
	}
	keyOffset = 3;

	fontW = 6;
	fontH = 8;

	statusX = 344;
	statusY = 4;
	statusBackW = 128;
	statusBackH = 35;
	{
		static const int kX[8] = { 0, 0, 0, 0, 68, 68, 68, 68 };
		static const int kY[8] = { 0, 9, 18, 27, 0, 9, 18, 27 };
		for (int i = 0; i < 8; i++) {
			pcmXOffset[i] = kX[i];
			pcmYOffset[i] = kY[i];
		}
	}

	levelMeterPalOfs = 32;
	levelMeterWidthCells = 64;

	bannerX = 476;
	bannerY = 4;
	bannerW = 160;
	bannerH = 54;

	titleX = 4;
	titleY = 348;
	titleW = 632;
	titleH = 14;

	fileListX = 4;
	fileListY = 366;
	fileListW = 620;
	fileListH = 110;
	fileListRows[0] = 11;
	fileListRows[1] = 8;
	fileListItemH[0] = 10;
	fileListItemH[1] = 13;
	// 旧 mxv の「文字数 × ItemHeight/2」をピクセルに直した値。
	fileListBaseNameX[0] = 5;
	fileListBaseNameX[1] = 6;
	fileListBaseNameW[0] = 120;
	fileListBaseNameW[1] = 156;
	fileListTitleX[0] = 125;
	fileListTitleX[1] = 162;
	fileListTitleW[0] = 480;
	fileListTitleW[1] = 624;

	scrollX = 624;
	scrollY = 366;
	scrollW = 12;
	scrollH = 110;
	// scrollbar.bmp (12x146) は上から つまみ / 上矢印(押下) / 下矢印(押下) /
	// 上矢印 / 溝 / 下矢印 の順に並んでいる。
	{
		const Xywh thumb = { 0, 0, 12, 12 };
		const Xywh upPress = { 0, 12, 12, 12 };
		const Xywh downPress = { 0, 24, 12, 12 };
		const Xywh up = { 0, 36, 12, 12 };
		const Xywh bar = { 0, 48, 12, 86 };
		const Xywh down = { 0, 134, 12, 12 };
		scrollSrcThumb = thumb;
		scrollSrcUpArrowPress = upPress;
		scrollSrcDownArrowPress = downPress;
		scrollSrcUpArrow = up;
		scrollSrcBar = bar;
		scrollSrcDownArrow = down;
	}
	scrollPosUpArrow[0] = 0;
	scrollPosUpArrow[1] = 0;
	scrollPosBar[0] = 0;
	scrollPosBar[1] = 12;
	scrollPosDownArrow[0] = 0;
	scrollPosDownArrow[1] = 98;

	progX = 476;
	progY = 270;
	progW = 160;
	progH = 6;
	progTimeXOfs = 16;
	progTimeYOfs = 8;

	volX = 476;
	volY = 300;
	volW = 64;
	volH = 16;
	volTimeXOfs = 64;
	volTimeYOfs = 6;
	volNobW = 8;
	{
		const Xywh nob = { 0, 0, 8, 16 };
		const Xywh slide = { 8, 0, 64, 16 };
		volRect[0] = nob;
		volRect[1] = slide;
	}

	playKeyX = 476;
	playKeyY = 300;
	numPlayKeys = 8;  // SHUFFLE は原典同様に対象外
	{
		static const Xywh kRect[9] = {
			{ 0, 0, 24, 24 },    // PREV
			{ 24, 0, 24, 24 },   // STOP
			{ 48, 0, 33, 24 },   // PLAY
			{ 81, 0, 15, 24 },   // FASTPLAY
			{ 96, 0, 24, 24 },   // PAUSE
			{ 120, 0, 24, 24 },  // NEXT
			{ 144, 0, 32, 13 },  // CONT
			{ 176, 0, 32, 13 },  // REPEAT
			{ 208, 0, 40, 13 },  // SHUFFLE
		};
		static const int kPos[9][2] = {
			{ 0, 20 }, { 28, 20 }, { 56, 20 }, { 89, 20 }, { 108, 20 },
			{ 136, 20 }, { 93, 0 }, { 128, 0 }, { 76, 0 },
		};
		for (int i = 0; i < 9; i++) {
			playKeyRect[i] = kRect[i];
			playKeyPos[i][0] = kPos[i][0];
			playKeyPos[i][1] = kPos[i][1];
		}
	}

	palPlayKeyKey = 6;
	palPlayLed = 7;
	palPauseLed = 8;
	palContLed = 9;
	palRepeatLed = 10;
	palDark = 2;
	palRed = 16;
	palGreen = 17;

	backBitmap = "back.bmp";
	kb0Bitmap = "kb0.bmp";
	kb1Bitmap = "kb1.bmp";
	kb2Bitmap = "kb2.bmp";
	font5x7Bitmap = "font5x7.bmp";
	levelMeterBitmap = "levelmeter.bmp";
	bannerBitmap = "banner.bmp";
	playKeyBitmap = "playkey.bmp";
	progressBarBitmap = "progressbar.bmp";
	volBarBitmap = "volbar.bmp";
	scrollBarBitmap = "scrollbar.bmp";
}

bool Skin::Load(const AssetPaths &paths, const std::string &ref, std::string *err) {
	*this = Skin();  // 既定値から組み立て直す
	ref_ = ref;

	// 自分 -> 土台 -> その土台 … と辿って、探索先を並べる。
	// 同じ指定へ戻ってきたら循環なので打ち切る。
	std::vector<std::string> visited;
	std::string current = ref;
	for (int depth = 0; depth < kMaxSkinDepth; depth++) {
		const std::string dir = paths.SkinDir(current);
		if (dir.empty()) {
			if (depth == 0) {
				*err = "スキン " + ref + " が見つかりません。";
				return false;
			}
			break;  // 土台が無いだけなら、そこまでで組み立てる
		}
		// 書き方違いで同じフォルダに戻ってくることがある
		// （ユーザー側に無い "X" と "assets:X" は同じものを指す）。
		bool already = false;
		for (size_t i = 0; i < dirs_.size() && !already; i++) already = (dirs_[i] == dir);
		if (already) break;
		dirs_.push_back(dir);
		visited.push_back(current);

		const std::string base = ReadBaseRef(dir);
		if (base.empty()) break;
		bool loop = false;
		for (size_t i = 0; i < visited.size() && !loop; i++) {
			loop = (CompareNoCase(visited[i], base) == 0);
		}
		if (loop) break;
		current = base;
	}

	// 優先度の低い方から重ねる（後から読んだ値が勝つ）。
	// dirs_ は「自分, 土台, その土台, …」の順なので、逆から回せば
	// 一番遠い土台 -> … -> 自分 になる。
	for (size_t i = dirs_.size(); i-- > 0;) ApplyLayout(dirs_[i]);
	return true;
}

void Skin::ApplyLayout(const std::string &skinDir) {
	Ini ini;
	if (!ini.Load(JoinPath(skinDir, "layout.ini"))) {
		// layout.ini が無いスキンは、既定レイアウトのまま使う。
		return;
	}

	screenW = ini.GetInt("Screen", "Width", screenW);
	screenH = ini.GetInt("Screen", "Height", screenH);
	backBitmap = ini.GetString("Screen", "ImgBack", backBitmap);

	GetXy(ini, "Keyboard", "Pos", &kbX, &kbY);
	GetIntList(ini, "Keyboard", "XOffset", kbXOffset, 13);
	kbYOffset = ini.GetInt("Keyboard", "YOffset", kbYOffset);
	GetIntList(ini, "Keyboard", "ChannelY", chYOffset, 9);
	keyOffset = ini.GetInt("Keyboard", "KeyOffset", keyOffset);
	kb0Bitmap = ini.GetString("Keyboard", "ImgKeyboard0", kb0Bitmap);
	kb1Bitmap = ini.GetString("Keyboard", "ImgKeyboard1", kb1Bitmap);
	kb2Bitmap = ini.GetString("Keyboard", "ImgKeyboard2", kb2Bitmap);

	fontW = ini.GetInt("Font5x7", "Width", fontW);
	fontH = ini.GetInt("Font5x7", "Height", fontH);
	font5x7Bitmap = ini.GetString("Font5x7", "ImgFont5x7", font5x7Bitmap);

	GetXy(ini, "Status", "Pos", &statusX, &statusY);
	statusBackW = ini.GetInt("Status", "BackWidth", statusBackW);
	statusBackH = ini.GetInt("Status", "BackHeight", statusBackH);
	GetIntList(ini, "Status", "PcmX", pcmXOffset, 8);
	GetIntList(ini, "Status", "PcmY", pcmYOffset, 8);

	levelMeterPalOfs = ini.GetInt("LevelMeter", "PaletteOffset", levelMeterPalOfs);
	levelMeterWidthCells = ini.GetInt("LevelMeter", "Cells", levelMeterWidthCells);
	levelMeterBitmap = ini.GetString("LevelMeter", "ImgLevelMeter", levelMeterBitmap);

	{
		Xywh r = { bannerX, bannerY, bannerW, bannerH };
		GetXywh(ini, "Banner", "Rect", &r);
		bannerX = r.x;
		bannerY = r.y;
		bannerW = r.w;
		bannerH = r.h;
	}
	bannerBitmap = ini.GetString("Banner", "ImgBanner", bannerBitmap);
	{
		Xywh r = { titleX, titleY, titleW, titleH };
		GetXywh(ini, "Title", "Rect", &r);
		titleX = r.x;
		titleY = r.y;
		titleW = r.w;
		titleH = r.h;
	}
	{
		Xywh r = { fileListX, fileListY, fileListW, fileListH };
		GetXywh(ini, "FileList", "Rect", &r);
		fileListX = r.x;
		fileListY = r.y;
		fileListW = r.w;
		fileListH = r.h;
	}
	// どれも「小さい文字, 大きい文字」の 2 つ組。1 つだけなら両方に効く。
	GetFontSizePair(ini, "FileList", "Rows", fileListRows);
	GetFontSizePair(ini, "FileList", "ItemHeight", fileListItemH);
	GetFontSizePair(ini, "FileList", "BaseNameX", fileListBaseNameX);
	GetFontSizePair(ini, "FileList", "BaseNameWidth", fileListBaseNameW);
	GetFontSizePair(ini, "FileList", "TitleX", fileListTitleX);
	GetFontSizePair(ini, "FileList", "TitleWidth", fileListTitleW);

	{
		Xywh r = { scrollX, scrollY, scrollW, scrollH };
		GetXywh(ini, "ScrollBar", "Rect", &r);
		scrollX = r.x;
		scrollY = r.y;
		scrollW = r.w;
		scrollH = r.h;
	}
	GetXywh(ini, "ScrollBar", "SrcThumb", &scrollSrcThumb);
	GetXywh(ini, "ScrollBar", "SrcUpArrowPress", &scrollSrcUpArrowPress);
	GetXywh(ini, "ScrollBar", "SrcDownArrowPress", &scrollSrcDownArrowPress);
	GetXywh(ini, "ScrollBar", "SrcUpArrow", &scrollSrcUpArrow);
	GetXywh(ini, "ScrollBar", "SrcBar", &scrollSrcBar);
	GetXywh(ini, "ScrollBar", "SrcDownArrow", &scrollSrcDownArrow);
	GetIntList(ini, "ScrollBar", "PosUpArrow", scrollPosUpArrow, 2);
	GetIntList(ini, "ScrollBar", "PosBar", scrollPosBar, 2);
	GetIntList(ini, "ScrollBar", "PosDownArrow", scrollPosDownArrow, 2);
	scrollBarBitmap = ini.GetString("ScrollBar", "ImgScrollBar", scrollBarBitmap);

	{
		Xywh r = { progX, progY, progW, progH };
		GetXywh(ini, "ProgressBar", "Rect", &r);
		progX = r.x;
		progY = r.y;
		progW = r.w;
		progH = r.h;
	}
	progTimeXOfs = ini.GetInt("ProgressBar", "TimeX", progTimeXOfs);
	progTimeYOfs = ini.GetInt("ProgressBar", "TimeY", progTimeYOfs);
	progressBarBitmap = ini.GetString("ProgressBar", "ImgProgressBar", progressBarBitmap);

	{
		Xywh r = { volX, volY, volW, volH };
		GetXywh(ini, "VolumeBar", "Rect", &r);
		volX = r.x;
		volY = r.y;
		volW = r.w;
		volH = r.h;
	}
	volTimeXOfs = ini.GetInt("VolumeBar", "TimeX", volTimeXOfs);
	volTimeYOfs = ini.GetInt("VolumeBar", "TimeY", volTimeYOfs);
	volNobW = ini.GetInt("VolumeBar", "NobWidth", volNobW);
	GetXywh(ini, "VolumeBar", "NobSrc", &volRect[0]);
	GetXywh(ini, "VolumeBar", "SlideSrc", &volRect[1]);
	volBarBitmap = ini.GetString("VolumeBar", "ImgVolumeBar", volBarBitmap);

	GetXy(ini, "PlayKey", "Pos", &playKeyX, &playKeyY);
	numPlayKeys = ini.GetInt("PlayKey", "Count", numPlayKeys);
	if (numPlayKeys < 0) numPlayKeys = 0;
	if (numPlayKeys > 9) numPlayKeys = 9;
	for (int i = 0; i < 9; i++) {
		GetXywh(ini, "PlayKey", IndexedKey("Src", i).c_str(), &playKeyRect[i]);
		GetIntList(ini, "PlayKey", IndexedKey("Pos", i).c_str(), playKeyPos[i], 2);
	}
	palPlayKeyKey = ini.GetInt("PlayKey", "PalKey", palPlayKeyKey);
	palPlayLed = ini.GetInt("PlayKey", "PalPlayLed", palPlayLed);
	palPauseLed = ini.GetInt("PlayKey", "PalPauseLed", palPauseLed);
	palContLed = ini.GetInt("PlayKey", "PalContLed", palContLed);
	palRepeatLed = ini.GetInt("PlayKey", "PalRepeatLed", palRepeatLed);
	palDark = ini.GetInt("PlayKey", "PalDark", palDark);
	palRed = ini.GetInt("PlayKey", "PalRed", palRed);
	palGreen = ini.GetInt("PlayKey", "PalGreen", palGreen);
	playKeyBitmap = ini.GetString("PlayKey", "ImgPlayKey", playKeyBitmap);
}

std::string Skin::FindFile(const std::string &name) const {
	for (size_t i = 0; i < dirs_.size(); i++) {
		const std::string path = JoinPath(dirs_[i], name);
		if (FileExists(path)) return path;
	}
	// 見つからなかったときは、エラー文言に出せるパスを返す。
	return dirs_.empty() ? name : JoinPath(dirs_[0], name);
}

std::vector<std::string> FontSearchDirs(const Skin &skin, const AssetPaths &paths) {
	std::vector<std::string> dirs = skin.dirs();
	const std::vector<std::string> roots = paths.Roots();
	dirs.insert(dirs.end(), roots.begin(), roots.end());
	return dirs;
}

}  // namespace mxv2
