namespace SkinEditor;

static class Program
{
    // コマンドライン（どちらも省略可。mxv2 本体の -userdir と同じ流儀）:
    //   SkinEditor.exe [-userdir <dir>] [<起動フォルダ>]
    //   -userdir <dir>  ユーザーフォルダモードで編集先にするフォルダ
    //                   （省略時は %APPDATA%\mxv2。検証や実験で本物の
    //                   ユーザーフォルダを汚したくないときに使う）
    //   <起動フォルダ>  assets/ のあるフォルダ（省略時はカレントと exe の隣）
    public static string? UserDirOverride { get; private set; }
    public static string? RootDirOverride { get; private set; }

    /// <summary>
    ///  The main entry point for the application.
    /// </summary>
    [STAThread]
    static void Main(string[] args)
    {
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "-userdir" && i + 1 < args.Length)
            {
                UserDirOverride = Path.GetFullPath(args[++i]);
            }
            else if (!args[i].StartsWith('-'))
            {
                RootDirOverride = Path.GetFullPath(args[i]);
            }
        }

        // To customize application configuration such as high DPI settings or default font,
        // see https://aka.ms/applicationconfiguration.
        ApplicationConfiguration.Initialize();

        // モードは起動したフォルダで一律に決める。どちらでもなければ、
        // 知らせて終わる（ユーザーの指示。フォルダを選び直す入口は無い）。
        var detection = Model.DevAssetRoot.DetectDefault(RootDirOverride, UserDirOverride);
        if (detection.Kind == Model.AssetRootKind.NotFound || detection.Root == null)
        {
            MessageBox.Show(detection.Message, "mxv2 スキンエディタ", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        Application.Run(new UI.SkinListForm(detection));
    }
}
