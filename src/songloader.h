// mxv2 - MDX / PDX の読み込みを別スレッドで行う
//
// 曲を開くと MDX を 1 本読み、PDX を探して（見つかれば）もう 1 本読む。
// ローカルなら一瞬だが、外部ファイルシステム (SAF / Web / SMB) では
// そのたびに通信が要るので、メインスレッドで読むと演奏も画面も止まる。
//
// 依頼は 1 件だけ持つ。新しく頼むと前の依頼は捨てられる（世代番号）。
// **読み終わるまで前の曲はそのまま鳴っている。** 音を切ってから待たせるより
// 途切れないほうがよいので、`Player::PlaySong` は届いてから呼ぶ。
//
// 読むのは Vfs 経由。今の FileSystem は Configure() のあと増減しない前提に
// 乗っている（dirlister.h と同じ）。取り外す前に Quiesce() を呼ぶこと。

#ifndef MXV2_SONGLOADER_H
#define MXV2_SONGLOADER_H

#include <string>
#include <vector>

#include <SDL.h>

#include "mdxsong.h"

namespace mxv2 {

class Vfs;

class SongLoader {
public:
	struct Result {
		std::string ref;   // どの曲を読んだか
		MdxSong song;
		std::string err;   // ok が false のときの理由 (UTF-8)
		bool ok;

		Result() : ok(false) {}
	};

	SongLoader();
	~SongLoader();

	// 読み込みを頼む。前の依頼は捨てる。
	void Start(const Vfs *vfs, const std::string &mdxRef,
	           const std::vector<std::string> &pdxSearchDirs);

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
	std::string pending_;
	std::vector<std::string> pendingDirs_;
	Result done_;
	bool hasDone_;
	uint32_t generation_;
	bool quit_;
	bool busy_;

	SongLoader(const SongLoader &);
	SongLoader &operator=(const SongLoader &);
};

}  // namespace mxv2

#endif  // MXV2_SONGLOADER_H
