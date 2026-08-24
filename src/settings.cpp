// mxv2 - 設定の保存と復元

#include "settings.h"

#include <cstdio>

#include "fileutil.h"
#include "ini.h"

namespace mxv2 {

Settings::Settings()
    : skinName("Default"),
      zoomPercent(0),
      legacyScale(0),
      scaleFilter("sharp"),
      fileListFontSize(0),
      folderFirst(false),
      sampleRate(48000),
      loops(2),
      fadeout(true),
      masterVolume(0),  // 中央
      latencyAuto(true),
      latencyMs(0),
      savePosition(true),
      windowX(-1),
      windowY(-1) {}

std::string Settings::PathIn(const std::string &dir) {
	return JoinPath(dir, "mxv2.ini");
}

bool Settings::Load(const std::string &path) {
	Ini ini;
	if (!ini.Load(path)) return false;

	// 旧い ini は Theme= だった。スキンのフォルダ名として読み替える。
	skinName = ini.GetString("Screen", "Skin", ini.GetString("Screen", "Theme", skinName));
	zoomPercent = ini.GetInt("Screen", "Zoom", zoomPercent);
	legacyScale = ini.GetInt("Screen", "Scale", 0);
	scaleFilter = ini.GetString("Screen", "Filter", scaleFilter);

	fileListFontSize = ini.GetInt("Filer", "FontSize", fileListFontSize) ? 1 : 0;
	folderFirst = ini.GetInt("Filer", "FolderFirst", folderFirst ? 1 : 0) != 0;
	lastDir = ini.GetString("Filer", "LastDir", lastDir);

	sampleRate = ini.GetInt("Play", "SampleRate", sampleRate);
	loops = ini.GetInt("Play", "N_Loop", loops);
	fadeout = ini.GetInt("Play", "Fadeout", fadeout ? 1 : 0) != 0;
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

	savePosition = ini.GetInt("Position", "Save", savePosition ? 1 : 0) != 0;
	windowX = ini.GetInt("Position", "X", windowX);
	windowY = ini.GetInt("Position", "Y", windowY);

	if (loops < 1) loops = 1;
	if (loops > 99) loops = 99;
	return true;
}

bool Settings::Save(const std::string &path) const {
	Ini ini;
	ini.Load(path);  // 知らないキーは残す

	ini.SetString("Screen", "Skin", skinName);
	ini.SetInt("Screen", "Zoom", zoomPercent);
	ini.SetString("Screen", "Filter", scaleFilter);

	ini.SetInt("Filer", "FontSize", fileListFontSize ? 1 : 0);
	ini.SetInt("Filer", "FolderFirst", folderFirst ? 1 : 0);
	ini.SetString("Filer", "LastDir", lastDir);

	ini.SetInt("Play", "SampleRate", sampleRate);
	ini.SetInt("Play", "N_Loop", loops);
	ini.SetInt("Play", "Fadeout", fadeout ? 1 : 0);
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

	ini.SetInt("Position", "Save", savePosition ? 1 : 0);
	ini.SetInt("Position", "X", windowX);
	ini.SetInt("Position", "Y", windowY);

	return ini.Save(path);
}

bool Settings::SaveFields(const std::string &path, unsigned fields) const {
	if (fields == 0) return true;

	// ファイルにある値を土台にして、変わった項目だけを載せ替える。
	Settings out;
	out.Load(path);

	if (fields & kFieldSkin) out.skinName = skinName;
	if (fields & kFieldZoom) out.zoomPercent = zoomPercent;
	if (fields & kFieldFilter) out.scaleFilter = scaleFilter;
	if (fields & kFieldFontSize) out.fileListFontSize = fileListFontSize;
	if (fields & kFieldFolderFirst) out.folderFirst = folderFirst;
	if (fields & kFieldLastDir) out.lastDir = lastDir;
	if (fields & kFieldSampleRate) out.sampleRate = sampleRate;
	if (fields & kFieldLoops) out.loops = loops;
	if (fields & kFieldFadeout) out.fadeout = fadeout;
	if (fields & kFieldVolume) out.masterVolume = masterVolume;
	if (fields & kFieldLatency) {
		out.latencyAuto = latencyAuto;
		out.latencyMs = latencyMs;
	}
	if (fields & kFieldPdxPath) out.pdxPath = pdxPath;
	if (fields & kFieldFileSystems) out.fileSystems = fileSystems;
	if (fields & kFieldWindowPos) {
		out.windowX = windowX;
		out.windowY = windowY;
	}
	return out.Save(path);
}

}  // namespace mxv2
