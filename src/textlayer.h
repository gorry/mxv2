// mxv2 - 文字だけを出力解像度で描くレイヤー
//
// mxv2 の本体画面は 640x480 のキャンバスで、SDL_RenderSetLogicalSize により
// 最近傍で拡大される。ドット絵はそれで正しいが、アンチエイリアスの効いた
// 文字を同じ経路に乗せると、拡大時に再びエイリアスされて線の太さが
// ばらついてしまう（DPI 175% なら 1.75 倍という半端な倍率になる）。
//
// そこでファイラーと曲名の文字だけは、このレイヤーが**拡大後の解像度**で
// 直接ラスタライズし、キャンバスを描いた後にアルファ合成で重ねる。
// 文字は常に等倍で出るので、倍率が半端でも品質が落ちない。
//
// 座標は呼び出し側から見て 640x480 の論理座標のまま。倍率の掛け算は
// ここに閉じ込めてある。

#ifndef MXV2_TEXTLAYER_H
#define MXV2_TEXTLAYER_H

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "bitmap.h"

namespace mxv2 {

class Screen;
class TextRenderer;

class TextLayer {
public:
	TextLayer();
	~TextLayer();

	// フォントを読み、出力解像度に合わせたバッファを作る。fontDirs は
	// フォントを探す場所の並び（Skin と AssetPaths が決める）。
	bool Init(Screen *screen, const std::vector<std::string> &fontDirs, std::string *err);
	void Shutdown();

	// フォントが読めたか。false なら呼び出し側がミニフォントの ASCII へ退避する。
	bool available() const;

	// ウィンドウの大きさが変わっていたら作り直す。作り直したら true を返す
	// （呼び出し側は画面を描き直す必要がある）。
	bool SyncToScreen(Screen *screen);

	// 大きさが同じでも作り直す（スキンの切り替えなど）。
	bool Rebuild(Screen *screen, std::string *err) { return Resize(screen, err); }

	void ClearAll();
	// 論理座標の矩形を透明に戻す。
	void ClearRect(int x, int y, int width, int height);

	// 論理座標に文字を描く。cellHeight は論理の行高、bright は配色の
	// Bright 値 (0..100)。
	//
	// clipY / clipH は書き込んでよい縦の範囲（論理座標）。ファイラーを画素単位で
	// スクロールすると上下の端に半端な行が出るので、字の位置 (y) は動かさずに
	// 出力側だけを切る必要がある。clipH <= 0 なら y..y+cellHeight を使う。
	void DrawText(int x, int y, int maxWidth, int cellHeight, const std::string &utf8,
	              const Rgb &color, int bright, int clipY = 0, int clipH = 0);

	// キャンバスの上へ重ねる。Screen::Draw と Present の間で毎フレーム呼ぶ。
	void Render(Screen *screen);

	// スキンが変わったらフォントを読み直す。
	void SetFontDirs(const std::vector<std::string> &fontDirs);

private:
	bool Resize(Screen *screen, std::string *err);
	void Release();

	std::vector<std::string> fontDirs_;
	TextRenderer *text_;
	SDL_Texture *texture_;

	int width_;   // 出力解像度でのレイヤーの大きさ
	int height_;
	float scaleX_;
	float scaleY_;

	std::vector<uint32_t> pixels_;  // ARGB8888 (ストレートアルファ)
	Bitmap scratch_;                // 8bpp のカバレッジ置き場
	bool dirty_;

	TextLayer(const TextLayer &);
	TextLayer &operator=(const TextLayer &);
};

}  // namespace mxv2

#endif  // MXV2_TEXTLAYER_H
