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
}

public static class StatusItems
{
    public const int Count = (int)StatusItem.PcmPtr + 1;

    // layout.ini の [Status] でのキー名。
    public static readonly string[] Keys =
    {
        "PosVolume", "PosLevelMeter", "PosPanpot", "PosDetune",
        "PosVoice", "PosQ", "PosPtr", "PosLFOPitch",
        "PosLFOPitch1", "PosLFOPitch2", "PosLFOPitch3", "PosLFOPitch4",
        "PosLFOVolume", "PosLFOVolume1", "PosLFOVolume2", "PosLFOVolume3",
        "PosPcmVolume", "PosPcmPtr",
    };

    public static readonly string[] Labels =
    {
        "音量", "レベルメータ", "パンポット", "デチューン",
        "音色番号", "Q", "ポインタ", "ピッチLFO",
        "ピッチLFO 1", "ピッチLFO 2", "ピッチLFO 3", "ピッチLFO 4",
        "音量LFO", "音量LFO 1", "音量LFO 2", "音量LFO 3",
        "PCM 音量", "PCM ポインタ",
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
    };
}
