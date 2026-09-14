// mxv2 スキンエディタ - 描画用の素材の解決（mxv2 本体 Skin::FindFile 相当）。
// 自スキンのフォルダ -> 土台のフォルダ の順に探し、読んだものはキャッシュする。

using SkinEditor.Model;
using SkinEditor.Model.Render;

namespace SkinEditor.UI;

public sealed class SkinAssetSource : IAssetSource
{
    private readonly SkinDocument _doc;
    private readonly Dictionary<string, RenderBitmap?> _cache = new(StringComparer.OrdinalIgnoreCase);

    public SkinAssetSource(SkinDocument doc) => _doc = doc;

    public RenderBitmap? Find(string fileName)
    {
        if (string.IsNullOrEmpty(fileName)) return null;
        if (_cache.TryGetValue(fileName, out var cached)) return cached;

        RenderBitmap? bmp = null;
        foreach (var dir in _doc.AllDirsNearToFar())
        {
            var path = Path.Combine(dir, fileName);
            if (!File.Exists(path)) continue;
            bmp = BmpLoader.Load(path);
            if (bmp != null) break;
        }
        _cache[fileName] = bmp;
        return bmp;
    }

    // 文字描画に使うフォント。本体 src/textrender.cpp と**同じ順**で探す。
    //
    // **名前は 2 つある。** font.ttf はスキン（かユーザー）が差し替えるぶんで、
    // MPLUS1p-Regular.ttf が同梱フォント。名前ごとに全フォルダを見るので、
    // 土台の font.ttf は自分の同梱フォントより優先される
    // （FindFirst(dirs, kUserFont) -> FindFirst(dirs, kBundledFont) の順）。
    // 探す場所はスキンのフォルダ（自分 -> 土台 …）-> assets/ のルート。
    //
    // 同梱フォントのほうを見ていなかったので、**自分の font.ttf を持たない
    // スキン（Phone など）はプレビューの文字が出なかった**（2026-09-09 に修正）。
    private const string UserFontName = "font.ttf";
    private const string BundledFontName = "MPLUS1p-Regular.ttf";

    public string? FindFontFile(string assetsDir) => FindFontFile(new[] { assetsDir });

    // rootDirs は本体の AssetPaths::Roots() に当たるもの（ユーザーフォルダ →
    // 同梱 assets/。DevAssetRoot.FontRootDirs()）。
    public string? FindFontFile(IEnumerable<string> rootDirs)
    {
        var dirs = new List<string>(_doc.AllDirsNearToFar());
        foreach (var d in rootDirs) if (!string.IsNullOrEmpty(d)) dirs.Add(d);
        foreach (var name in new[] { UserFontName, BundledFontName })
        {
            foreach (var dir in dirs)
            {
                var path = Path.Combine(dir, name);
                if (File.Exists(path)) return path;
            }
        }
        return null;
    }

    public void Invalidate() => _cache.Clear();
}
