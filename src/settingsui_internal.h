// mxv2 - 設定 UI の内部ヘッダ。settingsui_*.cpp だけが含める。
//
// SettingsUi は 1 つのクラスだが、実装はダイアログ単位でファイルを分けてある
// （2026-09-16。1 ファイル 3500 行を超えたため）:
//   settingsui.cpp             … 初期化・フレーム処理 (Build)・共通の小道具
//   settingsui_settings.cpp    … [mxv2 の設定] (F1)
//   settingsui_colors.cpp      … [配色設定] (F2)
//   settingsui_filesystems.cpp … [ファイルシステムの設定] (F3)・SAF
//   settingsui_bookmarks.cpp   … [ブックマークの設定] (F4)・Shift+M
//   settingsui_pdx.cpp         … [PDX の探索先]
//   settingsui_folder.cpp      … フォルダを選ぶ (L)
//   settingsui_menu.cpp        … コンテキストメニュー・終了の確認
//   settingsui_info.cpp        … [操作方法] (F11)・[バージョン情報] (F12)・起動時の警告
// ここにあるのは、複数のファイルで使う題名・定数・描画の小道具。

#ifndef MXV2_SETTINGSUI_INTERNAL_H
#define MXV2_SETTINGSUI_INTERNAL_H

#include <string>

#include "imgui.h"

#include "message.h"

namespace mxv2 {

class Player;

namespace settingsui {

// 指で押すところの高さの下限 (mm)。指の腹が当たる幅として Material Design は
// 48dp (約 7.6mm)、Apple は 44pt (約 7mm) を勧めているが、mxv2 のダイアログは
// 項目が多く画面が狭いので、下限として 6mm を採る。
const float kTouchTargetMm = 6.0f;

// 指で操作するときの字の大きさ (mm)。行だけ太くして字が小さいままだと
// 押せても読めないので、下限として持っている。日本語は仮想ボディいっぱいに
// 書かれるので、漢字の実寸はほぼこの値になる。
//
// **行の高さ (kTouchTargetMm) とは切り離してある**。かつては行の 0.6 倍
// (6mm の行に 3.6mm の字) にしていたが、それだと Android の縦画面で
// コンボやスライダーのラベルに 6 文字しか入らなかった。ImGui はラベルを
// 折り返さないので、はみ出したぶんは黙って切れる。
// 3.1mm は Android の 19sp 相当で、本文 (14sp = 約 2.2mm) より大きい。
// これでラベルはおよそ 7 文字ぶんになる。3.6mm では大きく 2.6mm では
// 小さいという実機での判断で、その中間を採った。
//
// 押せるところの高さは 6mm のままなので、**指での操作しやすさは変わらない**
// （字が小さくなったぶんは上下の余白に回る）。
const float kTouchFontMm = 3.1f;

// ダイアログの題名。文言はカタログ、"###" 以降は ImGui の id。
// SyncModal が題名をポインタで見分けるので、**毎フレーム同じ番地**を
// 返さないといけない。カタログの文字列はそのまま使えるが、id を繋いだ
// ものは 1 度だけ組み立てて使い回す。実体は settingsui.cpp。
extern const char *kSettingsTitle;
extern const char *kColorsTitle;
extern const char *kOverwriteTitle;
extern const char *kFolderTitle;
extern const char *kPdxFolderTitle;
extern const char *kBookmarkFolderTitle;
extern const char *kHelpTitle;
extern const char *kFileSystemsTitle;
extern const char *kFsRemoveTitle;
extern const char *kBookmarksTitle;
extern const char *kBmRemoveTitle;
extern const char *kBmToggleTitle;
extern const char *kPdxPathsTitle;
extern const char *kPdxRemoveTitle;
extern const char *kAboutTitle;
extern const char *kStartupTitle;
extern const char *kAddFsTitle;
extern const char *kQuitTitle;

// 題名を作る（1 度だけ）／言語を替えたあとに取り直す。
void InitTitles();
void ResetTitles();

// 文言に ImGui の id を足した名札。同じ文言を 1 つの画面で何度も使うため。
std::string L(const char *key, const char *id);

// 中身をドラッグしてスクロールする（指で操作するとき用）。
void DragToScroll(bool *dragging, bool *moved, bool hasTitleBar, bool fromItems);

// 折り返す色付きの文。注記（薄い字）と誤りの文言（赤）。
void TextWrapColor(const ImVec4 &color, const char *text);
void TextNote(const char *text);
void TextError(const char *text);
// 確認ダイアログの本文。
void ConfirmText(const char *text);
// 前の項目の右に label が入るなら SameLine、入らなければ改行。
void SameLineOrWrap(const char *label);
// 設定ウィンドウの見出し（畳める）と、その末尾の余白。
bool GroupHeader(const char *label);
void GroupTrailingSpace();
// 次のウィンドウを画面の中央に出す。
void CenterNextWindow(ImGuiCond cond);
// 前後の空白を落とす（入力欄の値）。
std::string TrimSpaces(const std::string &s);

}  // namespace settingsui
}  // namespace mxv2

#endif  // MXV2_SETTINGSUI_INTERNAL_H
