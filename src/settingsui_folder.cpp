// mxv2 - 設定 UI: フォルダを選ぶダイアログ (L)
//
// settingsui.cpp から切り出した。一覧は DirLister が別スレッドで読む。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "dirlister.h"
#include "fileutil.h"
#include "filer.h"
#include "screen.h"
#include "settings.h"
#include "vfs.h"

namespace mxv2 {

using namespace settingsui;

namespace {

// 「読み込み中」を出すまでの待ち時間 (ms)。filer.cpp と同じ考え方。
const uint32_t kLoadingDelayMs = 250;

// フォルダ名を並べるときの順。ファイラー (filer.cpp) と同じ規則。
bool LessPathNoCase(const std::string &a, const std::string &b) {
	return CompareNoCase(a, b) < 0;
}

}  // namespace

FolderDialog::FolderDialog()
    : target_(Target::Filer), returnTo_(Return::None), openPending_(false),
      lister_(new DirLister()), loading_(false), ticks_(0), hasPrev_(false) {
	pathBuf_[0] = '\0';
}

FolderDialog::~FolderDialog() {
	delete lister_;
}

void FolderDialog::Quiesce() {
	if (lister_ != 0) lister_->Quiesce();
}

void FolderDialog::Open(SettingsUi &ui, const std::string &dir) {
	target_ = Target::Filer;
	returnTo_ = Return::None;
	SetDir(ui, dir);
	show_ = true;
}

// 子フォルダの一覧だけ作り直す。毎フレーム読み直すと重いので、
// 行き先が変わったときだけ列挙する。
// dir が空のときはファイルシステムの選択（マウントされている FS が並ぶ）。
void FolderDialog::Relist(SettingsUi &ui, const std::string &dir) {
	if (ui.vfs_ == 0) return;

	FileSystem *fs = 0;
	std::string rel;
	if (!ui.vfs_->Parse(dir, &fs, &rel)) return;
	// "Bookmarks>" の中からはファイルシステムの選択から始める（あちらは
	// フォルダの一覧ではないので、開いても空になるだけ）。
	if (fs != 0 && fs->isJumpList()) {
		fs = 0;
		rel.clear();
	}
	const std::string want = Vfs::MakeRef(fs, rel);

	// 読めなかったときに戻る先を控える。読み込み中の（まだ空の）姿は
	// 控えない（続けて打ち込まれたときに空へ戻ってしまう）。
	if (!loading_) {
		prevDir_ = dir_;
		prevSelected_ = selected_;
		prevEntries_ = entries_;
		hasPrev_ = true;
	}

	loading_ = false;
	lister_->Cancel();
	dir_ = want;
	selected_.clear();
	entries_.clear();

	// ファイルシステムの選択。読み込みは要らない。
	if (fs == 0) {
		for (int i = 0; i < ui.vfs_->count(); i++) {
			FileSystem *m = ui.vfs_->at(i);
			if (!m->available()) continue;
			if (m->isJumpList()) continue;  // "Bookmarks>" はフォルダではない
			FolderEntry e;
			// 種類 ("SAF>") だけだと、同じ種類が 2 つあるときに見分けが付かない
			// （SAF のフォルダを 2 つ足したとき。2026-09-30）。[ファイルシステムの
			// 設定] と同じく名前を添える。
			e.name = std::string(m->prefix()) + "  " + m->label();
			e.ref = Vfs::MakeRef(m, m->Root());
			entries_.push_back(e);
		}
		return;
	}

	loading_ = true;
	ticks_ = SDL_GetTicks();
	lister_->Start(ui.vfs_, dir_);
}

// 別スレッドが読み終えた中身を一覧へ入れる。
void FolderDialog::PollDir(SettingsUi &ui) {
	DirLister::Result r;
	if (!lister_->Take(&r)) return;
	if (!loading_ || r.ref != dir_) return;

	loading_ = false;

	if (!r.ok) {
		// 開けない場所（打ち込みの途中、読めないドライブ）。前の一覧に戻す。
		if (hasPrev_) {
			dir_ = prevDir_;
			selected_ = prevSelected_;
			entries_ = prevEntries_;
		}
		return;
	}

	FileSystem *fs = 0;
	std::string rel;
	if (ui.vfs_ == 0 || !ui.vfs_->Parse(dir_, &fs, &rel) || fs == 0) return;

	entries_.clear();
	std::vector<std::string> names;
	for (size_t i = 0; i < r.entries.size(); i++) {
		if (r.entries[i].isDir) names.push_back(r.entries[i].name);
	}
	std::sort(names.begin(), names.end(), LessPathNoCase);
	for (size_t i = 0; i < names.size(); i++) {
		FolderEntry e;
		e.name = names[i];
		e.ref = Vfs::MakeRef(fs, fs->Join(rel, names[i]));
		entries_.push_back(e);
	}

	// ファイルシステムが足すもの（ローカル FS のドライブ一覧）。
	std::vector<FsExtraItem> extras;
	fs->AppendExtraItems(rel, &extras);
	for (size_t i = 0; i < extras.size(); i++) {
		FolderEntry e;
		e.name = extras[i].name;
		e.ref = Vfs::MakeRef(fs, extras[i].rel);
		entries_.push_back(e);
	}
}

// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
void FolderDialog::SetDir(SettingsUi &ui, const std::string &dir) {
	Relist(ui, dir);
	// folder_.dir_ は読み込みを始めた時点で行き先になっている（中身だけが
	// あとから届く）ので、入力欄はそのまま合わせてよい。
	snprintf(pathBuf_, sizeof(pathBuf_), "%s", dir_.c_str());
	error_.clear();
}

// 一覧で選んだものを入力欄へ移すだけ。中へは入らない。
// メイン画面のファイラーと同じで、クリックは選ぶだけ・ダブルクリックで移動。
void FolderDialog::SelectEntry(const std::string &path) {
	selected_ = path;
	snprintf(pathBuf_, sizeof(pathBuf_), "%s", path.c_str());
	error_.clear();
}

const char *FolderDialog::Title() const {
	if (target_ == Target::Pdx) return kPdxFolderTitle;
	if (target_ == Target::Bookmark) return kBookmarkFolderTitle;
	return kFolderTitle;
}

void FolderDialog::Build(SettingsUi &ui, Settings *settings, Filer *filer) {
	const char *title = Title();
	if (!ui.BeginModal(title, &show_, 460.0f, 400.0f)) return;

	// 一覧は別スレッドが読んでいる。届いていれば取り込む。
	PollDir(ui);

	// パスは直接打ってもよい。ENTER は「開く」と同じ扱い。
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##folderpath", pathBuf_, sizeof(pathBuf_),
	                              ImGuiInputTextFlags_EnterReturnsTrue);

	// 打ち込んだ先が実在するフォルダなら、下の一覧もそこへ合わせる。
	// 入力欄そのものは書き換えない。打っている最中に正規化された文字列で
	// 差し替えると、カーソルごと飛んでしまって打てなくなる。
	if (ImGui::IsItemEdited()) {
		const std::string typed = pathBuf_;
		std::string ref;
		// 打ち込みの途中は開けない場所を通るが、**それを確かめるのも
		// 別スレッドの仕事**（ここで IsDir を呼ぶと 1 打鍵ごとに固まる）。
		// 開けなければ PollFolderDir が前の一覧に戻す。
		if (!typed.empty() && ui.vfs_ != 0 && ui.vfs_->Resolve(typed, dir_, &ref) &&
		    ref != dir_) {
			Relist(ui, ref);
		}
		error_.clear();
	}

	// 親へ戻るのは一覧の ".." が受け持つので、専用のボタンは置かない。
	// この行は、下の一覧がどこを出しているのかを常に知らせる。入力欄と
	// 同じことも多いが、一覧で選んだだけのときや打ち込みの途中は食い違う。
	// 見出しは付けず、上の入力欄と桁を揃える。入力欄の文字は枠の内側に
	// FramePadding のぶん寄っているので、こちらも同じだけ下げる。
	// ファイルシステムのルートの 1 つ上は「ファイルシステムの選択」(ref は空)。
	const std::string up = (ui.vfs_ != 0) ? ui.vfs_->Parent(dir_) : std::string();
	const bool hasUp = !dir_.empty();
	{
		const float inset = ImGui::GetStyle().FramePadding.x;
		ImGui::Indent(inset);
		TextNote(dir_.empty() ? Msg("Folder.FileSystem") : dir_.c_str());
		ImGui::Unindent(inset);
	}

	// 子フォルダの一覧。クリックで選ぶだけ、ダブルクリックでその中へ入る。
	// メイン画面のファイラーと同じ操作感にしてある。
	// 一覧の作り直しは回している最中にやってはいけないので、行き先を
	// 控えてから動かす。
	std::string nextDir;
	bool nextDirValid = false;
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##folderlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);

		// 1 行ぶんの処理。AllowDoubleClick を付けると 1 回目のクリックでも
		// true が返るので、ダブルクリックかどうかを自分で見分ける。
		struct Row {
			static bool Hit(const char *label, const std::string &path,
			                const std::string &selected, bool *entered) {
				const bool on = (!selected.empty() && CompareNoCase(selected, path) == 0);
				if (!ImGui::Selectable(label, on, ImGuiSelectableFlags_AllowDoubleClick)) {
					return false;
				}
				*entered = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				return true;
			}
		};

		std::string pick;      // 選ばれたもの (ref)
		bool picked = false;   // ref は空にもなりうるので、有無は別に持つ
		bool entered = false;  // ダブルクリックだったか

		if (hasUp && Row::Hit("..", up, selected_, &entered)) {
			pick = up;
			picked = true;
		}
		for (size_t i = 0; i < entries_.size(); i++) {
			// 名札が同じ行が 2 つあると ImGui の ID がぶつかり、「2 visible items
			// with conflicting ID」のエラーが出る（同じ種類のファイルシステムが
			// 2 つあったとき。2026-09-30、r77）。行の番号で ID を分けておく。
			char label[600];
			snprintf(label, sizeof(label), "%s##folder%d", entries_[i].name.c_str(), (int)i);
			if (Row::Hit(label, entries_[i].ref, selected_, &entered)) {
				pick = entries_[i].ref;
				picked = true;
			}
		}

		// 手間取っているときだけ知らせる。ローカルのフォルダは一瞬で
		// 届くので、すぐ出すとちらつくだけになる。
		if (loading_ && (SDL_GetTicks() - ticks_) >= kLoadingDelayMs) {
			ImGui::TextDisabled("%s", Msg("Filer.Loading"));
		}

		// ドラッグでスクロールした指を離したときは、押した行を選ばない。
		// 中身は指に付いて動くので、離した先には押した行がそのまま居る。
		// これを拾ってしまうと「スクロールしたつもりが選択された」になる。
		if (picked && !ui.dragMoved_) {
			if (entered) {
				nextDir = pick;
				nextDirValid = true;
			} else {
				SelectEntry(pick);
			}
		}

		DragToScroll(&ui.dragScroll_, &ui.dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (error_.empty()) {
		TextNote(Msg("Folder.Hint"));
	} else {
		TextError(error_.c_str());
	}
	if (ImGui::Button(Msg("Button.Open"))) apply = true;
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel"))) show_ = false;

	if (nextDirValid) {
		SetDir(ui, nextDir);
	} else if (apply) {
		const std::string want = pathBuf_;
		std::string ref;
		const bool ok = (ui.vfs_ != 0) && ui.vfs_->Resolve(want, dir_, &ref);
		// ref が空なら「ファイルシステムの選択」。ファイラーは行けるが、
		// PDX の探索先には指定できない。
		// 今一覧に出している場所なら読めているのが分かっているので、
		// IsDir は省く（遅いファイルシステムでの往復を 1 回減らす）。
		const bool known = ok && (ref == dir_) && !loading_;
		if (!ok || (!ref.empty() && !known && !ui.vfs_->IsDir(ref))) {
			error_ = Msg("Folder.NotFound");
		} else if (target_ == Target::Pdx) {
			// PDX の探索先に足す。ルート（ファイルシステムの選択）と "Bookmarks>"
			// 自身、入れてある場所は入れられない。挿す位置は [PDX の探索先] で
			// 選んでいた行（無ければ末尾）。
			std::vector<std::string> &list = settings->pdxPaths;
			if (!ui.CanBookmark(ref)) {
				error_ = Msg("PdxPath.AddRoot");
			} else if (ui.FindBookmark(list, ref) >= 0) {
				error_ = MsgF("PdxPath.AddDuplicate", ui.vfs_->DisplayPath(ref));
			} else if ((int)list.size() >= Settings::kMaxPdxPaths) {
				error_ = Msg("PdxPath.AddFull");
			} else {
				ui.pdx_.Insert(&list, ref);
				ui.changedFields_ |= Settings::Field::PdxPaths;
				show_ = false;
			}
		} else if (target_ == Target::Bookmark) {
			// ブックマークに足す。ルート（ファイルシステムの選択）と
			// "Bookmarks>" 自身、控え済みの場所は入れられない。挿す位置は
			// [ブックマークの設定] で選んでいた行（無ければ末尾）。
			std::vector<std::string> &list = settings->bookmarks;
			if (!ui.CanBookmark(ref)) {
				error_ = Msg("Bookmark.AddRoot");
			} else if (ui.FindBookmark(list, ref) >= 0) {
				error_ = MsgF("Bookmark.AddDuplicate", ui.vfs_->DisplayPath(ref));
			} else if ((int)list.size() >= Settings::kMaxBookmarks) {
				error_ = Msg("Bookmark.AddFull");
			} else {
				ui.bm_.Insert(&list, ref);
				ui.changedFields_ |= Settings::Field::Bookmarks;
				show_ = false;
			}
		} else {
			// ファイラーを動かすのはメインループの持ち物なので、要求だけ積む。
			ui.requestedFolder_ = ref;
			ui.request_ = SettingsUi::Request::SetFolder;
			show_ = false;
		}
	}

	// このダイアログのスクロールは中の一覧が受け持つので、ここでは呼ばない。
	// 同じ dragScroll_ を二重に使うと、一覧を掴んだ状態がここへ漏れる。
	ImGui::EndPopup();
}

}  // namespace mxv2
