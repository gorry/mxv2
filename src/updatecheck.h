// mxv2 - 更新チェック（GitHub Releases API）
//
// 公開場所 (Profile.ini の [URL] Project = https://github.com/<owner>/<repo>)
// から API の URL を作り、最新リリース (releases/latest) のタグ名を今の版
// (MXV2_APP_VERSION) と比べる。下書きとプレリリースは latest に出てこない
// ので、試験用の版はプレリリースで公開すれば利用者には知らせずに済む。
//
// 通信と解析は作業スレッドで行い、結果はメインスレッドが Poll() で拾う。
// いつ確かめるか（1 日 1 回・設定の ON/OFF）は main.cpp が決める。

#ifndef MXV2_UPDATECHECK_H
#define MXV2_UPDATECHECK_H

#include <string>

namespace mxv2 {

struct UpdateResult {
	enum Status {
		kFailed = 0,  // 通信・解析に失敗した（error に理由。英語のまま）
		kLatest,      // 今の版が最新（か、公開版より新しい手元のビルド）
		kNewer,       // 新しい版が公開されている
	};
	Status status;
	bool manual;              // [今すぐ更新チェックを行う] から始めたか
	std::string latest;       // 公開されている最新の版（タグ名）
	std::string pageUrl;      // そのリリースのページ
	std::string error;
	UpdateResult() : status(kFailed), manual(false) {}
};

class UpdateChecker {
public:
	UpdateChecker();
	// 走っている最中なら、作業スレッドを切り離して待たない（終了を
	// 通信の時間切れまで待たせないため）。結果の置き場は作業スレッドが
	// 最後に片付ける。
	~UpdateChecker();

	// この環境で更新チェックができるか（HTTP が使えて、公開場所が GitHub）。
	static bool Available();

	// チェックを始める。**メインスレッドから呼ぶこと。** 走っている最中なら
	// 何もせず false。
	bool Start(bool manual);
	bool running() const { return state_ != 0; }

	// 終わっていれば結果を *out に入れて true（1 回だけ）。
	bool Poll(UpdateResult *out);

	// 版の比較。"2026.0918.2" のような「.」区切りの数字を頭から比べる
	// （頭の "v" は読み飛ばす）。a が新しければ正、同じなら 0、古ければ負。
	static int CompareVersions(const std::string &a, const std::string &b);

private:
	struct State;
	static int ThreadMain(void *arg);
	static void Release(State *s);

	State *state_;
};

}  // namespace mxv2

#endif  // MXV2_UPDATECHECK_H
