// mxv2 スキンエディタ - スキン参照 (ref) の扱い
//
// mxv2 本体の src/assetpath.h/.cpp の一部を移植したもの。今回は「開発用
// フォルダで起動し、assets/skin/ だけを編集する」場合だけを扱うので、
// ユーザーフォルダ (userDir) は常に空として扱う。結果として
// CanonicalSkinRef は常に "assets:<名前>" を返す（skineditor.md の
// 「[Skin].Base の参照先は、必ず assets:… となります」のとおり）。

namespace SkinEditor.Model;

public static class SkinRef
{
    public const string BundledPrefix = "assets:";

    public static bool IsBundled(string reference) =>
        reference.Length > BundledPrefix.Length &&
        reference[..BundledPrefix.Length].Equals(BundledPrefix, StringComparison.OrdinalIgnoreCase);

    public static string NameOf(string reference) =>
        IsBundled(reference) ? reference[BundledPrefix.Length..] : reference;

    public static string MakeBundled(string name) => BundledPrefix + name;
}
