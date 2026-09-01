// mxv2 スキンエディタ - 起動フォルダの判定と assets/skin/ の列挙
//
// skineditor.md の手順1: 「assets/ フォルダがあるディレクトリで起動する」。
// 判定できるのは mxv2 本体の CMakeLists.txt があるディレクトリ（開発用
// フォルダ）だけで、mxv2.exe の隣（ユーザーフォルダ側の編集）はまだ実装
// しない。判定方法: assets/ の隣に CMakeLists.txt があるかどうかで見分ける。

namespace SkinEditor.Model;

public enum AssetRootKind
{
    NotFound,
    DevFolder,
    ExeFolderNotImplemented,
}

public sealed record AssetRootDetection(AssetRootKind Kind, DevAssetRoot? Root, string Message);

public sealed class DevAssetRoot
{
    // assets/ の親（= mxv2 本体の CMakeLists.txt があるフォルダ）。
    public string DevRootDir { get; }
    public string AssetsDir { get; }
    public string SkinRootDir { get; }

    private DevAssetRoot(string devRootDir)
    {
        DevRootDir = devRootDir;
        AssetsDir = Path.Combine(devRootDir, "assets");
        SkinRootDir = Path.Combine(AssetsDir, "skin");
    }

    // candidateDirs を順に見て、最初に判定できたものを返す。
    public static AssetRootDetection Detect(IEnumerable<string> candidateDirs)
    {
        foreach (var dir in candidateDirs)
        {
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) continue;
            var assetsDir = Path.Combine(dir, "assets");
            if (!Directory.Exists(assetsDir)) continue;

            var cmakeLists = Path.Combine(dir, "CMakeLists.txt");
            if (File.Exists(cmakeLists))
            {
                return new AssetRootDetection(AssetRootKind.DevFolder, new DevAssetRoot(dir),
                    $"開発用フォルダとして認識しました: {dir}");
            }
            return new AssetRootDetection(AssetRootKind.ExeFolderNotImplemented, null,
                "mxv2.exe の隣（ユーザーフォルダ側のスキン編集）はこのバージョンでは未対応です。" +
                "mxv2 本体の CMakeLists.txt があるフォルダ（開発用フォルダ）で起動してください。");
        }
        return new AssetRootDetection(AssetRootKind.NotFound, null,
            "assets フォルダが見つかりませんでした。mxv2 本体の CMakeLists.txt があるフォルダを選んでください。");
    }

    public static AssetRootDetection DetectDefault()
    {
        var candidates = new List<string> { Environment.CurrentDirectory, AppContext.BaseDirectory };
        return Detect(candidates);
    }

    // 既存スキンのフォルダ。無ければ null。
    public string? SkinDir(string name)
    {
        var dir = Path.Combine(SkinRootDir, name);
        return Directory.Exists(dir) ? dir : null;
    }

    // 新規作成用（まだ無くてもパスを返す。作るのは呼び出し側）。
    public string SkinFolderPathFor(string name) => Path.Combine(SkinRootDir, name);

    public bool SkinExists(string name) =>
        Directory.Exists(Path.Combine(SkinRootDir, name));

    // 開発用フォルダでは Base の参照先は必ず "assets:<名前>"（skineditor.md）。
    public static string CanonicalRef(string name) => SkinRef.MakeBundled(name);

    public IReadOnlyList<string> ListSkinNames()
    {
        if (!Directory.Exists(SkinRootDir)) return Array.Empty<string>();
        return Directory.GetDirectories(SkinRootDir)
            .Select(Path.GetFileName)
            .Where(n => !string.IsNullOrEmpty(n))
            .Select(n => n!)
            .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }
}
