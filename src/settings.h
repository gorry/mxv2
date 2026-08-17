// mxv2 - 設定の保存と復元
//
// 旧 mxv の mxv.ini にあたる。実行ファイルの隣の mxv2.ini を読み書きする。
// 旧 mxv にあった [Switch] の SampleRate / Between / PCMBuf / ROMEO は
// mxv2 では固定（48000 / SDL 任せ / 非対応）なので持たない。
//
// コマンドラインオプションは、読み込んだ設定を上書きする形で効く。

#ifndef MXV2_SETTINGS_H
#define MXV2_SETTINGS_H

#include <string>

namespace mxv2 {

struct Settings {
	// [Screen]
	std::string skinName;  // assets/skin/<名前>
	// 表示倍率 (%)。100 でドット等倍。0 なら「まだ決まっていない」で、
	// 初回起動時にシステムの拡大率 (175% など) を拾って埋める。
	int zoomPercent;
	// 旧い ini の Scale=<整数倍>。0 なら無し。倍率を % へ移すための繋ぎで、
	// 読み込み時にしか使わない（保存はしない）。
	int legacyScale;
	// 拡大時の補間方法。"nearest" / "linear" / "sharp"。
	// 実際の値との変換は Screen::ScaleModeFromName / ScaleModeName。
	std::string scaleFilter;

	// [Filer]
	int fileListFontSize;  // 0 = 小 / 1 = 大 (旧 mxv の FontSize)
	bool folderFirst;
	std::string lastDir;  // 最後に開いていたディレクトリ

	// [Play]
	int loops;
	bool fadeout;
	int volumeBarPos;  // 0..Player::kVolumeBarMax

	// [Path]
	std::string pdxPath;  // PDX の追加探索先（1 つ。複数は -pdxpath で足す）

	// [Position] 復元用。savePosition が false なら使わない。
	bool savePosition;
	int windowX, windowY;

	Settings();

	// 実行ファイルの隣の mxv2.ini のパス。
	static std::string DefaultPath();

	bool Load(const std::string &path);
	bool Save(const std::string &path) const;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGS_H
