// mxv2 スキンエディタ - 役割ごとの素材ビットマップを doc の探索順で読み込み、
// キャッシュする。Base 切替やインポートのたびに Invalidate() で作り直す。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class BitmapCache : IDisposable
{
    private readonly Dictionary<BitmapRole, System.Drawing.Bitmap?> _cache = new();

    public System.Drawing.Bitmap? Get(SkinDocument doc, BitmapRole role)
    {
        if (_cache.TryGetValue(role, out var cached)) return cached;

        var fileName = BitmapRoleInfo.DefaultFileName(role);
        System.Drawing.Bitmap? bmp = null;
        foreach (var dir in doc.AllDirsNearToFar())
        {
            var path = Path.Combine(dir, fileName);
            if (!File.Exists(path)) continue;
            try
            {
                using var loaded = new System.Drawing.Bitmap(path);
                bmp = new System.Drawing.Bitmap(loaded);
                break;
            }
            catch
            {
                // 壊れた/未対応形式のファイルは無視してプレビューには出さない
            }
        }
        _cache[role] = bmp;
        return bmp;
    }

    public void Invalidate()
    {
        foreach (var b in _cache.Values) b?.Dispose();
        _cache.Clear();
    }

    public void Dispose() => Invalidate();
}
