// mxv2 - MDX / PDX の読み込みを別スレッドで行う

#include "songloader.h"

#include <algorithm>

#include "vfs.h"

namespace mxv2 {

SongLoader::SongLoader()
    : vfs_(0),
      mutex_(0),
      wake_(0),
      idle_(0),
      thread_(0),
      hasDone_(false),
      generation_(0),
      quit_(false),
      busy_(false) {}

SongLoader::~SongLoader() {
	Stop();
}

void SongLoader::Start(const Vfs *vfs, const std::string &mdxRef,
                       const std::vector<std::string> &pdxSearchDirs) {
	if (vfs == 0 || mdxRef.empty()) {
		Cancel();
		return;
	}
	if (!EnsureThread()) {
		// スレッドが作れない環境では、これまでどおりその場で読む。
		Result r;
		r.ref = mdxRef;
		r.ok = LoadMdxSong(*vfs, mdxRef, pdxSearchDirs, &r.song, &r.err);
		std::swap(done_, r);
		hasDone_ = true;
		return;
	}
	SDL_LockMutex(mutex_);
	vfs_ = vfs;
	generation_++;
	pending_ = mdxRef;
	pendingDirs_ = pdxSearchDirs;
	hasDone_ = false;
	SDL_UnlockMutex(mutex_);
	SDL_SemPost(wake_);
}

void SongLoader::Cancel() {
	if (mutex_ == 0) {
		hasDone_ = false;
		return;
	}
	SDL_LockMutex(mutex_);
	generation_++;
	pending_.clear();
	pendingDirs_.clear();
	hasDone_ = false;
	SDL_UnlockMutex(mutex_);
}

void SongLoader::Quiesce() {
	if (mutex_ == 0) {
		hasDone_ = false;
		return;
	}
	SDL_LockMutex(mutex_);
	generation_++;
	pending_.clear();
	pendingDirs_.clear();
	hasDone_ = false;
	while (busy_) SDL_CondWait(idle_, mutex_);
	SDL_UnlockMutex(mutex_);
}

bool SongLoader::Take(Result *out) {
	if (mutex_ == 0) {  // スレッド無しで読んだぶん
		if (!hasDone_) return false;
		std::swap(*out, done_);
		done_ = Result();
		hasDone_ = false;
		return true;
	}
	SDL_LockMutex(mutex_);
	const bool any = hasDone_;
	if (any) {
		std::swap(*out, done_);
		done_ = Result();
		hasDone_ = false;
	}
	SDL_UnlockMutex(mutex_);
	return any;
}

bool SongLoader::EnsureThread() {
	if (thread_ != 0) return true;
	if (mutex_ == 0) mutex_ = SDL_CreateMutex();
	if (wake_ == 0) wake_ = SDL_CreateSemaphore(0);
	if (idle_ == 0) idle_ = SDL_CreateCond();
	if (mutex_ == 0 || wake_ == 0 || idle_ == 0) return false;
	thread_ = SDL_CreateThread(Entry, "mxv2-song", this);
	return thread_ != 0;
}

void SongLoader::Stop() {
	if (thread_ != 0) {
		SDL_LockMutex(mutex_);
		quit_ = true;
		generation_++;
		SDL_UnlockMutex(mutex_);
		SDL_SemPost(wake_);
		SDL_WaitThread(thread_, 0);
		thread_ = 0;
	}
	if (wake_ != 0) {
		SDL_DestroySemaphore(wake_);
		wake_ = 0;
	}
	if (idle_ != 0) {
		SDL_DestroyCond(idle_);
		idle_ = 0;
	}
	if (mutex_ != 0) {
		SDL_DestroyMutex(mutex_);
		mutex_ = 0;
	}
}

int SDLCALL SongLoader::Entry(void *arg) {
	((SongLoader *)arg)->Run();
	return 0;
}

void SongLoader::Run() {
	for (;;) {
		SDL_SemWait(wake_);

		std::string ref;
		std::vector<std::string> dirs;
		const Vfs *vfs = 0;
		uint32_t gen = 0;
		SDL_LockMutex(mutex_);
		if (quit_) {
			SDL_UnlockMutex(mutex_);
			return;
		}
		ref.swap(pending_);
		dirs.swap(pendingDirs_);
		vfs = vfs_;
		gen = generation_;
		busy_ = !ref.empty();
		SDL_UnlockMutex(mutex_);
		if (vfs == 0 || ref.empty()) {
			Done();
			continue;
		}

		Result r;
		r.ref = ref;
		r.ok = LoadMdxSong(*vfs, ref, dirs, &r.song, &r.err);

		SDL_LockMutex(mutex_);
		// 読んでいる間に別の曲を頼まれていたら捨てる。
		if (!quit_ && gen == generation_) {
			std::swap(done_, r);
			hasDone_ = true;
		}
		busy_ = false;
		SDL_CondBroadcast(idle_);
		SDL_UnlockMutex(mutex_);
	}
}

// 1 回ぶんの依頼を読み終えた（または捨てた）ことを知らせる。
void SongLoader::Done() {
	SDL_LockMutex(mutex_);
	busy_ = false;
	SDL_CondBroadcast(idle_);
	SDL_UnlockMutex(mutex_);
}

}  // namespace mxv2
