// mxv2 - 設定 UI: [ファイルシステムの設定] ダイアログ (F3) と SAF の取り直し
//
// settingsui.cpp から切り出した。

#include "settingsui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "settingsui_internal.h"

#include "dirlister.h"
#include "settings.h"

#include "filer.h"
#include "openintent.h"
#include "safaccess.h"
#include "screen.h"
#include "songloader.h"
#include "vfs.h"

namespace mxv2 {

using namespace settingsui;

// ファイルシステムの設定 (F3)。ファイラーのルートに並べる顔ぶれと順番を
// 決める。実体は Vfs のマウント一覧なので、触ったらその場で効く。
// 並びは [FileSystem] へ書き戻す（changedFields_ 経由でメインループが書く）。
void SettingsUi::BuildFileSystemsWindow(Filer *filer) {
	if (!SyncModal(kFileSystemsTitle, &showFileSystems_)) return;

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 360.0f), placeCond());

	if (!ImGui::BeginPopupModal(kFileSystemsTitle, &showFileSystems_, DialogFlags())) {
		return;
	}
	if (vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	const int count = vfs_->count();
	if (fsSelected_ >= count) fsSelected_ = count - 1;
	if (fsSelected_ < 0) fsSelected_ = 0;

	// 一覧。削除できないもの（初回起動時から使えるもの）は薄く出して、
	// 削除できないことを見て分かるようにする。
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##fslist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		for (int i = 0; i < count; i++) {
			const FileSystem *fs = vfs_->at(i);
			// 削除できないものは薄く、アクセス許可が失われたものは赤く（注記つき）。
			const bool fixed = !fs->removable();
			const bool noAccess = !fs->accessible();
			char label[256];
			snprintf(label, sizeof(label), "%s  %s%s%s##fs%d", fs->prefix(),
			         fs->label().c_str(), noAccess ? " " : "", noAccess ? Msg("Fs.NoAccess") : "", i);
			if (noAccess) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
			} else if (fixed) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			}
			if (ImGui::Selectable(label, i == fsSelected_) && !dragMoved_) {
				fsSelected_ = i;
				fsError_.clear();
			}
			if (noAccess || fixed) ImGui::PopStyleColor();
		}
		DragToScroll(&dragScroll_, &dragMoved_, false, true);
		ImGui::EndChild();
	}

	// 「薄い項目は削除できない」の注記は 2026-09-24 に外した（ユーザーの
	// 指示。狭い画面で行を食うわりに、[削除] が押せないことで分かる）。
	if (!fsError_.empty()) TextError(fsError_.c_str());

	const FileSystem *sel = (count > 0) ? vfs_->at(fsSelected_) : 0;

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(fsSelected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		vfs_->Move(fsSelected_, -1);
		fsSelected_--;
		changedFields_ |= Settings::kFieldFileSystems;
		filer->Refresh();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(fsSelected_ < 0 || fsSelected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		vfs_->Move(fsSelected_, 1);
		fsSelected_++;
		changedFields_ |= Settings::kFieldFileSystems;
		filer->Refresh();
	}
	ImGui::EndDisabled();

	// [追加] で足せる種類はプラットフォームごとに 1 つしかないので、種類の
	// 選択は省いて場所を選ぶ画面を直に出す（filesystem.md）。
	//   Android … 端末のフォルダ (SAF)。選ぶ画面は OS が出し、結果は
	//             あとから届くので PollSafPicked() が拾う。
	//   その他  … フォルダマウント (dir:) の追加ダイアログ。
	// 3 種類以上になったら、ここに種類の選択を挟むこと。
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.AddFs"))) {
		fsError_.clear();
		if (SafAvailable()) {
			if (SafPickTree()) {
				safPicking_ = true;
			} else {
				fsError_ = Msg("FileSystems.PickFailed");
			}
		} else {
			addFsOpen_ = true;
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", Msg("FileSystems.AddHint"));
	}

	// [削除]。削除できないものはグレーアウト。カレントのものは押せるが、
	// 押したときに断る（仕様どおり）。
	ImGui::SameLine();
	ImGui::BeginDisabled(sel == 0 || !sel->removable());
	if (ImGui::Button(Msg("Button.Remove"))) {
		if (sel != 0 && filer != 0 && filer->fs() == sel) {
			fsError_ = Msg("FileSystems.RemoveCurrent");
		} else {
			fsError_.clear();
			fsOpenConfirm_ = true;
		}
	}
	ImGui::EndDisabled();

	// [許可を取り直す…]。アクセス許可が失われた SAF（再インストールで OS が
	// 権限を捨て、クラウドから戻った ini だけが残ったとき）を選んでいるときだけ
	// 押せる。同じフォルダを選び直せば、そのマウントが読める状態に戻り、
	// その先を指すブックマークもそのまま効く（PollSafPicked）。
	// 2026-09-16、ユーザーの指示。
	//
	// 2026-09-24 に行を分けた（ユーザーの指示）。何の許可かをラベルに足して
	// 長くなったのと、320px 幅の携帯 (XS17) の縦画面で右端が切れていたため。
	// SAF の無い環境（パソコンなど）では押せる場面が無いので出さない。
	if (SafAvailable()) {
		const bool canRegrant = (sel != 0) && !sel->accessible();
		ImGui::BeginDisabled(!canRegrant);
		if (ImGui::Button(Msg("Button.Regrant"))) {
			fsError_.clear();
			if (SafPickTree(sel->Root())) {
				safPicking_ = true;
				safRegrantRef_ = sel->mountRef();
			} else {
				fsError_ = Msg("FileSystems.PickFailed");
			}
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && canRegrant) {
			ImGui::SetTooltip("%s", Msg("FileSystems.RegrantHint"));
		}
	}

	BuildAddFsWindow(filer);
	BuildFsRemoveWindow(filer);

	ImGui::EndPopup();
}

void SettingsUi::SetBrowsedPath(const std::string &path) {
	snprintf(addFsPathBuf_, sizeof(addFsPathBuf_), "%s", path.c_str());
	addFsError_.clear();
}

// ファイルシステムの追加。今のところ足せるのはフォルダマウント (dir:) だけ
// なので、種類の選択は省いて場所だけを決める（filesystem.md）。
// 場所は **OS ネイティブのパス**なので、打ち込みと OS の「フォルダを探す」
// ダイアログで取る。ファイラーのフォルダ選択（VFS の中を辿る自前のもの）は
// ここでは使わない。
// 端末の「フォルダを選ぶ」画面 (SAF) の結果を拾う。あちらが出ている間
// mxv2 はバックグラウンドへ回っているので、結果が届くのは戻ってきたあと。
// Vfs::all_ は std::vector で、Add の push_back で再確保が起きうる。
// 曲名・フォルダ・曲の 3 本の読みスレッドは const Vfs* 越しに Parse で
// all_ を舐めるので、足すときも消すときも、先に読みかけを捨てさせて
// 手が離れるまで待つ（それぞれの Quiesce は待ち合わせまで行う）。
// 読みかけは捨てられるが、ファイラーは操作の直後に Refresh() で
// 読み直すので見た目には残らない。
void SettingsUi::QuiesceVfsReaders(Filer *filer) {
	if (filer != 0) filer->WaitIo();
	if (folderLister_ != 0) folderLister_->Quiesce();
	if (songLoader_ != 0) songLoader_->Quiesce();
}

void SettingsUi::OpenFileSystemsFor(const std::string &mountRef) {
	OpenFileSystems();
	if (!showFileSystems_ || vfs_ == 0) return;
	for (int i = 0; i < vfs_->count(); i++) {
		if (CompareNoCase(vfs_->at(i)->mountRef(), mountRef) != 0) continue;
		fsSelected_ = i;
		fsError_ = MsgF("FileSystems.NoAccessNote", vfs_->at(i)->label());
		break;
	}
}

void SettingsUi::RegrantAccess(const std::string &mountRef) {
	if (busy() || vfs_ == 0) return;
	FileSystem *fs = vfs_->FindByMountRef(mountRef);
	if (fs == 0 || fs->accessible()) return;
	// ピッカーを直に出す。出せなければ設定ダイアログで事情を示す。
	if (SafAvailable() && !safPicking_ && SafPickTree(fs->Root())) {
		safPicking_ = true;
		safRegrantRef_ = mountRef;
		safRegrantFromFiler_ = true;
		return;
	}
	OpenFileSystemsFor(mountRef);
}

void SettingsUi::PollSafPicked(Filer *filer) {
	if (!safPicking_) return;

	std::string uri;
	if (!SafPollPicked(&uri)) return;
	safPicking_ = false;
	const std::string regrant = safRegrantRef_;
	const bool fromFiler = safRegrantFromFiler_;
	safRegrantRef_.clear();
	safRegrantFromFiler_ = false;
	// 外から渡された MDX のために出したピッカーか（openintent.h）。
	const bool handed = !safHandedUri_.empty();
	safHandedUri_.clear();
	// 渡されたものの許可は**一覧の末尾**へ足す（設定ダイアログを開いて
	// いないので、選んでいる行に挿し込む意味が無い）。
	if (handed) fsSelected_ = -1;

	ApplyPickedTree(filer, uri, regrant, fromFiler, handed);

	// 許可が取れたフォルダの中から開き直す（取り消されたらもう一度尋ねる）。
	if (handed) FinishHandedAfterPick(uri.empty());
}

// 選び終わったツリーをマウントに反映する。[追加…] と [許可を取り直す…]、
// それに「渡された MDX のフォルダを許可する」の 3 つが通る。
void SettingsUi::ApplyPickedTree(Filer *filer, const std::string &uri,
                                 const std::string &regrant, bool fromFiler, bool handed) {
	if (uri.empty()) {
		// 取り消した。ファイラーから直に出したピッカーなら、設定ダイアログで
		// 「許可が無い」ことを示しておく（何も起きないと分からない）。
		if (!regrant.empty() && fromFiler) OpenFileSystemsFor(regrant);
		return;
	}
	if (vfs_ == 0) return;

	// [許可を取り直す…] から来て、同じフォルダを選び直したなら、そのマウントを
	// 読める状態に戻すだけ（ini も順番も変わらない）。別のフォルダを選んだ
	// ときは、下の [追加…] と同じ扱いで新しく足す（古いものは残る）。
	if (!regrant.empty()) {
		FileSystem *target = vfs_->FindByMountRef(regrant);
		if (target != 0 && CompareNoCase(target->mountRef(), std::string("saf:") + uri) == 0) {
			if (target->Reconnect()) {
				fsError_.clear();
				if (filer != 0) filer->Refresh();
				// 起動時の警告に同じ項目があれば、済んだ印に差し替える。
				for (size_t i = 0; i < startupLines_.size(); i++) {
					if (CompareNoCase(startupLines_[i].regrantRef, regrant) != 0) continue;
					startupLines_[i].done = true;
					startupLines_[i].text = MsgF("FileSystems.Regranted", target->label());
				}
			} else {
				fsError_ = Msg("FileSystems.RegrantFailed");
				if (fromFiler) OpenFileSystemsFor(regrant);
			}
			return;
		}
		// 別のフォルダを選んだ。新しく足したうえで、元の行が残っていることを
		// 設定ダイアログで見せる（ファイラーから来たときだけ）。
		if (fromFiler) OpenFileSystemsFor(regrant);
	}

	// すでに一覧にある場所を選び直したときは、**許可を取り直したもの**として
	// 扱う（一覧から外れていたらマウントもし直す）。ここを通るのは
	// [追加…] で同じフォルダを選んだときと、渡された MDX のために許可を
	// 取り直したとき（そのマウントは ini から作られていて許可だけ無い状態）。
	{
		FileSystem *exists = vfs_->FindByMountRef(std::string("saf:") + uri);
		if (exists != 0) {
			exists->Reconnect();
			if (!vfs_->IsMounted(exists)) {
				vfs_->Mount(exists);
				changedFields_ |= Settings::kFieldFileSystems;
			}
			// 渡されたものの流れでは一覧を出していないので、誤りの文言は残さない
			// （次に [ファイルシステムの設定] を開いたときに古い文が出る）。
			if (!handed) fsError_ = MsgF("AddFs.Duplicate", exists->mountRef());
			if (filer != 0) filer->Refresh();
			return;
		}
	}

	FileSystem *made = vfs_->CreateFromMountRef(std::string("saf:") + uri);
	if (made == 0) {
		fsError_ = Msg("AddFs.NotFound");
		return;
	}
	QuiesceVfsReaders(filer);
	if (!vfs_->Add(made)) {
		fsError_ = MsgF("AddFs.Duplicate", made->mountRef());
		delete made;
		return;
	}
	// 選んでいた位置へ挿し込む（[追加] ダイアログと同じ）。
	const int at = (fsSelected_ >= 0) ? fsSelected_ : vfs_->count();
	vfs_->MountAt(at, made);
	fsSelected_ = at;
	changedFields_ |= Settings::kFieldFileSystems;
	if (filer != 0) filer->Refresh();
}

// ---------------------------------------------------------------------------
// 外から渡された MDX が、許可のあるフォルダの外にあったとき（openintent.h）
//
// **どちらで開くかはユーザーが選ぶ**（2026-09-18、ユーザーの指示）。黙って
// 写して鳴らすと、PDX を使う曲が半端に鳴って違和感が大きい。
//   [フォルダを許可する…] … SAF のピッカーでそのフォルダの許可を取り、
//     マウントしてから saf: で開く。**同じフォルダの PDX も鳴る**。
//   [このまま演奏する]   … ユーザーフォルダへ写して開く。すぐ鳴るが
//     **PDX は付いてこない**。
//   [キャンセル] / ESC   … 何もしない。
// ---------------------------------------------------------------------------
void SettingsUi::BuildHandedWindow(Filer *filer) {
	if (handedAsk_) {
		handedAsk_ = false;
		// **OS の許可が残っているフォルダなら尋ねない。** 一覧から外して
		// あっても、ユーザーから見れば「許可済みのフォルダ」なので、黙って
		// 一覧へ戻して開く（2026-09-18、ユーザーの報告）。
		{
			std::string ref;
			if (MountGrantedTree(filer, handedUri_, &ref)) {
				handedUri_.clear();
				handedName_.clear();
				handedRef_ = ref;
				request_ = kRequestOpenHanded;
				return;
			}
		}
		ImGui::OpenPopup(kHandedTitle);
	}

	handedOpen_ = ImGui::IsPopupOpen(kHandedTitle);
	if (!handedOpen_) {
		handedClose_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kHandedTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ConfirmText(MsgF("Handed.Question", handedName_).c_str());
	ConfirmNote(Msg("Handed.Note"));
	ImGui::Separator();

	if (ImGui::Button(Msg("Button.HandedGrant"))) {
		// ピッカーは**渡されたファイルのフォルダ**から見せる（親を辿れない
		// 提供元なら既定の場所から）。出せないときは写して開く
		// ——何も起きないよりは鳴るほうがよい。
		if (!safPicking_ && SafAvailable() &&
		    SafPickTreeAtDoc(openintent::ParentDocUri(handedUri_))) {
			safPicking_ = true;
			safHandedUri_ = handedUri_;
			safRegrantRef_.clear();
			safRegrantFromFiler_ = false;
		} else {
			OpenHandedByCopy();
		}
		ImGui::CloseCurrentPopup();
	}
	SameLineOrWrap(Msg("Button.HandedCopy"));
	if (ImGui::Button(Msg("Button.HandedCopy"))) {
		OpenHandedByCopy();
		ImGui::CloseCurrentPopup();
	}
	SameLineOrWrap(Msg("Button.Cancel"));
	if (ImGui::Button(Msg("Button.Cancel")) || handedClose_) {
		handedClose_ = false;
		handedUri_.clear();
		handedName_.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// OS の許可（持続許可）が残っているツリーなら、ファイルシステムの一覧へ
// 戻して（すでに在れば必要ならマウントし直して）ref を作る。
//
// **許可を取り直す必要は無い**（OS はもう許可している）ので、ピッカーは
// 出さない。一覧から外れていたぶんは**末尾へ**足し、ini にも書き戻す。
bool SettingsUi::MountGrantedTree(Filer *filer, const std::string &uri, std::string *ref) {
	ref->clear();
	if (vfs_ == 0 || uri.empty()) return false;

	std::string tree, rel;
	if (!openintent::GrantedTree(uri, &tree, &rel)) return false;
	const std::string mountRef = std::string("saf:") + tree;

	FileSystem *fs = vfs_->FindByMountRef(mountRef);
	if (fs == 0) {
		fs = vfs_->CreateFromMountRef(mountRef);
		if (fs == 0) return false;
		QuiesceVfsReaders(filer);
		if (!vfs_->Add(fs)) {
			delete fs;
			return false;
		}
	}
	if (!vfs_->IsMounted(fs)) {
		if (!vfs_->Mount(fs)) return false;
		changedFields_ |= Settings::kFieldFileSystems;
		if (filer != 0) filer->Refresh();
		printf("info     : %s\n", MsgF("Log.HandedMounted", fs->label()).c_str());
		fflush(stdout);
	}
	return openintent::ResolveInTree(*vfs_, uri, ref);
}

// ピッカーから戻ったあと。許可が取れていれば saf: の ref で開く。
void SettingsUi::FinishHandedAfterPick(bool cancelled) {
	if (handedUri_.empty()) return;
	if (cancelled) {
		// **もう一度尋ねる。** 黙って引き下がると、叩いた曲が鳴らない理由が
		// 分からない（ここで [このまま演奏する] を選び直せる）。
		handedAsk_ = true;
		return;
	}
	if (vfs_ == 0) return;

	std::string ref;
	if (openintent::ResolveInTree(*vfs_, handedUri_, &ref)) {
		handedUri_.clear();
		handedName_.clear();
		handedRef_ = ref;
		request_ = kRequestOpenHanded;
		return;
	}
	// 許可は取れたが、その中に見つからなかった（別のフォルダを選んだ、
	// またはドキュメント ID と表示名が食い違う提供元）。写して開く。
	OpenHandedByCopy();
}

// 写して開く。読めない・写せないときは何も起きない（理由はログに出ている）。
void SettingsUi::OpenHandedByCopy() {
	if (vfs_ == 0 || handedUri_.empty()) return;
	std::string ref;
	const bool ok = openintent::CopyToUserDir(*vfs_, handedUri_, &ref);
	handedUri_.clear();
	handedName_.clear();
	if (!ok) return;
	handedRef_ = ref;
	request_ = kRequestOpenHanded;
}

void SettingsUi::BuildAddFsWindow(Filer *filer) {
	if (addFsOpen_) {
		addFsOpen_ = false;
		addFsPathBuf_[0] = '\0';
		addFsError_.clear();
		ImGui::OpenPopup(kAddFsTitle);
	}

	addFsShow_ = ImGui::IsPopupOpen(kAddFsTitle);
	if (!addFsShow_) {
		addFsClose_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	ImGui::SetNextWindowSize(DialogSize(460.0f, 0.0f), placeCond());
	if (!ImGui::BeginPopupModal(kAddFsTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted(Msg("AddFs.Path"));
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##addfspath", addFsPathBuf_, sizeof(addFsPathBuf_),
	                              ImGuiInputTextFlags_EnterReturnsTrue);
	TextNote(Msg("AddFs.Hint"));
	if (!addFsError_.empty()) {
		TextError(addFsError_.c_str());
	}
	ImGui::Separator();

	// OS のダイアログが無い環境（Android など）では打ち込みだけ。
	if (HasFolderBrowser()) {
		if (ImGui::Button(Msg("Button.Browse"))) {
			browseStart_ = TrimSpaces(addFsPathBuf_);
			pendingBrowse_ = true;
		}
		ImGui::SameLine();
	}
	if (ImGui::Button(Msg("Button.Add"))) apply = true;
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || addFsClose_) {
		addFsClose_ = false;
		ImGui::CloseCurrentPopup();
	}

	if (apply) {
		const std::string path = TrimSpaces(addFsPathBuf_);
		FileSystem *made =
		    (path.empty() || !IsDirectory(path) || vfs_ == 0)
		        ? 0
		        : vfs_->CreateFromMountRef(std::string("dir:") + path);
		if (made != 0) QuiesceVfsReaders(filer);
		if (made == 0) {
			addFsError_ = Msg("AddFs.NotFound");
		} else if (!vfs_->Add(made)) {
			addFsError_ = MsgF("AddFs.Duplicate", made->mountRef());
			delete made;
		} else {
			// 選んでいた位置へ挿し込む（filesystem.md）。
			const int at = (fsSelected_ >= 0) ? fsSelected_ : vfs_->count();
			vfs_->MountAt(at, made);
			fsSelected_ = at;
			changedFields_ |= Settings::kFieldFileSystems;
			if (filer != 0) filer->Refresh();
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::EndPopup();
}

// 「本当に削除するか」。ファイルシステムの設定の中に入れ子で開く。
void SettingsUi::BuildFsRemoveWindow(Filer *filer) {
	if (fsOpenConfirm_) {
		fsOpenConfirm_ = false;
		ImGui::OpenPopup(kFsRemoveTitle);
	}

	fsConfirmOpen_ = ImGui::IsPopupOpen(kFsRemoveTitle);
	if (!fsConfirmOpen_) {
		fsCloseConfirm_ = false;
		return;
	}

	CenterNextWindow(placeCond());
	if (!ImGui::BeginPopupModal(kFsRemoveTitle, NULL,
	                            DialogFlags() | ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const FileSystem *sel =
	    (vfs_ != 0 && fsSelected_ >= 0 && fsSelected_ < vfs_->count()) ? vfs_->at(fsSelected_)
	                                                                  : 0;
	ConfirmText(MsgF("FileSystems.RemoveConfirm", sel != 0 ? sel->label() : std::string())
	                .c_str());

	// **SAF は OS の許可を別に持っている。** 一覧から外すだけだと許可は
	// 残り、「一覧に無いのに許可はある」状態になる（そのときは外から MDX を
	// 渡されると黙って一覧へ戻す。openintent.h）。取り消すかどうかを
	// ここで尋ねる（2026-09-18、ユーザーの指示）。
	const bool isSaf = (sel != 0 && SafAvailable() && CompareNoCase(sel->id(), "saf") == 0);
	if (isSaf) {
		ImGui::Checkbox(Msg("FileSystems.RemoveRevoke"), &fsRemoveRevoke_);
		ConfirmNote(Msg(fsRemoveRevoke_ ? "FileSystems.RemoveRevokeNote"
		                                : "FileSystems.RemoveKeepNote"));
	}

	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		// 許可の返上は**実体を捨てる前に**（mountRef からツリーの URI を取る）。
		if (isSaf && fsRemoveRevoke_) {
			const std::string mountRef = sel->mountRef();
			const std::string tree = mountRef.substr(mountRef.find(':') + 1);
			if (SafReleaseTree(tree)) {
				printf("info     : %s\n", MsgF("Log.SafReleased", sel->label()).c_str());
			} else {
				printf("warning  : %s\n", MsgF("Log.SafReleaseFailed", sel->label()).c_str());
			}
			fflush(stdout);
		}
		// 動的に足したファイルシステムは実体も捨てるので、フォルダと
		// タイトルを読んでいるスレッドの手が離れるのを待ってからにする。
		QuiesceVfsReaders(filer);
		vfs_->RemoveMounted(fsSelected_);
		if (fsSelected_ >= vfs_->count()) fsSelected_ = vfs_->count() - 1;
		if (fsSelected_ < 0) fsSelected_ = 0;
		changedFields_ |= Settings::kFieldFileSystems;
		if (filer != 0) filer->Refresh();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(Msg("Button.Cancel")) || fsCloseConfirm_) {
		fsCloseConfirm_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

}  // namespace mxv2
