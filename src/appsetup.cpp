// mxv2 - 起動時の組み立て（main.cpp から切り出し）

#include "appsetup.h"

#include <cstdio>

#include <SDL.h>

#include "cmdline.h"
#include "drawscreen.h"
#include "filer.h"
#include "fileutil.h"
#include "message.h"
#include "orientlock.h"
#include "player.h"
#include "settings.h"
#include "skin.h"
#include "textlayer.h"
#include "vfs.h"

namespace mxv2 {
namespace app {

void Warn(Warnings *box, const std::string &text) {
	printf("warning  : %s\n", text.c_str());
	fflush(stdout);
	if (box != 0) {
		mxv2::SettingsUi::StartupWarning w;
		w.text = text;
		box->push_back(w);
	}
}

// アクセス許可が失われた SAF の警告。ダイアログではその場で取り直せる
// ボタンが付く（SettingsUi::StartupWarning::regrantRef）。
void WarnRegrant(Warnings *box, const std::string &text, const std::string &mountRef) {
	Warn(box, text);
	if (box != 0 && !box->empty()) box->back().regrantRef = mountRef;
}

// ini に書かれたスキンが無い（読めない）ときの落とし先。その系統
// （切り替え OFF / 縦 / 横）の**既定のスキン**で、プラットフォームごとに
// 違う（settings.cpp の kDefaultSkin*）。落ちたことは **ini に書き戻さない**
// （2026-09-12 の決定。スキンを置き直せば次の起動で元の名前へ戻る）。
std::string DefaultSkinFor(bool orientEnabled, mxv2::Screen::Orientation orient) {
	if (!orientEnabled) return mxv2::MakeBundledSkinRef(mxv2::Settings::DefaultSkinName());
	return (orient == mxv2::Screen::kPortrait) ? mxv2::Settings::DefaultSkinPortrait()
	                                            : mxv2::Settings::DefaultSkinLandscape();
}

// ini に書かれた順でファイルシステムをマウントする。仕様 (filesystem.md) の
// とおり、知らないものは警告して捨て、削除できないものが抜けていれば足す。
// 直したところがあれば true を返す（読み終えてから書き戻すため）。
bool LoadFileSystems(mxv2::Vfs *vfs, const std::vector<std::string> &refs,
                     Warnings *box) {
	bool fixed = false;
	vfs->ClearMounts();
	for (size_t i = 0; i < refs.size(); i++) {
		mxv2::FileSystem *fs = 0;

		// 場所を持つもの（フォルダマウントや外部 FS）は、ここで作って預ける。
		// 同じ場所が二重に書かれていたら、先に作ったほうを使う。
		mxv2::FileSystem *made = vfs->CreateFromMountRef(refs[i]);
		if (made != 0) {
			if (vfs->Add(made)) {
				fs = made;
			} else {
				fs = vfs->FindByMountRef(made->mountRef());
				delete made;
				fixed = true;
			}
		} else {
			std::string rel;
			if (!vfs->Parse(refs[i], &fs, &rel)) fs = 0;
		}

		if (fs == 0) {
			Warn(box, mxv2::MsgF("Log.UnknownFileSystem", refs[i]));
			fixed = true;
			continue;
		}
		// 同じものが二重に書かれていたか、この環境では使えない
		// （Android のローカル FS）。どちらも書き戻して消す。
		if (!vfs->Mount(fs)) fixed = true;
		// アクセス許可が失われているもの（再インストールで SAF の権限が
		// 消えた）は**残す**。警告だけ出し、ini は直さない（消すと、その先を
		// 指すブックマークまで失われる）。取り直しは [ファイルシステムの設定]。
		if (!fs->accessible()) {
			WarnRegrant(box, mxv2::MsgF("Log.FileSystemNoAccess", fs->label()), fs->mountRef());
		}
	}
	if (vfs->EnsureRequired()) fixed = true;
	return fixed;
}

// ini に書かれたブックマークを ref へ揃える。仕様 (bookmark.md) のとおり、
// 知らないファイルシステムは警告して捨てる。到達できるかどうかはここでは
// 見ない（時間が掛かるし、外付けが外れているだけかもしれない）。
// 直したところがあれば true を返す（読み終えてから書き戻すため）。
bool LoadBookmarks(const mxv2::Vfs &vfs, std::vector<std::string> *refs,
                   Warnings *box) {
	bool fixed = false;
	std::vector<std::string> out;
	for (size_t i = 0; i < refs->size(); i++) {
		mxv2::FileSystem *fs = 0;
		std::string rel;
		if (!vfs.Parse((*refs)[i], &fs, &rel)) {
			Warn(box, mxv2::MsgF("Log.BookmarkUnknownFs", (*refs)[i]));
			fixed = true;
			continue;
		}
		// ファイルシステムの選択そのもの（空の ref）と、ブックマークの
		// 一覧 (bookmark:) 自身は控えられない。
		if (fs == 0 || fs->isJumpList()) {
			fixed = true;
			continue;
		}
		const std::string ref = mxv2::Vfs::MakeRef(fs, rel);
		if (ref != (*refs)[i]) fixed = true;  // 書き方を揃えた
		out.push_back(ref);
	}
	if ((int)out.size() > mxv2::Settings::kMaxBookmarks) {
		out.resize(mxv2::Settings::kMaxBookmarks);
		fixed = true;
	}
	*refs = out;
	return fixed;
}

// 今のマウント順を ini に書く形へ。
std::vector<std::string> SaveFileSystems(const mxv2::Vfs &vfs) {
	std::vector<std::string> out;
	for (int i = 0; i < vfs.count(); i++) {
		// 場所を持つ外部ファイルシステムは "<id>:<場所>" を返す。
		out.push_back(vfs.at(i)->mountRef());
	}
	return out;
}

// 音まわりのログ 1 行。起動時と、出力レートを変えて開き直したときに出す。
void PrintAudioInfo(const mxv2::Player &player, bool latencyAuto) {
	// カタログの値は前後の空白が落ちるので、区切りはこちらで足す。
	std::string line =
	    mxv2::MsgF("Log.Audio", mxv2::MsgNum("%d", player.sampleRate()),
	               mxv2::MsgNum("%d", player.audioBufferFrames()),
	               mxv2::MsgNum("%d", player.displayLatencyFrames()),
	               mxv2::MsgNum("%.1f", player.displayLatencyFrames() * 1000.0 /
	                                        player.sampleRate()));
	if (latencyAuto) line += std::string(" ") + mxv2::Msg("Log.AudioAuto");
	printf("audio    : %s\n", line.c_str());
	// どの口で鳴らしているか。Android は AAudio と OpenSL ES で音の
	// 途切れやすさが変わるので、切り分けに要る。
	{
		const char *driver = SDL_GetCurrentAudioDriver();
		if (driver != 0) printf("audiodrv : %s\n", driver);
	}
	fflush(stdout);
}

// 前の版が mxv2.ini を置いていた場所から、1 度だけ引き取る（元は残す）。
// 引き取り先が既にあれば何もしない。心当たりは 2 つ:
//   ・実行ファイルの隣（デスクトップの旧い版）
//   ・Android の内部ストレージ（外から見えないので、ユーザーフォルダを
//     外部へ移した 2026-08-31 より前の版）
void MigrateLegacySettings(const std::string &newPath) {
	if (mxv2::FileExists(newPath)) return;

	std::vector<std::string> olds;
	olds.push_back(mxv2::JoinPath(mxv2::ExecutableDir(), "mxv2.ini"));
	{
		const std::string legacy = mxv2::LegacyUserDataDir(kUserDirName);
		if (!legacy.empty()) olds.push_back(mxv2::JoinPath(legacy, "mxv2.ini"));
	}

	for (size_t i = 0; i < olds.size(); i++) {
		const std::string &oldPath = olds[i];
		if (mxv2::DirNameOf(oldPath) == mxv2::DirNameOf(newPath)) continue;
		std::vector<uint8_t> data;
		if (!mxv2::FileExists(oldPath) || !mxv2::ReadWholeFile(oldPath, &data)) continue;
		if (!mxv2::WriteWholeFile(newPath, data)) continue;
		printf("settings : %s\n", mxv2::MsgF("Log.SettingsMigrated", oldPath).c_str());
		return;
	}
}

// 画面をまるごと描き直させる。GL コンテキストが失われたあと
// (SDL_RENDER_DEVICE_RESET / TARGETS_RESET) と、バックグラウンドから戻った
// ときに呼ぶ。テクスチャは中身だけでなく**器ごと**無効になっているので
// 作り直し、mxv2 は差分更新なので**「もう描いた」印まで戻す**。
void ForceRedrawAll(mxv2::Screen *screen, mxv2::TextLayer *textLayer, mxv2::DrawScreen *draw,
                    mxv2::Player *player, mxv2::SettingsUi *ui, bool *chromeRefresh,
                    bool *fileListRefresh) {
	std::string err;
	if (!screen->ResetTextures(&err)) printf("warning  : %s\n", err.c_str());
	if (!textLayer->Rebuild(screen, &err)) printf("warning  : %s\n", err.c_str());
	ui->HandleDeviceReset();
	draw->Reload();
	player->RequestStatusRefresh();
	*chromeRefresh = true;
	*fileListRefresh = true;
}

// 切り替えかたから、いま使うべき向きを決める。「起動時の方向で切り替える」と
// 「常に切り替える」は、渡された今の向きをそのまま使う。
mxv2::Screen::Orientation OrientationForMode(int mode, mxv2::Screen::Orientation now) {
	if (mode == mxv2::Settings::kOrientPortraitOnly) return mxv2::Screen::kPortrait;
	if (mode == mxv2::Settings::kOrientLandscapeOnly) return mxv2::Screen::kLandscape;
	return now;
}

// 端末そのものの向きを、切り替えかたに合わせて固定する（Android だけ。
// それ以外では orientlock が何もしない）。
//
// **固定しても上下反転は許す**（SENSOR_PORTRAIT / SENSOR_LANDSCAPE）。
// 「常に切り替える」なら自由に回してよい。
void ApplyOrientationMode(int mode, mxv2::Screen::Orientation now) {
	switch (mode) {
		case mxv2::Settings::kOrientPortraitOnly:
			mxv2::orientlock::Set(mxv2::orientlock::kPortrait);
			break;
		case mxv2::Settings::kOrientLandscapeOnly:
			mxv2::orientlock::Set(mxv2::orientlock::kLandscape);
			break;
		case mxv2::Settings::kOrientStartup:
			// 起動した（か、設定を閉じた）ときの向きで固定する。
			mxv2::orientlock::Set((now == mxv2::Screen::kPortrait)
			                          ? mxv2::orientlock::kPortrait
			                          : mxv2::orientlock::kLandscape);
			break;
		default:
			mxv2::orientlock::Set(mxv2::orientlock::kFree);
			break;
	}
}

// 窓の大きさに合わせてキャンバスを作り直す（fullscreen.md）。
//
// スキンは「宣言サイズ」でレイアウトを持っているが、窓の縦横比がそれと
// 違うときは、ファイラーを置いた向きだけ伸ばして窓に寄せる。伸ばせる量は
// 背景ビットマップの大きさで頭打ちになり、あふれたぶんは今までどおり
// レターボックスになる。
//
// 呼ぶのは**フレームの頭で 1 回だけ**。窓のリサイズはイベントで印を立てる
// だけにして、ここでまとめて作り直す。これがそのままリサイズ中のデバウンスに
// なる（ドラッグ中に何十回イベントが来ても、作り直すのは 1 フレームに 1 回）。
// 大きさが変わっていなければ何もしない。作り直したら true。
bool SyncCanvasToWindow(mxv2::Screen *screen, mxv2::TextLayer *textLayer,
                        mxv2::DrawScreen *draw, mxv2::Filer *filer, const mxv2::Skin &skin) {
	int outW = 0, outH = 0;
	screen->GetOutputSize(&outW, &outH);
	int cw = 0, ch = 0;
	skin.CanvasSizeFor(outW, outH, draw->stretchLimit(), &cw, &ch);
	if (cw == screen->width() && ch == screen->height()) return false;

	// 順番が大事: 画面 -> 文字レイヤー -> DrawScreen。
	// DrawScreen::Resize は最後に Reload() まで済ませて曲名を描き直すので、
	// その前にレイヤーを作り直しておく。
	std::string err;
	if (!screen->SetCanvasSize(cw, ch, &err)) {
		printf("warning  : %s\n", err.c_str());
		return false;
	}
	textLayer->SyncToScreen(screen);
	if (!draw->Resize(cw, ch, &err)) {
		printf("warning  : %s\n", err.c_str());
		return false;
	}
	filer->SetViewMetrics(draw->fileListRows(), draw->fileListItemH());
	return true;
}

}  // namespace app
}  // namespace mxv2
