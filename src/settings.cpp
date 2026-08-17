// mxv2 - 設定の保存と復元

#include "settings.h"

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
      loops(2),
      fadeout(true),
      volumeBarPos(28),  // Player::kVolumeBarMaxDefault / 2
      savePosition(true),
      windowX(-1),
      windowY(-1) {}

std::string Settings::DefaultPath() {
	return JoinPath(ExecutableDir(), "mxv2.ini");
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

	loops = ini.GetInt("Play", "N_Loop", loops);
	fadeout = ini.GetInt("Play", "Fadeout", fadeout ? 1 : 0) != 0;
	volumeBarPos = ini.GetInt("Play", "Volume", volumeBarPos);

	pdxPath = ini.GetString("Path", "PDX", pdxPath);

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

	ini.SetInt("Play", "N_Loop", loops);
	ini.SetInt("Play", "Fadeout", fadeout ? 1 : 0);
	ini.SetInt("Play", "Volume", volumeBarPos);

	ini.SetString("Path", "PDX", pdxPath);

	ini.SetInt("Position", "Save", savePosition ? 1 : 0);
	ini.SetInt("Position", "X", windowX);
	ini.SetInt("Position", "Y", windowY);

	return ini.Save(path);
}

}  // namespace mxv2
