// mxv2 - 設定 UI: [バージョン情報] (F12)・[操作方法] (F11)・起動時の警告
//
// どれも「見せるだけ」の小さなダイアログなので 1 つのクラスにまとめてある。
// 実装は settingsui_info.cpp。クラスの作法は settingsui_cast.h の頭を見ること。

#ifndef MXV2_SETTINGSUI_INFO_H
#define MXV2_SETTINGSUI_INFO_H

#include <string>
#include <vector>

namespace mxv2 {

class SettingsUi;

class InfoDialogs {
public:
	// 起動時に出た警告の 1 行。ウィンドウが開く前に出たものを持ち越して、
	// 最初のフレームでダイアログとして出す（ログにも同じものが出ている）。
	// regrantRef が空でなければ「アクセス許可が失われた SAF」の項目で、文の
	// 隣に [許可を取り直す…] を添える（押すと OS のピッカーへ。2026-09-16）。
	struct StartupWarning {
		std::string text;
		std::string regrantRef;  // 取り直す相手の mountRef。無ければ空
		bool done;               // 取り直せた（ボタンを消して文を差し替える）
		StartupWarning() : done(false) {}
	};

	InfoDialogs() : showAbout_(false), showHelp_(false), showStartup_(false) {}

	void OpenAbout() { showAbout_ = true; }
	void OpenHelp() { showHelp_ = true; }
	bool isOpen() const { return showAbout_ || showHelp_ || showStartup_; }
	bool CloseTop() {
		if (showAbout_) { showAbout_ = false; return true; }
		if (showHelp_) { showHelp_ = false; return true; }
		if (showStartup_) { showStartup_ = false; return true; }
		return false;
	}

	// バージョン情報の見出し（名前・版・ビルド日付・著作権表示）。文言は
	// main.cpp が持っているので渡してもらう。
	void SetAboutHeader(const std::string &text) { aboutHeader_ = text; }
	// 起動時の警告。空なら何も出ない。SettingsUi::Init() のあとに渡すこと。
	void SetStartupWarnings(const std::vector<StartupWarning> &lines);
	// mountRef の許可を取り直せた。起動時の警告に同じ項目があれば、済んだ印を
	// 付けて文を text に差し替える（FileSystemsDialog から）。
	void MarkRegranted(const std::string &mountRef, const std::string &text);
	// 操作方法の行をカタログから読む。起動時と、言語を替えたとき。
	void LoadHelpRows();

	void BuildAbout(SettingsUi &ui);
	void BuildHelp(SettingsUi &ui);
	void BuildStartup(SettingsUi &ui);

private:
	// 操作方法の 1 行。中身はメッセージカタログの [HelpKeys] [HelpMouse]
	// （-h の出力と同じもの）。同梱フォントはプロポーショナルなので、空白で
	// 桁は揃えられない。「キー名」と「説明」に分けて持っておき、表示するときに
	// 幅を測って揃える。
	struct HelpRow {
		std::string key;   // 見出し行のときは見出しそのもの
		std::string desc;  // 見出し行と説明の無い行では空
		bool header;
		HelpRow() : header(false) {}
	};

	bool showAbout_;
	std::string aboutHeader_;  // 名前・版・ビルド日付・著作権表示
	std::string aboutText_;    // NOTICE の中身（初回に読む）
	bool showHelp_;
	std::vector<HelpRow> helpRows_;
	bool showStartup_;
	std::vector<StartupWarning> startupLines_;
};

}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_INFO_H
