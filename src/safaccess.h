// mxv2 - SAF (Storage Access Framework) 越しのファイルシステム（Android 専用）
//
// Android では素のパスで端末のフォルダを触れない（`LocalFileSystem` は
// available() が false）。代わりに、ユーザーに選んでもらったフォルダの
// ツリー（`ACTION_OPEN_DOCUMENT_TREE` の結果）を 1 つのファイルシステムとして
// マウントする。読み書きの実体は Java 側の `SafBridge`（android/app/src/main/
// java/net/gorry/mxv2/SafBridge.java）で、ここはその JNI の窓口。
//
// **ref の形**: `saf:<ツリーの URI>/<相対パス>`。マウントの場所そのものが
// ツリーの URI なので、`mountRef()` は `saf:<ツリーの URI>` になり、
// `Contains()` が前方一致でどのマウントの持ち物かを決める（`dir:` と同じ流儀）。
//
// **選ぶのは非同期**。`SafPickTree()` は選択画面を出すだけで、戻ってくるのは
// アプリがバックグラウンドから復帰したあと。`SafPollPicked()` を毎フレーム
// 見て、結果が届いてからマウントする。
//
// Android 以外では、`SafAvailable()` が false を返すだけの空の実装になる。

#ifndef MXV2_SAFACCESS_H
#define MXV2_SAFACCESS_H

#include <string>

#include "vfs.h"

namespace mxv2 {

// この環境で SAF が使えるか。Android 以外は常に false。
bool SafAvailable();

// 端末の「フォルダを選ぶ」画面を出す。開けたら true。
// 結果は SafPollPicked() で拾う（この呼び出しでは待たない）。
// initialTreeUri を渡すと、その場所を最初に見せる（許可を取り直すときに、
// 同じフォルダを選びやすくする。Android 8 以降でだけ効く）。
bool SafPickTree(const std::string &initialTreeUri = std::string());

// 選び終わっていれば true を返し、*uri にツリーの URI を入れる。
// 取り消されたときも true で、*uri は空になる。まだなら false。
bool SafPollPicked(std::string *uri);

// ツリーの URI を根にするファイルシステムを作る。SAF が使えない環境では 0。
// **権限が切れていても作る**（accessible() が false のマウントになる。
// 再インストールでクラウドから戻った ini の項目を消さないため）。
// 所有権は呼び出し側（Vfs::Add に渡す）。
FileSystem *CreateSafFileSystem(const std::string &treeUri);

}  // namespace mxv2

#endif  // MXV2_SAFACCESS_H
