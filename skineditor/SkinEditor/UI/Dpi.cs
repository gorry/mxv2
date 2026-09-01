// mxv2 スキンエディタ - 手動 DPI スケーリング。
//
// このアプリの Width/Height/Margin の数値は、作った環境（175% 表示、
// Control.DeviceDpi で 168）でスクリーンショットを見ながら調整した値を
// そのまま書いている。WinForms 標準の AutoScaleMode（Font / Dpi）を
// 試したが、この環境では実際には何もスケールしてくれなかった
// （AutoScaleDimensions は書き換わるのに見た目が一切変わらない、という
// 挙動を実測した）。原因を追い切れなかったため、AutoScaleMode には頼らず、
// Control.DeviceDpi を直接見て自前で比率をかける。
//
// 基準 DPI を 96 ではなく 168（＝この環境。実際に調整・検証した値）に
// しているので、この環境では常に等倍（今まで確認してきた見た目のまま）。
// 100% 表示 (96dpi) の環境では 96/168 ≒ 0.57 倍に縮む。

namespace SkinEditor.UI;

internal static class Dpi
{
    // 実装・検証した環境の DPI（175% 表示）。この値を基準に、実行環境の
    // 実際の DPI との比率で拡大縮小する。
    public const int ReferenceDpi = 168;

    public static float ScaleOf(Control c) => c.DeviceDpi / (float)ReferenceDpi;

    // 基準環境でのピクセル数 v を、実行環境の DPI に合わせて換算する。
    public static int S(Control c, int v) => Math.Max(1, (int)Math.Round(v * ScaleOf(c)));
}
