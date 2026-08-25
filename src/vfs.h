// mxv2 - ファイルシステムの抽象化 (VFS)
//
// ファイラーのルートの 1 段上に「ファイルシステムの選択」を置くための土台。
// 場所の指定は全て **ref** の形に統一する。
//
//   "<スキーム>:<パス>"  例: "localfs:C:\mdx\foo.mdx" / "assets:ArctanX/a.mdx"
//   ""                   ファイルシステムの選択画面（どの FS にも居ない状態）
//
// スキームは「2 文字以上の英字 + ':'」。1 文字 + ':' は Windows の
// ドライブレターなので、"C:\mdx" は接頭辞の無いローカルパスとして扱われる。
//
// ネイティブのパスへ落とすのは各 FileSystem の実装の中だけにすること。
// 呼び出し側で裸のパスと ref を混ぜると、境界のたびに変換漏れが出る。
//
// layout.ini の [Skin] Base に書く "assets:<スキン名>" は綴りが同じだけの
// 別物（assetpath.h の kBundledSkinPrefix）。こちらのパーサを流用しないこと。

#ifndef MXV2_VFS_H
#define MXV2_VFS_H

#include <cstdint>
#include <string>
#include <vector>

#include "fileutil.h"

namespace mxv2 {

// ファイラーの一覧に足す項目（ローカル FS のドライブなど）。
struct FsExtraItem {
	std::string name;  // 表示名 ("C:")
	std::string rel;   // 行き先 (FS 内のパス)
};

// 1 つのファイルシステム。パスは FS 内の表記 (rel) で受け渡す。
// rel の書式は FS ごとに決めてよい（ローカル FS はネイティブのフルパス、
// ルート付きの FS は '/' 区切りの相対パスで、ルートは空文字列）。
class FileSystem {
public:
	virtual ~FileSystem() {}

	// ref のスキームに使う名前。
	virtual const char *id() const = 0;
	// ファイラーのルートと設定ダイアログに出す説明（曲名の位置）。
	virtual std::string label() const = 0;
	// 同じくファイル名の位置に出す表記 ("Assets>" など)。表示専用で、
	// ref には使わない（'>' はコマンドラインに渡せない）。
	virtual const char *prefix() const = 0;

	// 使えるか（Android のローカル FS のように、環境によって塞がるもの）。
	virtual bool available() const { return true; }
	// ルートの pdx/ を PDX の探索先に入れるか。ローカル FS だけは入れない
	// （ルートがドライブの根なので、そこに pdx/ を置く前提が立たない）。
	virtual bool hasPdxDir() const { return true; }
	// 設定ダイアログの [削除] で削除できるか。初回起動時から使えるものは
	// 削除できない。
	virtual bool removable() const { return false; }

	// rel の掃除（区切り文字を揃える、末尾の区切りを落とす、など）。
	virtual std::string Normalize(const std::string &rel) const = 0;
	// FS のルートか。
	virtual bool IsRoot(const std::string &rel) const = 0;
	// FS のルート。
	virtual std::string Root() const = 0;
	// 1 つ上。ルートならルートをそのまま返す。
	virtual std::string Parent(const std::string &rel) const = 0;
	// ディレクトリと名前の連結。
	virtual std::string Join(const std::string &dir, const std::string &name) const = 0;
	// ファイラーの最上段に出す文字列。
	virtual std::string DisplayPath(const std::string &rel) const = 0;

	// 接頭辞の無いパスを rel へ直す。base は相対パスの基準
	// （空ならこの FS のルート。ローカル FS はプロセスのカレント）。
	virtual std::string ResolveInput(const std::string &input,
	                                 const std::string &base) const = 0;

	virtual bool List(const std::string &rel, std::vector<DirEntry> *out) const = 0;
	virtual bool Read(const std::string &rel, std::vector<uint8_t> *out) const = 0;
	virtual bool Exists(const std::string &rel) const = 0;
	virtual bool IsDir(const std::string &rel) const = 0;

	// 同じ場所を指すか。大文字小文字の扱いは FS ごとの規則に従う。
	virtual bool SamePath(const std::string &a, const std::string &b) const = 0;

	// 根のネイティブパス。ネイティブのフォルダを根に据えた FS だけが返す
	// （ローカル FS と外部 FS は空）。フォルダを作るときだけに使う。
	virtual std::string nativeRoot() const { return std::string(); }

	// 一覧の末尾に足すもの（ローカル FS のドライブ一覧）。
	virtual void AppendExtraItems(const std::string &rel,
	                              std::vector<FsExtraItem> *out) const {
		(void)rel;
		(void)out;
	}
};

// 使える FS の一覧と、ファイラーのルートに並べる順（マウント）を持つ。
class Vfs {
public:
	Vfs();
	~Vfs();

	// 使える FS を用意する。assetsDir / userDir は同梱素材とユーザー
	// フォルダの場所（末尾の区切りはあってもなくてもよい）。空を渡すと
	// その FS は作らない（mxv2_chunktest はローカルだけで足りる）。
	void Configure(const std::string &assetsDir, const std::string &userDir);

	// 使える FS 全部。マウントされているとは限らない。
	int allCount() const { return (int)all_.size(); }
	FileSystem *all(int i) const { return all_[i]; }
	FileSystem *FindById(const std::string &id) const;

	// マウント一覧（ファイラーのルートに出る順）。
	int count() const { return (int)mounted_.size(); }
	FileSystem *at(int i) const { return mounted_[i]; }
	int IndexOf(const FileSystem *fs) const;
	bool IsMounted(const FileSystem *fs) const { return IndexOf(fs) >= 0; }

	void ClearMounts() { mounted_.clear(); }
	// 末尾に足す。既に入っていれば何もしない。
	bool Mount(FileSystem *fs);
	// pos の位置へ入れる。
	bool MountAt(int pos, FileSystem *fs);
	void Unmount(int index);
	void Move(int index, int delta);
	// 削除できない FS が抜けていたら末尾に足す。足したら true。
	bool EnsureRequired();

	// 「2 文字以上の英字 + ':'」の接頭辞を切り出す。無ければ false。
	static bool SplitRef(const std::string &ref, std::string *scheme, std::string *rest);

	// 同じ場所を指す ref か。大文字小文字の扱いは FS ごとの規則に従う
	// （読めない ref は false）。ブックマークの重複判定に使う。
	bool SameRef(const std::string &a, const std::string &b) const;

	// ref を FS と rel へ割る。
	//   ""            -> fs = 0, rel = ""（ファイルシステムの選択画面）
	//   知らないスキーム -> false
	// 接頭辞が無ければローカル FS のパスとみなす。
	bool Parse(const std::string &ref, FileSystem **fs, std::string *rel) const;

	static std::string MakeRef(const FileSystem *fs, const std::string &rel);

	// 外から来た文字列（ini / コマンドライン / 入力欄）を ref へ直す。
	// baseRef は相対パスの基準。読めなければ false。
	bool Resolve(const std::string &input, const std::string &baseRef,
	             std::string *outRef) const;

	// ref 一発で使える便利関数。FS が見つからなければ失敗を返す。
	bool Read(const std::string &ref, std::vector<uint8_t> *out) const;
	bool Exists(const std::string &ref) const;
	bool IsDir(const std::string &ref) const;
	// 1 つ上。FS のルートなら "" （選択画面）を返す。
	std::string Parent(const std::string &ref) const;
	std::string Join(const std::string &dirRef, const std::string &name) const;
	std::string DisplayPath(const std::string &ref) const;
	// ref の指す FS のルート。
	std::string RootRef(const std::string &ref) const;
	// ref の指す FS のルートにある pdx/ フォルダ。無い FS では空。
	std::string PdxDirRef(const std::string &ref) const;

private:
	std::vector<FileSystem *> owned_;
	std::vector<FileSystem *> all_;
	std::vector<FileSystem *> mounted_;

	Vfs(const Vfs &);
	Vfs &operator=(const Vfs &);
};

}  // namespace mxv2

#endif  // MXV2_VFS_H
