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
	if (!folder_.loading) {
		folder_.prevDir = folder_.dir;
		folder_.prevSelected = folder_.selected;
		folder_.prevEntries = folder_.entries;
		folder_.hasPrev = true;
	}

	folder_.loading = false;
	folder_.lister->Cancel();
	folder_.dir = want;
	folder_.selected.clear();
	folder_.entries.clear();

	// ファイルシステムの選択。読み込みは要らない。
	if (fs == 0) {
		for (int i = 0; i < vfs_->count(); i++) {
			FileSystem *m = vfs_->at(i);
			if (!m->available()) continue;
			if (m->isJumpList()) continue;  // "Bookmarks>" はフォルダではない
			FolderEntry e;
			// 種類 ("SAF>") だけだと、同じ種類が 2 つあるときに見分けが付かない
			// （SAF のフォルダを 2 つ足したとき。2026-09-30）。[ファイルシステムの
			// 設定] と同じく名前を添える。
			e.name = std::string(m->prefix()) + "  " + m->label();
			e.ref = Vfs::MakeRef(m, m->Root());
			folder_.entries.push_back(e);
		}
		return;
	}

	folder_.loading = true;
	folder_.ticks = SDL_GetTicks();
	folder_.lister->Start(vfs_, folder_.dir);
}

// 別スレッドが読み終えた中身を一覧へ入れる。
void SettingsUi::PollFolderDir() {
	DirLister::Result r;
	if (!folder_.lister->Take(&r)) return;
	if (!folder_.loading || r.ref != folder_.dir) return;

	folder_.loading = false;

	if (!r.ok) {
		// 開けない場所（打ち込みの途中、読めないドライブ）。前の一覧に戻す。
		if (folder_.hasPrev) {
			folder_.dir = folder_.prevDir;
			folder_.selected = folder_.prevSelected;
			folder_.entries = folder_.prevEntries;
		}
		return;
	}

	FileSystem *fs = 0;
	std::string rel;
	if (vfs_ == 0 || !vfs_->Parse(folder_.dir, &fs, &rel) || fs == 0) return;

	folder_.entries.clear();
	std::vector<std::string> names;
	for (size_t i = 0; i < r.entries.size(); i++) {
		if (r.entries[i].isDir) names.push_back(r.entries[i].name);
	}
	std::sort(names.begin(), names.end(), LessPathNoCase);
	for (size_t i = 0; i < names.size(); i++) {
		FolderEntry e;
		e.name = names[i];
		e.ref = Vfs::MakeRef(fs, fs->Join(rel, names[i]));
		folder_.entries.push_back(e);
	}

	// ファイルシステムが足すもの（ローカル FS のドライブ一覧）。
	std::vector<FsExtraItem> extras;
	fs->AppendExtraItems(rel, &extras);
	for (size_t i = 0; i < extras.size(); i++) {
		FolderEntry e;
		e.name = extras[i].name;
		e.ref = Vfs::MakeRef(fs, extras[i].rel);
		folder_.entries.push_back(e);
	}
}

// 一覧に出すフォルダを決めて、入力欄もそこへ合わせる。
void SettingsUi::SetFolderDir(const std::string &dir) {
	RelistFolder(dir);
	// folder_.dir は読み込みを始めた時点で行き先になっている（中身だけが
	// あとから届く）ので、入力欄はそのまま合わせてよい。
	snprintf(folder_.pathBuf, sizeof(folder_.pathBuf), "%s", folder_.dir.c_str());
	folder_.error.clear();
}

// 一覧で選んだものを入力欄へ移すだけ。中へは入らない。
// メイン画面のファイラーと同じで、クリックは選ぶだけ・ダブルクリックで移動。
void SettingsUi::SelectFolderEntry(const std::string &path) {
	folder_.selected = path;
	snprintf(folder_.pathBuf, sizeof(folder_.pathBuf), "%s", path.c_str());
	folder_.error.clear();
}

const char *SettingsUi::folderTitle() const {
	if (folder_.target == kFolderTargetPdx) return kPdxFolderTitle;
	if (folder_.target == kFolderTargetBookmark) return kBookmarkFolderTitle;
	return kFolderTitle;
}

void SettingsUi::BuildFolderWindow(Settings *settings, Filer *filer) {
	const char *title = folderTitle();
	if (!SyncModal(title, &folder_.show)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 400.0f), placeCond());

	if (!BeginDialog(title, &folder_.show, DialogFlags())) {
		return;
	}

	// 一覧は別スレッドが読んでいる。届いていれば取り込む。
	PollFolderDir();

	// パスは直接打ってもよい。ENTER は「開く」と同じ扱い。
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##folderpath", folder_.pathBuf, sizeof(folder_.pathBuf),
	                              ImGuiInputTextFlags_EnterReturnsTrue);

	// 打ち込んだ先が実在するフォルダなら、下の一覧もそこへ合わせる。
	// 入力欄そのものは書き換えない。打っている最中に正規化された文字列で
	// 差し替えると、カーソルごと飛んでしまって打てなくなる。
	if (ImGui::IsItemEdited()) {
		const std::string typed = folder_.pathBuf;
		std::string ref;
		// 打ち込みの途中は開けない場所を通るが、**それを確かめるのも
		// 別スレッドの仕事**（ここで IsDir を呼ぶと 1 打鍵ごとに固まる）。
		// 開けなければ PollFolderDir が前の一覧に戻す。
		if (!typed.empty() && vfs_ != 0 && vfs_->Resolve(typed, folder_.dir, &ref) &&
		    ref != folder_.dir) {
			RelistFolder(ref);
		}
		folder_.error.clear();
	}

	// 親へ戻るのは一覧の ".." が受け持つので、専用のボタンは置かない。
	// この行は、下の一覧がどこを出しているのかを常に知らせる。入力欄と
	// 同じことも多いが、一覧で選んだだけのときや打ち込みの途中は食い違う。
	// 見出しは付けず、上の入力欄と桁を揃える。入力欄の文字は枠の内側に
	// FramePadding のぶん寄っているので、こちらも同じだけ下げる。
	// ファイルシステムのルートの 1 つ上は「ファイルシステムの選択」(ref は空)。
	const std::string up = (vfs_ != 0) ? vfs_->Parent(folder_.dir) : std::string();
	const bool hasUp = !folder_.dir.empty();
	{
		const float inset = ImGui::GetStyle().FramePadding.x;
		ImGui::Indent(inset);
		TextNote(folder_.dir.empty() ? Msg("Folder.FileSystem") : folder_.dir.c_str());
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

		if (hasUp && Row::Hit("..", up, folder_.selected, &entered)) {
			pick = up;
			picked = true;
		}
		for (size_t i = 0; i < folder_.entries.size(); i++) {
			// 名札が同じ行が 2 つあると ImGui の ID がぶつかり、「2 visible items
			// with conflicting ID」のエラーが出る（同じ種類のファイルシステムが
			// 2 つあったとき。2026-09-30、r77）。行の番号で ID を分けておく。
			char label[600];
			snprintf(label, sizeof(label), "%s##folder%d", folder_.entries[i].name.c_str(), (int)i);
			if (Row::Hit(label, folder_.entries[i].ref, folder_.selected, &entered)) {
				pick = folder_.entries[i].ref;
				picked = true;
			}
		}

		// 手間取っているときだけ知らせる。ローカルのフォルダは一瞬で
		// 届くので、すぐ出すとちらつくだけになる。
		if (folder_.loading && (SDL_GetTicks() - folder_.ticks) >= kLoadingDelayMs) {
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

	if (folder_.error.empty()) {
		TextNote(Msg("Folder.Hint"));
	} else {
		TextError(folder_.error.c_str());
	}
	if (ImGui::Button(Msg("Button.Open"))) apply = true;
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel"))) folder_.show = false;

	if (nextDirValid) {
		SetFolderDir(nextDir);
	} else if (apply) {
		const std::string want = folder_.pathBuf;
		std::string ref;
		const bool ok = (vfs_ != 0) && vfs_->Resolve(want, folder_.dir, &ref);
		// ref が空なら「ファイルシステムの選択」。ファイラーは行けるが、
		// PDX の探索先には指定できない。
		// 今一覧に出している場所なら読めているのが分かっているので、
		// IsDir は省く（遅いファイルシステムでの往復を 1 回減らす）。
		const bool known = ok && (ref == folder_.dir) && !folder_.loading;
		if (!ok || (!ref.empty() && !known && !vfs_->IsDir(ref))) {
			folder_.error = Msg("Folder.NotFound");
		} else if (folder_.target == kFolderTargetPdx) {
			// PDX の探索先に足す。ルート（ファイルシステムの選択）と "Bookmarks>"
			// 自身、入れてある場所は入れられない。挿す位置は [PDX の探索先] で
			// 選んでいた行（無ければ末尾）。
			std::vector<std::string> &list = settings->pdxPaths;
			if (!CanBookmark(ref)) {
				folder_.error = Msg("PdxPath.AddRoot");
			} else if (FindBookmark(list, ref) >= 0) {
				folder_.error = MsgF("PdxPath.AddDuplicate", vfs_->DisplayPath(ref));
			} else if ((int)list.size() >= Settings::kMaxPdxPaths) {
				folder_.error = Msg("PdxPath.AddFull");
			} else {
				InsertPdxPath(&list, ref);
				changedFields_ |= Settings::kFieldPdxPaths;
				folder_.show = false;
			}
		} else if (folder_.target == kFolderTargetBookmark) {
			// ブックマークに足す。ルート（ファイルシステムの選択）と
			// "Bookmarks>" 自身、控え済みの場所は入れられない。挿す位置は
			// [ブックマークの設定] で選んでいた行（無ければ末尾）。
			std::vector<std::string> &list = settings->bookmarks;
			if (!CanBookmark(ref)) {
				folder_.error = Msg("Bookmark.AddRoot");
			} else if (FindBookmark(list, ref) >= 0) {
				folder_.error = MsgF("Bookmark.AddDuplicate", vfs_->DisplayPath(ref));
			} else if ((int)list.size() >= Settings::kMaxBookmarks) {
				folder_.error = Msg("Bookmark.AddFull");
			} else {
				InsertBookmark(&list, ref);
				changedFields_ |= Settings::kFieldBookmarks;
				folder_.show = false;
			}
		} else {
			// ファイラーを動かすのはメインループの持ち物なので、要求だけ積む。
			requestedFolder_ = ref;
			request_ = kRequestSetFolder;
			folder_.show = false;
		}
	}

	// このダイアログのスクロールは中の一覧が受け持つので、ここでは呼ばない。
	// 同じ dragScroll_ を二重に使うと、一覧を掴んだ状態がここへ漏れる。
	ImGui::EndPopup();
}

}  // namespace mxv2
