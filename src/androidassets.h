// mxv2 - apk の中の同梱素材を内部ストレージへ展開する（Android 専用）
//
// Android の apk に入れた assets は **fopen で開けず、列挙もできない**。
// mxv2 の読み出しは skin も font も配色もメッセージカタログもすべて
// fileutil の FILE* 経由なので、そのままでは何一つ読めない。
//
// そこで、初回起動時に apk の中身を内部ストレージへ展開し、以降は
// 「実行ファイルの隣」と同じ姿（ExecutableDir() の下に assets/ と
// NOTICE / LICENSE が並ぶ）で読む。ExecutableDir() は Android では
// SDL_AndroidGetInternalStoragePath() を返す（fileutil.cpp）。
//
// **何を展開するかはビルド時に作った索引 assetindex.txt が持つ。**
// apk の中は列挙できないので、実行時に数え上げることはできない。索引は
// android/app/build.gradle の generateMxv2AssetIndex が作る。書式は
//
//   # コメント
//   <crc32(8桁16進)> <バイト数> <展開先の相対パス>
//
// **展開先は内部ストレージの根ではなく、その下の bundled/**（`ExecutableDir()`
// が返す場所）。apk を読む `SDL_RWFromFile` は相対パスを渡されると、まず
// <内部ストレージ>/<相対パス> を開きにいって、無いときだけ apk を見る。
// 根へ展開すると展開結果が apk を覆い隠し、2 回目からは古いものを読み直す
// だけになる（素材を差し替えても更新できない）。fileutil.cpp も参照。
//
// 展開済みのものと索引を突き合わせ、**変わったものだけ**書き直す
// （毎回 2.6MB を書き戻すと起動が遅くなる）。索引から消えたファイルは
// 消す。展開先は apk ごとのプライベート領域なので、ユーザーが置いた
// ものを巻き添えにすることはない（ユーザーぶんは UserDataDir 側）。

#ifndef MXV2_ANDROIDASSETS_H
#define MXV2_ANDROIDASSETS_H

#include <string>
#include <vector>

namespace mxv2 {

// apk の assets を destDir（= ExecutableDir()）へ展開する。
// すでに同じものが展開済みなら何もしない。
// 途中で失敗しても残りは続け、うまくいかなかったものを warnings へ足す
// （文言はカタログを読む前に呼ばれるので英語のまま）。
// 1 つも取りこぼさなければ true。
bool ExtractBundledAssets(const std::string &destDir, std::vector<std::string> *warnings);

}  // namespace mxv2

#endif  // MXV2_ANDROIDASSETS_H
