// mxv2 - ファイラー（旧 mxv/Filer.cpp の移植）

#include "filer.h"

#include <algorithm>
#include <cstring>

#include <SDL.h>

#include "dirlister.h"
#include "fileutil.h"
#include "mdxsong.h"
#include "message.h"
#include "text.h"
#include "vfs.h"

#include <mdx_util.h>

namespace mxv2 {

namespace {

// 並び順は「見た目の順」なので、どのファイルシステムでも大文字小文字を
// 無視して並べる（同じものかどうかの判定は FileSystem::SamePath）。
bool LessNoCase(const FileItem &a, const FileItem &b) {
	return CompareNoCase(a.baseName, b.baseName) < 0;
}

// 「読み込み中」の行を出すまでの待ち時間 (ms)。ローカルのフォルダは
// 一瞬で届くので、すぐ出すとちらつくだけになる。
const uint32_t kLoadingRowDelayMs = 250;

// 1 曲ぶんのタイトルを読む。読めなければ false（一覧では空欄のまま）。
bool ReadOneTitle(const Vfs &vfs, const std::string &ref, std::string *out) {
	std::vector<uint8_t> data;
	if (!vfs.Read(ref, &data) || data.empty()) return false;

	char title[512];
	if (!MdxGetTitle(&data[0], (uint32_t)data.size(), title, sizeof(title))) return false;
	*out = SjisToUtf8(TrimTrailingControl(std::string(title)));
	return true;
}

}  // namespace

// MDX のタイトルを別スレッドで読む。
//
// フォルダを開くたびに、その中の MDX を全部開くことになる。ローカルなら
// 一瞬だが、外部ファイルシステム (SMB / Web) では 1 曲 1 リクエストなので、
// メインスレッドで回すと開いた瞬間に画面が固まる。
//
// 依頼には**世代番号**を振ってある。フォルダを移ると番号が変わり、
// 前のフォルダのぶんは読みかけでも捨てられる（結果が混ざらない）。
//
// 読むのは Vfs 経由。今の FileSystem は Configure() のあと増減しないので
// 別スレッドから触ってよい。**外部ファイルシステムを足し引きできるように
// するときは、ここの取り決めを見直すこと。**
class TitleReader {
public:
	struct Job {
		int index;
		std::string ref;
	};
	struct Result {
		int index;
		std::string title;
	};

	TitleReader()
	    : vfs_(0),
	      mutex_(0),
	      wake_(0),
	      idle_(0),
	      thread_(0),
	      generation_(0),
	      quit_(false),
	      busy_(false) {}
	~TitleReader() { Stop(); }

	// 読み直しを頼む。前の依頼は捨てる。
	void Start(const Vfs *vfs, const std::vector<Job> &jobs) {
		if (jobs.empty()) {
			if (mutex_ != 0) {
				SDL_LockMutex(mutex_);
				generation_++;
				pending_.clear();
				done_.clear();
				SDL_UnlockMutex(mutex_);
			}
			return;
		}
		if (!EnsureThread()) {
			// スレッドが作れない環境では、これまでどおりその場で読む。
			done_.clear();
			for (size_t i = 0; i < jobs.size(); i++) {
				Result r;
				r.index = jobs[i].index;
				if (ReadOneTitle(*vfs, jobs[i].ref, &r.title)) done_.push_back(r);
			}
			return;
		}
		SDL_LockMutex(mutex_);
		vfs_ = vfs;
		generation_++;
		pending_ = jobs;
		done_.clear();
		SDL_UnlockMutex(mutex_);
		SDL_SemPost(wake_);
	}

	// 読みかけを捨てて、スレッドが手を離すまで待つ。
	void Quiesce() {
		if (mutex_ == 0) return;
		SDL_LockMutex(mutex_);
		generation_++;  // 読みかけを捨てさせる
		pending_.clear();
		done_.clear();
		while (busy_) SDL_CondWait(idle_, mutex_);
		SDL_UnlockMutex(mutex_);
	}

	// 届いているぶんを引き取る。何も無ければ false。
	bool Take(std::vector<Result> *out) {
		out->clear();
		if (mutex_ == 0) {  // スレッド無しで読んだぶん
			if (done_.empty()) return false;
			out->swap(done_);
			return true;
		}
		SDL_LockMutex(mutex_);
		const bool any = !done_.empty();
		if (any) out->swap(done_);
		SDL_UnlockMutex(mutex_);
		return any;
	}

private:
	bool EnsureThread() {
		if (thread_ != 0) return true;
		if (mutex_ == 0) mutex_ = SDL_CreateMutex();
		if (wake_ == 0) wake_ = SDL_CreateSemaphore(0);
		if (idle_ == 0) idle_ = SDL_CreateCond();
		if (mutex_ == 0 || wake_ == 0 || idle_ == 0) return false;
		thread_ = SDL_CreateThread(Entry, "mxv2-titles", this);
		return thread_ != 0;
	}

	void Stop() {
		if (thread_ != 0) {
			SDL_LockMutex(mutex_);
			quit_ = true;
			generation_++;  // 読みかけを捨てさせる
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

	static int SDLCALL Entry(void *arg) {
		((TitleReader *)arg)->Run();
		return 0;
	}

	void Run() {
		for (;;) {
			SDL_SemWait(wake_);

			std::vector<Job> jobs;
			const Vfs *vfs = 0;
			uint32_t gen = 0;
			SDL_LockMutex(mutex_);
			if (quit_) {
				SDL_UnlockMutex(mutex_);
				return;
			}
			jobs.swap(pending_);
			vfs = vfs_;
			gen = generation_;
			busy_ = !jobs.empty();
			SDL_UnlockMutex(mutex_);
			if (vfs == 0 || jobs.empty()) {
				Done();
				continue;
			}

			for (size_t i = 0; i < jobs.size(); i++) {
				SDL_LockMutex(mutex_);
				const bool stale = quit_ || (gen != generation_);
				SDL_UnlockMutex(mutex_);
				if (stale) break;

				Result r;
				r.index = jobs[i].index;
				if (!ReadOneTitle(*vfs, jobs[i].ref, &r.title)) continue;

				SDL_LockMutex(mutex_);
				if (gen == generation_) done_.push_back(r);
				SDL_UnlockMutex(mutex_);
			}
			Done();
		}
	}

	// 1 回ぶんの依頼を読み終えた（または捨てた）ことを知らせる。
	void Done() {
		SDL_LockMutex(mutex_);
		busy_ = false;
		SDL_CondBroadcast(idle_);
		SDL_UnlockMutex(mutex_);
	}

	const Vfs *vfs_;
	SDL_mutex *mutex_;
	SDL_sem *wake_;
	SDL_cond *idle_;
	SDL_Thread *thread_;
	std::vector<Job> pending_;
	std::vector<Result> done_;
	uint32_t generation_;
	bool quit_;
	bool busy_;

	TitleReader(const TitleReader &);
	TitleReader &operator=(const TitleReader &);
};

Filer::Filer()
    : vfs_(0),
      fs_(0),
      cursor_(0),
      topPx_(0),
      rowHeightPx_(1),
      visibleRows_(11),
      folderFirst_(false),
      titles_(new TitleReader()),
      lister_(new DirLister()),
      loading_(false),
      loadingRow_(false),
      loadTicks_(0),
      resetCursor_(true),
      keepCursor_(0),
      keepTopPx_(0),
      hasPendingSelect_(false) {}

Filer::~Filer() {
	delete lister_;
	delete titles_;
}

void Filer::SetFolderFirst(bool on) {
	folderFirst_ = on;
}

void Filer::SetCurrentRef(const std::string &ref) {
	SaveView();  // 開けなかったときはここへ戻る
	fs_ = 0;
	rel_.clear();
	currentRef_.clear();
	if (vfs_ != 0) {
		FileSystem *fs = 0;
		std::string rel;
		if (vfs_->Parse(ref, &fs, &rel) && fs != 0) {
			fs_ = fs;
			rel_ = rel;
			currentRef_ = Vfs::MakeRef(fs, rel);
		}
	}
	BeginLoad(true);
}

// 場所は変えずに読み直す。**ファイルシステムは ref から引き直す**
// （取り外された直後に呼ばれることがあるので、fs_ を信じない）。
void Filer::Refresh() {
	SaveView();
	const std::string ref = currentRef_;
	fs_ = 0;
	rel_.clear();
	currentRef_.clear();
	if (vfs_ != 0) {
		FileSystem *fs = 0;
		std::string rel;
		if (vfs_->Parse(ref, &fs, &rel) && fs != 0) {
			fs_ = fs;
			rel_ = rel;
			currentRef_ = Vfs::MakeRef(fs, rel);
		}
	}
	BeginLoad(false);
}

// 今の見た目を控える。**読み込み中は控えない**（先頭 1 行だけの途中の姿を
// 控えてしまうと、続けて移動して失敗したときにそこへ戻ることになる）。
void Filer::SaveView() {
	if (loading_) return;
	saved_.ref = currentRef_;
	saved_.items = items_;
	saved_.cursor = cursor_;
	saved_.topPx = topPx_;
	saved_.valid = true;
}

void Filer::RestoreView() {
	loading_ = false;
	loadingRow_ = false;
	hasPendingSelect_ = false;
	pendingSelect_.clear();

	// 戻り先のファイルシステムを引き直す。控えたあとに取り外されていたら
	// もう戻れないので、ファイルシステムの選択へ抜ける。
	FileSystem *fs = 0;
	std::string rel;
	const bool alive = saved_.valid && vfs_ != 0 && vfs_->Parse(saved_.ref, &fs, &rel);
	if (!alive || (fs == 0 && !saved_.ref.empty())) {
		saved_.valid = false;
		fs_ = 0;
		rel_.clear();
		currentRef_.clear();
		BeginLoad(true);
		return;
	}

	fs_ = fs;
	rel_ = rel;
	currentRef_ = saved_.ref;
	items_ = saved_.items;
	cursor_ = saved_.cursor;
	topPx_ = saved_.topPx;
	EnsureCursorVisible();
}

// 一覧の読み込みを始める。中身が届くのはあとのフレーム (PollDir)。
// resetCursor が false なら、届いたときにカーソルとスクロールを戻す。
void Filer::BeginLoad(bool resetCursor) {
	resetCursor_ = resetCursor;
	keepCursor_ = cursor_;
	keepTopPx_ = topPx_;
	pendingSelect_.clear();
	hasPendingSelect_ = false;
	loadingRow_ = false;
	loading_ = false;
	lister_->Cancel();
	items_.clear();
	// 前のフォルダのタイトル読みも打ち切る（一覧が空になるので、
	// StartReadTitles は「依頼なし」を伝えることになる）。
	StartReadTitles();
	if (vfs_ == 0) return;

	// ファイルシステムの選択。画面固定の行ではなく「ref が空のときの一覧」
	// として作るので、カーソルもスクロールも当たり判定もそのまま使える。
	// ここは読み込みが要らないのでその場で仕上げる。
	if (fs_ == 0) {
		AppendFileSystems(&items_);
		FinishLoad();
		return;
	}

	// 先頭の行だけ先に出しておく。場所の表示と「戻る」は待たずに使える。
	AppendParentRow(&items_);
	loading_ = true;
	loadTicks_ = SDL_GetTicks();
	lister_->Start(vfs_, currentRef_);
}

// 別スレッドが読んできた中身で一覧を組み立てる。
void Filer::BuildItems(const std::vector<DirEntry> &entries) {
	items_.clear();
	AppendParentRow(&items_);
	if (folderFirst_) {
		AppendDirs(entries, &items_);
		AppendMdx(entries, &items_);
	} else {
		AppendMdx(entries, &items_);
		AppendDirs(entries, &items_);
	}
	AppendExtras(&items_);
}

// 一覧が揃ったあとの後始末。カーソルを決めてタイトルを読み始める。
void Filer::FinishLoad() {
	loading_ = false;
	loadingRow_ = false;

	if (resetCursor_) {
		topPx_ = 0;
		// 旧 mxv と同じく、開いた直後は 1 番目（".." の次）にカーソルを置く。
		// ファイルシステムの選択には ".." が無いので、そちらは先頭。
		cursor_ = (fs_ == 0) ? 0 : std::min((int)items_.size() - 1, 1);
	} else {
		cursor_ = keepCursor_;
		topPx_ = keepTopPx_;
	}
	if (cursor_ >= (int)items_.size()) cursor_ = (int)items_.size() - 1;
	if (cursor_ < 0) cursor_ = 0;

	StartReadTitles();

	// 読み込み中に頼まれていたカーソル合わせ。
	if (hasPendingSelect_) {
		const std::string want = pendingSelect_;
		hasPendingSelect_ = false;
		pendingSelect_.clear();
		SelectByPath(want);
	}
	EnsureCursorVisible();
}

bool Filer::PollDir() {
	DirLister::Result r;
	if (lister_->Take(&r)) {
		// 世代番号で古いものは捨てられているが、念のため場所も見る。
		if (loading_ && r.ref == currentRef_) {
			if (!r.ok) {
				// 読めないドライブや消えたフォルダ。元の場所へ戻す。
				RestoreView();
				return true;
			}
			BuildItems(r.entries);
			FinishLoad();
			return true;
		}
	}

	// 手間取っているときだけ「読み込み中」を足す。
	if (loading_ && !loadingRow_ && (SDL_GetTicks() - loadTicks_) >= kLoadingRowDelayMs) {
		FileItem f;
		f.title = Msg("Filer.Loading");
		f.type = kFileItemLoading;
		items_.push_back(f);
		loadingRow_ = true;
		return true;
	}
	return false;
}

// 先頭は必ず親ディレクトリ。ルートでは「ファイルシステムの選択」へ抜ける
// 行になる（旧 mxv はここが "\" で、押しても何も起きなかった）。
void Filer::AppendParentRow(std::vector<FileItem> *out) {
	FileItem f;
	const bool atRoot = fs_->IsRoot(rel_);
	f.baseName = atRoot ? "[FS]" : "..";
	f.path = atRoot ? std::string() : Vfs::MakeRef(fs_, fs_->Parent(rel_));
	f.title = fs_->DisplayPath(rel_);
	f.type = atRoot ? kFileItemFileSystem : kFileItemDir;
	out->push_back(f);
}

// ファイルシステムの選択。並び順は [ファイルシステムの設定] で決めたもの。
void Filer::AppendFileSystems(std::vector<FileItem> *out) {
	for (int i = 0; i < vfs_->count(); i++) {
		const FileSystem *fs = vfs_->at(i);
		if (!fs->available()) continue;  // Android のローカル FS など
		FileItem f;
		f.baseName = fs->prefix();
		f.title = fs->label();
		f.path = Vfs::MakeRef(fs, fs->Root());
		f.type = kFileItemFileSystem;
		out->push_back(f);
	}
	{
		FileItem f;
		f.baseName = "[Setting]";
		f.title = Msg("Filer.SettingTitle");
		f.type = kFileItemSetting;
		out->push_back(f);
	}
}

void Filer::AppendDirs(const std::vector<DirEntry> &entries, std::vector<FileItem> *out) {
	std::vector<FileItem> dirs;
	for (size_t i = 0; i < entries.size(); i++) {
		if (!entries[i].isDir) continue;
		FileItem f;
		f.baseName = entries[i].name;
		f.path = Vfs::MakeRef(fs_, fs_->Join(rel_, entries[i].name));
		f.type = kFileItemDir;
		dirs.push_back(f);
	}
	std::sort(dirs.begin(), dirs.end(), LessNoCase);
	out->insert(out->end(), dirs.begin(), dirs.end());
}

void Filer::AppendMdx(const std::vector<DirEntry> &entries, std::vector<FileItem> *out) {
	std::vector<FileItem> files;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].isDir) continue;
		// 一覧では拡張子だけで判断する（中身まで見ると全部読むことになる）。
		if (!IsMdxFileName(entries[i].name)) continue;
		FileItem f;
		f.baseName = entries[i].name;
		f.path = Vfs::MakeRef(fs_, fs_->Join(rel_, entries[i].name));
		f.type = kFileItemMdx;
		files.push_back(f);
	}
	std::sort(files.begin(), files.end(), LessNoCase);
	out->insert(out->end(), files.begin(), files.end());
}

// ファイルシステムが足す項目（ローカル FS のドライブ一覧）。
void Filer::AppendExtras(std::vector<FileItem> *out) {
	std::vector<FsExtraItem> extras;
	fs_->AppendExtraItems(rel_, &extras);
	for (size_t i = 0; i < extras.size(); i++) {
		FileItem f;
		f.baseName = extras[i].name;
		f.path = Vfs::MakeRef(fs_, extras[i].rel);
		f.type = kFileItemDrive;
		out->push_back(f);
	}
}

void Filer::StartReadTitles() {
	std::vector<TitleReader::Job> jobs;
	for (size_t i = 0; i < items_.size(); i++) {
		if ((items_[i].type & kFileItemMdx) == 0) continue;
		TitleReader::Job job;
		job.index = (int)i;
		job.ref = items_[i].path;
		jobs.push_back(job);
	}
	titles_->Start(vfs_, jobs);
}

void Filer::WaitIo() {
	titles_->Quiesce();
	lister_->Quiesce();
}

bool Filer::PollTitles() {
	std::vector<TitleReader::Result> got;
	if (!titles_->Take(&got)) return false;

	bool changed = false;
	for (size_t i = 0; i < got.size(); i++) {
		const int at = got[i].index;
		// 依頼したときの一覧に対する位置。世代が変わったぶんは捨てられて
		// いるので、ここへ来るのは今の一覧のものだけ。
		if (at < 0 || at >= (int)items_.size()) continue;
		if ((items_[at].type & kFileItemMdx) == 0) continue;
		if (items_[at].title == got[i].title) continue;
		items_[at].title = got[i].title;
		changed = true;
	}
	return changed;
}

void Filer::SetCursor(int i) {
	if (items_.empty()) {
		cursor_ = 0;
		return;
	}
	if (i < 0) i = 0;
	if (i >= (int)items_.size()) i = (int)items_.size() - 1;
	cursor_ = i;
	EnsureCursorVisible();
}

void Filer::MoveCursor(int delta) {
	SetCursor(cursor_ + delta);
}

int Filer::maxTopPx() const {
	const int maxTop = std::max(0, (int)items_.size() - visibleRows_);
	return maxTop * rowHeightPx_;
}

// 行単位の指定。端数は落とすので、キー移動・ホイール・スクロールバーは
// 必ず行の切れ目に揃う。
void Filer::SetTop(int t) {
	SetTopPx(t * rowHeightPx_);
}

void Filer::SetTopPx(int px) {
	const int maxPx = maxTopPx();
	if (px < 0) px = 0;
	if (px > maxPx) px = maxPx;
	topPx_ = px;
}

void Filer::SetViewMetrics(int rows, int rowHeightPx) {
	// 行の高さが変わると画素位置の意味も変わるので、今の先頭項目を保って
	// 測り直す（文字サイズの切り替えで表示が飛ばないように）。
	const int keepTop = top();
	visibleRows_ = (rows > 0) ? rows : 1;
	rowHeightPx_ = (rowHeightPx > 0) ? rowHeightPx : 1;
	SetTop(keepTop);
	EnsureCursorVisible();
}

void Filer::EnsureCursorVisible() {
	const int top = topPx_ / rowHeightPx_;
	if (cursor_ < top) {
		SetTop(cursor_);
	} else if (cursor_ >= top + visibleRows_) {
		SetTop(cursor_ - visibleRows_ + 1);
	} else if (topOffsetPx() != 0) {
		// 端数が残っていると先頭と末尾の行が欠けて見える。カーソルを
		// 動かしたときは行に揃え直す。
		SetTop(top);
	} else {
		SetTopPx(topPx_);  // 項目が減ったときの詰め直し
	}
}

FilerOpen Filer::Open(std::string *playPath) {
	playPath->clear();
	if (items_.empty()) return kFilerOpenNone;
	const FileItem f = items_[cursor_];  // SetCurrentRef が items_ を作り直す

	if (f.type & kFileItemMdx) {
		*playPath = f.path;
		return kFilerOpenPlay;
	}
	if (f.type & kFileItemSetting) {
		return kFilerOpenSettings;
	}
	if (f.type & kFileItemFileSystem) {
		// 選択画面の 1 行ならその FS のルートへ、ルートの "[FS]" なら
		// 選択画面へ（path が空）。
		const std::string leaving = currentRef_;
		SetCurrentRef(f.path);
		if (f.path.empty()) SelectByPath(leaving);
		return kFilerOpenMoved;
	}
	if (f.type & (kFileItemDir | kFileItemDrive)) {
		// 「そこを開けるか」は読み出しスレッドの List が兼ねる。開けなければ
		// PollDir が元の場所へ戻すので、ここで IsDir を呼ぶ必要はない
		// （呼ぶと遅いファイルシステムではその場で固まる）。
		SetCurrentRef(f.path);
		return kFilerOpenMoved;
	}
	return kFilerOpenNone;
}

void Filer::GoParent() {
	if (fs_ == 0) return;  // 選択画面より上は無い
	const std::string leaving = currentRef_;
	// ルートの 1 つ上は「ファイルシステムの選択」(ref が空)。
	SetCurrentRef(fs_->IsRoot(rel_) ? std::string()
	                                : Vfs::MakeRef(fs_, fs_->Parent(rel_)));
	// 出てきたディレクトリ（またはファイルシステム）にカーソルを合わせる
	SelectByPath(leaving);
}

void Filer::GoRoot() {
	if (fs_ == 0 || fs_->IsRoot(rel_)) return;
	// 旧 mxv はカレントを 3 文字 ("C:\") へ切り詰めていたが、それだと
	// Windows のドライブ名前提になる。変わらなくなるまで親を辿れば
	// 他のプラットフォームでも同じ場所に行き着く。
	std::string rel = rel_;
	for (int i = 0; i < 64; i++) {
		const std::string up = fs_->Parent(rel);
		if (up == rel) break;
		rel = up;
		if (fs_->IsRoot(rel)) break;
	}
	const std::string leaving = currentRef_;
	SetCurrentRef(Vfs::MakeRef(fs_, rel));
	SelectByPath(leaving);
}

bool Filer::SelectByPath(const std::string &ref) {
	if (vfs_ == 0) return false;
	// 一覧がまだ無いので、届いてから合わせる（SetCurrentRef の直後に
	// 呼ばれるのが普通なので、ここを外すと毎回外れてしまう）。
	if (loading_) {
		pendingSelect_ = ref;
		hasPendingSelect_ = true;
		return true;
	}
	FileSystem *wantFs = 0;
	std::string wantRel;
	if (!vfs_->Parse(ref, &wantFs, &wantRel) || wantFs == 0) return false;

	// 選択画面には ".." が無いので先頭から見る。そちらはファイルシステムが
	// 合っていればよい（どこから戻ってきても、その FS の行に合わせたい）。
	const bool picking = (fs_ == 0);
	for (size_t i = picking ? 0 : 1; i < items_.size(); i++) {
		FileSystem *haveFs = 0;
		std::string haveRel;
		if (!vfs_->Parse(items_[i].path, &haveFs, &haveRel)) continue;
		if (haveFs != wantFs) continue;
		if (!picking && !wantFs->SamePath(haveRel, wantRel)) continue;
		SetCursor((int)i);
		return true;
	}
	return false;
}

bool Filer::NextMdx(std::string *playPath) {
	for (int i = cursor_ + 1; i < (int)items_.size(); i++) {
		if (items_[i].type & kFileItemMdx) {
			SetCursor(i);
			*playPath = items_[i].path;
			return true;
		}
	}
	return false;
}

bool Filer::PrevMdx(std::string *playPath) {
	for (int i = cursor_ - 1; i >= 0; i--) {
		if (items_[i].type & kFileItemMdx) {
			SetCursor(i);
			*playPath = items_[i].path;
			return true;
		}
	}
	return false;
}

}  // namespace mxv2
