// mxv2 - 設定の保存と復元

#include "settings.h"

#include <cstdio>
#include <cstdlib>

#include "fileutil.h"
#include "ini.h"

namespace mxv2 {

// 既定のスキン（2026-09-12 に決めた組み合わせ）。どれも設定で変えられる。
//   デスクトップ: Default (640x480)。縦横切り替え（デバッグ用）は
//                 Default-Portrait / Default-Landscape。
//   モバイル:     Phone (480x720) / Phone-Landscape (720x480)。切り替え OFF
//                 （デバッグ用）のときは Phone。
#if defined(__ANDROID__) || defined(__IPHONEOS__)
const char kDefaultSkinName[] = "Phone";
const char kDefaultSkinPortrait[] = "assets:Phone";
const char kDefaultSkinLandscape[] = "assets:Phone-Landscape";
#else
const char kDefaultSkinName[] = "Default";
const char kDefaultSkinPortrait[] = "assets:Default-Portrait";
const char kDefaultSkinLandscape[] = "assets:Default-Landscape";
#endif

const char *Settings::DefaultSkinName() { return kDefaultSkinName; }
const char *Settings::DefaultSkinPortrait() { return kDefaultSkinPortrait; }
const char *Settings::DefaultSkinLandscape() { return kDefaultSkinLandscape; }

// OrientationMode の並び。ini には名前で書く（手で編集する人に分かるように、
// また layout.ini の [Screen] FilerSide と同じ流儀に揃えるため）。
static const char *const kOrientModeNames[Settings::OrientationMode::Count] = {
	"PortraitOnly", "LandscapeOnly", "Startup", "Always",
};

const char *Settings::OrientationModeName(int mode) {
	if (mode < 0 || mode >= OrientationMode::Count) mode = OrientationMode::Always;
	return kOrientModeNames[mode];
}

int Settings::OrientationModeFromName(const std::string &name, int fallback) {
	if (name.empty()) return fallback;
	for (int i = 0; i < OrientationMode::Count; i++) {
		if (CompareNoCase(name, kOrientModeNames[i]) == 0) return i;
	}
	return fallback;
}

Settings::Settings()
    : skinName(kDefaultSkinName),
      skinPortrait(kDefaultSkinPortrait),
      skinLandscape(kDefaultSkinLandscape),
      orientationMode(OrientationMode::Always),
      zoomPercent(0),
      legacyScale(0),
      scaleFilter("sharp"),
      fullScreen(false),
      touchUi(TouchUi::Auto),
      pipMode(PipMode::Playing),
      fileListFontSize(0),
      folderFirst(false),
      fileListScroll(FileListScroll::Cursor),
      lastPlaying(false),
      resumePlayOnStart(false),
      sampleRate(48000),
      loops(2),
      fadeout(true),
      autoNext(false),
      autoRepeat(false),
      masterVolume(0),  // 中央
      latencyAuto(true),
      latencyMs(0),
#if defined(_WIN32)
      btDefaultLatencyMs(80),
#else
      btDefaultLatencyMs(0),
#endif
      btDefaultSwapArtistAlbum(false),
      btDefaultResumeOnReconnect(true),
      bookmarksDefaulted(true),
      windowX(-1),
      windowY(-1),
      windowW(0),
      windowH(0),
      windowIconic(false),
      windowMaximized(false),
      updateCheck(true),
      nextUpdateCheck(0),
      castQuality(kCastQualityDefault),
      castMuteLocal(true),
      castVideoAdvanceMs(kCastAdvanceMsDefault),
      tutorialDone(false) {
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
	fullScreen = ini.GetInt("Screen", "FullScreen", fullScreen ? 1 : 0) != 0;
	touchUi = ini.GetInt("Screen", "TouchUI", touchUi);
	if (touchUi < TouchUi::Auto || touchUi > TouchUi::Off) touchUi = TouchUi::Auto;
	pipMode = ini.GetInt("Screen", "Pip", pipMode);
	if (pipMode < PipMode::Off || pipMode >= PipMode::Count) pipMode = PipMode::Playing;

	fileListFontSize = ini.GetInt("Filer", "FontSize", fileListFontSize) ? 1 : 0;
	folderFirst = ini.GetInt("Filer", "FolderFirst", folderFirst ? 1 : 0) != 0;
	fileListScroll = ini.GetInt("Filer", "TitleScroll", fileListScroll);
	if (fileListScroll < FileListScroll::None || fileListScroll > FileListScroll::All) {
		fileListScroll = FileListScroll::Cursor;
	}
	lastDir = ini.GetString("Filer", "LastDir", lastDir);
	lastFile = ini.GetString("Filer", "LastFile", lastFile);
	lastPlaying = ini.GetInt("Filer", "LastPlaying", lastPlaying ? 1 : 0) != 0;
	resumePlayOnStart =
	    ini.GetInt("Behavior", "ResumePlayOnStart", resumePlayOnStart ? 1 : 0) != 0;

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
#if defined(_WIN32)
	// 以前の置き場所（[Play] BluetoothLatency → [Bluetooth] Latency）も読む
	// （次の保存で DefaultLatency へ移る）。Android はそれらを使っていなかった
	// （値が書かれていても 80 の既定が残っているだけ）ので引き継がない。
	btDefaultLatencyMs = ini.GetInt("Play", "BluetoothLatency", btDefaultLatencyMs);
	btDefaultLatencyMs = ini.GetInt("Bluetooth", "Latency", btDefaultLatencyMs);
#endif
	btDefaultLatencyMs = ini.GetInt("Bluetooth", "DefaultLatency", btDefaultLatencyMs);
	if (btDefaultLatencyMs < 0) btDefaultLatencyMs = 0;
	if (btDefaultLatencyMs > kBluetoothLatencyMsMax) btDefaultLatencyMs = kBluetoothLatencyMsMax;
	// 機器ごとにする前の置き場所も読む（次の保存で Default… へ移る）。
	btDefaultSwapArtistAlbum =
	    ini.GetInt("Bluetooth", "SwapArtistAlbum", btDefaultSwapArtistAlbum ? 1 : 0) != 0;
	btDefaultSwapArtistAlbum =
	    ini.GetInt("Bluetooth", "DefaultSwapArtistAlbum", btDefaultSwapArtistAlbum ? 1 : 0) != 0;
	btDefaultResumeOnReconnect =
	    ini.GetInt("Bluetooth", "ResumeOnReconnect", btDefaultResumeOnReconnect ? 1 : 0) != 0;
	btDefaultResumeOnReconnect = ini.GetInt("Bluetooth", "DefaultResumeOnReconnect",
	                                        btDefaultResumeOnReconnect ? 1 : 0) != 0;
	{
		btDevices.clear();
		int count = ini.GetInt("Bluetooth", "DeviceCount", 0);
		if (count > kMaxBtDevices) count = kMaxBtDevices;
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Device%d", i);
			BtDevice d;
			d.name = ini.GetString("Bluetooth", key, "");
			if (d.name.empty()) continue;
			snprintf(key, sizeof(key), "Device%dLatency", i);
			d.latencyMs = ini.GetInt("Bluetooth", key, btDefaultLatencyMs);
			if (d.latencyMs < 0) d.latencyMs = 0;
			if (d.latencyMs > kBluetoothLatencyMsMax) d.latencyMs = kBluetoothLatencyMsMax;
			snprintf(key, sizeof(key), "Device%dSwap", i);
			d.swapArtistAlbum =
			    ini.GetInt("Bluetooth", key, btDefaultSwapArtistAlbum ? 1 : 0) != 0;
			snprintf(key, sizeof(key), "Device%dResume", i);
			d.resumeOnReconnect =
			    ini.GetInt("Bluetooth", key, btDefaultResumeOnReconnect ? 1 : 0) != 0;
			btDevices.push_back(d);
		}
	}

	// PDX の探索先。[Path] PdxCount があればその並び、無ければ旧形式の
	// [Path] PDX（1 本）を 1 件目にする（次の保存で新形式へ書き換わる）。
	if (ini.Has("Path", "PdxCount")) {
		pdxPaths.clear();
		int count = ini.GetInt("Path", "PdxCount", 0);
		if (count > kMaxPdxPaths) count = kMaxPdxPaths;
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Pdx%d", i);
			const std::string v = ini.GetString("Path", key, std::string());
			if (!v.empty()) pdxPaths.push_back(v);
		}
	} else {
		const std::string legacy = ini.GetString("Path", "PDX", std::string());
		if (!legacy.empty()) {
			pdxPaths.clear();
			pdxPaths.push_back(legacy);
		}
	}

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

	// [Position] Save は廃止した（2026-09-18）。読み捨てる。
	windowX = ini.GetInt("Position", "X", windowX);
	windowY = ini.GetInt("Position", "Y", windowY);
	windowW = ini.GetInt("Position", "Width", windowW);
	windowH = ini.GetInt("Position", "Height", windowH);
	windowIconic = ini.GetInt("Position", "Iconic", windowIconic ? 1 : 0) != 0;
	windowMaximized = ini.GetInt("Position", "Maximized", windowMaximized ? 1 : 0) != 0;

	updateCheck = ini.GetInt("Network", "UpdateCheck", updateCheck ? 1 : 0) != 0;
	castQuality = ini.GetInt("Cast", "Quality", castQuality);
	if (castQuality < 0) castQuality = 0;
	if (castQuality > kCastQualityMax) castQuality = kCastQualityMax;
	castMuteLocal = ini.GetInt("Cast", "MuteLocal", castMuteLocal ? 1 : 0) != 0;
	castVideoAdvanceMs = ini.GetInt("Cast", "VideoAdvanceMs", castVideoAdvanceMs);
	if (castVideoAdvanceMs < 0) castVideoAdvanceMs = 0;
	if (castVideoAdvanceMs > kCastAdvanceMsMax) castVideoAdvanceMs = kCastAdvanceMsMax;
	// 時刻は 32 ビットに収まらなくなる日が来るので、文字列で読み書きする。
	{
		const std::string v = ini.GetString("Network", "NextUpdateCheck", std::string());
		if (!v.empty()) nextUpdateCheck = strtoll(v.c_str(), 0, 10);
		if (nextUpdateCheck < 0) nextUpdateCheck = 0;
	}

	tutorialDone = ini.GetInt("Tutorial", "Done", tutorialDone ? 1 : 0) != 0;

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
	ini.SetInt("Screen", "FullScreen", fullScreen ? 1 : 0);
	ini.SetInt("Screen", "TouchUI", touchUi);
	ini.SetInt("Screen", "Pip", pipMode);

	ini.SetInt("Filer", "FontSize", fileListFontSize ? 1 : 0);
	ini.SetInt("Filer", "FolderFirst", folderFirst ? 1 : 0);
	ini.SetInt("Filer", "TitleScroll", fileListScroll);
	ini.SetString("Filer", "LastDir", lastDir);
	ini.SetString("Filer", "LastFile", lastFile);
	ini.SetInt("Filer", "LastPlaying", lastPlaying ? 1 : 0);
	ini.SetInt("Behavior", "ResumePlayOnStart", resumePlayOnStart ? 1 : 0);

	ini.SetInt("Play", "SampleRate", sampleRate);
	ini.SetInt("Play", "N_Loop", loops);
	ini.SetInt("Play", "Fadeout", fadeout ? 1 : 0);
	ini.SetInt("Play", "Cont", autoNext ? 1 : 0);
	ini.SetInt("Play", "Repeat", autoRepeat ? 1 : 0);
	ini.SetInt("Play", "Volume", masterVolume);
	ini.SetInt("Play", "LatencyAuto", latencyAuto ? 1 : 0);
	ini.SetInt("Play", "Latency", latencyMs);
	ini.Remove("Play", "BluetoothLatency");
	ini.Remove("Bluetooth", "Latency");
	ini.SetInt("Bluetooth", "DefaultLatency", btDefaultLatencyMs);
	{
		int count = (int)btDevices.size();
		if (count > kMaxBtDevices) count = kMaxBtDevices;
		ini.SetInt("Bluetooth", "DeviceCount", count);
		for (int i = 1; i <= kMaxBtDevices; i++) {
			char key[32];
			char keyLatency[32];
			char keySwap[32];
			char keyResume[32];
			snprintf(key, sizeof(key), "Device%d", i);
			snprintf(keyLatency, sizeof(keyLatency), "Device%dLatency", i);
			snprintf(keySwap, sizeof(keySwap), "Device%dSwap", i);
			snprintf(keyResume, sizeof(keyResume), "Device%dResume", i);
			if (i <= count) {
				const BtDevice &d = btDevices[(size_t)i - 1];
				ini.SetString("Bluetooth", key, d.name);
				ini.SetInt("Bluetooth", keyLatency, d.latencyMs);
				ini.SetInt("Bluetooth", keySwap, d.swapArtistAlbum ? 1 : 0);
				ini.SetInt("Bluetooth", keyResume, d.resumeOnReconnect ? 1 : 0);
			} else {
				ini.Remove("Bluetooth", key);
				ini.Remove("Bluetooth", keyLatency);
				ini.Remove("Bluetooth", keySwap);
				ini.Remove("Bluetooth", keyResume);
			}
		}
	}
	ini.Remove("Bluetooth", "SwapArtistAlbum");
	ini.Remove("Bluetooth", "ResumeOnReconnect");
	ini.SetInt("Bluetooth", "DefaultSwapArtistAlbum", btDefaultSwapArtistAlbum ? 1 : 0);
	ini.SetInt("Bluetooth", "DefaultResumeOnReconnect", btDefaultResumeOnReconnect ? 1 : 0);

	{
		int count = (int)pdxPaths.size();
		if (count > kMaxPdxPaths) count = kMaxPdxPaths;
		ini.SetInt("Path", "PdxCount", count);
		for (int i = 1; i <= count; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Pdx%d", i);
			ini.SetString("Path", key, pdxPaths[i - 1]);
		}
		// Count を超えた古い Pdx<n> は消す（FS<n> と同じ理由）。
		for (int i = count + 1; i <= kMaxPdxPaths; i++) {
			char key[32];
			snprintf(key, sizeof(key), "Pdx%d", i);
			ini.Remove("Path", key);
		}
		// 旧形式の [Path] PDX は取り込み済みなので消す。
		ini.Remove("Path", "PDX");
	}

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

	// 廃止した [Position] Save を消す（Ini は知らないキーを残すので、
	// 置いておくと「切ったつもりなのに効かない」もとになる）。
	ini.Remove("Position", "Save");
	ini.SetInt("Position", "X", windowX);
	ini.SetInt("Position", "Y", windowY);
	ini.SetInt("Position", "Width", windowW);
	ini.SetInt("Position", "Height", windowH);
	ini.SetInt("Position", "Iconic", windowIconic ? 1 : 0);
	ini.SetInt("Position", "Maximized", windowMaximized ? 1 : 0);

	ini.SetInt("Network", "UpdateCheck", updateCheck ? 1 : 0);
	ini.SetInt("Cast", "Quality", castQuality);
	ini.SetInt("Cast", "MuteLocal", castMuteLocal ? 1 : 0);
	ini.SetInt("Cast", "VideoAdvanceMs", castVideoAdvanceMs);
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%lld", nextUpdateCheck);
		ini.SetString("Network", "NextUpdateCheck", buf);
	}

	ini.SetInt("Tutorial", "Done", tutorialDone ? 1 : 0);

	return ini.Save(path);
}

bool Settings::SaveFields(const std::string &path, unsigned fields) const {
	if (fields == 0) return true;

	// ファイルにある値を土台にして、変わった項目だけを載せ替える。
	Settings out;
	out.Load(path);

	if (fields & Field::Locale) out.locale = locale;
	if (fields & Field::Skin) out.skinName = skinName;
	if (fields & Field::OrientSkin) {
		out.skinPortrait = skinPortrait;
		out.skinLandscape = skinLandscape;
	}
	if (fields & Field::OrientMode) out.orientationMode = orientationMode;
	if (fields & Field::Zoom) out.zoomPercent = zoomPercent;
	if (fields & Field::Filter) out.scaleFilter = scaleFilter;
	if (fields & Field::FullScreen) out.fullScreen = fullScreen;
	if (fields & Field::TouchUi) out.touchUi = touchUi;
	if (fields & Field::Pip) out.pipMode = pipMode;
	if (fields & Field::FontSize) out.fileListFontSize = fileListFontSize;
	if (fields & Field::FolderFirst) out.folderFirst = folderFirst;
	if (fields & Field::FileListScroll) out.fileListScroll = fileListScroll;
	if (fields & Field::LastDir) out.lastDir = lastDir;
	if (fields & Field::LastFile) {
		out.lastFile = lastFile;
		out.lastPlaying = lastPlaying;
	}
	if (fields & Field::ResumePlay) out.resumePlayOnStart = resumePlayOnStart;
	if (fields & Field::SampleRate) out.sampleRate = sampleRate;
	if (fields & Field::Loops) out.loops = loops;
	if (fields & Field::Fadeout) out.fadeout = fadeout;
	if (fields & Field::ContRepeat) {
		out.autoNext = autoNext;
		out.autoRepeat = autoRepeat;
	}
	if (fields & Field::Volume) out.masterVolume = masterVolume;
	if (fields & Field::Latency) {
		out.latencyAuto = latencyAuto;
		out.latencyMs = latencyMs;
	}
	if (fields & Field::Bluetooth) {
		out.btDefaultLatencyMs = btDefaultLatencyMs;
		out.btDefaultSwapArtistAlbum = btDefaultSwapArtistAlbum;
		out.btDefaultResumeOnReconnect = btDefaultResumeOnReconnect;
		out.btDevices = btDevices;
	}
	if (fields & Field::PdxPaths) out.pdxPaths = pdxPaths;
	if (fields & Field::FileSystems) out.fileSystems = fileSystems;
	if (fields & Field::Bookmarks) out.bookmarks = bookmarks;
	if (fields & Field::WindowPos) {
		out.windowX = windowX;
		out.windowY = windowY;
		out.windowIconic = windowIconic;
		out.windowMaximized = windowMaximized;
	}
	if (fields & Field::Tutorial) out.tutorialDone = tutorialDone;
	if (fields & Field::UpdateCheck) out.updateCheck = updateCheck;
	if (fields & Field::UpdateSchedule) out.nextUpdateCheck = nextUpdateCheck;
	if (fields & Field::Cast) {
		out.castQuality = castQuality;
		out.castMuteLocal = castMuteLocal;
		out.castVideoAdvanceMs = castVideoAdvanceMs;
	}
	return out.Save(path);
}

Settings::BtDevice Settings::BluetoothDeviceFor(const std::string &name) const {
	for (size_t i = 0; i < btDevices.size(); i++) {
		if (btDevices[i].name == name) return btDevices[i];
	}
	BtDevice d;
	d.name = name;
	d.latencyMs = btDefaultLatencyMs;
	d.swapArtistAlbum = btDefaultSwapArtistAlbum;
	d.resumeOnReconnect = btDefaultResumeOnReconnect;
	return d;
}

int Settings::BluetoothLatencyFor(const std::string &name) const {
	return BluetoothDeviceFor(name).latencyMs;
}

void Settings::SetBluetoothDevice(const BtDevice &device) {
	BtDevice d = device;
	if (d.latencyMs < 0) d.latencyMs = 0;
	if (d.latencyMs > kBluetoothLatencyMsMax) d.latencyMs = kBluetoothLatencyMsMax;
	for (size_t i = 0; i < btDevices.size(); i++) {
		if (btDevices[i].name == d.name) {
			btDevices[i] = d;
			return;
		}
	}
	btDevices.push_back(d);
	if ((int)btDevices.size() > kMaxBtDevices) btDevices.erase(btDevices.begin());
}

}  // namespace mxv2
