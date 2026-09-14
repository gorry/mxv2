// mxv2 スキンエディタ - アプリの名前・版・著作権・ビルド日付。
//
// 値の置き場は mxv2/Profile.ini だけ（著作者専用。本体の main.cpp の kApp* と
// 同じ出どころ）。SkinEditor.csproj がビルドのたびに Profile.ini を読んで
// アセンブリ属性へ写す（[Title] Text-SkinEditor → AssemblyTitle、[Version] Text
// → AssemblyInformationalVersion、[Copyright] Text-SkinEditor → AssemblyCopyright、
// ビルドした日 → AssemblyMetadata "BuildDate"）。ここはそれを読み出すだけで、
// 値を持たない。**ここに文字列を書かないこと。**

using System.Reflection;

namespace SkinEditor.Model;

public static class AppProfile
{
    private static readonly Assembly Asm = typeof(AppProfile).Assembly;

    // [Title] Text-SkinEditor（例: "mxv2 - Skin Editor"）
    public static string Title =>
        Asm.GetCustomAttribute<AssemblyTitleAttribute>()?.Title ?? "SkinEditor";

    // [Version] Text（例: "2026.0913.1"。本体と同じ値）
    public static string Version =>
        Asm.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "";

    // [Copyright] Text-SkinEditor
    public static string Copyright =>
        Asm.GetCustomAttribute<AssemblyCopyrightAttribute>()?.Copyright ?? "";

    // ビルドした日（yyyy-MM-dd。csproj が生成する）。
    public static string BuildDate =>
        Asm.GetCustomAttributes<AssemblyMetadataAttribute>()
            .FirstOrDefault(a => a.Key == "BuildDate")?.Value ?? "";

    // 「バージョン情報」ダイアログの本文。
    public static string AboutText =>
        Title + Environment.NewLine +
        "バージョン " + Version + Environment.NewLine +
        "ビルド日付 " + BuildDate + Environment.NewLine +
        Copyright;
}
