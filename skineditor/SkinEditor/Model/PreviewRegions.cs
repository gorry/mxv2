// mxv2 スキンエディタ - プレビュー上の「アイテム」の矩形。
//
// 各編集値のコントロールにフォーカスがあるとき、そのコントロールに対応する
// アイテムへ枠を出す（skineditor_hitcheck.md）。矩形は **描画とは切り離して
// layout の値から作る**（本体 drawscreen.cpp / DrawScreenPort が描く位置と
// 同じ式をここでもう一度書く）。描画側の内部状態に依存させないためで、
// 素材の大きさだけは実物を見ないと決まらないので sizeOf で受け取る。
//
// id は LayoutFieldSchema / ColorFieldSchema / BitmapRoleInfo から文字列で
// 参照する。無い id を指すと枠が出ないだけで落ちはしないが、綴りは
// PreviewRegions.Ids の定数を使うこと。

using System.Drawing;

namespace SkinEditor.Model;

public static class PreviewRegions
{
    public static class Ids
    {
        public const string None = "";
        public const string Screen = "screen";
        public const string KeyboardAll = "kb.all";
        public const string KeyboardRow = "kb.row";      // + 段番号 (0..8)
        public const string KeyboardOctave = "kb.octave";
        public const string KeyboardKey = "kb.key";      // + 音名番号 (0..11)
        public const string Status = "status";           // FM1 の 1 段
        public const string LevelMeter = "levelmeter";
        public const string StatusItem = "status.item";  // + StatusItem の番号
        public const string PcmSlot = "pcm.slot";        // + PCM ch (0..7)
        public const string PcmAll = "pcm.all";          // PCM 8ch ぶんの外接矩形
        // PCM の各スロットの基準点（PosPcmVolume / PosPcmPtr の原点）。
        // ドラッグで相対値を出すためだけに使う。PcmSlot のほうは「音量」と
        // 「ポインタ」を囲む矩形なので、左上は基準点とは限らない。
        public const string PcmOrigin = "pcm.origin";    // + PCM ch (0..7)
        public const string Banner = "banner";
        public const string Title = "title";
        public const string FilerSide = "filerside";  // ファイラー側の矩形（2 分割の片方）
        public const string FileList = "filelist";
        public const string ScrollBar = "scrollbar";
        public const string ScrollHit = "scrollbar.hit";  // 当たり判定（描画より広い）
        public const string ScrollThumb = "scroll.thumb";
        public const string ScrollUpArrow = "scroll.up";
        public const string ScrollBarGroove = "scroll.groove";
        public const string ScrollDownArrow = "scroll.down";
        public const string ProgressBar = "progressbar";
        public const string ProgressTime = "progressbar.time";
        public const string VolumeBar = "volumebar";
        public const string VolumeTime = "volumebar.time";
        public const string VolumeNob = "volumebar.nob";
        public const string PlayKey = "playkey";
        public const string PlayKeyButton = "playkey.button";  // + ボタン番号 (0..8)

        public static string Indexed(string id, int index) => id + index.ToString();
    }

    // ステータス欄の各項目の桁数。DrawScreenPort の PutVolume 等が実際に流す
    // 文字列の長さで、Default の layout.ini のコメントとも一致している
    // （並びは StatusItem。LevelMeter は文字ではないので 0）。
    private static readonly int[] StatusItemDigits =
    {
        4, 0, 1, 6, 4, 4, 6, 6, 1, 4, 5, 4, 6, 1, 4, 5, 4, 6,
    };

    // プログレスバーの時刻表示（"PLAY TIME: 00:00 / 00:00"）と
    // 音量バーの音量表示（"+000"）の桁数。
    private const int ProgressTimeDigits = 24;
    private const int VolumeTimeDigits = 4;

    // sizeOf は素材のファイル名から (幅, 高さ) を返す。読めない素材は null。
    // volume は音量バーのつまみの位置に効く（プレビューの状態そのまま）。
    public static Dictionary<string, Rectangle> Build(
        SkinLayout e, Func<string, Size?> sizeOf, int volume)
    {
        var r = new Dictionary<string, Rectangle>();
        r[Ids.Screen] = new Rectangle(0, 0, Math.Max(1, e.screenW), Math.Max(1, e.screenH));

        AddKeyboard(r, e, sizeOf);
        AddStatus(r, e, sizeOf);

        r[Ids.Banner] = new Rectangle(e.bannerX, e.bannerY, e.bannerW, e.bannerH);
        r[Ids.Title] = new Rectangle(e.titleX, e.titleY, e.titleW, e.titleH);
        r[Ids.FilerSide] = FilerSideRect(e);
        r[Ids.FileList] = new Rectangle(e.fileListX, e.fileListY, e.fileListW, e.fileListH);

        AddScrollBar(r, e);
        AddBars(r, e, sizeOf, volume);
        AddPlayKey(r, e);
        return r;
    }

    private static void AddKeyboard(Dictionary<string, Rectangle> r, SkinLayout e, Func<string, Size?> sizeOf)
    {
        var kb0 = sizeOf(e.kb0Bitmap) ?? new Size(340, 35);
        for (int i = 0; i < 9; i++)
        {
            r[Ids.Indexed(Ids.KeyboardRow, i)] =
                new Rectangle(e.kbX, e.kbY + e.chYOffset[i] + e.kbYOffset, kb0.Width, kb0.Height);
        }
        var first = r[Ids.Indexed(Ids.KeyboardRow, 0)];
        var last = r[Ids.Indexed(Ids.KeyboardRow, 8)];
        r[Ids.KeyboardAll] = Rectangle.Union(first, last);

        // 鍵 1 個の矩形。DrawScreenPort.PutNoteOn と同じ式:
        //   x = kbX + kbXOffset[音名] + kbXOffset[12] * オクターブ - kbXOffset[keyOffset]
        // 左端に半端に切れて出るオクターブがあるので、**ド(C) が完全に見える
        // 一番左のオクターブ**を「最初のオクターブ」として使う。
        var kb1 = sizeOf(e.kb1Bitmap) ?? new Size(7, kb0.Height);
        int keyW = 7;  // CutKeyboardBitmap の切り出し幅（本体と同じ固定値）
        int keyH = kb1.Height;
        int span = e.kbXOffset[12];
        int keyOfs = e.keyOffset >= 0 && e.keyOffset < 13 ? e.kbXOffset[e.keyOffset] : 0;
        int oct = 0;
        if (span > 0)
        {
            while (e.kbXOffset[0] + span * oct - keyOfs < 0 && oct < 16) oct++;
        }
        int y = e.kbY + e.chYOffset[0] + e.kbYOffset;
        Rectangle? octave = null;
        for (int i = 0; i < 12; i++)
        {
            int x = e.kbX + e.kbXOffset[i] + span * oct - keyOfs;
            var key = new Rectangle(x, y, keyW, keyH);
            r[Ids.Indexed(Ids.KeyboardKey, i)] = key;
            octave = octave == null ? key : Rectangle.Union(octave.Value, key);
        }
        r[Ids.KeyboardOctave] = octave ?? new Rectangle(e.kbX, y, span, keyH);
    }

    private static void AddStatus(Dictionary<string, Rectangle> r, SkinLayout e, Func<string, Size?> sizeOf)
    {
        // 段は 9 つあるが、枠を出すのは FM1（0 段目）だけ。
        r[Ids.Status] = new Rectangle(e.statusX, e.statusY + e.chYOffset[0], e.statusW, e.statusH);

        var mini = sizeOf(e.miniFontBitmap);
        int glyphW = mini != null ? mini.Value.Width / 16 : e.miniFontW;
        int glyphH = mini != null ? mini.Value.Height / 5 : e.miniFontH;

        for (int i = 0; i < StatusItems.Count; i++)
        {
            int[] pos = e.statusPos[i];
            int x, y;
            if (i == (int)StatusItem.PcmVolume || i == (int)StatusItem.PcmPtr)
            {
                // PCM の 2 項目は PCM 段の 1ch のスロットからの相対。
                x = e.statusX + e.pcmXOffset[0] + pos[0];
                y = e.statusY + e.chYOffset[8] + e.pcmYOffset[0] + pos[1];
            }
            else
            {
                x = e.statusX + pos[0];
                y = e.statusY + e.chYOffset[0] + pos[1];
            }
            r[Ids.Indexed(Ids.StatusItem, i)] = new Rectangle(x, y, TextWidth(StatusItemDigits[i], e.miniFontW, glyphW), glyphH);
        }

        // レベルメータは文字ではなく素材。左端 SrcX を切って描く。
        var lm = sizeOf(e.levelMeterBitmap);
        var lmPos = r[Ids.Indexed(Ids.StatusItem, (int)StatusItem.LevelMeter)];
        r[Ids.LevelMeter] = lm != null
            ? new Rectangle(lmPos.X, lmPos.Y, Math.Max(1, lm.Value.Width - e.levelMeterSrcX), lm.Value.Height)
            : new Rectangle(lmPos.X, lmPos.Y, Math.Max(1, e.levelMeterWidthCells), glyphH);
        // 「配置」タブから外してある項目なので、StatusItem 側の id も同じ矩形に
        // 揃えておく（レベルメータの各項目はこちらを指す）。
        r[Ids.Indexed(Ids.StatusItem, (int)StatusItem.LevelMeter)] = r[Ids.LevelMeter];

        // PCM 1ch〜8ch は、その ch の「音量」と「ポインタ」を含む矩形。
        int[] volPos = e.statusPos[(int)StatusItem.PcmVolume];
        int[] ptrPos = e.statusPos[(int)StatusItem.PcmPtr];
        int volW = TextWidth(StatusItemDigits[(int)StatusItem.PcmVolume], e.miniFontW, glyphW);
        int ptrW = TextWidth(StatusItemDigits[(int)StatusItem.PcmPtr], e.miniFontW, glyphW);
        Rectangle? pcmAll = null;
        for (int i = 0; i < 8; i++)
        {
            int bx = e.statusX + e.pcmXOffset[i];
            int by = e.statusY + e.chYOffset[8] + e.pcmYOffset[i];
            var a = new Rectangle(bx + volPos[0], by + volPos[1], volW, glyphH);
            var b = new Rectangle(bx + ptrPos[0], by + ptrPos[1], ptrW, glyphH);
            var slot = Rectangle.Union(a, b);
            r[Ids.Indexed(Ids.PcmSlot, i)] = slot;
            r[Ids.Indexed(Ids.PcmOrigin, i)] = new Rectangle(bx, by, 1, 1);
            pcmAll = pcmAll == null ? slot : Rectangle.Union(pcmAll.Value, slot);
        }
        // 「PCM の位置」のチェックボックス（8 行まとめて継承を切り替える行）は
        // どれか 1 ch を指せないので、8 ch ぶんの外接矩形を指す。
        r[Ids.PcmAll] = pcmAll ?? r[Ids.Status];
    }

    // ファイラー側の矩形（画面を 2 つに分けた片方）。SkinLayout.PlacedFor と
    // 同じ式をここでもう一度書く（描画側の内部状態に依存させないため）。
    private static Rectangle FilerSideRect(SkinLayout e)
    {
        int w = Math.Max(1, e.placedCanvasW);
        int h = Math.Max(1, e.placedCanvasH);
        int fixedV = e.screenH - e.filerExtent;
        int fixedH = e.screenW - e.filerExtent;
        return (FilerSide)e.filerSide switch
        {
            FilerSide.Top => new Rectangle(0, 0, w, Math.Max(1, h - fixedV)),
            FilerSide.Left => new Rectangle(0, 0, Math.Max(1, w - fixedH), h),
            FilerSide.Right => new Rectangle(fixedH, 0, Math.Max(1, w - fixedH), h),
            _ => new Rectangle(0, fixedV, w, Math.Max(1, h - fixedV)),
        };
    }

    private static void AddScrollBar(Dictionary<string, Rectangle> r, SkinLayout e)
    {
        r[Ids.ScrollBar] = new Rectangle(e.scrollX, e.scrollY, e.scrollW, e.scrollH);
        r[Ids.ScrollHit] = new Rectangle(e.scrollHitX, e.scrollY, e.scrollHitW, e.scrollH);
        r[Ids.ScrollUpArrow] = new Rectangle(e.scrollX + e.scrollPosUpArrow[0], e.scrollY + e.scrollPosUpArrow[1],
            e.scrollSrcUpArrow.W, e.scrollSrcUpArrow.H);
        // 溝は素材を繰り返して敷くので、枠は**描く高さ**で出す。
        r[Ids.ScrollBarGroove] = new Rectangle(e.scrollX + e.scrollPosBar[0], e.scrollY + e.scrollPosBar[1],
            e.scrollSrcBar.W, e.scrollGrooveH);
        r[Ids.ScrollDownArrow] = new Rectangle(e.scrollX + e.scrollPosDownArrow[0], e.scrollY + e.scrollPosDownArrow[1],
            e.scrollSrcDownArrow.W, e.scrollSrcDownArrow.H);
        // つまみは溝の中を動く。枠は動かない値にしたいので一番上に置いた姿。
        r[Ids.ScrollThumb] = new Rectangle(e.scrollX + e.scrollPosBar[0], e.scrollY + e.scrollPosBar[1],
            e.scrollSrcThumb.W, e.scrollSrcThumb.H);
    }

    private static void AddBars(Dictionary<string, Rectangle> r, SkinLayout e, Func<string, Size?> sizeOf, int volume)
    {
        var mini = sizeOf(e.miniFontBitmap);
        int glyphW = mini != null ? mini.Value.Width / 16 : e.miniFontW;
        int glyphH = mini != null ? mini.Value.Height / 5 : e.miniFontH;

        r[Ids.ProgressBar] = new Rectangle(e.progX, e.progY, e.progW, e.progH);
        r[Ids.ProgressTime] = new Rectangle(e.progX + e.progTimePos[0], e.progY + e.progTimePos[1],
            TextWidth(ProgressTimeDigits, e.miniFontW, glyphW), glyphH);

        r[Ids.VolumeBar] = new Rectangle(e.volX, e.volY, e.volW, e.volH);
        r[Ids.VolumeTime] = new Rectangle(e.volX + e.volTimePos[0], e.volY + e.volTimePos[1],
            TextWidth(VolumeTimeDigits, e.miniFontW, glyphW), glyphH);

        // つまみは音量で左右に動く。プレビューが今出している音量に合わせる。
        int movement = Math.Max(0, e.volW - e.volNobW);
        int v = Math.Max(-100, Math.Min(100, volume));
        int barPos = movement <= 0 ? 0 : Math.Max(0, Math.Min(movement, (v + 100) * movement / 200));
        r[Ids.VolumeNob] = new Rectangle(e.volX + barPos, e.volY, e.volRect[0].W, e.volRect[1].H);
    }

    private static void AddPlayKey(Dictionary<string, Rectangle> r, SkinLayout e)
    {
        r[Ids.PlayKey] = new Rectangle(e.playKeyX, e.playKeyY, e.playKeyW, e.playKeyH);
        for (int i = 0; i < 9; i++)
        {
            r[Ids.Indexed(Ids.PlayKeyButton, i)] = new Rectangle(
                e.playKeyX + e.playKeyPos[i][0], e.playKeyY + e.playKeyPos[i][1],
                e.playKeyRect[i].W, e.playKeyRect[i].H);
        }
    }

    // ミニフォントの文字列の幅。送りは miniFontW だが、最後の 1 文字だけは
    // 素材の 1 文字ぶん (glyphW) がそのまま出る。
    private static int TextWidth(int digits, int advance, int glyphW) =>
        digits <= 0 ? Math.Max(1, glyphW) : (digits - 1) * advance + glyphW;
}
