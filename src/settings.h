// mxv2 - 設定の保存と復元
//
// 旧 mxv の mxv.ini にあたる。ユーザーフォルダの mxv2.ini を読み書きする。
// 旧 mxv にあった [Switch] の SampleRate / Between / PCMBuf / ROMEO は
// mxv2 では固定（48000 / SDL 任せ / 非対応）なので持たない。
//
// コマンドラインオプションは、読み込んだ設定を上書きする形で効く。

#ifndef MXV2_SETTINGS_H
#define MXV2_SETTINGS_H

#include <string>
#include <vector>

namespace mxv2 {

struct Settings {
	// [UI]
	// 画面とログの文言に使う言語（assets/locale/<名前>）。**空なら「自動」**で、
	// 動作環境の言語 (Screen::SystemLocale) に一番近い同梱ぶんを使う。
	// -locale はこれより優先されるが、ini には残さない。
	std::string locale;

	// [Screen]
	std::string skinName;  // skin/<名前>（同梱ぶんとユーザーぶんの両方から探す）
	// 表示倍率 (%)。100 でドット等倍。0 なら「まだ決まっていない」で、
	// 初回起動時にシステムの拡大率 (175% など) を拾って埋める。
	int zoomPercent;
	// 旧い ini の Scale=<整数倍>。0 なら無し。倍率を % へ移すための繋ぎで、
	// 読み込み時にしか使わない（保存はしない）。
	int legacyScale;
	// 拡大時の補間方法。"nearest" / "linear" / "sharp"。
	// 実際の値との変換は Screen::ScaleModeFromName / ScaleModeName。
	std::string scaleFilter;
	// 指で操作する端末向けに、ダイアログの押せるところを広げるか。
	// kTouchAuto なら Screen::TouchPreferred() に従う（Android は有効）。
	int touchUi;
	enum TouchUi { kTouchAuto = 0, kTouchOn = 1, kTouchOff = 2 };

	// [Filer]
	int fileListFontSize;  // 0 = 小 / 1 = 大 (旧 mxv の FontSize)
	bool folderFirst;
	// ファイラーの曲名が桁に収まらないときの横スクロール。
	int fileListScroll;
	enum FileListScroll {
		kScrollNone = 0,    // しない
		kScrollCursor = 1,  // カーソル行だけ
		kScrollAll = 2,     // 全て
	};
	std::string lastDir;  // 最後に開いていたディレクトリ

	// [Play]
	// 出力サンプリングレート。既定は 48000 で、x68sound が 96kHz に対応して
	// いる版（X68SOUND_SUPPORT_96KHZ）でだけ 96000 も選べる。対応していない
	// 値が書かれていても捨てはしない（別のビルドで書いた値かもしれないので、
	// そのときは Player 側が既定へ落として鳴らす）。
	int sampleRate;
	int loops;
	bool fadeout;
	// メイン画面の CONT / REPEAT ボタン。押した状態を次の起動へ持ち越す。
	bool autoNext;    // CONT   … 演奏が終わったら次の曲へ
	bool autoRepeat;  // REPEAT … 演奏が終わったら同じ曲をもう一度
	// マスター音量 -100..+100 (0 = 中央)。メイン画面の音量はその場かぎりの
	// 調整なので記録しない（起動時は必ず 0 から始まる）。
	int masterVolume;
	// 画面を音に合わせて遅らせる量。latencyAuto なら latencyMs は見ず、
	// オーディオ装置のバッファ長をそのまま使う（既定）。
	// 手動のときは ms で、正の値で表示が遅れ、負で先行する。
	bool latencyAuto;
	int latencyMs;
	// 手動指定できる幅。設定ウィンドウのスライダもこの範囲。
	static const int kLatencyMsMin = -200;
	static const int kLatencyMsMax = 500;

	// [Path]
	std::string pdxPath;  // PDX の追加探索先（1 つ。複数は -pdxpath で足す）

	// [FileSystem]
	// ファイラーのルート（ファイルシステムの選択）に並べる順。中身は
	// "assets:" のような ref。書き出す本数の上限は kMaxFileSystems。
	std::vector<std::string> fileSystems;
	static const int kMaxFileSystems = 64;

	// [Bookmark]
	// よく開く場所の控え。中身はフォルダの ref で、並び順がそのまま
	// ダイアログの並び。初回起動時は空。
	std::vector<std::string> bookmarks;
	static const int kMaxBookmarks = 64;

	// [Position] 復元用。savePosition が false なら使わない。
	bool savePosition;
	int windowX, windowY;

	// 保存する項目。「変わった項目だけを書き戻す」ために使う。
	// 設定ウィンドウには保存ボタンが無く、触った時点で保存する作りなので、
	// 何を触ったかをこのビットで積んでいく。
	enum Field {
		kFieldSkin = 1 << 0,
		kFieldZoom = 1 << 1,
		kFieldFilter = 1 << 2,
		kFieldFontSize = 1 << 3,
		kFieldFolderFirst = 1 << 4,
		kFieldLastDir = 1 << 5,
		kFieldLoops = 1 << 6,
		kFieldFadeout = 1 << 7,
		kFieldVolume = 1 << 8,
		kFieldPdxPath = 1 << 9,
		kFieldWindowPos = 1 << 10,
		kFieldLatency = 1 << 11,
		kFieldFileSystems = 1 << 12,
		kFieldSampleRate = 1 << 13,
		kFieldBookmarks = 1 << 14,
		kFieldTouchUi = 1 << 15,
		kFieldFileListScroll = 1 << 16,
		kFieldContRepeat = 1 << 17,
		kFieldLocale = 1 << 18,
	};

	Settings();

	// dir に置く mxv2.ini のパス。dir はふつうユーザーフォルダ
	// （UserDataDir()。実行ファイルの隣は書けないことがある）。
	static std::string PathIn(const std::string &dir);

	bool Load(const std::string &path);
	bool Save(const std::string &path) const;

	// ファイルを読み直してから fields の項目だけを差し替えて書く。
	// こうしないと -nofade のようなコマンドラインの一時指定まで
	// residue として ini に焼き付いてしまう。
	bool SaveFields(const std::string &path, unsigned fields) const;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGS_H
