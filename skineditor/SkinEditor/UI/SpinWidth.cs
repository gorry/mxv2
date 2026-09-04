// mxv2 スキンエディタ - NumericUpDown（スピンボタン）の幅。
//
// FieldEditControl の元の 72px は正の4桁（例: "9999"）は実測で収まっては
// いたが、右パディングが約1pxしか無くぎりぎりだった。「素材に依存する
// 座標や幅」（素材は最大 9999×9999 を想定＝2026-09-06、ユーザー指示）を
// 収める項目では4桁入力が実際によく起きる想定にすべきで、ぎりぎり欠けない
// 程度では不十分と判断し、既定幅を 80px（正の4桁に余裕を持たせた値）へ
// 広げた。
// Minimum が負の項目（"-9999" のように符号が要る）は、右詰め表示の
// 都合で符号の1文字ぶんが左へはみ出して欠けて見える不具合を実測で確認した
// （2026-09-06、ユーザー指摘）。符号1文字ぶんの追加幅（実測 16px、
// 72→88 で欠けなくなることを確認した差分）を、幅の基準値が違う
// LabeledValueRow（既定 66px）にも同じ増分で適用する。

namespace SkinEditor.UI;

internal static class SpinWidth
{
    public const int FieldEditBase = 80;
    public const int SignExtra = 16;

    public static int For(Control ctx, int baseWidth, int min) =>
        Dpi.S(ctx, min < 0 ? baseWidth + SignExtra : baseWidth);

    public static int For(Control ctx, int min) => For(ctx, FieldEditBase, min);
}
