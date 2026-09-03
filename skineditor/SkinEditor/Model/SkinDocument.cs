// mxv2 スキンエディタ - 1 スキンぶんの編集セッション。
//
// Own（自分の layout.ini / colors.ini。ディスクへはまだ書かれていないかも
// しれない編集中の内容）と、Base 鎖（ディスク上の祖先スキン、読むだけ）を
// 持ち、Effective（実効値。mxv2 本体の Skin::Load と同じアルゴリズムで
// 都度再計算する）を提供する。保存は明示の Save() でのみ行うが、
// [Skin].Base の切替だけは skineditor.md の指示どおりその場で保存する。

namespace SkinEditor.Model;

public sealed class SkinDocument
{
    public DevAssetRoot Root { get; }
    public string Name { get; private set; } = "";
    public string OwnDir { get; private set; } = "";
    public bool IsNew { get; private set; }

    public IniDocument OwnLayoutIni { get; private set; } = new();
    public bool HasOwnColors { get; private set; }
    private ColorsValues _colorsWorking = new();

    public string BaseRef { get; private set; } = "";
    public bool BaseCycleDetected { get; private set; }

    // Base 鎖（own を含まない。近い順）。実在するフォルダのみ。
    private readonly List<string> _baseDirs = new();
    private readonly List<IniDocument> _baseLayoutInis = new();

    public SkinLayout Effective { get; private set; } = new();
    public ColorsValues EffectiveColors { get; private set; } = new();

    public bool IsDirty { get; private set; }

    public event Action? Changed;

    private SkinDocument(DevAssetRoot root) => Root = root;

    // ---- 生成 ----------------------------------------------------------
    public static SkinDocument Open(DevAssetRoot root, string name)
    {
        var dir = root.SkinDir(name) ?? throw new DirectoryNotFoundException(
            $"スキンフォルダが見つかりません: {name}");
        var doc = new SkinDocument(root) { Name = name, OwnDir = dir, IsNew = false };

        doc.OwnLayoutIni.Load(Path.Combine(dir, "layout.ini"));
        doc.LoadOwnColorsFromDisk();
        doc.ResolveBaseChain();
        doc.Recompute();
        doc.IsDirty = false;
        return doc;
    }

    public static SkinDocument CreateNew(DevAssetRoot root, string name, string? baseSkinName)
    {
        var doc = new SkinDocument(root)
        {
            Name = name,
            OwnDir = root.SkinFolderPathFor(name),
            IsNew = true,
        };
        if (!string.IsNullOrEmpty(baseSkinName))
            doc.OwnLayoutIni.SetString("Skin", "Base", DevAssetRoot.CanonicalRef(baseSkinName));
        doc.HasOwnColors = false;
        doc.ResolveBaseChain();
        doc.Recompute();
        doc.IsDirty = true;  // 一度も保存していない
        return doc;
    }

    // 既存スキンを「コピーして新規スキンにする」（参照ではなく独立した実体を
    // 作る）。実効値を own へ丸ごと複製し素材ファイルもコピーする必要が
    // あるため、SwitchBaseOff() をそのまま使う。SwitchBaseOff() は
    // skineditor.md の指示どおりその場で保存するので、参照作成（CreateNew）
    // と違いこちらは作成した瞬間にディスクへ書かれる。
    public static SkinDocument CreateNewAsCopy(DevAssetRoot root, string name, string baseSkinName)
    {
        var doc = CreateNew(root, name, baseSkinName);
        doc.SwitchBaseOff();
        return doc;
    }

    // ---- Base 鎖の解決 ---------------------------------------------------
    private void ResolveBaseChain()
    {
        _baseDirs.Clear();
        _baseLayoutInis.Clear();
        BaseCycleDetected = false;

        var visited = new List<string> { DevAssetRoot.CanonicalRef(Name) };
        string? current = SkinLayoutIo.ReadBaseRef(OwnLayoutIni);

        for (int depth = 0; depth < 8 && !string.IsNullOrEmpty(current); depth++)
        {
            var baseName = SkinRef.NameOf(current);
            var dir = Root.SkinDir(baseName);
            if (dir == null) break;  // 土台が無い（壊れた参照）
            if (_baseDirs.Contains(dir)) break;  // 同じフォルダへ戻ってきた

            var canon = DevAssetRoot.CanonicalRef(baseName);
            if (visited.Any(v => string.Equals(v, canon, StringComparison.OrdinalIgnoreCase)))
            {
                BaseCycleDetected = true;
                break;
            }
            visited.Add(canon);
            _baseDirs.Add(dir);

            var ini = new IniDocument();
            ini.Load(Path.Combine(dir, "layout.ini"));
            _baseLayoutInis.Add(ini);
            current = SkinLayoutIo.ReadBaseRef(ini);
        }
    }

    // ---- 実効値の再計算 ---------------------------------------------------
    public void Recompute()
    {
        BaseRef = SkinLayoutIo.ReadBaseRef(OwnLayoutIni);

        var eff = new SkinLayout();
        for (int i = _baseLayoutInis.Count - 1; i >= 0; i--) SkinLayoutIo.ApplyLayout(_baseLayoutInis[i], eff);
        SkinLayoutIo.ApplyLayout(OwnLayoutIni, eff);
        Effective = eff;

        EffectiveColors = HasOwnColors ? _colorsWorking : ResolveColorsFromDirs(_baseDirs);
        Changed?.Invoke();
    }

    private static ColorsValues ResolveColorsFromDirs(IReadOnlyList<string> dirsNearToFar)
    {
        foreach (var dir in dirsNearToFar)
        {
            var path = Path.Combine(dir, "colors.ini");
            if (!File.Exists(path)) path = Path.Combine(dir, "theme.mxv");
            if (!File.Exists(path)) continue;
            var ini = new IniDocument();
            ini.Load(path);
            var v = new ColorsValues();
            ColorsIo.Apply(ini, v);
            return v;
        }
        return new ColorsValues();
    }

    private void LoadOwnColorsFromDisk()
    {
        var colorsPath = Path.Combine(OwnDir, "colors.ini");
        var legacyPath = Path.Combine(OwnDir, "theme.mxv");
        if (File.Exists(colorsPath))
        {
            var ini = new IniDocument();
            ini.Load(colorsPath);
            _colorsWorking = new ColorsValues();
            ColorsIo.Apply(ini, _colorsWorking);
            HasOwnColors = true;
        }
        else if (File.Exists(legacyPath))
        {
            var ini = new IniDocument();
            ini.Load(legacyPath);
            _colorsWorking = new ColorsValues();
            ColorsIo.Apply(ini, _colorsWorking);
            HasOwnColors = true;  // 保存すれば colors.ini へ移行し、theme.mxv は消える
        }
        else
        {
            HasOwnColors = false;
        }
    }

    // ---- layout.ini の編集 -------------------------------------------------
    public bool IsLayoutOwn(string section, string key) => OwnLayoutIni.Has(section, key);

    public bool CanRevertLayout(string section, string key) =>
        !string.IsNullOrEmpty(BaseRef) && IsLayoutOwn(section, key);

    public void SetLayoutRaw(string section, string key, string rawText)
    {
        OwnLayoutIni.SetString(section, key, rawText);
        MarkDirty();
        Recompute();
    }

    public void RevertLayout(string section, string key)
    {
        OwnLayoutIni.Remove(section, key);
        MarkDirty();
        Recompute();
    }

    // ---- colors.ini の編集（ファイル単位。継承か、全キー明示かの二択） -------
    public void MutateColors(Action<ColorsValues> mutate)
    {
        if (!HasOwnColors)
        {
            _colorsWorking = CloneColors(EffectiveColors);
            HasOwnColors = true;
        }
        mutate(_colorsWorking);
        MarkDirty();
        Recompute();
    }

    public void RevertColorsToInherited()
    {
        HasOwnColors = false;
        _colorsWorking = new ColorsValues();
        MarkDirty();
        Recompute();
    }

    private static ColorsValues CloneColors(ColorsValues src) => src.Clone();

    // ---- Base の切替（skineditor.md「Baseによる参照」） -----------------------
    public IReadOnlyList<string> ListBaseCandidates() =>
        Root.ListSkinNames().Where(n => !string.Equals(n, Name, StringComparison.OrdinalIgnoreCase)).ToList();

    // 無参照 -> 参照。実行後、その場で保存する。
    public void SwitchBaseOn(string baseSkinName)
    {
        OwnLayoutIni.SetString("Skin", "Base", DevAssetRoot.CanonicalRef(baseSkinName));
        ResolveBaseChain();

        // Base 鎖だけで出る値と同じになった自スキンのキーは間引く。
        var baseOnly = new SkinLayout();
        for (int i = _baseLayoutInis.Count - 1; i >= 0; i--) SkinLayoutIo.ApplyLayout(_baseLayoutInis[i], baseOnly);

        foreach (var section in OwnLayoutIni.Sections().ToList())
        {
            if (section == "Skin") continue;  // Base 自身は対象外
            foreach (var key in OwnLayoutIni.Keys(section).ToList())
            {
                var single = new IniDocument();
                single.SetString(section, key, OwnLayoutIni.GetString(section, key, ""));
                var probe = baseOnly.Clone();
                SkinLayoutIo.ApplyLayout(single, probe);
                if (ReflectionEquals.DeepEquals(baseOnly, probe))
                    OwnLayoutIni.Remove(section, key);
            }
        }

        if (HasOwnColors)
        {
            var baseOnlyColors = ResolveColorsFromDirs(_baseDirs);
            if (ReflectionEquals.DeepEquals(baseOnlyColors, _colorsWorking))
            {
                HasOwnColors = false;
                _colorsWorking = new ColorsValues();
            }
        }

        Recompute();
        Save();
    }

    // 参照 -> 無参照。実効値をすべて自スキン側へコピーしたうえで、その場で保存する。
    public void SwitchBaseOff()
    {
        var snapshot = Effective;  // Base ありの状態での実効値
        var newOwnIni = new IniDocument();
        SkinLayoutIo.WriteAll(snapshot, newOwnIni);
        OwnLayoutIni = newOwnIni;  // [Skin]Base は書かれないので無参照になる

        CopyMissingAssetFiles(snapshot);

        _colorsWorking = CloneColors(EffectiveColors);
        HasOwnColors = true;

        ResolveBaseChain();
        Recompute();
        Save();
    }

    private void CopyMissingAssetFiles(SkinLayout eff)
    {
        Directory.CreateDirectory(OwnDir);
        var names = new[]
        {
            eff.backBitmap, eff.kb0Bitmap, eff.kb1Bitmap, eff.kb2Bitmap, eff.miniFontBitmap,
            eff.levelMeterBitmap, eff.bannerBitmap, eff.playKeyBitmap, eff.progressBarBitmap,
            eff.volBarBitmap, eff.scrollBarBitmap, "font.ttf",
        };

        foreach (var name in names.Distinct())
        {
            var srcDir = _baseDirs.FirstOrDefault(d => File.Exists(Path.Combine(d, name)));
            if (srcDir == null) continue;  // 参照先チェーンのどこにも無ければ何もしない
            var srcPath = Path.Combine(srcDir, name);
            var destPath = Path.Combine(OwnDir, name);

            if (File.Exists(destPath))
            {
                // 自スキンに同名のファイルがすでにある。中身が参照先と同じなら
                // コピーする意味が無いので退避もしない（ユーザー指示）。違う
                // 場合は、参照中に実際使われていたのは参照先のファイルなので、
                // 自スキン側の古いファイルを "_nouse" へ退避してから参照先を
                // 複製する。
                if (NouseFolder.FilesEqual(destPath, srcPath)) continue;
                NouseFolder.Evacuate(OwnDir, destPath);
            }
            File.Copy(srcPath, destPath, overwrite: false);
        }
    }

    // ---- ビットマップインポート後のクリップ ---------------------------------
    // 新しい素材がこれまでより小さいと、layout.ini の Src 系矩形がビットマップの
    // 外を指したままになりうる（skineditor.md の指示）。はみ出す矩形だけ、
    // 新しいビットマップの範囲内に丸めて own 側の値にする。
    public void ClipRectsForImportedBitmap(BitmapRole role, int bmpW, int bmpH)
    {
        void ClipOne(string section, string key, Func<SkinLayout, Xywh> get)
        {
            var r = get(Effective);
            var clipped = ClipRect(r, bmpW, bmpH);
            if (!clipped.Equals(r)) SetLayoutRaw(section, key, clipped.ToString());
        }

        switch (role)
        {
            case BitmapRole.PlayKey:
                for (int i = 0; i < 9; i++)
                {
                    int idx = i;
                    ClipOne("PlayKey", $"Src{idx}", e => e.playKeyRect[idx]);
                }
                break;
            case BitmapRole.ScrollBar:
                ClipOne("ScrollBar", "SrcThumb", e => e.scrollSrcThumb);
                ClipOne("ScrollBar", "SrcUpArrowPress", e => e.scrollSrcUpArrowPress);
                ClipOne("ScrollBar", "SrcDownArrowPress", e => e.scrollSrcDownArrowPress);
                ClipOne("ScrollBar", "SrcUpArrow", e => e.scrollSrcUpArrow);
                ClipOne("ScrollBar", "SrcBar", e => e.scrollSrcBar);
                ClipOne("ScrollBar", "SrcDownArrow", e => e.scrollSrcDownArrow);
                break;
            case BitmapRole.VolumeBar:
                ClipOne("VolumeBar", "NobSrc", e => e.volRect[0]);
                ClipOne("VolumeBar", "SlideSrc", e => e.volRect[1]);
                break;
            case BitmapRole.Banner:
            {
                int w = Math.Min(Effective.bannerW, bmpW);
                int h = Math.Min(Effective.bannerH, bmpH);
                if (w != Effective.bannerW || h != Effective.bannerH)
                    SetLayoutRaw("Banner", "Rect", $"{Effective.bannerX},{Effective.bannerY},{w},{h}");
                break;
            }
            case BitmapRole.ProgressBar:
            {
                // 素材は上段・下段の 2 段構成（skin.cpp のコメント参照）。
                int maxH = Math.Max(1, bmpH / 2);
                int w = Math.Min(Effective.progW, bmpW);
                int h = Math.Min(Effective.progH, maxH);
                if (w != Effective.progW || h != Effective.progH)
                    SetLayoutRaw("ProgressBar", "Rect", $"{Effective.progX},{Effective.progY},{w},{h}");
                break;
            }
            default:
                // Back / Kb0-2 / MiniFont / LevelMeter は明示の Src 矩形を持たないため対象外。
                break;
        }
    }

    private static Xywh ClipRect(Xywh r, int bmpW, int bmpH)
    {
        int x = Math.Clamp(r.X, 0, Math.Max(0, bmpW - 1));
        int y = Math.Clamp(r.Y, 0, Math.Max(0, bmpH - 1));
        int w = Math.Clamp(r.W, 1, Math.Max(1, bmpW - x));
        int h = Math.Clamp(r.H, 1, Math.Max(1, bmpH - y));
        return new Xywh(x, y, w, h);
    }

    // ---- 保存 ------------------------------------------------------------
    public void Save()
    {
        Directory.CreateDirectory(OwnDir);
        OwnLayoutIni.Save(Path.Combine(OwnDir, "layout.ini"));

        var colorsPath = Path.Combine(OwnDir, "colors.ini");
        var legacyPath = Path.Combine(OwnDir, "theme.mxv");
        if (HasOwnColors)
        {
            var ini = new IniDocument();
            ColorsIo.WriteAll(_colorsWorking, ini);
            ini.Save(colorsPath);
            if (File.Exists(legacyPath)) File.Delete(legacyPath);
        }
        else
        {
            if (File.Exists(colorsPath)) File.Delete(colorsPath);
            if (File.Exists(legacyPath)) File.Delete(legacyPath);
        }

        IsNew = false;
        IsDirty = false;
    }

    private void MarkDirty() => IsDirty = true;

    // BitmapImporter などが「実際に見えているファイル探索先」を知るために使う。
    public IReadOnlyList<string> AllDirsNearToFar()
    {
        var list = new List<string> { OwnDir };
        list.AddRange(_baseDirs);
        return list;
    }
}
