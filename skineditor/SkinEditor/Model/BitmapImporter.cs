// mxv2 スキンエディタ - 外部ファイルのインポート。
//
// パレット依存の役割（鍵盤・操作ボタン・レベルメータ・ミニフォント）は
// 8bpp インデックスカラーの BMP しか受け付けない。パレット番号の意味を
// 壊さないよう、自動減色はしない。それ以外の役割は一般的な画像形式を
// 受け付け、24bpp の BMP として保存する。
//
// 実際のファイルコピーは（layout.ini の数値と違って）インポートした時点で
// 即座にディスクへ書く。バイナリ素材を保存確定まで宙に置く意味は薄く、
// 元ファイルはそのまま残るので取り消しが効くため。

using System.Drawing;
using System.Drawing.Imaging;

namespace SkinEditor.Model;

public static class BitmapImporter
{
    public sealed record Result(bool Success, string? Error, int Width, int Height);

    public static Result Import(SkinDocument doc, BitmapRole role, string sourcePath)
    {
        Bitmap source;
        try
        {
            using var loaded = new Bitmap(sourcePath);
            source = new Bitmap(loaded);  // ファイルへのロックを切り離す
        }
        catch (Exception ex)
        {
            return new Result(false, $"画像として読み込めませんでした: {ex.Message}", 0, 0);
        }

        using (source)
        {
            bool paletteDependent = BitmapRoleInfo.IsPaletteDependent(role);
            if (paletteDependent && source.PixelFormat != PixelFormat.Format8bppIndexed)
            {
                return new Result(false,
                    $"{BitmapRoleInfo.DisplayName(role)} はパレット番号で発色させる素材のため、" +
                    "あらかじめ 8bpp インデックスカラーの BMP として用意してください。", 0, 0);
            }

            Directory.CreateDirectory(doc.OwnDir);
            var destPath = Path.Combine(doc.OwnDir, BitmapRoleInfo.DefaultFileName(role));

            if (paletteDependent)
            {
                source.Save(destPath, ImageFormat.Bmp);
            }
            else
            {
                using var converted = new Bitmap(source.Width, source.Height, PixelFormat.Format24bppRgb);
                using (var g = Graphics.FromImage(converted)) g.DrawImage(source, 0, 0, source.Width, source.Height);
                converted.Save(destPath, ImageFormat.Bmp);
            }

            doc.ClipRectsForImportedBitmap(role, source.Width, source.Height);
            return new Result(true, null, source.Width, source.Height);
        }
    }
}
