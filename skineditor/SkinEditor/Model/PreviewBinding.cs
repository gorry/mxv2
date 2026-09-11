// mxv2 スキンエディタ - 「編集値のコントロール」と「プレビュー上のアイテム」の
// 対応表（skineditor_hitcheck.md の一覧をそのまま写したもの）。
//
// ここに書くのは 3 つ:
//   Region … フォーカスがあるときに枠を出す矩形 (PreviewRegions.Ids)
//   Hit    … プレビューのクリックで拾う優先順位。0 なら拾わない
//   Drag   … 選択中にドラッグしたとき、どう書き戻すか
//
// **スキーマ (LayoutFieldSchema / ColorFieldSchema) 側には持たせていない。**
// 対応表を 1 か所に固めておけば仕様書と行単位で見比べられるからで、その代わり
// 項目を増やしたら**ここにも足す**必要がある。足し忘れは
// PreviewBindingTests が拾う（全フィールドに対応があること・指す Region が
// 実在することを確認している）。

namespace SkinEditor.Model;

// Rect … x,y を動かして w,h は据え置き（[Banner] Rect などの「矩形」）
// Xy   … 2 値のキーをそのまま書き換える（「位置」「配置」）。DragParent が
//        空でなければ、その領域の左上からの相対値にしてから書く。
public enum PreviewDrag { None, Rect, Xy }

public sealed record PreviewBinding(
    string Region, int Hit = 0, PreviewDrag Drag = PreviewDrag.None, string DragParent = "");

public static class PreviewDragMath
{
    // アイテムの左上を (nx, ny)（絶対座標・skin px）へ動かすとき、layout.ini の
    // そのキーへ書く値。regions は今の実効値での全アイテムの矩形。
    // ドラッグできない項目なら null。
    public static string? ValueFor(PreviewBinding b, IReadOnlyDictionary<string, System.Drawing.Rectangle> regions,
        int nx, int ny)
    {
        switch (b.Drag)
        {
            case PreviewDrag.Rect:
                // 「矩形」は x,y だけ動かし、w,h は据え置く。
                if (!regions.TryGetValue(b.Region, out var r)) return null;
                return $"{nx},{ny},{r.Width},{r.Height}";
            case PreviewDrag.Xy:
                // 「位置」「配置」。DragParent があれば、その左上からの相対値。
                int ox = 0, oy = 0;
                if (!string.IsNullOrEmpty(b.DragParent))
                {
                    if (!regions.TryGetValue(b.DragParent, out var p)) return null;
                    ox = p.X;
                    oy = p.Y;
                }
                return $"{nx - ox},{ny - oy}";
            default:
                return null;
        }
    }
}

public static class PreviewBindings
{
    private static readonly PreviewBinding None = new(PreviewRegions.Ids.None);
    private const string Kb = "Keyboard";
    private const string St = "Status";

    // layout.ini のフィールド 1 つぶん。index は「1 キーに複数個の値」の
    // 何番目か（[Keyboard] XOffset の音名、[Keyboard] ChannelY のチャンネル、
    // [Status] PcmX/PcmY の PCM ch）。それ以外は -1。
    public static PreviewBinding For(string section, string key, int index = -1)
    {
        var ids = typeof(PreviewRegions.Ids);
        _ = ids;  // 参照を分かりやすくするためだけ

        switch (section)
        {
            case "Screen":
                // 2 分割の指定はファイラー側の矩形へ枠を出す（どこで割れて
                // いるかが見えるように）。それ以外は画面全体。
                // 厚みのほうに H を付けて、ファイラーの外側の余白を
                // クリックしたら 2 分割の設定へ飛べるようにする。
                if (key == "FilerExtent") return new PreviewBinding(PreviewRegions.Ids.FilerSide, 5);
                if (key == "FilerSide") return new PreviewBinding(PreviewRegions.Ids.FilerSide);
                return new PreviewBinding(PreviewRegions.Ids.Screen);

            case Kb:
                switch (key)
                {
                    case "Pos":
                        return new PreviewBinding(PreviewRegions.Ids.KeyboardAll, 10, PreviewDrag.Xy);
                    case "YOffset":
                        return new PreviewBinding(PreviewRegions.Ids.KeyboardAll);
                    case "KeyOffset":
                        return new PreviewBinding(PreviewRegions.Ids.Indexed(PreviewRegions.Ids.KeyboardRow, 0));
                    case "XOffset":
                        // 0〜11 は各鍵、12 は「オクターブ幅」なのでオクターブ全体。
                        return new PreviewBinding(index >= 0 && index < 12
                            ? PreviewRegions.Ids.Indexed(PreviewRegions.Ids.KeyboardKey, index)
                            : PreviewRegions.Ids.KeyboardOctave);
                    case "ChannelY":
                        return new PreviewBinding(
                            PreviewRegions.Ids.Indexed(PreviewRegions.Ids.KeyboardRow, Math.Clamp(index, 0, 8)), 11);
                }
                break;

            case St:
                if (key == "Rect") return new PreviewBinding(PreviewRegions.Ids.Status, 10, PreviewDrag.Rect);
                // PCM 8ch の位置。PcmX/PcmY は 8 個ずつの並びなのでドラッグは
                // 対象外にしてある（「位置」「配置」「矩形」の 2 値キーではない）。
                if (key is "PcmX" or "PcmY")
                    return new PreviewBinding(PreviewRegions.Ids.Indexed(PreviewRegions.Ids.PcmSlot, Math.Clamp(index, 0, 7)));
                // Pos<名前>（[配置] 系のサブタブと、レベルメータの「位置」）。
                for (int i = 0; i < StatusItems.Count; i++)
                {
                    if (StatusItems.Keys[i] != key) continue;
                    bool pcm = i is (int)StatusItem.PcmVolume or (int)StatusItem.PcmPtr;
                    // 相対値の原点は、FM は段の左上 (Status)、PCM は 1ch の
                    // スロットの基準点。PcmSlot（音量とポインタを囲む矩形）の
                    // 左上は基準点とは限らないので、そちらを使ってはいけない。
                    return new PreviewBinding(
                        i == (int)StatusItem.LevelMeter
                            ? PreviewRegions.Ids.LevelMeter
                            : PreviewRegions.Ids.Indexed(PreviewRegions.Ids.StatusItem, i),
                        0, PreviewDrag.Xy,
                        pcm ? PreviewRegions.Ids.Indexed(PreviewRegions.Ids.PcmOrigin, 0) : PreviewRegions.Ids.Status);
                }
                break;

            case "LevelMeter":
                return new PreviewBinding(PreviewRegions.Ids.LevelMeter);

            // ミニフォントは対応するアイテムなし（枠を出さない）。
            case "MiniFont":
                return None;

            case "Banner":
                return new PreviewBinding(PreviewRegions.Ids.Banner, 10, PreviewDrag.Rect);

            case "Title":
                return new PreviewBinding(PreviewRegions.Ids.Title, 10, PreviewDrag.Rect);

            // ファイラーの矩形は「ファイラー側の矩形からのマージン」になったので、
            // 掴んで動かす形にはならない（ドラッグ無し）。ただし**クリックで
            // 選べなくなると困る**ので、旧 [FileList] Rect が持っていた H=10 は
            // マージンのほうへ引き継ぐ。
            case "FileList":
                return key == "Margin"
                    ? new PreviewBinding(PreviewRegions.Ids.FileList, 10)
                    : new PreviewBinding(PreviewRegions.Ids.FileList);

            case "ScrollBar":
                switch (key)
                {
                    // 旧 [ScrollBar] Rect の H=20 は描く幅へ引き継ぐ。
                    // 当たり判定の幅は描画より広い（＝一覧に食い込む）ぶん、
                    // 一段低い H にして「描画 -> 当たり判定 -> 一覧」の順に
                    // 選び直せるようにする。
                    case "Width": return new PreviewBinding(PreviewRegions.Ids.ScrollBar, 20);
                    case "HitWidth": return new PreviewBinding(PreviewRegions.Ids.ScrollHit, 15);
                    case "SrcThumb": return new PreviewBinding(PreviewRegions.Ids.ScrollThumb);
                    case "SrcUpArrowPress":
                    case "SrcUpArrow": return new PreviewBinding(PreviewRegions.Ids.ScrollUpArrow);
                    case "SrcDownArrowPress":
                    case "SrcDownArrow": return new PreviewBinding(PreviewRegions.Ids.ScrollDownArrow);
                    case "SrcBar": return new PreviewBinding(PreviewRegions.Ids.ScrollBarGroove);
                }
                break;

            case "ProgressBar":
                return key switch
                {
                    "Rect" => new PreviewBinding(PreviewRegions.Ids.ProgressBar, 20, PreviewDrag.Rect),
                    "TimePos" => new PreviewBinding(PreviewRegions.Ids.ProgressTime, 21,
                        PreviewDrag.Xy, PreviewRegions.Ids.ProgressBar),
                    _ => new PreviewBinding(PreviewRegions.Ids.ProgressBar),
                };

            case "VolumeBar":
                return key switch
                {
                    "Rect" => new PreviewBinding(PreviewRegions.Ids.VolumeBar, 20, PreviewDrag.Rect),
                    "TimePos" => new PreviewBinding(PreviewRegions.Ids.VolumeTime, 21,
                        PreviewDrag.Xy, PreviewRegions.Ids.VolumeBar),
                    "SrcThumb" => new PreviewBinding(PreviewRegions.Ids.VolumeNob),
                    _ => new PreviewBinding(PreviewRegions.Ids.VolumeBar),
                };

            case "PlayKey":
                if (key == "Rect") return new PreviewBinding(PreviewRegions.Ids.PlayKey, 10, PreviewDrag.Rect);
                for (int i = 0; i < 9; i++)
                {
                    string btn = PreviewRegions.Ids.Indexed(PreviewRegions.Ids.PlayKeyButton, i);
                    if (key == $"Src{i}") return new PreviewBinding(btn);
                    if (key == $"Pos{i}")
                        return new PreviewBinding(btn, 11, PreviewDrag.Xy, PreviewRegions.Ids.PlayKey);
                }
                // Count と Pal* はまとまり全体を指す。
                return new PreviewBinding(PreviewRegions.Ids.PlayKey);
        }
        return None;
    }

    // 素材のインポート行。役割ごとに 1 か所しか出ないので固定でよい。
    public static PreviewBinding ForBitmap(BitmapRole role) => role switch
    {
        // 背景だけは「どこにも当たらなかったとき」の受け皿として H=1。
        BitmapRole.Back => new PreviewBinding(PreviewRegions.Ids.Screen, 1),
        BitmapRole.Kb0 => new PreviewBinding(PreviewRegions.Ids.Indexed(PreviewRegions.Ids.KeyboardRow, 0)),
        BitmapRole.Kb1 => new PreviewBinding(PreviewRegions.Ids.KeyboardOctave, 13),
        BitmapRole.Kb2 => new PreviewBinding(PreviewRegions.Ids.KeyboardOctave, 12),
        BitmapRole.MiniFont => None,
        BitmapRole.LevelMeter => new PreviewBinding(PreviewRegions.Ids.LevelMeter),
        BitmapRole.Banner => new PreviewBinding(PreviewRegions.Ids.Banner),
        BitmapRole.PlayKey => new PreviewBinding(PreviewRegions.Ids.PlayKey),
        BitmapRole.ProgressBar => new PreviewBinding(PreviewRegions.Ids.ProgressBar),
        BitmapRole.VolumeBar => new PreviewBinding(PreviewRegions.Ids.VolumeBar),
        BitmapRole.ScrollBar => new PreviewBinding(PreviewRegions.Ids.ScrollBar),
        _ => None,
    };

    // 配色（colors.ini）はセクション単位でまとめて 1 つのアイテムを指す。
    // 引数は ColorSectionDef.Title。
    public static PreviewBinding ForColorSection(string title) => title switch
    {
        "背景 (Back)" => new PreviewBinding(PreviewRegions.Ids.Screen),
        "鍵盤 (KB)" => new PreviewBinding(PreviewRegions.Ids.Indexed(PreviewRegions.Ids.KeyboardRow, 0)),
        "ステータス (Status)" => new PreviewBinding(PreviewRegions.Ids.Status),
        "曲名 (MDXTitle)" => new PreviewBinding(PreviewRegions.Ids.Title),
        "ファイラー (Filer)" => new PreviewBinding(PreviewRegions.Ids.FileList),
        "操作ボタン (PlayKey)" => new PreviewBinding(PreviewRegions.Ids.PlayKey),
        _ => None,
    };
}
