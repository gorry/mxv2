// mxv2 - スキン（画面レイアウトと素材の置き場所）

#include "skin.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "fileutil.h"
#include "ini.h"
#include "message.h"

namespace mxv2 {

const char kColorsFile[] = "colors.ini";
const char kLegacyColorsFile[] = "theme.mxv";

// FilerSide の並び。
static const char *const kFilerSideNames[kNumFilerSides] = {
	"Bottom", "Top", "Left", "Right",
};

const char *FilerSideName(int side) {
	if (side < 0 || side >= kNumFilerSides) side = kFilerSideBottom;
	return kFilerSideNames[side];
}

int FilerSideFromName(const std::string &name, int fallback) {
	if (name.empty()) return fallback;
	for (int i = 0; i < kNumFilerSides; i++) {
		if (CompareNoCase(name, kFilerSideNames[i]) == 0) return i;
	}
	return fallback;
}

// StatusItem の並び。
const char *const kStatusItemKeys[kNumStatusItems] = {
	"PosVolume",     "PosLevelMeter", "PosPanpot",    "PosDetune",
	"PosVoice",      "PosQ",          "PosPtr",       "PosLFOPitch",
	"PosLFOPitch1",  "PosLFOPitch2",  "PosLFOPitch3", "PosLFOPitch4",
	"PosLFOVolume",  "PosLFOVolume1", "PosLFOVolume2", "PosLFOVolume3",
	"PosPcmVolume",  "PosPcmPtr",
	// 音色データ表示（tonedata.md の綴りのまま。Algorythm は仕様書どおり）。
	"PosOPMAlgorythm",   "PosOPMFeedback",     "PosOPMAttackRate",  "PosOPMDecayRate",
	"PosOPMSustainRate", "PosOPMReleaseRate",  "PosOPMSustainLevel", "PosOPMTotalLevel",
	"PosOPMKeyScaling",  "PosOPMMultiple",     "PosOPMDetune1",     "PosOPMDetune2",
	"PosOPMAMSEnable",
	"PosOPMNoise",       "PosOPMClockB",       "PosOPMLFOFreq",     "PosOPMLFOPMD",
	"PosOPMLFOAMD",      "PosOPMLFOWAVE",
};

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

// ファイラーの「小さい文字, 大きい文字」の 2 つ組を読む。
// 1 つしか書かれていなければ、その値が両方に効く。
// スクロールの速さ (%) の範囲。1 未満は止まってしまうので下限 1。
int ClampScrollSpeed(int v) {
	if (v < 1) return 1;
	if (v > 1000) return 1000;
	return v;
}

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

	// ファイラーは下。境界は y=366（旧 mxv の一覧の上辺）なので、
	// ファイラー側の厚みは 480-366 = 114。
	filerSide = kFilerSideBottom;
	filerExtent = 114;

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

	miniFontW = 6;
	miniFontH = 8;

	statusX = 344;
	statusY = 4;
	statusW = 128;
	statusH = 35;
	regMapX = 0;
	regMapY = 0;
	regMapW = 340;
	regMapH = 229;
	regMapPosX = 1;
	regMapPosY = 1;
	{
		static const int kX[8] = { 0, 0, 0, 0, 68, 68, 68, 68 };
		static const int kY[8] = { 0, 9, 18, 27, 0, 9, 18, 27 };
		for (int i = 0; i < 8; i++) {
			pcmXOffset[i] = kX[i];
			pcmYOffset[i] = kY[i];
		}
	}
	{
		// 旧 mxv/draw.cpp の各 Screen_Put* にあった CX_D / CY_D。
		static const int kPos[kNumStatusItems][2] = {
			{ 2, 0 },    // Volume
			{ 26, 0 },   // LevelMeter
			{ 122, 0 },  // Panpot
			{ 2, 9 },    // Detune
			{ 40, 9 },   // Voice
			{ 66, 9 },   // Q
			{ 92, 9 },   // Ptr
			{ 2, 18 },   // LFOPitch
			{ 40, 18 },  // LFOPitch1
			{ 46, 18 },  // LFOPitch2
			{ 72, 18 },  // LFOPitch3
			{ 104, 18 }, // LFOPitch4
			{ 2, 27 },   // LFOVolume
			{ 40, 27 },  // LFOVolume1
			{ 46, 27 },  // LFOVolume2
			{ 72, 27 },  // LFOVolume3
			{ 2, 0 },    // PcmVolume（PcmX/PcmY からの相対）
			{ 24, 0 },   // PcmPtr
			// 音色データ表示（tonedata.md の既定値）。
			{ 0, 0 },    // OPMAlgorithm
			{ 0, 9 },    // OPMFeedback
			{ 14, 0 },   // OPMAttackRate（以下 AMSEnable まで OPMOperatorY を加算）
			{ 28, 0 },   // OPMDecayRate
			{ 42, 0 },   // OPMSustainRate
			{ 56, 0 },   // OPMReleaseRate
			{ 64, 0 },   // OPMSustainLevel
			{ 72, 0 },   // OPMTotalLevel
			{ 86, 0 },   // OPMKeyScaling
			{ 94, 0 },   // OPMMultiple
			{ 102, 0 },  // OPMDetune1
			{ 110, 0 },  // OPMDetune2
			{ 118, 0 },  // OPMAMSEnable
			{ 0, 0 },    // OPMNoise（PCM の段の左上からの相対）
			{ 0, 9 },    // OPMClockB
			{ 68, 0 },   // OPMLFOFreq
			{ 68, 9 },   // OPMLFOPMD
			{ 68, 18 },  // OPMLFOAMD
			{ 68, 27 },  // OPMLFOWave
		};
		for (int i = 0; i < kNumStatusItems; i++) {
			statusPos[i][0] = kPos[i][0];
			statusPos[i][1] = kPos[i][1];
		}
		static const int kOpY[4] = { 0, 18, 9, 27 };
		for (int i = 0; i < 4; i++) opmOperatorY[i] = kOpY[i];
	}

	levelMeterPalOfs = 32;
	levelMeterWidthCells = 64;
	levelMeterSrcX = 32;

	bannerX = 476;
	bannerY = 4;
	bannerW = 160;
	bannerH = 54;

	titleX = 4;
	titleY = 348;
	titleW = 632;
	titleH = 14;
	// 既定はそれまでの速さ（40 論理px/秒を Pixel 7a の Phone スキンで換算）。
	// 曲名欄は高さ 24px で 100%、ファイラーは小さい字 16px で 150%。
	titleScrollSpeed = 100;
	fileListScrollSpeed = 150;

	// ファイラー側の矩形 (0,366,640,114) からの内側マージン。
	// 一覧 + スクロールバーで (4,366,632,110) になる。
	fileListMargin[0] = 4;
	fileListMargin[1] = 0;
	fileListMargin[2] = 4;
	fileListMargin[3] = 4;
	fileListItemH[0] = 10;
	fileListItemH[1] = 13;
	// 旧 mxv の「文字数 × ItemHeight/2」をピクセルに直した値。
	fileListBaseNameX[0] = 5;
	fileListBaseNameX[1] = 6;
	fileListBaseNameW[0] = 120;
	fileListBaseNameW[1] = 156;
	fileListTitleX[0] = 125;
	fileListTitleX[1] = 162;

	scrollWidth = 12;
	scrollHitWidth = 12;
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
	// 導出値。PlacedFor() を通すまでは宣言サイズでの値を入れておく。
	placedCanvasW = screenW;
	placedCanvasH = screenH;
	placedOtherX = 0;
	placedOtherY = 0;
	fileListX = 4;
	fileListY = 366;
	fileListW = 620;
	fileListH = 110;
	fileListRows[0] = 11;
	fileListRows[1] = 8;
	fileListTitleW[0] = 495;
	fileListTitleW[1] = 458;
	scrollX = 624;
	scrollY = 366;
	scrollW = 12;
	scrollH = 110;
	scrollHitX = 624;
	scrollHitW = 12;
	scrollGrooveH = 86;
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
	progTimePos[0] = 16;
	progTimePos[1] = 8;
	{
		// 同梱素材の並び: 左端 / 右端 / 中央（残り）。上段が未再生。
		const Xywh left = { 0, 0, 12, 6 };
		const Xywh right = { 12, 0, 12, 6 };
		const Xywh bar = { 24, 0, 136, 6 };
		progSrcBarLeft = left;
		progSrcBarRight = right;
		progSrcBar = bar;
	}

	volX = 476;
	volY = 300;
	volW = 64;
	volH = 16;
	volVolumePos[0] = 64;
	volVolumePos[1] = 6;
	{
		// 同梱素材の並び: つまみ / 左端 / 右端 / 中央（残り）。
		const Xywh thumb = { 0, 0, 8, 16 };
		const Xywh left = { 8, 0, 8, 16 };
		const Xywh right = { 16, 0, 8, 16 };
		const Xywh bar = { 24, 0, 48, 16 };
		volSrcThumb = thumb;
		volSrcBarLeft = left;
		volSrcBarRight = right;
		volSrcBar = bar;
	}

	playKeyX = 476;
	playKeyY = 300;
	// 下の kRect / kPos で使う 8 個（SHUFFLE を除く）を覆う大きさ。
	playKeyW = 160;
	playKeyH = 44;
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
	palYellow = 18;
	palBlue = 19;

	backBitmap = "back.bmp";
	kb0Bitmap = "kb0.bmp";
	kb1Bitmap = "kb1.bmp";
	kb2Bitmap = "kb2.bmp";
	miniFontBitmap = "minifont.bmp";
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
				*err = MsgF("Error.SkinNotFound", ref);
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
		if (depth == 0) baseRef_ = base;  // 自分の Base だけは表示に使う
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
	filerSide = FilerSideFromName(ini.GetString("Screen", "FilerSide", std::string()), filerSide);
	filerExtent = ini.GetInt("Screen", "FilerExtent", filerExtent);
	backBitmap = ini.GetString("Screen", "ImgBack", backBitmap);

	GetXy(ini, "Keyboard", "Pos", &kbX, &kbY);
	GetIntList(ini, "Keyboard", "XOffset", kbXOffset, 13);
	kbYOffset = ini.GetInt("Keyboard", "YOffset", kbYOffset);
	GetIntList(ini, "Keyboard", "ChannelY", chYOffset, 9);
	keyOffset = ini.GetInt("Keyboard", "KeyOffset", keyOffset);
	kb0Bitmap = ini.GetString("Keyboard", "ImgKeyboard0", kb0Bitmap);
	kb1Bitmap = ini.GetString("Keyboard", "ImgKeyboard1", kb1Bitmap);
	kb2Bitmap = ini.GetString("Keyboard", "ImgKeyboard2", kb2Bitmap);

	miniFontW = ini.GetInt("MiniFont", "Width", miniFontW);
	miniFontH = ini.GetInt("MiniFont", "Height", miniFontH);
	miniFontBitmap = ini.GetString("MiniFont", "ImgMiniFont", miniFontBitmap);

	{
		Xywh r = { statusX, statusY, statusW, statusH };
		GetXywh(ini, "Status", "Rect", &r);
		statusX = r.x;
		statusY = r.y;
		statusW = r.w;
		statusH = r.h;
	}
	GetIntList(ini, "Status", "PcmX", pcmXOffset, 8);
	GetIntList(ini, "Status", "PcmY", pcmYOffset, 8);
	for (int i = 0; i < kNumStatusItems; i++) {
		GetIntList(ini, "Status", kStatusItemKeys[i], statusPos[i], 2);
	}
	GetIntList(ini, "Status", "OPMOperatorY", opmOperatorY, 4);

	{
		Xywh r = { regMapX, regMapY, regMapW, regMapH };
		GetXywh(ini, "RegMap", "Rect", &r);
		regMapX = r.x;
		regMapY = r.y;
		regMapW = r.w;
		regMapH = r.h;
	}
	GetXy(ini, "RegMap", "Pos", &regMapPosX, &regMapPosY);

	levelMeterPalOfs = ini.GetInt("LevelMeter", "PaletteOffset", levelMeterPalOfs);
	levelMeterWidthCells = ini.GetInt("LevelMeter", "Cells", levelMeterWidthCells);
	levelMeterSrcX = ini.GetInt("LevelMeter", "SrcX", levelMeterSrcX);
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
	titleScrollSpeed = ClampScrollSpeed(ini.GetInt("Title", "ScrollSpeed", titleScrollSpeed));
	GetIntList(ini, "FileList", "Margin", fileListMargin, 4);
	fileListScrollSpeed =
	    ClampScrollSpeed(ini.GetInt("FileList", "ScrollSpeed", fileListScrollSpeed));
	// どれも「小さい文字, 大きい文字」の 2 つ組。1 つだけなら両方に効く。
	GetFontSizePair(ini, "FileList", "ItemHeight", fileListItemH);
	GetFontSizePair(ini, "FileList", "BaseNameX", fileListBaseNameX);
	GetFontSizePair(ini, "FileList", "BaseNameWidth", fileListBaseNameW);
	GetFontSizePair(ini, "FileList", "TitleX", fileListTitleX);

	scrollWidth = ini.GetInt("ScrollBar", "Width", scrollWidth);
	scrollHitWidth = ini.GetInt("ScrollBar", "HitWidth", scrollHitWidth);
	GetXywh(ini, "ScrollBar", "SrcThumb", &scrollSrcThumb);
	GetXywh(ini, "ScrollBar", "SrcUpArrowPress", &scrollSrcUpArrowPress);
	GetXywh(ini, "ScrollBar", "SrcDownArrowPress", &scrollSrcDownArrowPress);
	GetXywh(ini, "ScrollBar", "SrcUpArrow", &scrollSrcUpArrow);
	GetXywh(ini, "ScrollBar", "SrcBar", &scrollSrcBar);
	GetXywh(ini, "ScrollBar", "SrcDownArrow", &scrollSrcDownArrow);
	scrollBarBitmap = ini.GetString("ScrollBar", "ImgScrollBar", scrollBarBitmap);

	{
		Xywh r = { progX, progY, progW, progH };
		GetXywh(ini, "ProgressBar", "Rect", &r);
		progX = r.x;
		progY = r.y;
		progW = r.w;
		progH = r.h;
	}
	GetIntList(ini, "ProgressBar", "TimePos", progTimePos, 2);
	GetXywh(ini, "ProgressBar", "SrcBarLeft", &progSrcBarLeft);
	GetXywh(ini, "ProgressBar", "SrcBarRight", &progSrcBarRight);
	GetXywh(ini, "ProgressBar", "SrcBar", &progSrcBar);
	progressBarBitmap = ini.GetString("ProgressBar", "ImgProgressBar", progressBarBitmap);

	{
		Xywh r = { volX, volY, volW, volH };
		GetXywh(ini, "VolumeBar", "Rect", &r);
		volX = r.x;
		volY = r.y;
		volW = r.w;
		volH = r.h;
	}
	GetIntList(ini, "VolumeBar", "VolumePos", volVolumePos, 2);
	GetXywh(ini, "VolumeBar", "SrcThumb", &volSrcThumb);
	GetXywh(ini, "VolumeBar", "SrcBarLeft", &volSrcBarLeft);
	GetXywh(ini, "VolumeBar", "SrcBarRight", &volSrcBarRight);
	GetXywh(ini, "VolumeBar", "SrcBar", &volSrcBar);
	volBarBitmap = ini.GetString("VolumeBar", "ImgVolumeBar", volBarBitmap);

	{
		Xywh r = { playKeyX, playKeyY, playKeyW, playKeyH };
		GetXywh(ini, "PlayKey", "Rect", &r);
		playKeyX = r.x;
		playKeyY = r.y;
		playKeyW = r.w;
		playKeyH = r.h;
	}
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
	palYellow = ini.GetInt("PlayKey", "PalYellow", palYellow);
	palBlue = ini.GetInt("PlayKey", "PalBlue", palBlue);
	playKeyBitmap = ini.GetString("PlayKey", "ImgPlayKey", playKeyBitmap);
}

// ---------------------------------------------------------------------------
// キャンバスの大きさに合わせる（fullscreen.md）
// ---------------------------------------------------------------------------

void Skin::CanvasSizeFor(int outW, int outH, int stretchLimit, int *cw, int *ch) const {
	int w = screenW;
	int h = screenH;
	if (outW > 0 && outH > 0 && screenW > 0 && screenH > 0) {
		if (FilerSideVertical(filerSide)) {
			// 幅を宣言サイズに合わせ、高さだけ出力の縦横比へ寄せる。
			h = (int)(((int64_t)outH * screenW + outW / 2) / outW);
			int limit = stretchLimit;
			const int hard = screenH * kMaxCanvasStretch;
			if (limit <= 0 || limit > hard) limit = hard;
			if (h < screenH) h = screenH;  // 縮む方向へは伸ばさない（左右が余白になる）
			if (h > limit) h = limit;
		} else {
			w = (int)(((int64_t)outW * screenH + outH / 2) / outH);
			int limit = stretchLimit;
			const int hard = screenW * kMaxCanvasStretch;
			if (limit <= 0 || limit > hard) limit = hard;
			if (w < screenW) w = screenW;
			if (w > limit) w = limit;
		}
	}
	if (cw != 0) *cw = w;
	if (ch != 0) *ch = h;
}

Skin Skin::PlacedFor(int canvasW, int canvasH) const {
	Skin s = *this;
	if (canvasW < screenW) canvasW = screenW;
	if (canvasH < screenH) canvasH = screenH;
	s.placedCanvasW = canvasW;
	s.placedCanvasH = canvasH;

	// 画面を 2 つに分ける。伸びたぶんはファイラー側だけが受け取り、
	// ファイラー以外側の厚みは宣言サイズのまま。
	const int fixedV = screenH - filerExtent;  // 上下配置での「ファイラー以外側」の高さ
	const int fixedH = screenW - filerExtent;  // 左右配置での「ファイラー以外側」の幅
	Xywh filer = { 0, 0, canvasW, canvasH };
	int otherX = 0, otherY = 0;
	switch (filerSide) {
		case kFilerSideTop:
			filer.h = canvasH - fixedV;
			otherY = filer.h;
			break;
		case kFilerSideLeft:
			filer.w = canvasW - fixedH;
			otherX = filer.w;
			break;
		case kFilerSideRight:
			filer.x = fixedH;
			filer.w = canvasW - fixedH;
			break;
		case kFilerSideBottom:
		default:
			filer.y = fixedV;
			filer.h = canvasH - fixedV;
			break;
	}
	if (filer.w < 1) filer.w = 1;
	if (filer.h < 1) filer.h = 1;
	s.placedOtherX = otherX;
	s.placedOtherY = otherY;

	// ファイラー側の矩形から内側へマージンを取ると、一覧とスクロールバーで
	// 分け合う矩形になる。
	Xywh inner;
	inner.x = filer.x + fileListMargin[0];
	inner.y = filer.y + fileListMargin[1];
	inner.w = filer.w - fileListMargin[0] - fileListMargin[2];
	inner.h = filer.h - fileListMargin[1] - fileListMargin[3];
	if (inner.w < 1) inner.w = 1;
	if (inner.h < 1) inner.h = 1;

	int sw = scrollWidth;
	if (sw < 0) sw = 0;
	if (sw > inner.w - 1) sw = inner.w - 1;

	s.fileListX = inner.x;
	s.fileListY = inner.y;
	s.fileListW = inner.w - sw;
	s.fileListH = inner.h;

	s.scrollX = inner.x + inner.w - sw;
	s.scrollY = inner.y;
	s.scrollW = sw;
	s.scrollH = inner.h;

	// 当たり判定は描画より広くできる（左へ広がる）。
	int hitW = scrollHitWidth;
	if (hitW < sw) hitW = sw;
	if (hitW > inner.w) hitW = inner.w;
	s.scrollHitX = inner.x + inner.w - hitW;
	s.scrollHitW = hitW;

	// 矢印は上端と下端に貼り付き、溝は残りぶん。
	const int upH = scrollSrcUpArrow.h;
	const int downH = scrollSrcDownArrow.h;
	int groove = s.scrollH - upH - downH;
	if (groove < 0) groove = 0;
	s.scrollGrooveH = groove;
	s.scrollPosUpArrow[0] = 0;
	s.scrollPosUpArrow[1] = 0;
	s.scrollPosBar[0] = 0;
	s.scrollPosBar[1] = upH;
	s.scrollPosDownArrow[0] = 0;
	s.scrollPosDownArrow[1] = s.scrollH - downH;

	// 行数と曲名の幅は矩形から決まる。行数は切り捨て（半端な帯は
	// 描かないし、当たり判定でも弾く）。
	for (int i = 0; i < 2; i++) {
		const int ih = (fileListItemH[i] > 0) ? fileListItemH[i] : 1;
		int rows = s.fileListH / ih;
		if (rows < 1) rows = 1;
		s.fileListRows[i] = rows;
		int tw = s.fileListW - fileListTitleX[i];
		if (tw < 0) tw = 0;
		s.fileListTitleW[i] = tw;
	}

	// ファイラー以外側の部品は、その矩形の原点からの相対で書かれている。
	// 下・右にファイラーを置くなら原点は (0,0) のままなので何も動かない。
	if (otherX != 0 || otherY != 0) {
		s.kbX += otherX;
		s.kbY += otherY;
		s.statusX += otherX;
		s.statusY += otherY;
		s.regMapX += otherX;
		s.regMapY += otherY;
		s.bannerX += otherX;
		s.bannerY += otherY;
		s.titleX += otherX;
		s.titleY += otherY;
		s.progX += otherX;
		s.progY += otherY;
		s.volX += otherX;
		s.volY += otherY;
		s.playKeyX += otherX;
		s.playKeyY += otherY;
	}
	return s;
}

std::string Skin::FindFile(const std::string &name) const {
	for (size_t i = 0; i < dirs_.size(); i++) {
		const std::string path = JoinPath(dirs_[i], name);
		if (FileExists(path)) return path;
	}
	// 見つからなかったときは、エラー文言に出せるパスを返す。
	return dirs_.empty() ? name : JoinPath(dirs_[0], name);
}

std::string Skin::FindColorsFile() const {
	for (size_t i = 0; i < dirs_.size(); i++) {
		std::string path = JoinPath(dirs_[i], kColorsFile);
		if (FileExists(path)) return path;
		path = JoinPath(dirs_[i], kLegacyColorsFile);
		if (FileExists(path)) return path;
	}
	return std::string();
}

std::vector<std::string> FontSearchDirs(const Skin &skin, const AssetPaths &paths) {
	std::vector<std::string> dirs = skin.dirs();
	const std::vector<std::string> roots = paths.Roots();
	dirs.insert(dirs.end(), roots.begin(), roots.end());
	return dirs;
}

}  // namespace mxv2
