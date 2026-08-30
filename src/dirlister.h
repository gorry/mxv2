// mxv2 - フォルダの中身を別スレッドで読む
//
// 外部ファイルシステム (SAF / Web / SMB) では List 1 回ごとに通信が要る。
// メインスレッドで呼ぶとフォルダを開いた瞬間に画面が固まるので、読むのは
// 別スレッドへ回し、届いたぶんを毎フレーム Take() で引き取る。
//
// 依頼は 1 件だけ持つ。新しく頼むと前の依頼は捨てられる（世代番号で
// 見分けるので、古いフォルダの結果が混ざることはない）。
//
// **「そこを開けるか」の判定も List の成否で兼ねる。** IsDir を別に呼ぶと
// 往復が 2 回になるため。今あるファイルシステムはどれも ListDirectory を
// 使っており、ファイルや読めないドライブでは false になる。
//
// 読むのは Vfs 経由。今の FileSystem は Configure() のあと増減しない前提に
// 乗っている（filer.cpp の TitleReader と同じ）。**外部ファイルシステムを
// 動かしている最中に取り外すときは、先に Quiesce() を呼ぶこと。**

#ifndef MXV2_DIRLISTER_H
#define MXV2_DIRLISTER_H

#include <string>
#include <vector>

#include <SDL.h>

#include "fileutil.h"

namespace mxv2 {

class Vfs;

class DirLister {
public:
	struct Result {
		std::string ref;               // どの場所を読んだか
		std::vector<DirEntry> entries;
		bool ok;                       // 開けたか（false なら移動しない）

		Result() : ok(false) {}
	};

	DirLister();
	~DirLister();

	// ref の中身を読む依頼を出す。前の依頼は捨てる。
	void Start(const Vfs *vfs, const std::string &ref);

	// 読みかけを捨てる（待たない）。
	void Cancel();

	// 読みかけを捨てて、スレッドが手を離すまで待つ。
	// **ファイルシステムを取り外す前に呼ぶこと。**
	void Quiesce();

	// 読み終わっていれば引き取る。何も無ければ false。
	bool Take(Result *out);

private:
	bool EnsureThread();
	void Stop();
	void Done();
	void Run();
	static int SDLCALL Entry(void *arg);

	const Vfs *vfs_;
	SDL_mutex *mutex_;
	SDL_sem *wake_;
	SDL_cond *idle_;
	SDL_Thread *thread_;
	std::string pending_;   // これから読む場所（空なら依頼なし）
	Result done_;
	bool hasDone_;
	uint32_t generation_;
	bool quit_;
	bool busy_;

	DirLister(const DirLister &);
	DirLister &operator=(const DirLister &);
};

}  // namespace mxv2

#endif  // MXV2_DIRLISTER_H
