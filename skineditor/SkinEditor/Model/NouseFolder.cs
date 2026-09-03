// mxv2 スキンエディタ - 使われなくなった素材ファイルの退避先 "_nouse"。
//
// スキンを参照にする/参照を解除するとき、自スキンの素材ファイルが不要に
// なったり、参照先からのコピーで上書きされそうになったりすることがある。
// 消してしまうと元に戻せないので、常にこの "_nouse" サブフォルダへ移す
// （ユーザー指示）。同名ファイルがすでにあれば、日付時刻を付けて衝突を
// 避ける。

namespace SkinEditor.Model;

public static class NouseFolder
{
    public const string DirName = "_nouse";

    // ownDir 直下の "_nouse" へ filePath を移動する。同名がすでにあれば
    // ファイル名に日付時刻（さらに衝突したら連番）を付けて上書きを避ける。
    public static void Evacuate(string ownDir, string filePath)
    {
        var nouseDir = Path.Combine(ownDir, DirName);
        Directory.CreateDirectory(nouseDir);

        var fileName = Path.GetFileName(filePath);
        var dest = Path.Combine(nouseDir, fileName);
        if (File.Exists(dest))
        {
            var stem = Path.GetFileNameWithoutExtension(fileName);
            var ext = Path.GetExtension(fileName);
            var stamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");
            dest = Path.Combine(nouseDir, $"{stem}_{stamp}{ext}");
            int n = 2;
            while (File.Exists(dest))
            {
                dest = Path.Combine(nouseDir, $"{stem}_{stamp}_{n}{ext}");
                n++;
            }
        }
        File.Move(filePath, dest);
    }

    // 2 ファイルのバイト列が完全に一致するか。
    public static bool FilesEqual(string pathA, string pathB)
    {
        using var a = File.OpenRead(pathA);
        using var b = File.OpenRead(pathB);
        if (a.Length != b.Length) return false;

        Span<byte> bufA = stackalloc byte[8192];
        Span<byte> bufB = stackalloc byte[8192];
        while (true)
        {
            int nA = a.Read(bufA);
            int nB = b.Read(bufB);
            if (nA != nB) return false;
            if (nA == 0) return true;
            if (!bufA[..nA].SequenceEqual(bufB[..nB])) return false;
        }
    }
}
