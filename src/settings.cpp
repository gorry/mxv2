// mxv2 - 設定の保存と復元

#include "settings.h"

#include <cstdio>

#include "fileutil.h"
#include "ini.h"

namespace mxv2 {

// 既定のスキン。Android は縦長の画面なので Phone (480x720) を既定にする
// （Windows は横長の Default 640x480）。どちらも設定で変えられる。
#ifdef __ANDROID__
const char kDefaultSkinName[] = "Phone";
#else
const char kDefaultSkinName[] = "Default";
#endif

// 縦横切り替えが ON のときの既定のスキン。どちらも同梱ぶんで、今のところ
// Phone / Phone-R への別名（layout.ini に Base しか書いていない）。
const char kDefaultSkinPortrait[] = "assets:Default-Portrait";
const char kDefaultSkinLandscape[] = "assets:Default-Landscape";

// OrientationMode の並び。ini には名前で書く（手で編集する人に分かるように、
// また layout.ini の [Screen] FilerSide と同じ流儀に揃えるため）。
static const char *const kOrientModeNames[Settings::kNumOrientModes] = {
	"PortraitOnly", "LandscapeOnly", "Startup", "Always",
};

const char *Settings::OrientationModeName(int mode) {
	if (mode < 0 || mode >= kNumOrientModes) mode = kOrientAlways;
	return kOrientModeNames[mode];
}

int Settings::OrientationModeFromName(const std::string &name, int fallback) {
	if (name.empty()) return fallback;
	for (int i = 0; i < kNumOrientModes; i++) {
		if (CompareNoCase(name, kOrientModeNames[i]) == 0) return i;
	}
	return fallback;
}

Settings::Settings()
    : skinName(kDefaultSkinName),
      skinPortrait(kDefaultSkinPortrait),
      skinLandscape(kDefaultSkinLandscape),
      orientationMode(kOrientAlways),
      zoomPercent(0),
      legacyScale(0),
      scaleFilter("sharp"),
      touchUi(kTouchAuto),
      fileListFontSize(0),
      folderFirst(false),
      fileListScroll(kScrollCursor),
      sampleRate(48000),
      loops(2),
      fadeout(true),
      autoNext(false),
      autoRepeat(false),
      masterVolume(0),  // 中央
      latencyAuto(true),
      latencyMs(0),
      bookmarksDefaulted(true),
      savePosition(true),
      windowX(-1),
      windowY(-1),
      windowW(0),
      windowH(0) {
	// 初回起動のブックマーク（bookmark.md）。ini に [Bookmark] があれば
	// Load() が置き換える。
	bookmarks.push_back("assets:");
}

std::string Settings::PathIn(const std::string &dir) {
	return JoinPath(dir, "mxv2.ini");
}

bool Settings::Load(const std::string &path) {
	Ini ini;
	if (!ini.Load(path)) return false;

	// 空なら「自動」（環境の言語）。知らない名前が書かれていても捨てない
	// （別の版で足された言語かもしれないので、そのときは MatchLocale が
	// 一番近いものへ落とす）。
	locale = ini.GetString("UI", "Locale", locale);

	// 旧い ini は Theme= だった。スキンのフォルダ名として読み替える。
	skinName = ini.GetString("Screen", "Skin", ini.GetString("Screen", "Theme", skinName));
	skinPortrait = ini.GetString("Screen", "SkinPortrait", skinPortrait);
	skinLandscape = ini.GetString("Screen", "SkinLandscape", skinLandscape);
	orientationMode =
	    OrientationModeFromName(ini.GetString("Screen", "Orientation", std::string()), orientationMode);
	zoomPercent = ini.GetInt("Screen", "Zoom", zoomPercent);
	legacyScale = ini.GetInt("Screen", "Scale", 0);
	scaleFilter = ini.GetString("Screen", "Filter", scaleFilter);
	touchUi = ini.GetInt("Screen", "TouchUI", touchUi);
	if (touchUi < kTouchAuto || touchUi > kTouchOff) touchUi = kTouchAuto;

	fileListFontSize = ini.GetInt("Filer", "FontSize", fileListFontSize) ? 1 : 0;
	folderFirst = ini.GetInt("Filer", "FolderFirst", folderFirst ? 1 : 0) != 0;
	fileListScroll = ini.GetInt("Filer", "TitleScroll", fileListScroll);
	if (fileListScroll < kScrollNone || fileListScroll > kScrollAll) {
		fileListScroll = kScrollCursor;
	}
	lastDir = ini.GetString("Filer", "LastDir", lastDir);

	sampleRate = ini.GetInt("Play", "SampleRate", sampleRate);
	loops = ini.GetInt("Play", "N_Loop", loops);
	fadeout = ini.GetInt("Play", "Fadeout", fadeout ? 1 : 0) != 0;
	autoNext = ini.GetInt("Play", "Cont", autoNext ? 1 : 0) != 0;
	autoRepeat = ini.GetInt("Play", "Repeat", autoRepeat ? 1 : 0) != 0;
	masterVolume = ini.GetInt("Play", "Volume", masterVolume);
	if (masterVolume < -100) masterVolume = -100;
	if (masterVolume > 100) masterVolume = 100;
	latencyAuto = ini.GetInt("Play", "LatencyAuto", latencyAuto ? 1 : 0) != 0;
	latencyMs = ini.GetInt("Play", "Latency", latencyMs);
	if (latencyMs < kLatencyMsMin) latencyMs = kLatencyMsMin;
	if (latencyMs > kLatencyMsMax) latencyMs = kLatencyMsMax;

	pdxPath = ini.GetString("Path", "PDX", pdxPath);

	// 中身の妥当性（知らないファイルシステム、削除できないものの欠落）は
	// VFS 側で見る。ここは書いてある順に並べるだけ。
	fileSystems.clear();
	{
		int count = ini.GetInt("FileSystem", "Count", 0);
		if (count > kMaxFileSystems) count = kMaxFileSystems;
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "FS%d", i);
			const std::string v = ini.GetString("FileSystem", key, std::string());
			if (!v.empty()) fileSystems.push_back(v);
		}
	}

	// 中身の妥当性（知らないファイルシステム）は VFS を持っている側で見る。
	// ここは書いてある順に並べるだけ。[Bookmark] Count そのものが無ければ
	// 初回起動なので、コンストラクタの初期値 ("assets:") をそのまま使う
	// （Count=0 は「全部消した」なので空のまま）。
	if (ini.Has("Bookmark", "Count")) {
		bookmarksDefaulted = false;
		bookmarks.clear();
		int count = ini.GetInt("Bookmark", "Count", 0);
		if (count > kMaxBookmarks) count = kMaxBookmarks;
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Bookmark%d", i);
			const std::string v = ini.GetString("Bookmark", key, std::string());
			if (!v.empty()) bookmarks.push_back(v);
		}
	}

	savePosition = ini.GetInt("Position", "Save", savePosition ? 1 : 0) != 0;
	windowX = ini.GetInt("Position", "X", windowX);
	windowY = ini.GetInt("Position", "Y", windowY);
	windowW = ini.GetInt("Position", "Width", windowW);
	windowH = ini.GetInt("Position", "Height", windowH);

	if (loops < 1) loops = 1;
	if (loops > 99) loops = 99;
	return true;
}

bool Settings::Save(const std::string &path) const {
	Ini ini;
	ini.Load(path);  // 知らないキーは残す

	ini.SetString("UI", "Locale", locale);

	ini.SetString("Screen", "Skin", skinName);
	ini.SetString("Screen", "SkinPortrait", skinPortrait);
	ini.SetString("Screen", "SkinLandscape", skinLandscape);
	ini.SetString("Screen", "Orientation", OrientationModeName(orientationMode));
	ini.SetInt("Screen", "Zoom", zoomPercent);
	ini.SetString("Screen", "Filter", scaleFilter);
	ini.SetInt("Screen", "TouchUI", touchUi);

	ini.SetInt("Filer", "FontSize", fileListFontSize ? 1 : 0);
	ini.SetInt("Filer", "FolderFirst", folderFirst ? 1 : 0);
	ini.SetInt("Filer", "TitleScroll", fileListScroll);
	ini.SetString("Filer", "LastDir", lastDir);

	ini.SetInt("Play", "SampleRate", sampleRate);
	ini.SetInt("Play", "N_Loop", loops);
	ini.SetInt("Play", "Fadeout", fadeout ? 1 : 0);
	ini.SetInt("Play", "Cont", autoNext ? 1 : 0);
	ini.SetInt("Play", "Repeat", autoRepeat ? 1 : 0);
	ini.SetInt("Play", "Volume", masterVolume);
	ini.SetInt("Play", "LatencyAuto", latencyAuto ? 1 : 0);
	ini.SetInt("Play", "Latency", latencyMs);

	ini.SetString("Path", "PDX", pdxPath);

	{
		int count = (int)fileSystems.size();
		if (count > kMaxFileSystems) count = kMaxFileSystems;
		ini.SetInt("FileSystem", "Count", count);
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "FS%d", i);
			ini.SetString("FileSystem", key, fileSystems[i - 1]);
		}
		// Count を超えた古い FS<n> は消す。残しておくと、次に読んだときに
		// また出てきてしまう（Ini は知らないキーをそのまま残すため）。
		for (int i = count + 1; i <= kMaxFileSystems; i++) {
			char key[32];
			snprintf(key, sizeof(key), "FS%d", i);
			ini.Remove("FileSystem", key);
		}
	}

	{
		int count = (int)bookmarks.size();
		if (count > kMaxBookmarks) count = kMaxBookmarks;
		ini.SetInt("Bookmark", "Count", count);
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Bookmark%d", i);
			ini.SetString("Bookmark", key, bookmarks[i - 1]);
		}
		// Count を超えた古い Bookmark<n> は消す（FS<n> と同じ理由）。
		for (int i = count + 1; i <= kMaxBookmarks; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Bookmark%d", i);
			ini.Remove("Bookmark", key);
		}
	}

	ini.SetInt("Position", "Save", savePosition ? 1 : 0);
	ini.SetInt("Position", "X", windowX);
	ini.SetInt("Position", "Y", windowY);
	ini.SetInt("Position", "Width", windowW);
	ini.SetInt("Position", "Height", windowH);

	return ini.Save(path);
}

bool Settings::SaveFields(const std::string &path, unsigned fields) const {
	if (fields == 0) return true;

	// ファイルにある値を土台にして、変わった項目だけを載せ替える。
	Settings out;
	out.Load(path);

	if (fields & kFieldLocale) out.locale = locale;
	if (fields & kFieldSkin) out.skinName = skinName;
	if (fields & kFieldOrientSkin) {
		out.skinPortrait = skinPortrait;
		out.skinLandscape = skinLandscape;
	}
	if (fields & kFieldOrientMode) out.orientationMode = orientationMode;
	if (fields & kFieldZoom) out.zoomPercent = zoomPercent;
	if (fields & kFieldFilter) out.scaleFilter = scaleFilter;
	if (fields & kFieldTouchUi) out.touchUi = touchUi;
	if (fields & kFieldFontSize) out.fileListFontSize = fileListFontSize;
	if (fields & kFieldFolderFirst) out.folderFirst = folderFirst;
	if (fields & kFieldFileListScroll) out.fileListScroll = fileListScroll;
	if (fields & kFieldLastDir) out.lastDir = lastDir;
	if (fields & kFieldSampleRate) out.sampleRate = sampleRate;
	if (fields & kFieldLoops) out.loops = loops;
	if (fields & kFieldFadeout) out.fadeout = fadeout;
	if (fields & kFieldContRepeat) {
		out.autoNext = autoNext;
		out.autoRepeat = autoRepeat;
	}
	if (fields & kFieldVolume) out.masterVolume = masterVolume;
	if (fields & kFieldLatency) {
		out.latencyAuto = latencyAuto;
		out.latencyMs = latencyMs;
	}
	if (fields & kFieldPdxPath) out.pdxPath = pdxPath;
	if (fields & kFieldFileSystems) out.fileSystems = fileSystems;
	if (fields & kFieldBookmarks) out.bookmarks = bookmarks;
	if (fields & kFieldWindowPos) {
		out.windowX = windowX;
		out.windowY = windowY;
	}
	return out.Save(path);
}

}  // namespace mxv2
