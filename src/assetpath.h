// mxv2 - 素材の置き場所（同梱ぶんとユーザーぶん）
//
// 素材は 2 か所に分かれる。
//
//   <実行ファイル>/assets/    同梱ぶん。**読むだけ**
//   <ユーザーフォルダ>/       ユーザーぶん。書き込み可
//
// 同梱ぶんを書き換えないのは、
//   ・アプリを入れ替えると消える（ユーザーの手が入ったものを巻き添えにする）
//   ・Program Files の下や Android の apk の中は、そもそも書けない
// ため。設定ウィンドウで変えた配色や、ユーザーが足したスキンは
// すべてユーザーフォルダ側へ入る。ユーザーフォルダの場所は
// UserDataDir() が決める（Windows なら %APPDATA%\mxv2\）。
//
// ルート直下のファイル（font.ttf など）は両方を見る。探す順はユーザー
// フォルダが先なので、同じ名前のものがあればユーザーぶんが勝つ。
//
// スキンは「どちらのフォルダのものか」を名前で区別する。スキンの指定
// （このコードでは ref と呼ぶ）は 2 通り:
//
//   "<名前>"          ユーザーフォルダの skin/<名前>。
//                     そこに無ければ同梱の skin/<名前>
//   "assets:<名前>"   同梱の skin/<名前> を名指しする
//
// 同じ名前のスキンが両方にあっても混ざらない。**1 つのスキンは
// どちらか一方のフォルダのもの**で、もう一方を使いたければ layout.ini に
// `[Skin] Base=assets:<名前>` と書いて土台にする。一覧に出すときは
// 同梱ぶんに "assets:" を付けて出すので、名前がぶつかっていても選び分けられる。

#ifndef MXV2_ASSETPATH_H
#define MXV2_ASSETPATH_H

#include <string>
#include <vector>

namespace mxv2 {

// 同梱ぶんを名指しする接頭辞。フォルダ名に ':' は使えないので、
// 名前とぶつかることはない。
extern const char kBundledSkinPrefix[];

// "assets:<名前>" か。
bool IsBundledSkinRef(const std::string &ref);
// 接頭辞を外した名前。付いていなければそのまま。
std::string SkinRefName(const std::string &ref);
// 接頭辞を付けた ref。
std::string MakeBundledSkinRef(const std::string &name);

struct AssetPaths {
	std::string userDir;     // 書き込み可。末尾に区切りを含む
	std::string bundledDir;  // 実行ファイルの隣の assets（読み取り専用）

	// 読む順に並べたルート。空のものは含めない。
	std::vector<std::string> Roots() const;

	// ルートからの相対パスで探す。見つからなければ空文字列。
	std::string Find(const std::string &relative) const;

	// ref の指すスキンのフォルダ。無ければ空文字列。
	std::string SkinDir(const std::string &ref) const;

	// 書き込み先の skin/<名前>（接頭辞は付けないこと）。まだ無くても
	// パスを返す（作るのは呼び出し側）。
	std::string UserSkinDir(const std::string &name) const;

	// ユーザーフォルダ側にその名前のスキンがあるか。
	bool UserSkinExists(const std::string &name) const;

	// ref が実際に指しているフォルダから、あいまいさの無い ref を作る。
	// 同梱ぶんを指していれば "assets:" が付く。layout.ini の Base に
	// 書き出すときは、これを通してから書くこと（接頭辞なしの名前は、
	// あとでユーザーが同じ名前のスキンを足すと意味が変わってしまう）。
	std::string CanonicalSkinRef(const std::string &ref) const;

	// 選べるスキンを ref の形で集める。ユーザーぶんが先、同梱ぶん
	// （"assets:" 付き）が後。それぞれ大文字小文字を無視した名前順。
	void ListSkinRefs(std::vector<std::string> *out) const;
};

}  // namespace mxv2

#endif  // MXV2_ASSETPATH_H
