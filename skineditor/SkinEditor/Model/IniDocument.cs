// mxv2 スキンエディタ - コメントを保つ INI 読み書き
//
// 読む側は mxv2 本体の src/ini.cpp と同じ書式・同じ挙動:
//   - 行頭の ';' '#' はコメント（行全体）
//   - セクション名・キー名は大文字小文字を区別する
//   - 同じキーが複数あれば後のものが勝つ
//   - GetInt は C の atoi 相当（先頭の空白・符号・数字だけを見る。
//     数字が無ければ 0）
//
// 書く側は **元のファイルの行をそのまま持ち続けて、キーの値だけを差し替える**
// （2026-09-15、ユーザーの指示。それまではセクションとキーだけを書き出す作りで、
// 開発フォルダモードで同梱スキンを保存すると layout.ini の説明コメントが全部
// 消え、手で戻したときの写し間違いで Phone の [Screen] が壊れた）。
//   - コメント行・空行・行の並びは読んだときのまま
//   - 既存のキーは元の行の位置で値だけ書き換える（`Key=Value` に正規化）
//   - 消したキーは行ごと消す（前後のコメントは残す）
//   - 新しいキーはそのセクションの最後のキー行の直後に足す
//     （セクション末尾の空行やコメントより前）
//   - 新しいセクションはファイルの末尾に、空行を 1 つ挟んで足す
//   - 文字コードは UTF-8（BOM なし）、改行は LF に揃える

using System.Text;

namespace SkinEditor.Model;

public sealed class IniDocument
{
    private enum Kind { Other, Section, Key }

    // 1 行。Raw は書き出すときの行そのもの（改行は含まない）。
    private sealed class Line
    {
        public Kind Kind;
        public string Raw = "";
        public string Section = "";  // Section 行ならその名前。Key 行なら属するセクション
        public string Key = "";
        public string Value = "";
    }

    private readonly List<Line> _lines = new();

    public bool Load(string path)
    {
        if (!File.Exists(path)) return false;
        LoadText(ReadUtf8NoBom(path));
        return true;
    }

    // 文字列から読む（テストと、ファイル以外の出どころ用）。
    public void LoadText(string text)
    {
        _lines.Clear();
        string section = "";
        int pos = 0;
        while (pos < text.Length)
        {
            int nl = text.IndexOf('\n', pos);
            string raw = nl < 0 ? text[pos..] : text[pos..nl];
            pos = nl < 0 ? text.Length : nl + 1;
            if (raw.EndsWith('\r')) raw = raw[..^1];
            _lines.Add(Parse(raw, ref section));
        }
    }

    private static Line Parse(string raw, ref string section)
    {
        string line = Trim(raw);
        if (line.Length == 0 || line[0] == ';' || line[0] == '#')
            return new Line { Kind = Kind.Other, Raw = raw, Section = section };
        if (line[0] == '[')
        {
            int close = line.IndexOf(']');
            if (close >= 0)
            {
                section = Trim(line[1..close]);
                return new Line { Kind = Kind.Section, Raw = raw, Section = section };
            }
            return new Line { Kind = Kind.Other, Raw = raw, Section = section };
        }
        int eq = line.IndexOf('=');
        if (eq < 0) return new Line { Kind = Kind.Other, Raw = raw, Section = section };
        return new Line
        {
            Kind = Kind.Key, Raw = raw, Section = section,
            Key = Trim(line[..eq]), Value = Trim(line[(eq + 1)..]),
        };
    }

    public bool Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        File.WriteAllText(path, ToText(), new UTF8Encoding(false));
        return true;
    }

    // 書き出す内容。末尾は必ず改行で終える。
    public string ToText()
    {
        var sb = new StringBuilder();
        foreach (var l in _lines) sb.Append(l.Raw).Append('\n');
        return sb.ToString();
    }

    public bool Has(string section, string key) => FindKey(section, key) >= 0;

    public int GetInt(string section, string key, int fallback)
    {
        int i = FindKey(section, key);
        return i < 0 ? fallback : AtoI(_lines[i].Value);
    }

    public string GetString(string section, string key, string fallback)
    {
        int i = FindKey(section, key);
        return i < 0 ? fallback : _lines[i].Value;
    }

    public void SetInt(string section, string key, int value) =>
        SetString(section, key, value.ToString(System.Globalization.CultureInfo.InvariantCulture));

    public void SetString(string section, string key, string value)
    {
        int i = FindKey(section, key);
        if (i >= 0)
        {
            var l = _lines[i];
            if (l.Value == value) return;
            l.Value = value;
            l.Raw = key + "=" + value;
            return;
        }

        var add = new Line { Kind = Kind.Key, Raw = key + "=" + value, Section = section, Key = key, Value = value };

        // セクションの最後のキー行の直後。キー行が無ければセクション見出しの直後。
        int lastKey = -1, header = -1;
        for (int k = 0; k < _lines.Count; k++)
        {
            var l = _lines[k];
            if (l.Section != section) continue;
            if (l.Kind == Kind.Section && header < 0) header = k;
            if (l.Kind == Kind.Key) lastKey = k;
        }
        if (lastKey >= 0)
        {
            _lines.Insert(lastKey + 1, add);
            return;
        }
        if (header >= 0)
        {
            _lines.Insert(header + 1, add);
            return;
        }
        if (section.Length == 0)
        {
            // セクション無し（ファイル先頭）のキー。先頭に置く。
            _lines.Insert(0, add);
            return;
        }
        // 新しいセクション。末尾に空行を 1 つ挟んで足す。
        if (_lines.Count > 0 && Trim(_lines[^1].Raw).Length > 0)
            _lines.Add(new Line { Kind = Kind.Other, Raw = "", Section = _lines[^1].Section });
        _lines.Add(new Line { Kind = Kind.Section, Raw = "[" + section + "]", Section = section });
        _lines.Add(add);
    }

    public void Remove(string section, string key)
    {
        _lines.RemoveAll(l => l.Kind == Kind.Key && l.Section == section && l.Key == key);
    }

    // セクション名を出現順に。見出しの無い先頭部分にキーがあれば "" も含む。
    public IReadOnlyList<string> Sections()
    {
        var list = new List<string>();
        foreach (var l in _lines)
        {
            if (l.Kind == Kind.Section && !list.Contains(l.Section)) list.Add(l.Section);
            else if (l.Kind == Kind.Key && l.Section.Length == 0 && !list.Contains("")) list.Add("");
        }
        return list;
    }

    // キー名を出現順に（重複は最初の位置で 1 回）。
    public IReadOnlyList<string> Keys(string section)
    {
        var list = new List<string>();
        foreach (var l in _lines)
        {
            if (l.Kind == Kind.Key && l.Section == section && !list.Contains(l.Key)) list.Add(l.Key);
        }
        return list;
    }

    // 自分と同じ内容（コメントも含めて）の深いコピーを作る。
    public IniDocument Clone()
    {
        var c = new IniDocument();
        foreach (var l in _lines)
        {
            c._lines.Add(new Line { Kind = l.Kind, Raw = l.Raw, Section = l.Section, Key = l.Key, Value = l.Value });
        }
        return c;
    }

    // 同じキーが複数あれば後のものが勝つ（本体の ini.cpp と同じ）。
    private int FindKey(string section, string key)
    {
        for (int i = _lines.Count - 1; i >= 0; i--)
        {
            var l = _lines[i];
            if (l.Kind == Kind.Key && l.Section == section && l.Key == key) return i;
        }
        return -1;
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
