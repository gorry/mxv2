// mxv2 スキンエディタ - 起動フォルダの判定と、スキンの置き場所の解決
//
// skineditor.md の手順1: 「assets/ フォルダがあるディレクトリで起動する」。
// 2 つのモードがある（どちらも assets/ の隣で見分ける。名前はユーザーが決めた）:
//
//   ・開発フォルダモード（mxv2 本体の CMakeLists.txt がある）
//       編集対象 = assets/skin/ のスキン（同梱ぶんそのもの）。
//       [Skin].Base の参照先は必ず "assets:<名前>"。
//   ・mxv2.exe の隣（ユーザーフォルダモード。2026-09-14 に追加）
//       編集対象 = ユーザーフォルダ（Windows なら %APPDATA%\mxv2\）の
//       skin/<名前>/。同梱の assets/skin/ は**参照先としてだけ**使えて、
//       編集はできない。[Skin].Base はユーザーのスキン（"<名前>"）と同梱の
//       スキン（"assets:<名前>"）のどちらも指定できる。
//
// 参照 (ref) の解決は mxv2 本体の src/assetpath.cpp と同じ:
//   "assets:<名前>" → 同梱の assets/skin/<名前>
//   "<名前>"        → ユーザーフォルダの skin/<名前> があればそれ、無ければ同梱
// 名前は大文字小文字を区別しない（Windows のフォルダ名と同じ）。

namespace SkinEditor.Model;

public enum AssetRootKind
{
    NotFound,
    DevFolder,
    ExeFolder,
}

public sealed record AssetRootDetection(AssetRootKind Kind, DevAssetRoot? Root, string Message);

// 名前は歴史的経緯でそのまま（開発フォルダモードだけだった頃の名残）。
// 実際には「どちらのモードでも、スキンの置き場所を解決する窓口」。
public sealed class DevAssetRoot
{
    public AssetRootKind Kind { get; }
    public bool IsUserMode => Kind == AssetRootKind.ExeFolder;

    // assets/ の親（開発フォルダモードなら CMakeLists.txt のある場所、
    // ユーザーフォルダモードなら mxv2.exe のある場所）。
    public string DevRootDir { get; }
    // 同梱の素材 (<DevRootDir>/assets)。ユーザーフォルダモードでは読むだけ。
    public string AssetsDir { get; }
    public string BundledSkinRootDir { get; }
    // ユーザーフォルダ（ユーザーフォルダモードだけ。開発フォルダモードでは null）。
    public string? UserDir { get; }
    // **編集できる**スキンの置き場所。開発用なら assets/skin、ユーザー
    // フォルダモードなら <UserDir>/skin。
    public string SkinRootDir { get; }

    private DevAssetRoot(AssetRootKind kind, string devRootDir, string? userDir)
    {
        Kind = kind;
        DevRootDir = devRootDir;
        AssetsDir = Path.Combine(devRootDir, "assets");
        BundledSkinRootDir = Path.Combine(AssetsDir, "skin");
        UserDir = userDir;
        SkinRootDir = userDir != null ? Path.Combine(userDir, "skin") : BundledSkinRootDir;
    }

    // mxv2 本体の FileUtil::UserDataDir("mxv2") の Windows 版と同じ場所
    // （%APPDATA%\mxv2）。本体と同じフォルダを見ないと、作ったスキンが
    // mxv2 の [設定] のスキン一覧に出ない。
    public static string DefaultUserDir() =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "mxv2");

    // candidateDirs を順に見て、最初に判定できたものを返す。userDir は
    // ユーザーフォルダモードで使う場所（テストで差し替えるための引数。
    // 省略時は DefaultUserDir()）。
    public static AssetRootDetection Detect(IEnumerable<string> candidateDirs, string? userDir = null)
    {
        foreach (var dir in candidateDirs)
        {
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) continue;
            var assetsDir = Path.Combine(dir, "assets");
            if (!Directory.Exists(assetsDir)) continue;

            var cmakeLists = Path.Combine(dir, "CMakeLists.txt");
            if (File.Exists(cmakeLists))
            {
                return new AssetRootDetection(AssetRootKind.DevFolder,
                    new DevAssetRoot(AssetRootKind.DevFolder, dir, null),
                    "開発フォルダモードで起動しました。mxv2同梱のスキンを直接編集します。\n" +
                    $"編集するスキンのフォルダ: {Path.Combine(dir, "assets", "skin")}\n" +
                    "スキンの一覧:");
            }

            // CMakeLists.txt が無ければ mxv2.exe の隣（配布物）とみなす。
            // mxv2.exe そのものの有無は見ない（Android から持ってきた assets/
            // だけのフォルダでも同じ扱いでよい）。
            var user = userDir ?? DefaultUserDir();
            return new AssetRootDetection(AssetRootKind.ExeFolder,
                new DevAssetRoot(AssetRootKind.ExeFolder, dir, user),
                "ユーザーフォルダモードで起動しました。mxv2同梱のスキンは参照のみ可能です（編集できません）。\n" +
                $"編集するスキンのフォルダ: {Path.Combine(user, "skin")}\n" +
                "スキンの一覧:");
        }
        return new AssetRootDetection(AssetRootKind.NotFound, null,
            "「mxv2.exeのあるフォルダ（ユーザーフォルダモード）」か、" +
            "「CMakeLists.txtのあるフォルダ（開発フォルダモード）」で起動してください");
    }

    // 起動時の判定。rootDir が指定されていればそこだけ、無ければカレント →
    // exe の隣の順に見る。userDir はユーザーフォルダモードの編集先の
    // 差し替え（省略時は DefaultUserDir()）。
    public static AssetRootDetection DetectDefault(string? rootDir = null, string? userDir = null)
    {
        var candidates = rootDir != null
            ? new List<string> { rootDir }
            : new List<string> { Environment.CurrentDirectory, AppContext.BaseDirectory };
        return Detect(candidates, userDir);
    }

    // ---- 編集できるスキン（SkinRootDir の下） ----------------------------

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

    public IReadOnlyList<string> ListSkinNames() => ListDirNames(SkinRootDir);

    // 同梱スキンの名前（ユーザーフォルダモードでは参照先の候補。開発用
    // フォルダでは ListSkinNames と同じもの）。
    public IReadOnlyList<string> ListBundledSkinNames() => ListDirNames(BundledSkinRootDir);

    private static IReadOnlyList<string> ListDirNames(string root)
    {
        if (!Directory.Exists(root)) return Array.Empty<string>();
        return Directory.GetDirectories(root)
            .Select(Path.GetFileName)
            .Where(n => !string.IsNullOrEmpty(n))
            .Select(n => n!)
            .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    // ---- 参照 (ref) ------------------------------------------------------

    // 編集できるスキン（自分や、自分と同じ置き場所のスキン）を指す ref。
    // 開発フォルダモードでは "assets:<名前>"（skineditor.md）、ユーザーフォルダ
    // モードでは接頭辞なしの "<名前>"（本体の CanonicalSkinRef と同じ）。
    public string CanonicalRef(string name) =>
        IsUserMode ? SkinRef.NameOf(name) : SkinRef.MakeBundled(SkinRef.NameOf(name));

    // 任意の ref（または素の名前）を、本体の AssetPaths::CanonicalSkinRef と
    // 同じ規則で正規化する: 接頭辞なしでユーザーのスキンが実在すれば
    // "<名前>"、そうでなければ "assets:<名前>"。
    public string CanonicalRefOf(string reference)
    {
        var name = SkinRef.NameOf(reference);
        if (IsUserMode && !SkinRef.IsBundled(reference) && SkinExists(name)) return name;
        return SkinRef.MakeBundled(name);
    }

    // ref が指すフォルダ（本体の AssetPaths::SkinDir と同じ）。無ければ null。
    public string? ResolveRefDir(string reference)
    {
        var name = SkinRef.NameOf(reference);
        if (string.IsNullOrEmpty(name)) return null;
        if (IsUserMode && !SkinRef.IsBundled(reference))
        {
            var own = Path.Combine(SkinRootDir, name);
            if (Directory.Exists(own)) return own;
        }
        var bundled = Path.Combine(BundledSkinRootDir, name);
        return Directory.Exists(bundled) ? bundled : null;
    }

    // [Skin].Base に選べる ref の一覧（本体の ListSkinRefs と同じ並び:
    // ユーザーぶん → 同梱ぶん、それぞれ名前順）。開発フォルダモードでは
    // 同梱ぶんだけ。
    public IReadOnlyList<string> ListBaseRefs()
    {
        var refs = new List<string>();
        if (IsUserMode) refs.AddRange(ListSkinNames());
        refs.AddRange(ListBundledSkinNames().Select(SkinRef.MakeBundled));
        return refs;
    }

    // 一覧に出すときの表示名。開発フォルダモードは "assets:" しか無いので名前
    // だけ、ユーザーフォルダモードは ref そのもの（"assets:" 付きかどうかで
    // 同梱かユーザーかが分かる）。
    public string DisplayRef(string reference) =>
        IsUserMode ? CanonicalRefOf(reference) : SkinRef.NameOf(reference);

    // 文字（font.ttf / 同梱フォント）を探すルート。本体の AssetPaths::Roots()
    // と同じ順（ユーザーフォルダ → 同梱 assets/）。スキンのフォルダの後ろに
    // 繋いで使う（本体の FontSearchDirs）。
    public IReadOnlyList<string> FontRootDirs()
    {
        var dirs = new List<string>();
        if (UserDir != null) dirs.Add(UserDir);
        dirs.Add(AssetsDir);
        return dirs;
    }
}
