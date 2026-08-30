// mxv2 - フォルダの中身を別スレッドで読む

#include "dirlister.h"

#include "vfs.h"

namespace mxv2 {

namespace {

// 1 か所ぶん読む。開けなければ ok = false（呼んだ側はそこへ移動しない）。
void ListOne(const Vfs &vfs, const std::string &ref, DirLister::Result *out) {
	out->ref = ref;
	out->entries.clear();
	out->ok = false;

	FileSystem *fs = 0;
	std::string rel;
	if (!vfs.Parse(ref, &fs, &rel) || fs == 0) return;
	out->ok = fs->List(rel, &out->entries);
	if (!out->ok) out->entries.clear();
}

}  // namespace

DirLister::DirLister()
    : vfs_(0),
      mutex_(0),
      wake_(0),
      idle_(0),
      thread_(0),
      hasDone_(false),
      generation_(0),
      quit_(false),
      busy_(false) {}

DirLister::~DirLister() {
	Stop();
}

void DirLister::Start(const Vfs *vfs, const std::string &ref) {
	if (vfs == 0 || ref.empty()) {
		Cancel();
		return;
	}
	if (!EnsureThread()) {
		// スレッドが作れない環境では、これまでどおりその場で読む。
		ListOne(*vfs, ref, &done_);
		hasDone_ = true;
		return;
	}
	SDL_LockMutex(mutex_);
	vfs_ = vfs;
	generation_++;
	pending_ = ref;
	hasDone_ = false;
	SDL_UnlockMutex(mutex_);
	SDL_SemPost(wake_);
}

void DirLister::Cancel() {
	if (mutex_ == 0) {
		hasDone_ = false;
		return;
	}
	SDL_LockMutex(mutex_);
	generation_++;
	pending_.clear();
	hasDone_ = false;
	SDL_UnlockMutex(mutex_);
}

void DirLister::Quiesce() {
	if (mutex_ == 0) {
		hasDone_ = false;
		return;
	}
	SDL_LockMutex(mutex_);
	generation_++;
	pending_.clear();
	hasDone_ = false;
	while (busy_) SDL_CondWait(idle_, mutex_);
	SDL_UnlockMutex(mutex_);
}

bool DirLister::Take(Result *out) {
	if (mutex_ == 0) {  // スレッド無しで読んだぶん
		if (!hasDone_) return false;
		*out = done_;
		done_ = Result();
		hasDone_ = false;
		return true;
	}
	SDL_LockMutex(mutex_);
	const bool any = hasDone_;
	if (any) {
		*out = done_;
		done_ = Result();
		hasDone_ = false;
	}
	SDL_UnlockMutex(mutex_);
	return any;
}

bool DirLister::EnsureThread() {
	if (thread_ != 0) return true;
	if (mutex_ == 0) mutex_ = SDL_CreateMutex();
	if (wake_ == 0) wake_ = SDL_CreateSemaphore(0);
	if (idle_ == 0) idle_ = SDL_CreateCond();
	if (mutex_ == 0 || wake_ == 0 || idle_ == 0) return false;
	thread_ = SDL_CreateThread(Entry, "mxv2-dirlist", this);
	return thread_ != 0;
}

void DirLister::Stop() {
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

int SDLCALL DirLister::Entry(void *arg) {
	((DirLister *)arg)->Run();
	return 0;
}

void DirLister::Run() {
	for (;;) {
		SDL_SemWait(wake_);

		std::string ref;
		const Vfs *vfs = 0;
		uint32_t gen = 0;
		SDL_LockMutex(mutex_);
		if (quit_) {
			SDL_UnlockMutex(mutex_);
			return;
		}
		ref.swap(pending_);
		vfs = vfs_;
		gen = generation_;
		busy_ = !ref.empty();
		SDL_UnlockMutex(mutex_);
		if (vfs == 0 || ref.empty()) {
			Done();
			continue;
		}

		Result r;
		ListOne(*vfs, ref, &r);

		SDL_LockMutex(mutex_);
		// 読んでいる間にフォルダが変わっていたら捨てる。
		if (!quit_ && gen == generation_) {
			done_ = r;
			hasDone_ = true;
		}
		busy_ = false;
		SDL_CondBroadcast(idle_);
		SDL_UnlockMutex(mutex_);
	}
}

// 1 回ぶんの依頼を読み終えた（または捨てた）ことを知らせる。
void DirLister::Done() {
	SDL_LockMutex(mutex_);
	busy_ = false;
	SDL_CondBroadcast(idle_);
	SDL_UnlockMutex(mutex_);
}

}  // namespace mxv2
