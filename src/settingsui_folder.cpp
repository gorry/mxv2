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

// 子フォルダの一覧だけ作り直す。毎フレーム読み直すと重いので、
// 行き先が変わったときだけ列挙する。
// dir が空のときはファイルシステムの選択（マウントされている FS が並ぶ）。
void SettingsUi::RelistFolder(const std::string &dir) {
	if (vfs_ == 0) return;

	FileSystem *fs = 0;
	std::string rel;
	if (!vfs_->Parse(dir, &fs, &rel)) return;
	// "Bookmarks>" の中からはファイルシステムの選択から始める（あちらは
	// フォルダの一覧ではないので、開いても空になるだけ）。
	if (fs != 0 && fs->isJumpList()) {
		fs = 0;
		rel.clear();
	}
	const std::string want = Vfs::MakeRef(fs, rel);

	// 読めなかったときに戻る先を控える。読み込み中の（まだ空の）姿は
	// 控えない（続けて打ち込まれたときに空へ戻ってしまう）。
	if (!folderLoading_) {
		folderPrevDir_ = folderDir_;
		folderPrevSelected_ = folderSelected_;
		folderPrevEntries_ = folderEntries_;
		folderHasPrev_ = true;
	}

	folderLoading_ = false;
	folderLister_->Cancel();
	folderDir_ = want;
	folderSelected_.clear();
	folderEntries_.clear();

	// ファイルシステムの選択。読み込みは要らない。
	if (fs == 0) {
		for (int i = 0; i < vfs_->count(); i++) {
			FileSystem *m = vfs_->at(i);
			if (!m->available()) continue;
			if (m->isJumpList()) continue;  // "Bookmarks>" はフォルダではない
			FolderEntry e;
			e.name = m->prefix();
			e.ref = Vfs::MakeRef(m, m->Root());
			folderEntries_.push_back(e);
		}
		return;
	}

	folderLoading_ = true;
	folderTicks_ = SDL_GetTicks();
	folderLister_->Start(vfs_, folderDir_);
}

// 別スレッドが読み終えた中身を一覧へ入れる。
void SettingsUi::PollFolderDir() {
	DirLister::Result r;
	if (!folderLister_->Take(&r)) return;
	if (!folderLoading_ || r.ref != folderDir_) return;

	folderLoading_ = false;

	if (!r.ok) {
		// 開けない場所（打ち込みの途中、読めないドライブ）。前の一覧に戻す。
		if (folderHasPrev_) {
			folderDir_ = folderPrevDir_;
			folderSelected_ = folderPrevSelected_;
			folderEntries_ = folderPrevEntries_;
		}
		return;
	}

	FileSystem *fs = 0;
	std::string rel;
	if (vfs_ == 0 || !vfs_->Parse(folderDir_, &fs, &rel) || fs == 0) return;

	folderEntries_.clear();
	std::vector<std::string> names;
	for (size_t i = 0; i < r.entries.size(); i++) {
		if (r.entries[i].isDir) names.push_back(r.entries[i].name);
	}
	std::sort(names.begin(), names.end(), LessPathNoCase);
	for (size_t i = 0; i < names.size(); i++) {
		FolderEntry e;
		e.name = names[i];
		e.ref = Vfs::MakeRef(fs, fs->Join(rel, names[i]));
		folderEntries_.push_back(e);
	}

	// ファイルシステムが足すもの（ローカル FS のドライブ一覧）。
	std::vector<FsExtraItem> extras;
	fs->AppendExtraItems(rel, &extras);
	for (size_t i = 0; i < extras.size(); i++) {
		FolderEntry e;
		e.name = extras[i].name;
		e.ref = Vfs::MakeRef(fs, extras[i].rel);
		folderEntries_.push_back(e);
	}
}

// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
void SettingsUi::SetFolderDir(const std::string &dir) {
	RelistFolder(dir);
	// folderDir_ は読み込みを始めた時点で行き先になっている（中身だけが
	// あとから届く）ので、入力欄はそのまま合わせてよい。
	snprintf(folderPathBuf_, sizeof(folderPathBuf_), "%s", folderDir_.c_str());
	folderError_.clear();
}

// 一覧で選んだものを入力欄へ移すだけ。中へは入らない。
// メイン画面のファイラーと同じで、クリックは選ぶだけ・ダブルクリックで移動。
void SettingsUi::SelectFolderEntry(const std::string &path) {
	folderSelected_ = path;
	snprintf(folderPathBuf_, sizeof(folderPathBuf_), "%s", path.c_str());
	folderError_.clear();
}

const char *SettingsUi::folderTitle() const {
	if (folderTarget_ == kFolderTargetPdx) return kPdxFolderTitle;
	if (folderTarget_ == kFolderTargetBookmark) return kBookmarkFolderTitle;
	return kFolderTitle;
}

void SettingsUi::BuildFolderWindow(Settings *settings, Filer *filer) {
	const char *title = folderTitle();
	if (!SyncModal(title, &showFolder_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 400.0f), placeCond());

	if (!ImGui::BeginPopupModal(title, &showFolder_, DialogFlags())) {
		return;
	}

	// 一覧は別スレッドが読んでいる。届いていれば取り込む。
	PollFolderDir();

	// パスは直接打ってもよい。ENTER は「開く」と同じ扱い。
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##folderpath", folderPathBuf_, sizeof(folderPathBuf_),
	                              ImGuiInputTextFlags_EnterReturnsTrue);

	// 打ち込んだ先が実在するフォルダなら、下の一覧もそこへ合わせる。
	// 入力欄そのものは書き換えない。打っている最中に正規化された文字列で
	// 差し替えると、カーソルごと飛んでしまって打てなくなる。
	if (ImGui::IsItemEdited()) {
		const std::string typed = folderPathBuf_;
		std::string ref;
		// 打ち込みの途中は開けない場所を通るが、**それを確かめるのも
		// 別スレッドの仕事**（ここで IsDir を呼ぶと 1 打鍵ごとに固まる）。
		// 開けなければ PollFolderDir が前の一覧に戻す。
		if (!typed.empty() && vfs_ != 0 && vfs_->Resolve(typed, folderDir_, &ref) &&
		    ref != folderDir_) {
			RelistFolder(ref);
		}
		folderError_.clear();
	}

	// 親へ戻るのは一覧の ".." が受け持つので、専用のボタンは置かない。
	// この行は、下の一覧がどこを出しているのかを常に知らせる。入力欄と
	// 同じことも多いが、一覧で選んだだけのときや打ち込みの途中は食い違う。
	// 見出しは付けず、上の入力欄と桁を揃える。入力欄の文字は枠の内側に
	// FramePadding のぶん寄っているので、こちらも同じだけ下げる。
	// ファイルシステムのルートの 1 つ上は「ファイルシステムの選択」(ref は空)。
	const std::string up = (vfs_ != 0) ? vfs_->Parent(folderDir_) : std::string();
	const bool hasUp = !folderDir_.empty();
	{
		const float inset = ImGui::GetStyle().FramePadding.x;
		ImGui::Indent(inset);
		TextNote(folderDir_.empty() ? Msg("Folder.FileSystem") : folderDir_.c_str());
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

		if (hasUp && Row::Hit("..", up, folderSelected_, &entered)) {
			pick = up;
			picked = true;
		}
		for (size_t i = 0; i < folderEntries_.size(); i++) {
			if (Row::Hit(folderEntries_[i].name.c_str(), folderEntries_[i].ref,
			             folderSelected_, &entered)) {
				pick = folderEntries_[i].ref;
				picked = true;
			}
		}

		// 手間取っているときだけ知らせる。ローカルのフォルダは一瞬で
		// 届くので、すぐ出すとちらつくだけになる。
		if (folderLoading_ && (SDL_GetTicks() - folderTicks_) >= kLoadingDelayMs) {
			ImGui::TextDisabled("%s", Msg("Filer.Loading"));
		}

		// ドラッグでスクロールした指を離したときは、押した行を選ばない。
		// 中身は指に付いて動くので、離した先には押した行がそのまま居る。
		// これを拾ってしまうと「スクロールしたつもりが選択された」になる。
		if (picked && !dragMoved_) {
			if (entered) {
				nextDir = pick;
				nextDirValid = true;
			} else {
				SelectFolderEntry(pick);
			}
		}

		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (folderError_.empty()) {
		TextNote(Msg("Folder.Hint"));
	} else {
		TextError(folderError_.c_str());
	}
	if (ImGui::Button(Msg("Button.Open"))) apply = true;
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel"))) showFolder_ = false;

	if (nextDirValid) {
		SetFolderDir(nextDir);
	} else if (apply) {
		const std::string want = folderPathBuf_;
		std::string ref;
		const bool ok = (vfs_ != 0) && vfs_->Resolve(want, folderDir_, &ref);
		// ref が空なら「ファイルシステムの選択」。ファイラーは行けるが、
		// PDX の探索先には指定できない。
		// 今一覧に出している場所なら読めているのが分かっているので、
		// IsDir は省く（遅いファイルシステムでの往復を 1 回減らす）。
		const bool known = ok && (ref == folderDir_) && !folderLoading_;
		if (!ok || (!ref.empty() && !known && !vfs_->IsDir(ref))) {
			folderError_ = Msg("Folder.NotFound");
		} else if (folderTarget_ == kFolderTargetPdx) {
			// PDX の探索先に足す。ルート（ファイルシステムの選択）と "Bookmarks>"
			// 自身、入れてある場所は入れられない。挿す位置は [PDX の探索先] で
			// 選んでいた行（無ければ末尾）。
			std::vector<std::string> &list = settings->pdxPaths;
			if (!CanBookmark(ref)) {
				folderError_ = Msg("PdxPath.AddRoot");
			} else if (FindBookmark(list, ref) >= 0) {
				folderError_ = MsgF("PdxPath.AddDuplicate", vfs_->DisplayPath(ref));
			} else if ((int)list.size() >= Settings::kMaxPdxPaths) {
				folderError_ = Msg("PdxPath.AddFull");
			} else {
				const int count = (int)list.size();
				const int at = (pdxSelected_ >= 0 && pdxSelected_ <= count) ? pdxSelected_ : count;
				list.insert(list.begin() + at, ref);
				pdxSelected_ = at;
				changedFields_ |= Settings::kFieldPdxPaths;
				showFolder_ = false;
			}
		} else if (folderTarget_ == kFolderTargetBookmark) {
			// ブックマークに足す。ルート（ファイルシステムの選択）と
			// "Bookmarks>" 自身、控え済みの場所は入れられない。挿す位置は
			// [ブックマークの設定] で選んでいた行（無ければ末尾）。
			std::vector<std::string> &list = settings->bookmarks;
			if (!CanBookmark(ref)) {
				folderError_ = Msg("Bookmark.AddRoot");
			} else if (FindBookmark(list, ref) >= 0) {
				folderError_ = MsgF("Bookmark.AddDuplicate", vfs_->DisplayPath(ref));
			} else if ((int)list.size() >= Settings::kMaxBookmarks) {
				folderError_ = Msg("Bookmark.AddFull");
			} else {
				const int count = (int)list.size();
				const int at = (bmSelected_ >= 0 && bmSelected_ <= count) ? bmSelected_ : count;
				list.insert(list.begin() + at, ref);
				bmSelected_ = at;
				changedFields_ |= Settings::kFieldBookmarks;
				showFolder_ = false;
			}
		} else {
			// ファイラーを動かすのはメインループの持ち物なので、要求だけ積む。
			requestedFolder_ = ref;
			request_ = kRequestSetFolder;
			showFolder_ = false;
		}
	}

	// このダイアログのスクロールは中の一覧が受け持つので、ここでは呼ばない。
	// 同じ dragScroll_ を二重に使うと、一覧を掴んだ状態がここへ漏れる。
	ImGui::EndPopup();
}

}  // namespace mxv2
