// mxv2 - 外から渡された MDX を開く（Android の VIEW インテント）
//
// ファイルマネージャなどで *.mdx を叩くと、mxv2 が起動する（または動いている
// mxv2 が前面に出る）。渡されるのは**素のパスではなく URI**（たいていは
// `content://...`）で、Android では素のパスで端末のファイルを触れないので、
// ここで VFS の ref へ直してから main.cpp の OpenHandedPath へ渡す。
//
// **ref へ直す道は 2 つあり、どちらで開くかはユーザーが選ぶ**
// （2026-09-18、ユーザーの指示。PDX を使う曲が半端に鳴るのは違和感が大きい）:
//   1. `ResolveInTree()` … すでに許可のあるツリー (SAF) の中にあれば
//      `saf:<ツリー>/<相対パス>`。**隣の PDX も読め、ファイラーもその
//      フォルダを開く**。UI を出さずに済むので、まずこれを試す。
//   2. 外れたら [渡されたファイル] のダイアログで尋ねる:
//      **[フォルダを許可する…]** … SAF のピッカーでそのフォルダの許可を
//        取り、マウントしてから 1. と同じ形で開く（PDX も鳴る）。
//      **[このまま演奏する]** … `CopyToUserDir()` でユーザーフォルダの
//        `mdx/inbox/` へ写して `userdir:inbox/<名前>`。すぐ鳴るが、単独の
//        ドキュメントの許可では隣を読めないので **PDX は付いてこない**
//        （`mdx/pdx/` や [PDX の探索先] に置いてあるぶんは効く）。
//
// 受け取り口は 2 つある。**起動のとき**は Java の getArguments() が URI を
// 最後の引数に足すので、cmdline の target としてここへ来る（IsUri で見分ける）。
// **動いている最中**に渡されたぶんは Java が溜め、Poll() で汲む
// （Windows の singleinstance::Poll と同じ位置づけ）。
//
// Android 以外では何もしない（Available() も IsUri() も false）。

#ifndef MXV2_OPENINTENT_H
#define MXV2_OPENINTENT_H

#include <string>

#include "vfs.h"

namespace mxv2 {
namespace openintent {

// この環境で外からファイルを渡されうるか（Android だけ true）。
bool Available();

// 外から渡された URI の形か（"content://" / "file://"）。Android 以外は
// 常に false ——「ref でもパスでもないもの」を作らないため。
bool IsUri(const std::string &s);

// 起動後に渡されたものを 1 つ取り出す（毎フレーム、空になるまで呼ぶ）。
// 中身は URI なので、開く前に ref へ直すこと。
// **前面に出すのは OS がやってくれる**ので、呼び出し側は開くだけでよい。
bool Poll(std::string *uri);

// OS の許可（持続許可）のあるツリーの中にあるか。あれば *treeUri にツリーの
// URI、*rel にその中の相対パスを入れる。**マウントしているかは見ない**
// （mxv2 の一覧から外してあっても、許可が残っていれば true）。
bool GrantedTree(const std::string &uri, std::string *treeUri, std::string *rel);

// 許可があって**マウントもしている**ツリーの中にあれば ref を作る。
// **UI は出さない**ので、渡されたものを受け取ったらまずこれを試す。
//
// **確かめるのはそのツリーのマウントだけ** (`Vfs::FindByMountRef`)。
// `Vfs::Exists()` で見てはいけない——マウントしていないツリーの ref は
// `Vfs::FindForRef` が**別の saf マウントに割り当ててしまう**ので、
// 「別のフォルダの根」が在ることをもって在ると答えてしまう
// （2026-09-18 の不具合。開こうとすると 2 つの URI が繋がった ref になる）。
bool ResolveInTree(const Vfs &vfs, const std::string &uri, std::string *ref);

// ユーザーフォルダの mdx/inbox/ へ写して ref を作る（**PDX は付いてこない**）。
// 読めない・写せないときは false（理由はログに出してある）。
//
// **ファイルの大きさぶんの読み書きが要る**。MDX は数十 KB なので、
// メインスレッドで呼んで構わない。
bool CopyToUserDir(const Vfs &vfs, const std::string &uri, std::string *ref);

// ダイアログに出す名前（提供元が名乗る表示名。取れなければ URI の末尾）。
std::string DisplayName(const std::string &uri);

// SAF のピッカーに最初に見せる場所（渡されたファイルの**親フォルダ**の
// ドキュメント URI）。作れなければ空（そのときはピッカーの既定の場所から）。
std::string ParentDocUri(const std::string &uri);

}  // namespace openintent
}  // namespace mxv2

#endif  // MXV2_OPENINTENT_H
