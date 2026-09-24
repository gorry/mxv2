// mxv2 - 設定 UI: [ブックマークの設定] ダイアログ (F4) と Shift+M の確認
//
// settingsui.cpp から切り出した。ジャンプ専用の一覧はファイラーの
// "Bookmarks>" (M)。控えるのはフォルダの ref だけ。実体は Settings::bookmarks で、
// 触ったら kFieldBookmarks を立ててメインループに ini へ書き戻してもらう。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "filer.h"
#include "settings.h"
#include "vfs.h"

namespace mxv2 {

using namespace settingsui;

int SettingsUi::FindBookmark(const std::vector<std::string> &list,
                             const std::string &ref) const {
	if (vfs_ == 0 || ref.empty()) return -1;
	for (size_t i = 0; i < list.size(); i++) {
		if (vfs_->SameRef(list[i], ref)) return (int)i;
	}
	return -1;
}

// ブックマークを開く。控えた先がファイルだったときは「そのファイルのある
// フォルダ」へ直し、控えの方も書き換えてから開く（仕様どおり、直した結果が
// 他と重なったら相手側を消す）。開けなければ bmError_ に理由を入れて残る。
void SettingsUi::OpenBookmark(Settings *settings, int index) {
	std::vector<std::string> &list = settings->bookmarks;
	if (vfs_ == 0 || index < 0 || index >= (int)list.size()) return;

	std::string ref = list[index];
	// 行き先のファイルシステムのアクセス許可が失われている（SAF）ときは、
	// 「見つかりません」ではなくその旨を出す（控えは消さない）。
	{
		FileSystem *fs = 0;
		std::string rel;
		if (vfs_->Parse(ref, &fs, &rel) && fs != 0 && !fs->accessible()) {
			bmError_ = MsgF("FileSystems.NoAccessNote", fs->label());
			return;
		}
	}
	if (!vfs_->IsDir(ref)) {
		const std::string parent = vfs_->Parent(ref);
		if (!vfs_->Exists(ref) || parent.empty() || !vfs_->IsDir(parent)) {
			bmError_ = Msg("Bookmark.NotFound");
			return;
		}
		list[index] = parent;
		// 重複したら相手側を消す。消したのが上の行なら、こちらの位置も繰り上がる。
		for (int i = (int)list.size() - 1; i >= 0; i--) {
			if (i == index) continue;
			if (!vfs_->SameRef(list[i], parent)) continue;
			list.erase(list.begin() + i);
			if (i < index) index--;
		}
		bmSelected_ = index;
		changedFields_ |= Settings::kFieldBookmarks;
		ref = parent;
	}

	// ファイラーを動かすのはメインループの持ち物なので、要求だけ積む
	// （フォルダ選択やコンテキストメニューと同じ作法）。
	requestedFolder_ = ref;
	request_ = kRequestSetFolder;
	bmError_.clear();
	showBookmarks_ = false;
}

void SettingsUi::OpenBookmarkList() {
	if (busy() || vfs_ == 0) return;
	requestedFolder_ = vfs_->BookmarkRootRef();
	if (requestedFolder_.empty()) return;
	request_ = kRequestSetFolder;
}

void SettingsUi::JumpToBookmarkRef(Settings *settings, const std::string &ref) {
	if (vfs_ == 0 || ref.empty()) return;
	// 行き先の SAF の許可が失われていれば、[ファイルシステムの設定] を
	// その行を選んだ状態で開く（取り直してもらう）。
	{
		FileSystem *fs = 0;
		std::string rel;
		if (vfs_->Parse(ref, &fs, &rel) && fs != 0 && !fs->accessible()) {
			RegrantAccess(fs->mountRef());
			return;
		}
	}
	const int index = FindBookmark(settings->bookmarks, ref);
	if (index >= 0) {
		OpenBookmark(settings, index);
		if (bmError_.empty()) return;  // request_ に積まれた
		bmError_.clear();
	}
	// 控えに無い（並べ替え中に消えた）か、開けない。ファイラーに任せる。
	requestedFolder_ = ref;
	request_ = kRequestSetFolder;
}

bool SettingsUi::CanBookmark(const std::string &ref) const {
	if (vfs_ == 0 || ref.empty()) return false;
	FileSystem *fs = 0;
	std::string rel;
	if (!vfs_->Parse(ref, &fs, &rel) || fs == 0) return false;
	return !fs->isJumpList();
}

void SettingsUi::BuildBookmarksWindow(Settings *settings, Filer *filer) {
	if (!SyncModal(kBookmarksTitle, &showBookmarks_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 360.0f), placeCond());

	if (!ImGui::BeginPopupModal(kBookmarksTitle, &showBookmarks_, DialogFlags())) {
		return;
	}
	if (vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const int count = (int)list.size();
	if (bmSelected_ >= count) bmSelected_ = count - 1;
	if (bmSelected_ < 0 && count > 0) bmSelected_ = 0;

	// 一覧。クリックで選ぶだけ、ダブルクリックで開く（メイン画面の
	// ファイラーと同じ操作感）。開くのは一覧を組み終わってからにする。
	int openIndex = -1;
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 3.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##bmlist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		if (count == 0) ImGui::TextDisabled("%s", Msg("Bookmark.Empty"));
		for (int i = 0; i < count; i++) {
			char label[512];
			snprintf(label, sizeof(label), "%s##bm%d", vfs_->DisplayPath(list[i]).c_str(), i);
			if (ImGui::Selectable(label, i == bmSelected_,
			                      ImGuiSelectableFlags_AllowDoubleClick) &&
			    !dragMoved_) {
				bmSelected_ = i;
				bmError_.clear();
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openIndex = i;
			}
			// 表示は見やすさ優先で DisplayPath なので、生の ref はここで見せる。
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].c_str());
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	if (bmError_.empty()) {
		TextNote(Msg("Bookmark.Hint"));
	} else {
		TextError(bmError_.c_str());
	}

	// [開く] は 1 行を占有する大きなボタン（仕様どおり）。
	ImGui::BeginDisabled(bmSelected_ < 0);
	if (ImGui::Button(Msg("Button.Open"), ImVec2(-FLT_MIN, 0.0f))) openIndex = bmSelected_;
	ImGui::EndDisabled();

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(bmSelected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		std::swap(list[bmSelected_], list[bmSelected_ - 1]);
		bmSelected_--;
		changedFields_ |= Settings::kFieldBookmarks;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(bmSelected_ < 0 || bmSelected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		std::swap(list[bmSelected_], list[bmSelected_ + 1]);
		bmSelected_++;
		changedFields_ |= Settings::kFieldBookmarks;
	}
	ImGui::EndDisabled();

	// [追加] はフォルダ選択ダイアログを経由して、選んだフォルダを選択位置へ
	// 挿し込む（2026-09-16、ユーザーの指示。それまではカレントフォルダを
	// そのまま入れていた）。ダイアログの開始位置はファイラーの今の場所。
	// モーダル同士は入れ子にせず、いったんこのダイアログを閉じてから出し、
	// 閉じたらまた開く（設定ウィンドウの [参照...] と同じ往復）。
	// 入れられない場所（ルート・"Bookmarks>"・控え済み）の判定は、選んだ
	// あとでフォルダ選択の側が行う。ここで見るのは「いっぱい」だけ。
	(void)filer;
	const bool full = (count >= Settings::kMaxBookmarks);
	ImGui::SameLine();
	ImGui::BeginDisabled(full);
	// ダイアログを開くボタンなので末尾に "…"（Button.AddBookmark）。
	if (ImGui::Button(Msg("Button.AddBookmark"))) {
		bmError_.clear();
		folderTarget_ = kFolderTargetBookmark;
		folderReturnToBookmarks_ = true;
		folderOpenPending_ = true;
		showBookmarks_ = false;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", full ? Msg("Bookmark.AddFull") : Msg("Bookmark.AddBrowse"));
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(bmSelected_ < 0);
	if (ImGui::Button(Msg("Button.Remove"))) {
		bmError_.clear();
		bmOpenRemove_ = true;
	}
	ImGui::EndDisabled();

	BuildBookmarkRemoveWindow(settings);

	if (openIndex >= 0) OpenBookmark(settings, openIndex);

	ImGui::EndPopup();
}

// 「本当に削除するか」。ブックマークの設定の中に入れ子で開く。
void SettingsUi::BuildBookmarkRemoveWindow(Settings *settings) {
	if (bmOpenRemove_) {
		bmOpenRemove_ = false;
		ImGui::OpenPopup(kBmRemoveTitle);
	}

	bmRemoveOpen_ = ImGui::IsPopupOpen(kBmRemoveTitle);
	if (!bmRemoveOpen_) {
		bmCloseRemove_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kBmRemoveTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const bool valid = (bmSelected_ >= 0 && bmSelected_ < (int)list.size());
	ConfirmText(MsgF("Bookmark.RemoveConfirm",
	                 (valid && vfs_ != 0) ? vfs_->DisplayPath(list[bmSelected_])
	                                      : std::string())
	                .c_str());
	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (valid) {
			list.erase(list.begin() + bmSelected_);
			if (bmSelected_ >= (int)list.size()) bmSelected_ = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldBookmarks;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || bmCloseRemove_) {
		bmCloseRemove_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// Shift+M（とコンテキストメニュー）の確認。カレントフォルダが控えてあれば
// 削除、無ければ末尾へ追加する。メイン画面から直に出すので、他のダイアログの
// 入れ子ではなく単独のモーダルとして開く。
void SettingsUi::BuildBookmarkToggleWindow(Settings *settings, Filer *filer) {
	if (bmOpenToggle_) {
		bmOpenToggle_ = false;
		const std::string cur = (filer != 0) ? filer->currentRef() : std::string();
		// 追加できない場所（ファイルシステムの選択、"Bookmarks>"）では何もしない。
		if (CanBookmark(cur)) {
			bmToggleRef_ = cur;
			ImGui::OpenPopup(kBmToggleTitle);
		}
	}

	bmToggleOpen_ = ImGui::IsPopupOpen(kBmToggleTitle);
	if (!bmToggleOpen_) {
		bmCloseToggle_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kBmToggleTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	std::vector<std::string> &list = settings->bookmarks;
	const int at = FindBookmark(list, bmToggleRef_);
	const std::string shown = (vfs_ != 0) ? vfs_->DisplayPath(bmToggleRef_) : bmToggleRef_;

	if (at >= 0) {
		ConfirmText(MsgF("Bookmark.RemoveConfirm", shown).c_str());
		ImGui::Separator();
		if (ImGui::Button(Msg("Button.Remove"))) {
			list.erase(list.begin() + at);
			if (bmSelected_ >= (int)list.size()) bmSelected_ = (int)list.size() - 1;
			changedFields_ |= Settings::kFieldBookmarks;
			ImGui::CloseCurrentPopup();
		}
	} else {
		const bool full = ((int)list.size() >= Settings::kMaxBookmarks);
		ConfirmText(MsgF("Bookmark.AddConfirm", shown).c_str());
		if (full) {
			TextError(Msg("Bookmark.Full"));
		}
		ImGui::Separator();
		ImGui::BeginDisabled(full);
		if (ImGui::Button(Msg("Button.Add"))) {
			list.push_back(bmToggleRef_);  // 追加は末尾（仕様どおり）
			changedFields_ |= Settings::kFieldBookmarks;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || bmCloseToggle_) {
		bmCloseToggle_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
