// テスト用: このテストアセンブリの場所から mxv2 の開発用フォルダ
// （CMakeLists.txt と assets/ があるフォルダ）を遡って見つける。

using SkinEditor.Model;

namespace SkinEditor.Tests;

public static class TestPaths
{
    public static DevAssetRoot FindDevRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "CMakeLists.txt")) &&
                Directory.Exists(Path.Combine(dir.FullName, "assets")))
            {
                var detection = DevAssetRoot.Detect(new[] { dir.FullName });
                if (detection.Kind == AssetRootKind.DevFolder && detection.Root != null) return detection.Root;
            }
            dir = dir.Parent;
        }
        throw new DirectoryNotFoundException("mxv2 の開発用フォルダ（CMakeLists.txt + assets/）が見つかりませんでした。");
    }

    // 比較用の参照画像（mxv2 本体のスクリーンショット）の置き場所。
    // "testdata" にすると .gitignore の testdata/ に食われて追跡されないので、
    // 別の名前にしてある。
    public static string RefImageDir =>
        Path.Combine(FindDevRoot().DevRootDir, "skineditor", "SkinEditor.Tests", "refimage");
}
