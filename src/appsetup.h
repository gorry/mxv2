// mxv2 - 起動時の組み立て（main.cpp から切り出し）
//
// 起動時の警告の箱、ini からのファイルシステムとブックマークの復元、
// スキンの落とし先、画面の向き、キャンバスの合わせ直し。

#ifndef MXV2_APPSETUP_H
#define MXV2_APPSETUP_H

#include <string>
#include <vector>

#include "screen.h"
#include "settingsui.h"

namespace mxv2 {

class DrawScreen;
class Filer;
class Player;
struct Skin;
class TextLayer;
class Vfs;

namespace app {

// 起動時の警告。ウィンドウが開く前に起きたことは、ログに出しても
// 気付かれないので、ためておいて最初のフレームでダイアログに出す。
// 演奏中に出る警告（PDX が無い、など）はログだけ。あちらは操作の結果として
// その場で出るものなので、起動時の箱には入れない。
typedef std::vector<mxv2::SettingsUi::StartupWarning> Warnings;

void Warn(Warnings *box, const std::string &text);
void WarnRegrant(Warnings *box, const std::string &text, const std::string &mountRef);

std::string DefaultSkinFor(bool orientEnabled, mxv2::Screen::Orientation orient);

bool LoadFileSystems(mxv2::Vfs *vfs, const std::vector<std::string> &refs, Warnings *box);
bool LoadBookmarks(const mxv2::Vfs &vfs, std::vector<std::string> *refs, Warnings *box);
std::vector<std::string> SaveFileSystems(const mxv2::Vfs &vfs);

void PrintAudioInfo(const mxv2::Player &player, bool latencyAuto);
void MigrateLegacySettings(const std::string &newPath);

mxv2::Screen::Orientation OrientationForMode(int mode, mxv2::Screen::Orientation now);
void ApplyOrientationMode(int mode, mxv2::Screen::Orientation now);

bool SyncCanvasToWindow(mxv2::Screen *screen, mxv2::TextLayer *textLayer,
                        mxv2::DrawScreen *draw, mxv2::Filer *filer, const mxv2::Skin &skin);
void ForceRedrawAll(mxv2::Screen *screen, mxv2::TextLayer *textLayer, mxv2::DrawScreen *draw,
                    mxv2::Player *player, mxv2::SettingsUi *ui, bool *chromeRefresh,
                    bool *fileListRefresh);

}  // namespace app
}  // namespace mxv2

#endif  // MXV2_APPSETUP_H
