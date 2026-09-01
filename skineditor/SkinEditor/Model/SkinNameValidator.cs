// mxv2 スキンエディタ - 新規スキン名のバリデーション。
// mxv2 本体の SettingsUi（配色の「名前を付けて保存」）と同じ禁則を使う
// （フォルダ名になるので、Windows のパス上不正な文字と "." を弾く）。
namespace SkinEditor.Model;

public static class SkinNameValidator
{
    private static readonly char[] Forbidden = { '\\', '/', ':', '*', '?', '"', '<', '>', '|' };

    // 問題無ければ null、問題があれば理由の文言を返す。
    public static string? Validate(string name)
    {
        if (string.IsNullOrEmpty(name)) return "スキン名を入力してください。";
        if (name == ".") return "「.」だけの名前は使えません。";
        if (name.EndsWith('.')) return "末尾がピリオドの名前は使えません。";
        foreach (var ch in Forbidden)
        {
            if (name.Contains(ch)) return $"名前に使えない文字が含まれています: {ch}";
        }
        return null;
    }
}
