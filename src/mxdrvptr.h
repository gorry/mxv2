// mxv2 - MXDRV ワーク内オフセットの解決
//
// portable_mdx はポインタを 32bit オフセットで持つ (MXWORK_CH::S0000 /
// S0004 / MXWORK_GLOBAL::L001e34 など)。基準は MxdrvContext::m_impl の
// アドレスで、これは公開ヘッダ mxdrv_context.h の構造体メンバなので、
// portable_mdx を改変せずに同じ変換を再現できる。
//
// (portable_mdx 内部の mxdrv_context.internal.h にある TO_PTR と同じ計算)

#ifndef MXV2_MXDRVPTR_H
#define MXV2_MXDRVPTR_H

#include <cstdint>

#include <mxdrv_context.h>

namespace mxv2 {

inline const uint8_t *MxdrvOfsToPtr(const MxdrvContext *context, uint32_t ofs) {
	if (ofs == 0) return 0;
	return (const uint8_t *)((uintptr_t)context->m_impl + (uintptr_t)ofs);
}

}  // namespace mxv2

#endif  // MXV2_MXDRVPTR_H
