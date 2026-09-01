// mxv2 スキンエディタ - 素朴な INI 読み書き
//
// mxv2 本体の src/ini.cpp と同じ書式・同じ挙動になるように 1:1 で移植する。
//   - 行頭の ';' '#' はコメント（行全体）
//   - セクション名・キー名は大文字小文字を区別する
//   - 保存時、セクションは追加した順・キーは出現順に並ぶ
//   - 値の無いセクションは保存時に出力しない（ini.cpp と同じ）
//   - GetInt は C の atoi 相当（先頭の空白・符号・数字だけを見る。
//     数字が無ければ 0）

using System.Text;

namespace SkinEditor.Model;

public sealed class IniDocument
{
    private sealed class SectionData
    {
        public string Name = "";
        public List<string> Order = new();
        public Dictionary<string, string> Values = new(StringComparer.Ordinal);
    }

    private readonly List<SectionData> _sections = new();

    public bool Load(string path)
    {
        if (!File.Exists(path)) return false;
        string text = ReadUtf8NoBom(path);

        string section = "";
        int pos = 0;
        while (pos <= text.Length)
        {
            int nl = text.IndexOf('\n', pos);
            string raw = nl < 0 ? text[pos..] : text[pos..nl];
            pos = nl < 0 ? text.Length + 1 : nl + 1;
            string line = Trim(raw);

            if (line.Length == 0 || line[0] == ';' || line[0] == '#') continue;
            if (line[0] == '[')
            {
                int close = line.IndexOf(']');
                if (close >= 0)
                {
                    section = Trim(line[1..close]);
                    FindOrAdd(section);
                }
                continue;
            }
            int eq = line.IndexOf('=');
            if (eq < 0) continue;
            SetString(section, Trim(line[..eq]), Trim(line[(eq + 1)..]));
        }
        return true;
    }

    public bool Save(string path)
    {
        var sb = new StringBuilder();
        bool firstSection = true;
        foreach (var s in _sections)
        {
            if (s.Values.Count == 0) continue;
            if (!firstSection) sb.Append('\n');
            firstSection = false;
            if (s.Name.Length > 0) sb.Append('[').Append(s.Name).Append("]\n");
            foreach (var key in s.Order)
            {
                if (!s.Values.TryGetValue(key, out var v)) continue;
                sb.Append(key).Append('=').Append(v).Append('\n');
            }
        }

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
        return true;
    }

    public bool Has(string section, string key)
    {
        var s = Find(section);
        return s != null && s.Values.ContainsKey(key);
    }

    public int GetInt(string section, string key, int fallback)
    {
        var s = Find(section);
        if (s == null) return fallback;
        return s.Values.TryGetValue(key, out var v) ? AtoI(v) : fallback;
    }

    public string GetString(string section, string key, string fallback)
    {
        var s = Find(section);
        if (s == null) return fallback;
        return s.Values.TryGetValue(key, out var v) ? v : fallback;
    }

    public void SetInt(string section, string key, int value) =>
        SetString(section, key, value.ToString(System.Globalization.CultureInfo.InvariantCulture));

    public void SetString(string section, string key, string value)
    {
        var s = FindOrAdd(section);
        if (!s.Values.ContainsKey(key)) s.Order.Add(key);
        s.Values[key] = value;
    }

    public void Remove(string section, string key)
    {
        var s = Find(section);
        if (s == null) return;
        if (!s.Values.Remove(key)) return;
        s.Order.Remove(key);
    }

    public IReadOnlyList<string> Sections() => _sections.Select(s => s.Name).ToList();

    public IReadOnlyList<string> Keys(string section)
    {
        var s = Find(section);
        if (s == null) return Array.Empty<string>();
        return s.Order.Where(k => s.Values.ContainsKey(k)).ToList();
    }

    // 自分と同じ内容の深いコピーを作る（Base 切替や実効値コピーで使う）。
    public IniDocument Clone()
    {
        var c = new IniDocument();
        foreach (var s in _sections)
        {
            var ns = new SectionData { Name = s.Name };
            ns.Order.AddRange(s.Order);
            foreach (var kv in s.Values) ns.Values[kv.Key] = kv.Value;
            c._sections.Add(ns);
        }
        return c;
    }

    private SectionData? Find(string name) => _sections.FirstOrDefault(s => s.Name == name);

    private SectionData FindOrAdd(string name)
    {
        var s = Find(name);
        if (s != null) return s;
        var add = new SectionData { Name = name };
        _sections.Add(add);
        return add;
    }

    private static string Trim(string s)
    {
        int b = 0, e = s.Length;
        while (b < e && s[b] <= 0x20) b++;
        while (e > b && s[e - 1] <= 0x20) e--;
        return s[b..e];
    }

    private static int AtoI(string s)
    {
        int i = 0, n = s.Length;
        while (i < n && (s[i] == ' ' || (s[i] >= 0x09 && s[i] <= 0x0d))) i++;
        int sign = 1;
        if (i < n && (s[i] == '+' || s[i] == '-'))
        {
            if (s[i] == '-') sign = -1;
            i++;
        }
        long val = 0;
        bool any = false;
        while (i < n && s[i] >= '0' && s[i] <= '9')
        {
            any = true;
            val = val * 10 + (s[i] - '0');
            if (val > int.MaxValue) val = int.MaxValue;
            i++;
        }
        return any ? (int)(sign * val) : 0;
    }

    private static string ReadUtf8NoBom(string path)
    {
        byte[] bytes = File.ReadAllBytes(path);
        int offset = 0;
        if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) offset = 3;
        return new UTF8Encoding(false).GetString(bytes, offset, bytes.Length - offset);
    }
}
