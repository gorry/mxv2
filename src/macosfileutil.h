// macOS の素材パス。SDL に依存しない検証ツールからも呼べる。
#ifndef MXV2_MACOSFILEUTIL_H
#define MXV2_MACOSFILEUTIL_H

#include <string>

namespace mxv2 {
std::string MacResourceDir();
std::string MacExecutableBaseName();
}

#endif
