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
void FileSystemsDialog::Build(SettingsUi &ui, Filer *filer) {
	if (!ui.BeginModal(kFileSystemsTitle, &show_, 460.0f, 360.0f)) return;
	if (ui.vfs_ == 0) {
		ImGui::TextUnformatted(Msg("FileSystems.Empty"));
		ImGui::EndPopup();
		return;
	}

	const int count = ui.vfs_->count();
	if (selected_ >= count) selected_ = count - 1;
	if (selected_ < 0) selected_ = 0;

	// 一覧。削除できないもの（初回起動時から使えるもの）は薄く出して、
	// 削除できないことを見て分かるようにする。
	{
		const float foot = ImGui::GetFrameHeightWithSpacing() * 2.0f +
		                   ImGui::GetTextLineHeightWithSpacing();
		ImGui::BeginChild("##fslist", ImVec2(0, -foot), ImGuiChildFlags_Borders);
		for (int i = 0; i < count; i++) {
			const FileSystem *fs = ui.vfs_->at(i);
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
			if (ImGui::Selectable(label, i == selected_) && !ui.dragMoved_) {
				selected_ = i;
				error_.clear();
			}
			if (noAccess || fixed) ImGui::PopStyleColor();
		}
		DragToScroll(&ui.dragScroll_, &ui.dragMoved_, false, true);
		ImGui::EndChild();
	}

	// 「薄い項目は削除できない」の注記は 2026-09-24 に外した（ユーザーの
	// 指示。狭い画面で行を食うわりに、[削除] が押せないことで分かる）。
	if (!error_.empty()) TextError(error_.c_str());

	const FileSystem *sel = (count > 0) ? ui.vfs_->at(selected_) : 0;

	// [上へ] [下へ]。端まで来たら押せなくする。
	ImGui::BeginDisabled(selected_ <= 0);
	if (ImGui::Button(Msg("Button.Up"))) {
		ui.vfs_->Move(selected_, -1);
		selected_--;
		ui.changedFields_ |= Settings::Field::FileSystems;
		filer->Refresh();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selected_ < 0 || selected_ >= count - 1);
	if (ImGui::Button(Msg("Button.Down"))) {
		ui.vfs_->Move(selected_, 1);
		selected_++;
		ui.changedFields_ |= Settings::Field::FileSystems;
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
		error_.clear();
		if (SafAvailable()) {
			if (SafPickTree()) {
				saf_.picking = true;
			} else {
				error_ = Msg("FileSystems.PickFailed");
			}
		} else {
			addFs_.popup.ask = true;
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
			error_ = Msg("FileSystems.RemoveCurrent");
		} else {
			error_.clear();
			remove_.popup.ask = true;
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
			error_.clear();
			if (SafPickTree(sel->Root())) {
				saf_.picking = true;
				saf_.regrantRef = sel->mountRef();
			} else {
				error_ = Msg("FileSystems.PickFailed");
			}
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && canRegrant) {
			ImGui::SetTooltip("%s", Msg("FileSystems.RegrantHint"));
		}
	}

	BuildAddWindow(ui, filer);
	BuildRemoveWindow(ui, filer);

	ImGui::EndPopup();
}

void FileSystemsDialog::SetBrowsedPath(const std::string &path) {
	snprintf(addFs_.pathBuf, sizeof(addFs_.pathBuf), "%s", path.c_str());
	addFs_.error.clear();
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
void FileSystemsDialog::QuiesceVfsReaders(SettingsUi &ui, Filer *filer) {
	if (filer != 0) filer->WaitIo();
	ui.folder_.Quiesce();
	if (ui.songLoader_ != 0) ui.songLoader_->Quiesce();
}

void FileSystemsDialog::OpenFor(SettingsUi &ui, const std::string &mountRef) {
	ui.OpenFileSystems();
	if (!show_ || ui.vfs_ == 0) return;
	for (int i = 0; i < ui.vfs_->count(); i++) {
		if (CompareNoCase(ui.vfs_->at(i)->mountRef(), mountRef) != 0) continue;
		selected_ = i;
		error_ = MsgF("FileSystems.NoAccessNote", ui.vfs_->at(i)->label());
		break;
	}
}

bool FileSystemsDialog::StartSafRegrant(SettingsUi &ui, const std::string &mountRef,
                                        bool fromFiler) {
	if (ui.vfs_ == 0 || !SafAvailable() || saf_.picking) return false;
	FileSystem *fs = ui.vfs_->FindByMountRef(mountRef);
	if (fs == 0 || !SafPickTree(fs->Root())) return false;
	saf_.picking = true;
	saf_.regrantRef = mountRef;
	saf_.regrantFromFiler = fromFiler;
	return true;
}

void FileSystemsDialog::RegrantAccess(SettingsUi &ui, const std::string &mountRef) {
	if (ui.busy() || ui.vfs_ == 0) return;
	FileSystem *fs = ui.vfs_->FindByMountRef(mountRef);
	if (fs == 0 || fs->accessible()) return;
	// ピッカーを直に出す。出せなければ設定ダイアログで事情を示す。
	if (StartSafRegrant(ui, mountRef, true)) return;
	OpenFor(ui, mountRef);
}

void FileSystemsDialog::PollSafPicked(SettingsUi &ui, Filer *filer) {
	if (!saf_.picking) return;

	std::string uri;
	if (!SafPollPicked(&uri)) return;
	saf_.picking = false;
	const std::string regrant = saf_.regrantRef;
	const bool fromFiler = saf_.regrantFromFiler;
	saf_.regrantRef.clear();
	saf_.regrantFromFiler = false;
	// 外から渡された MDX のために出したピッカーか（openintent.h）。
	const bool handed = !saf_.handedUri.empty();
	saf_.handedUri.clear();
	// 渡されたものの許可は**一覧の末尾**へ足す（設定ダイアログを開いて
	// いないので、選んでいる行に挿し込む意味が無い）。
	if (handed) selected_ = -1;

	ApplyPickedTree(ui, filer, uri, regrant, fromFiler, handed);

	// 許可が取れたフォルダの中から開き直す（取り消されたらもう一度尋ねる）。
	if (handed) FinishHandedAfterPick(ui, uri.empty());
}

// 選び終わったツリーをマウントに反映する。[追加…] と [許可を取り直す…]、
// それに「渡された MDX のフォルダを許可する」の 3 つが通る。
void FileSystemsDialog::ApplyPickedTree(SettingsUi &ui, Filer *filer, const std::string &uri,
                                        const std::string &regrant, bool fromFiler, bool handed) {
	if (uri.empty()) {
		// 取り消した。ファイラーから直に出したピッカーなら、設定ダイアログで
		// 「許可が無い」ことを示しておく（何も起きないと分からない）。
		if (!regrant.empty() && fromFiler) OpenFor(ui, regrant);
		return;
	}
	if (ui.vfs_ == 0) return;

	// [許可を取り直す…] から来て、同じフォルダを選び直したなら、そのマウントを
	// 読める状態に戻すだけ（ini も順番も変わらない）。別のフォルダを選んだ
	// ときは、下の [追加…] と同じ扱いで新しく足す（古いものは残る）。
	if (!regrant.empty()) {
		FileSystem *target = ui.vfs_->FindByMountRef(regrant);
		if (target != 0 && CompareNoCase(target->mountRef(), std::string("saf:") + uri) == 0) {
			if (target->Reconnect()) {
				error_.clear();
				if (filer != 0) filer->Refresh();
				// 起動時の警告に同じ項目があれば、済んだ印に差し替える。
				ui.info_.MarkRegranted(regrant, MsgF("FileSystems.Regranted", target->label()));
			} else {
				error_ = Msg("FileSystems.RegrantFailed");
				if (fromFiler) OpenFor(ui, regrant);
			}
			return;
		}
		// 別のフォルダを選んだ。新しく足したうえで、元の行が残っていることを
		// 設定ダイアログで見せる（ファイラーから来たときだけ）。
		if (fromFiler) OpenFor(ui, regrant);
	}

	// すでに一覧にある場所を選び直したときは、**許可を取り直したもの**として
	// 扱う（一覧から外れていたらマウントもし直す）。ここを通るのは
	// [追加…] で同じフォルダを選んだときと、渡された MDX のために許可を
	// 取り直したとき（そのマウントは ini から作られていて許可だけ無い状態）。
	{
		FileSystem *exists = ui.vfs_->FindByMountRef(std::string("saf:") + uri);
		if (exists != 0) {
			exists->Reconnect();
			if (!ui.vfs_->IsMounted(exists)) {
				ui.vfs_->Mount(exists);
				ui.changedFields_ |= Settings::Field::FileSystems;
			}
			// 渡されたものの流れでは一覧を出していないので、誤りの文言は残さない
			// （次に [ファイルシステムの設定] を開いたときに古い文が出る）。
			if (!handed) error_ = MsgF("AddFs.Duplicate", exists->mountRef());
			if (filer != 0) filer->Refresh();
			return;
		}
	}

	FileSystem *made = ui.vfs_->CreateFromMountRef(std::string("saf:") + uri);
	if (made == 0) {
		error_ = Msg("AddFs.NotFound");
		return;
	}
	QuiesceVfsReaders(ui, filer);
	if (!ui.vfs_->Add(made)) {
		error_ = MsgF("AddFs.Duplicate", made->mountRef());
		delete made;
		return;
	}
	// 選んでいた位置へ挿し込む（[追加] ダイアログと同じ）。
	const int at = (selected_ >= 0) ? selected_ : ui.vfs_->count();
	ui.vfs_->MountAt(at, made);
	selected_ = at;
	ui.changedFields_ |= Settings::Field::FileSystems;
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
void FileSystemsDialog::BuildHandedWindow(SettingsUi &ui, Filer *filer) {
	if (handed_.popup.ask) {
		// **OS の許可が残っているフォルダなら尋ねない。** 一覧から外して
		// あっても、ユーザーから見れば「許可済みのフォルダ」なので、黙って
		// 一覧へ戻して開く（2026-09-18、ユーザーの報告）。
		{
			std::string ref;
			if (MountGrantedTree(ui, filer, handed_.uri, &ref)) {
				handed_.popup.ask = false;  // 尋ねずに開くので Begin に開かせない
				handed_.uri.clear();
				handed_.name.clear();
				handed_.ref = ref;
				ui.request_ = SettingsUi::Request::OpenHanded;
				return;
			}
		}
	}
	if (!handed_.popup.Begin(ui, kHandedTitle)) return;

	ConfirmText(MsgF("Handed.Question", handed_.name).c_str());
	ConfirmNote(Msg("Handed.Note"));
	ImGui::Separator();

	if (ImGui::Button(Msg("Button.HandedGrant"))) {
		// ピッカーは**渡されたファイルのフォルダ**から見せる（親を辿れない
		// 提供元なら既定の場所から）。出せないときは写して開く
		// ——何も起きないよりは鳴るほうがよい。
		if (!saf_.picking && SafAvailable() &&
		    SafPickTreeAtDoc(openintent::ParentDocUri(handed_.uri))) {
			saf_.picking = true;
			saf_.handedUri = handed_.uri;
			saf_.regrantRef.clear();
			saf_.regrantFromFiler = false;
		} else {
			OpenHandedByCopy(ui);
		}
		ImGui::CloseCurrentPopup();
	}
	SameLineOrWrap(Msg("Button.HandedCopy"));
	if (ImGui::Button(Msg("Button.HandedCopy"))) {
		OpenHandedByCopy(ui);
		ImGui::CloseCurrentPopup();
	}
	SameLineOrWrap(Msg("Button.Cancel"));
	if (handed_.popup.CancelButton(Msg("Button.Cancel"))) {
		handed_.uri.clear();
		handed_.name.clear();
	}
	ImGui::EndPopup();
}

// OS の許可（持続許可）が残っているツリーなら、ファイルシステムの一覧へ
// 戻して（すでに在れば必要ならマウントし直して）ref を作る。
//
// **許可を取り直す必要は無い**（OS はもう許可している）ので、ピッカーは
// 出さない。一覧から外れていたぶんは**末尾へ**足し、ini にも書き戻す。
bool FileSystemsDialog::MountGrantedTree(SettingsUi &ui, Filer *filer, const std::string &uri,
                                         std::string *ref) {
	ref->clear();
	if (ui.vfs_ == 0 || uri.empty()) return false;

	std::string tree, rel;
	if (!openintent::GrantedTree(uri, &tree, &rel)) return false;
	const std::string mountRef = std::string("saf:") + tree;

	FileSystem *fs = ui.vfs_->FindByMountRef(mountRef);
	if (fs == 0) {
		fs = ui.vfs_->CreateFromMountRef(mountRef);
		if (fs == 0) return false;
		QuiesceVfsReaders(ui, filer);
		if (!ui.vfs_->Add(fs)) {
			delete fs;
			return false;
		}
	}
	if (!ui.vfs_->IsMounted(fs)) {
		if (!ui.vfs_->Mount(fs)) return false;
		ui.changedFields_ |= Settings::Field::FileSystems;
		if (filer != 0) filer->Refresh();
		printf("info     : %s\n", MsgF("Log.HandedMounted", fs->label()).c_str());
		fflush(stdout);
	}
	return openintent::ResolveInTree(*ui.vfs_, uri, ref);
}

// ピッカーから戻ったあと。許可が取れていれば saf: の ref で開く。
void FileSystemsDialog::FinishHandedAfterPick(SettingsUi &ui, bool cancelled) {
	if (handed_.uri.empty()) return;
	if (cancelled) {
		// **もう一度尋ねる。** 黙って引き下がると、叩いた曲が鳴らない理由が
		// 分からない（ここで [このまま演奏する] を選び直せる）。
		handed_.popup.ask = true;
		return;
	}
	if (ui.vfs_ == 0) return;

	std::string ref;
	if (openintent::ResolveInTree(*ui.vfs_, handed_.uri, &ref)) {
		handed_.uri.clear();
		handed_.name.clear();
		handed_.ref = ref;
		ui.request_ = SettingsUi::Request::OpenHanded;
		return;
	}
	// 許可は取れたが、その中に見つからなかった（別のフォルダを選んだ、
	// またはドキュメント ID と表示名が食い違う提供元）。写して開く。
	OpenHandedByCopy(ui);
}

// 写して開く。読めない・写せないときは何も起きない（理由はログに出ている）。
void FileSystemsDialog::OpenHandedByCopy(SettingsUi &ui) {
	if (ui.vfs_ == 0 || handed_.uri.empty()) return;
	std::string ref;
	const bool ok = openintent::CopyToUserDir(*ui.vfs_, handed_.uri, &ref);
	handed_.uri.clear();
	handed_.name.clear();
	if (!ok) return;
	handed_.ref = ref;
	ui.request_ = SettingsUi::Request::OpenHanded;
}

void FileSystemsDialog::BuildAddWindow(SettingsUi &ui, Filer *filer) {
	if (addFs_.popup.ask) {
		addFs_.pathBuf[0] = '\0';
		addFs_.error.clear();
	}
	if (!addFs_.popup.Begin(ui, kAddFsTitle)) return;

	ImGui::TextUnformatted(Msg("AddFs.Path"));
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool apply = ImGui::InputText("##addfspath", addFs_.pathBuf, sizeof(addFs_.pathBuf),
	                              ImGuiInputTextFlags_EnterReturnsTrue);
	TextNote(Msg("AddFs.Hint"));
	if (!addFs_.error.empty()) {
		TextError(addFs_.error.c_str());
	}
	ImGui::Separator();

	// OS のダイアログが無い環境（Android など）では打ち込みだけ。
	if (HasFolderBrowser()) {
		if (ImGui::Button(Msg("Button.Browse"))) {
			addFs_.browseStart = TrimSpaces(addFs_.pathBuf);
			addFs_.browsePending = true;
		}
		ImGui::SameLine();
	}
	if (ImGui::Button(Msg("Button.Add"))) apply = true;
	ImGui::SameLine();
	addFs_.popup.CancelButton(Msg("Button.Cancel"));

	if (apply) {
		const std::string path = TrimSpaces(addFs_.pathBuf);
		FileSystem *made =
		    (path.empty() || !IsDirectory(path) || ui.vfs_ == 0)
		        ? 0
		        : ui.vfs_->CreateFromMountRef(std::string("dir:") + path);
		if (made != 0) QuiesceVfsReaders(ui, filer);
		if (made == 0) {
			addFs_.error = Msg("AddFs.NotFound");
		} else if (!ui.vfs_->Add(made)) {
			addFs_.error = MsgF("AddFs.Duplicate", made->mountRef());
			delete made;
		} else {
			// 選んでいた位置へ挿し込む（filesystem.md）。
			const int at = (selected_ >= 0) ? selected_ : ui.vfs_->count();
			ui.vfs_->MountAt(at, made);
			selected_ = at;
			ui.changedFields_ |= Settings::Field::FileSystems;
			if (filer != 0) filer->Refresh();
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::EndPopup();
}

// 「本当に削除するか」。ファイルシステムの設定の中に入れ子で開く。
void FileSystemsDialog::BuildRemoveWindow(SettingsUi &ui, Filer *filer) {
	if (!remove_.popup.Begin(ui, kFsRemoveTitle)) return;

	const FileSystem *sel =
	    (ui.vfs_ != 0 && selected_ >= 0 && selected_ < ui.vfs_->count()) ? ui.vfs_->at(selected_)
	                                                                  : 0;
	ConfirmText(MsgF("FileSystems.RemoveConfirm", sel != 0 ? sel->label() : std::string())
	                .c_str());

	// **SAF は OS の許可を別に持っている。** 一覧から外すだけだと許可は
	// 残り、「一覧に無いのに許可はある」状態になる（そのときは外から MDX を
	// 渡されると黙って一覧へ戻す。openintent.h）。取り消すかどうかを
	// ここで尋ねる（2026-09-18、ユーザーの指示）。
	const bool isSaf = (sel != 0 && SafAvailable() && CompareNoCase(sel->id(), "saf") == 0);
	if (isSaf) {
		ImGui::Checkbox(Msg("FileSystems.RemoveRevoke"), &remove_.revoke);
		ConfirmNote(Msg(remove_.revoke ? "FileSystems.RemoveRevokeNote"
		                                : "FileSystems.RemoveKeepNote"));
	}

	ImGui::Separator();
	if (ImGui::Button(Msg("Button.Remove"))) {
		// 許可の返上は**実体を捨てる前に**（mountRef からツリーの URI を取る）。
		if (isSaf && remove_.revoke) {
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
		QuiesceVfsReaders(ui, filer);
		ui.vfs_->RemoveMounted(selected_);
		if (selected_ >= ui.vfs_->count()) selected_ = ui.vfs_->count() - 1;
		if (selected_ < 0) selected_ = 0;
		ui.changedFields_ |= Settings::Field::FileSystems;
		if (filer != 0) filer->Refresh();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	remove_.popup.CancelButton(Msg("Button.Cancel"));
	ImGui::EndPopup();
}

}  // namespace mxv2
