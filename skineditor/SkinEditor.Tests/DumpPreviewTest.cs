// 確認用の道具: プレビューの絵を PNG に落とし、1 枚あたりの時間を測る。
// 環境変数 MXV2_DUMP_DIR に出力先を入れたときだけ動く（普段は何もしない）。
//
//   MXV2_DUMP_DIR=<フォルダ> dotnet test --filter FullyQualifiedName~DumpPreviewTest
//
// 参照画像との突き合わせは RenderCompareTests。こちらは目で見たいときや、
// 描画が重くなっていないかを測りたいとき用。

using System.Diagnostics;
using SkinEditor.Model;
using SkinEditor.UI;

namespace SkinEditor.Tests;

public class DumpPreviewTest
{
    [Fact]
    public void Dump()
    {
        var outDir = Environment.GetEnvironmentVariable("MXV2_DUMP_DIR");
        if (string.IsNullOrEmpty(outDir)) return;

        var root = TestPaths.FindDevRoot();
        var lines = new List<string>();
        foreach (var name in new[] { "Default", "Default-Midnight", "Phone" })
        {
            var doc = SkinDocument.Open(root, name);
            using var renderer = new PreviewRenderer(doc);
            var state = new PreviewState { Play = true, Cont = true, Volume = 0, Progress = 0.4 };

            renderer.Render(state);  // 1 回目は素材の読み込みを含むので捨てる
            const int n = 20;
            var sw = Stopwatch.StartNew();
            for (int i = 0; i < n; i++) renderer.Render(state);
            sw.Stop();

            var bmp = renderer.Render(state);
            bmp?.Save(Path.Combine(outDir, $"port-{name}.png"), System.Drawing.Imaging.ImageFormat.Png);
            lines.Add($"{name}: {sw.Elapsed.TotalMilliseconds / n:F1} ms/frame");
        }
        File.WriteAllLines(Path.Combine(outDir, "render-time.txt"), lines);
    }
}
