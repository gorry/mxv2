// mxv2 スキンエディタ - ステータス欄の項目（mxv2 本体 src/skin.h の
// enum StatusItem と src/skin.cpp の kStatusItemKeys の移植）。
// 並び・キー名・既定値は C++ 側と 1:1 に保つこと。
//
// 表示名は本体のコード名（LFOPitch1〜4 など）に合わせた素っ気ないものに
// してある。原典 mxv/draw.cpp も「LFO Pitch 1」以上の名前を付けておらず、
// 中身の意味を勝手に決めつけないため。

namespace SkinEditor.Model;

public enum StatusItem
{
    Volume = 0,
    LevelMeter,
    Panpot,
    Detune,
    Voice,
    Q,
    Ptr,
    LFOPitch,
    LFOPitch1,
    LFOPitch2,
    LFOPitch3,
    LFOPitch4,
    LFOVolume,
    LFOVolume1,
    LFOVolume2,
    LFOVolume3,
    PcmVolume,
    PcmPtr,
    // ---- 音色データ表示（tonedata.md。本体 skin.h と同じ並び） ----
    OPMAlgorithm,
    OPMFeedback,
    OPMAttackRate,
    OPMDecayRate,
    OPMSustainRate,
    OPMReleaseRate,
    OPMSustainLevel,
    OPMTotalLevel,
    OPMKeyScaling,
    OPMMultiple,
    OPMDetune1,
    OPMDetune2,
    OPMAMSEnable,
    OPMNoise,
    OPMClockB,
    OPMLFOFreq,
    OPMLFOPMD,
    OPMLFOAMD,
    OPMLFOWave,
}

public static class StatusItems
{
    public const int Count = (int)StatusItem.OPMLFOWave + 1;

    // 音色データ表示の項目の分類（本体 skin.h の IsStatusOpm*Item）。
    // オペレータごとの項目は [Status] OPMOperatorY のスロットぶんが y に足され、
    // PCM の段の項目は PCM 段 (chYOffset[8]) の左上からの相対。
    public static bool IsOpmOperatorItem(StatusItem item) =>
        item >= StatusItem.OPMAttackRate && item <= StatusItem.OPMAMSEnable;
    public static bool IsOpmGlobalItem(StatusItem item) =>
        item >= StatusItem.OPMNoise && item <= StatusItem.OPMLFOWave;
    public static bool IsOpmItem(StatusItem item) =>
        item >= StatusItem.OPMAlgorithm && item <= StatusItem.OPMLFOWave;

    // layout.ini の [Status] でのキー名。
    public static readonly string[] Keys =
    {
        "PosVolume", "PosLevelMeter", "PosPanpot", "PosDetune",
        "PosVoice", "PosQ", "PosPtr", "PosLFOPitch",
        "PosLFOPitch1", "PosLFOPitch2", "PosLFOPitch3", "PosLFOPitch4",
        "PosLFOVolume", "PosLFOVolume1", "PosLFOVolume2", "PosLFOVolume3",
        "PosPcmVolume", "PosPcmPtr",
        // 音色データ表示（tonedata.md の綴りのまま。Algorythm は仕様書どおり）。
        "PosOPMAlgorythm", "PosOPMFeedback", "PosOPMAttackRate", "PosOPMDecayRate",
        "PosOPMSustainRate", "PosOPMReleaseRate", "PosOPMSustainLevel", "PosOPMTotalLevel",
        "PosOPMKeyScaling", "PosOPMMultiple", "PosOPMDetune1", "PosOPMDetune2",
        "PosOPMAMSEnable",
        "PosOPMNoise", "PosOPMClockB", "PosOPMLFOFreq", "PosOPMLFOPMD",
        "PosOPMLFOAMD", "PosOPMLFOWAVE",
    };

    public static readonly string[] Labels =
    {
        "音量", "レベルメータ", "パンポット", "デチューン",
        "音色番号", "Q", "ポインタ", "ピッチLFO",
        "ピッチLFO 1", "ピッチLFO 2", "ピッチLFO 3", "ピッチLFO 4",
        "音量LFO", "音量LFO 1", "音量LFO 2", "音量LFO 3",
        "PCM 音量", "PCM ポインタ",
        "アルゴリズム", "フィードバック", "アタックレート", "ディケイレート",
        "サスティンレート", "リリースレート", "サスティンレベル", "トータルレベル",
        "キースケーリング", "マルチプル", "デチューン", "デチューン2",
        "AMSイネーブル",
        "NOISE", "CLKB", "LFRQ", "PMD", "AMD", "WAVE",
    };

    // src/skin.cpp の Skin::Skin() と同じ既定値（旧 mxv/draw.cpp の CX_D/CY_D）。
    public static int[][] DefaultPos() => new[]
    {
        new[] { 2, 0 },    // Volume
        new[] { 26, 0 },   // LevelMeter
        new[] { 122, 0 },  // Panpot
        new[] { 2, 9 },    // Detune
        new[] { 40, 9 },   // Voice
        new[] { 66, 9 },   // Q
        new[] { 92, 9 },   // Ptr
        new[] { 2, 18 },   // LFOPitch
        new[] { 40, 18 },  // LFOPitch1
        new[] { 46, 18 },  // LFOPitch2
        new[] { 72, 18 },  // LFOPitch3
        new[] { 104, 18 }, // LFOPitch4
        new[] { 2, 27 },   // LFOVolume
        new[] { 40, 27 },  // LFOVolume1
        new[] { 46, 27 },  // LFOVolume2
        new[] { 72, 27 },  // LFOVolume3
        new[] { 2, 0 },    // PcmVolume（PcmX/PcmY からの相対）
        new[] { 24, 0 },   // PcmPtr
        // 音色データ表示（tonedata.md の既定値）
        new[] { 0, 0 },    // OPMAlgorithm
        new[] { 0, 9 },    // OPMFeedback
        new[] { 14, 0 },   // OPMAttackRate（以下 AMSEnable まで OPMOperatorY を加算）
        new[] { 28, 0 },   // OPMDecayRate
        new[] { 42, 0 },   // OPMSustainRate
        new[] { 56, 0 },   // OPMReleaseRate
        new[] { 64, 0 },   // OPMSustainLevel
        new[] { 72, 0 },   // OPMTotalLevel
        new[] { 86, 0 },   // OPMKeyScaling
        new[] { 94, 0 },   // OPMMultiple
        new[] { 102, 0 },  // OPMDetune1
        new[] { 110, 0 },  // OPMDetune2
        new[] { 118, 0 },  // OPMAMSEnable
        new[] { 0, 0 },    // OPMNoise（PCM の段の左上からの相対）
        new[] { 0, 9 },    // OPMClockB
        new[] { 68, 0 },   // OPMLFOFreq
        new[] { 68, 9 },   // OPMLFOPMD
        new[] { 68, 18 },  // OPMLFOAMD
        new[] { 68, 27 },  // OPMLFOWave
    };

    // [Status] OPMOperatorY の既定値（OPM のスロット順 M1, M2, C1, C2 の y）。
    public static int[] DefaultOperatorY() => new[] { 0, 18, 9, 27 };
}
