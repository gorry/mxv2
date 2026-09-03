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

    // font.ttf の場所。本体 FontSearchDirs と同じ並び（スキン -> 素材のルート）。
    public string? FindFontFile(string assetsDir)
    {
        foreach (var dir in _doc.AllDirsNearToFar())
        {
            var path = Path.Combine(dir, "font.ttf");
            if (File.Exists(path)) return path;
        }
        var root = Path.Combine(assetsDir, "font.ttf");
        return File.Exists(root) ? root : null;
    }

    public void Invalidate() => _cache.Clear();
}
