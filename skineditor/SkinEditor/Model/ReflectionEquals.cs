// mxv2 スキンエディタ - SkinLayout / ColorsModel の全フィールドを再帰的に
// 比較するユーティリティ。「Base 切替後、自スキンのキーが Base 側と同値に
// なったら間引く」判定で、SkinLayout 全体を比較すれば十分なので使う
// （1 キーは ApplyLayout 上で必ず特定フィールドだけを書き換えるので、
// 他が変わっていなければそのキーは無害＝そのフィールドだけの比較と同じ）。

using System.Reflection;

namespace SkinEditor.Model;

public static class ReflectionEquals
{
    public static bool DeepEquals(object? a, object? b)
    {
        if (ReferenceEquals(a, b)) return true;
        if (a is null || b is null) return false;
        var t = a.GetType();
        if (t != b.GetType()) return false;

        if (t.IsPrimitive || t == typeof(string) || t == typeof(decimal))
            return a.Equals(b);

        if (t.IsArray)
        {
            var aa = (Array)a;
            var bb = (Array)b;
            if (aa.Length != bb.Length) return false;
            for (int i = 0; i < aa.Length; i++)
                if (!DeepEquals(aa.GetValue(i), bb.GetValue(i))) return false;
            return true;
        }

        foreach (var f in t.GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            if (!DeepEquals(f.GetValue(a), f.GetValue(b))) return false;
        }
        return true;
    }
}
