// mxv2 - MDX / PDX の読み込み
//
// 旧 mxv の PlayMDX() が自前でやっていた MDX ヘッダ解析を、portable_mdx の
// mdx_util へ委譲したもの。PDX の探索順は旧 mxv の挙動を踏襲する。
//
// 場所の指定は全て ref（vfs.h）。読み込みは Vfs 経由なので、同梱アセットや
// ユーザーフォルダの曲もローカルの曲と同じ道を通る。

#ifndef MXV2_MDXSONG_H
#define MXV2_MDXSONG_H

#include <cstdint>
#include <string>
#include <vector>

namespace mxv2 {

class Vfs;

struct MdxSong {
	std::string path;         // 読み込んだ MDX の ref (UTF-8)
	std::string titleSjis;    // タイトル (MDX 内の生バイト = Shift_JIS)
	std::string title;        // タイトル (UTF-8)
	std::string pdxFileName;  // MDX が要求する PDX ファイル名 (Shift_JIS)
	std::string pdxPath;      // 実際に読めた PDX の ref。無ければ空
	bool requiresPdx;         // MDX が PDX を要求しているか
	bool hasPdx;              // PDX を実際に読み込めたか

	std::vector<uint8_t> mdxBuffer;  // MXDRV_SetData2 へ渡す形式
	std::vector<uint8_t> pdxBuffer;  // 同上。PDX 無しなら空

	MdxSong() : requiresPdx(false), hasPdx(false) {}
};

// MDX を読み込み、MXDRV へ渡せる形に整える。
//   pdxSearchDirs: MDX と同じディレクトリで見つからなかったときに探す場所
//                  （ref。ファイルシステムをまたいでもよい）。
// PDX が見つからなくても MDX 自体が読めていれば true を返す（FM のみで鳴る）。
// 失敗時は err にメッセージ (UTF-8) を入れる。
bool LoadMdxSong(const Vfs &vfs,
                 const std::string &mdxRef,
                 const std::vector<std::string> &pdxSearchDirs,
                 MdxSong *out,
                 std::string *err);

}  // namespace mxv2

#endif  // MXV2_MDXSONG_H
