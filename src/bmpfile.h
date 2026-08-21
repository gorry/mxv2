// mxv2 - Windows BMP ファイルの読み込み
//
// 旧 mxv は素材ビットマップを .rc のリソースとして持っていたが、mxv2 は
// スキンのフォルダに置いた .bmp をそのまま読む（探す場所は assetpath.h）。
//
// 1/4/8bpp はパレット付き 8bpp として、16/24/32bpp は 24bpp として読み込む。
// 8bpp のままにするのは、素材のパレットを差し替えて色を変える旧 mxv の
// 手口（鍵盤の色、レベルメータの点灯色など）をそのまま使うため。

#ifndef MXV2_BMPFILE_H
#define MXV2_BMPFILE_H

#include <string>

#include "bitmap.h"

namespace mxv2 {

bool LoadBmpFile(const std::string &path, Bitmap *out, std::string *err);

}  // namespace mxv2

#endif  // MXV2_BMPFILE_H
