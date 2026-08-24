// mxv2 - ファイル / パスユーティリティ
//
// パス文字列は全て UTF-8 で扱う。Windows ではワイド文字 API へ変換して
// アクセスするため、非 ASCII を含むパスでも正しく開ける。

#ifndef MXV2_FILEUTIL_H
#define MXV2_FILEUTIL_H

#include <cstdint>
#include <string>
#include <vector>

namespace mxv2 {

// ファイル全体をメモリへ読み込む。
bool ReadWholeFile(const std::string &path, std::vector<uint8_t> *out);

// ファイルを丸ごと書き出す（既存の内容は捨てる）。
bool WriteWholeFile(const std::string &path, const std::vector<uint8_t> &data);

// ファイルが存在し、読み込めるか。
bool FileExists(const std::string &path);

// ファイルを消す。もともと無ければ true。
bool RemoveFile(const std::string &path);

// パス末尾の区切りまで（区切りを含む）を返す。区切りが無ければ空文字列。
std::string DirNameOf(const std::string &path);

// パス末尾のファイル名部分を返す。
std::string BaseNameOf(const std::string &path);

// 拡張子を除いたファイル名部分を返す。
std::string StemOf(const std::string &path);

// ディレクトリとファイル名を連結する。dir が空なら name をそのまま返す。
std::string JoinPath(const std::string &dir, const std::string &name);

// 実行ファイルのあるディレクトリ（末尾に区切りを含む）。取得できなければ "./"。
std::string ExecutableDir();

// アプリごとの書き込み可能なフォルダ（末尾に区切りを含む）。
//
// 実行ファイルの隣は書けるとは限らない（Program Files の下、Android の apk の
// 中）ので、設定やユーザーが足した素材はこちらへ置く。appName はフォルダ名に
// そのまま使う。取得できなければ実行ファイルの隣を返す。
std::string UserDataDir(const std::string &appName);

// ディレクトリか。
bool IsDirectory(const std::string &path);

// ディレクトリを作る。途中のものもまとめて作る。すでにあれば true。
bool MakeDirectories(const std::string &path);

struct DirEntry {
	std::string name;  // ファイル名のみ (UTF-8)
	bool isDir;

	DirEntry() : isDir(false) {}
};

// ディレクトリの中身を列挙する ("." ".." は含まない)。
bool ListDirectory(const std::string &dir, std::vector<DirEntry> *out);

// 利用できるドライブのルートパス ("C:\\" 等)。Windows 以外では空。
std::vector<std::string> ListDrives();

// OS のシステムドライブのルート ("C:\\" 等)。Windows 以外では空。
std::string SystemDriveRoot();

// 絶対パスへ正規化する。失敗したら入力をそのまま返す。
std::string AbsolutePath(const std::string &path);

// カレントディレクトリ（末尾に区切りを含む）。
std::string CurrentDir();

// 親ディレクトリ。ルートならそのまま返す。
std::string ParentDir(const std::string &dir);

// 大文字小文字を無視した比較 (ASCII のみ)。
int CompareNoCase(const std::string &a, const std::string &b);

}  // namespace mxv2

#endif  // MXV2_FILEUTIL_H
