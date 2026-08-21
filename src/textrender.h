// mxv2 - 日本語を含む文字列の描画
//
// 旧 mxv は曲名とファイルリストを GDI + ＭＳ ゴシックで描いていた。当時は
// それしか手が無かったからで、実体は「小さなサイズ用の埋め込みビットマップを
// GDI が拾ってくれる」ことに頼った作りだった。mxv2 では現代化の方針として、
// アウトラインフォントをアンチエイリアス付きで焼く。
//
// 描画結果は「8bpp バッファに 0..255 のカバレッジを置く」形で返す。色を乗せる
// のは呼び出し側 (BmpBlendMask) の仕事。この形にしておくと、テーマの文字色と
// Bright をそのまま使える。
//
// 実装は stb_truetype (third_party/imgui に同梱) と同梱フォントだけで
// 完結する。OS のフォント API に依存しないので、Windows / Android /
// Emscripten で同じ絵が出る。
//
// 5x7 フォント (スキンの font5x7.bmp) はビジュアライザ用であって、ここの
// 代わりにはならない。フォントが読めなかったときだけ、最低限の非常用として
// 呼び出し側が使う。

#ifndef MXV2_TEXTRENDER_H
#define MXV2_TEXTRENDER_H

#include <string>
#include <vector>

#include "bitmap.h"

namespace mxv2 {

class TextRenderer {
public:
	virtual ~TextRenderer() {}

	// 使える実装があるか。false ならこのオブジェクトの Draw は何もしない。
	virtual bool available() const = 0;

	// dst (8bpp) の (x,y) に UTF-8 文字列を描く。画素にはカバレッジ
	// (0=透明 .. 255=不透明) が入る。既にある値より大きいときだけ書く。
	// maxWidth を超える分は切り捨てる。cellHeight は 1 行の高さ (px)。
	// 出力解像度へ拡大して描くため、cellHeight は実数で受ける。
	virtual void Draw(Bitmap *dst, int x, int y, int maxWidth, float cellHeight,
	                  const std::string &utf8) = 0;
};

// 実装を作る。searchDirs を順に見て font.ttf を探し、どこにも無ければ
// 同じ順で同梱フォントを探す（差し替え用の font.ttf が常に優先されるよう、
// 2 周に分けている）。並びは Skin と AssetPaths が決める (FontSearchDirs)。
// 呼び出し側が delete する。
TextRenderer *CreateTextRenderer(const std::vector<std::string> &searchDirs);

}  // namespace mxv2

#endif  // MXV2_TEXTRENDER_H
