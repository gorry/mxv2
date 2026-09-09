import cv2
import numpy as np
import argparse
import os
import sys

# プリセットの定義
PRESETS = {
    'silver-light': {'r': 230, 'g': 240, 'b': 255, 'intensity': 0.3},
    'silver-heavy': {'r': 200, 'g': 220, 'b': 255, 'intensity': 0.6},
    'gold-light':   {'r': 255, 'g': 220, 'b': 100, 'intensity': 0.4},
    'gold-heavy':   {'r': 255, 'g': 180, 'b': 30,  'intensity': 0.7},
    'dark-light':   {'r': 50,  'g': 50,  'b': 50,  'intensity': 0.5},
    'dark-heavy':   {'r': 20,  'g': 20,  'b': 20,  'intensity': 0.8},
}

def colorize_texture(gray_img, r, g, b, alpha=255, intensity=0.5):
    """
    オーバーレイ合成を用いてグレー画像に色を付ける
    """
    if len(gray_img.shape) == 2:
        base = cv2.cvtColor(gray_img, cv2.COLOR_GRAY2BGR)
    else:
        base = gray_img.copy()

    base_f = base.astype(np.float32) / 255.0
    color_f = np.array([b, g, r], dtype=np.float32) / 255.0 # BGR順

    # オーバーレイ合成
    overlay = np.where(
        base_f < 0.5,
        2.0 * base_f * color_f,
        1.0 - 2.0 * (1.0 - base_f) * (1.0 - color_f)
    )
    
    # ブレンド
    result_f = base_f * (1.0 - intensity) + overlay * intensity
    result = np.clip(result_f * 255.0, 0, 255).astype(np.uint8)
    
    # RGBA化してアルファチャンネルを追加
    result_rgba = cv2.cvtColor(result, cv2.COLOR_BGR2BGRA)
    result_rgba[:, :, 3] = alpha
    
    return result_rgba

def apply_light(img, light_color=(255, 255, 255), pos=(0.5, 0.5), intensity=0.8, radius=0.35, spread=0.5, stretch=2.5, direction='horizontal'):
    """
    ヘアライン画像に光の反射効果（異方性ハイライト）を適用する
    """
    has_alpha = (len(img.shape) == 3 and img.shape[2] == 4)
    if has_alpha:
        bgr = img[:, :, :3]
        alpha = img[:, :, 3]
    elif len(img.shape) == 3:
        bgr = img
        alpha = None
    else:
        bgr = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
        alpha = None

    h, w = bgr.shape[:2]
    cx = pos[0] * (w - 1)
    cy = pos[1] * (h - 1)

    # 座標グリッドの生成
    ys, xs = np.indices((h, w), dtype=np.float32)
    scale = min(w, h)
    dx = (xs - cx) / scale
    dy = (ys - cy) / scale

    # 異方性反射の適用（ヘアラインの筋と直角の方向にハイライトを引き伸ばす）
    stretch = max(stretch, 0.01)
    if direction == 'horizontal':
        # 水平ヘアライン -> 縦方向に伸ばす
        dy_adj = dy / stretch
        dx_adj = dx
    elif direction == 'vertical':
        # 垂直ヘアライン -> 横方向に伸ばす
        dx_adj = dx / stretch
        dy_adj = dy
    else:
        dx_adj = dx
        dy_adj = dy

    dist = np.sqrt(dx_adj**2 + dy_adj**2)

    # 減衰カーブ（フォールオフ）
    r = max(radius, 0.001)
    s = max(spread, 0.01)
    power = 1.0 / s
    light_map = np.exp(-0.5 * ((dist / r) ** power))

    # 光源色 (RGB -> BGR, float 0..1)
    r_val, g_val, b_val = light_color
    l_color_bgr = np.array([b_val, g_val, r_val], dtype=np.float32) / 255.0

    # ベース画像を float 0..1 に正規化
    base_f = bgr.astype(np.float32) / 255.0
    gray_f = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY).astype(np.float32) / 255.0

    # 金属テクスチャの微細な研磨筋をハイライトに反映
    texture_factor = 0.3 + 0.7 * (gray_f ** 1.2)
    highlight = light_map * texture_factor * intensity
    highlight_3ch = np.dstack([highlight, highlight, highlight]) * l_color_bgr

    # スクリーン合成 + ハイライトコア加算（白飛びを防ぎつつ金属の輝きを表現）
    screen_blend = 1.0 - (1.0 - base_f) * (1.0 - np.clip(highlight_3ch, 0.0, 1.0))
    result_f = np.clip(screen_blend + highlight_3ch * 0.2, 0.0, 1.0)
    result = (result_f * 255.0).astype(np.uint8)

    if has_alpha:
        result = cv2.cvtColor(result, cv2.COLOR_BGR2BGRA)
        result[:, :, 3] = alpha

    return result

def parse_brightness_range(range_str):
    """
    明るさレンジ文字列を (min_val, max_val) のタプルに変換する。
    16進数 (例: '0x20,0x80')、10進数 (例: '50,220')、小数 (例: '0.2,0.8') に対応。
    """
    def parse_val(v_str):
        v = v_str.strip()
        if v.lower().startswith('0x'):
            return float(int(v, 16))
        val = float(v)
        if 0.0 <= val <= 1.0 and '.' in v:
            return val * 255.0
        return val

    parts = range_str.split(',')
    if len(parts) != 2:
        raise ValueError(f"Invalid range format '{range_str}'. Expected 'min,max' (e.g. '0x20,0x80' or '50,220').")
    v1 = parse_val(parts[0])
    v2 = parse_val(parts[1])
    min_v = max(0.0, min(v1, v2))
    max_v = min(255.0, max(v1, v2))
    return min_v, max_v

def generate_hairline(width=1024, height=1024, direction='horizontal', length=150, intensity=1.0, groove=0.0, brightness_range=(0x90, 0xdf), fine=1.0):
    """
    ヘアライン加工風のテクスチャを生成する
    """
    print(f"Generating noise ({width}x{height})...")
    # 1. 一様乱数によるノイズ画像を生成
    # 金属のベースとなるノイズ
    noise = np.random.randint(0, 256, (height, width), dtype=np.uint8)

    # 2. モーションブラーを適用して線状の模様（ヘアライン）を作る
    kernel_size = max(1, int(length))
    print(f"Applying motion blur (direction: {direction}, length: {kernel_size}px)...")
    kernel = np.zeros((kernel_size, kernel_size), dtype=np.float32)
    
    if direction == 'horizontal':
        # 水平方向のカーネル
        kernel[int((kernel_size - 1) / 2), :] = np.ones(kernel_size, dtype=np.float32)
    elif direction == 'vertical':
        # 垂直方向のカーネル
        kernel[:, int((kernel_size - 1) / 2)] = np.ones(kernel_size, dtype=np.float32)
    else:
        raise ValueError("direction must be 'horizontal' or 'vertical'")
        
    kernel = kernel / kernel_size
    
    # フィルタを適用
    hairline = cv2.filter2D(noise.astype(np.float32), -1, kernel)

    # 3. 進行方向と垂直な方向の微細化処理（1pxライン生成・低周波カット）
    # 横ブラーのみだと垂直方向に低周波うねり（太い溝）が残るため、垂直差分で1px幅のエンボスラインを抽出
    if fine > 0.0:
        print(f"Applying fine single-pixel lines (fine: {fine})...")
        if direction == 'horizontal':
            diff = hairline[1:, :] - hairline[:-1, :]
            diff = np.vstack([diff, diff[-1:, :]])
        else:
            diff = hairline[:, 1:] - hairline[:, :-1]
            diff = np.hstack([diff, diff[:, -1:]])
            
        b_std = np.std(hairline)
        d_std = np.std(diff)
        b_norm = (hairline - np.mean(hairline)) / (b_std + 1e-6)
        d_norm = (diff - np.mean(diff)) / (d_std + 1e-6)
        pattern = (1.0 - fine) * b_norm + fine * d_norm
    else:
        pattern = hairline

    # 4. 乱数の範囲に対するレンジ・コントラスト調整
    min_v, max_v = brightness_range
    print(f"Adjusting brightness range ({int(min_v)} [0x{int(min_v):02x}] - {int(max_v)} [0x{int(max_v):02x}], intensity: {intensity})...")
    # 外れ値の影響を抑えるため 0.1%〜99.9% パーセンタイルで 0.0〜1.0 に正規化
    p_low, p_high = np.percentile(pattern, [0.1, 99.9])
    norm = np.clip((pattern - p_low) / (p_high - p_low + 1e-6), 0.0, 1.0)
    
    # intensity による乱数ノイズのコントラスト・振幅調整（中心 0.5 を基準に伸縮）
    if intensity != 1.0:
        norm = np.clip(0.5 + (norm - 0.5) * intensity, 0.0, 1.0)

    # 指定された明るさレンジにスケーリングしてベース画像を生成
    base = min_v + norm * (max_v - min_v)

    # 5. 溝の立体感（追加のエンボス効果: エッジ強調・彫りの深さ）
    if groove > 0:
        print(f"Applying 3D groove depth (groove: {groove})...")
        if direction == 'horizontal':
            # 水平ヘアライン: Y方向に溝の明暗エッジ（斜め上からの光）
            k_groove = np.array([
                [-0.5, -1.0, -0.5],
                [ 0.0,  0.0,  0.0],
                [ 0.5,  1.0,  0.5]
            ], dtype=np.float32) * 0.5
        else:
            # 垂直ヘアライン: X方向に溝の明暗エッジ
            k_groove = np.array([
                [-0.5, 0.0, 0.5],
                [-1.0, 0.0, 1.0],
                [-0.5, 0.0, 0.5]
            ], dtype=np.float32) * 0.5

        emboss = cv2.filter2D(base, -1, k_groove)
        adjusted = np.clip(base + emboss * groove, 0, 255).astype(np.uint8)
    else:
        adjusted = np.clip(base, 0, 255).astype(np.uint8)

    return adjusted

def main():
    parser = argparse.ArgumentParser(description="Generate hairline metal texture.")
    parser.add_argument('--width', type=int, default=1920, help="Width of the image")
    parser.add_argument('--height', type=int, default=1080, help="Height of the image")
    parser.add_argument('--direction', type=str, default='horizontal', choices=['horizontal', 'vertical'], help="Direction of the hairline")
    parser.add_argument('--length', '--hairline-length', dest='length', type=int, default=150,
                        help="Length of the hairline in pixels (default: 150)")
    parser.add_argument('--intensity', type=float, default=1.0,
                        help="Groove intensity/contrast multiplier of the hairline (default: 1.0)")
    parser.add_argument('--fine', '--fine-lines', dest='fine', type=float, default=1.0,
                        help="Ratio of single-pixel fine hairline lines (1.0: sharp 1px embossed lines without low-frequency thick swelling, 0.0: standard blurred noise, default: 1.0)")
    parser.add_argument('--groove', '--emboss', '--depth', dest='groove', type=float, default=0.0,
                        help="Additional 3D groove depth / emboss effect (default: 0.0, recommended 0.0-0.5 when fine=1.0)")
    parser.add_argument('--preset', type=str, default=None, choices=list(PRESETS.keys()), help="Color preset")
    parser.add_argument('--rgba', type=str, default=None, help="Comma-separated RGBA values (e.g. 255,215,0,255)")
    parser.add_argument('--color-intensity', type=float, default=None, help="Color blend intensity (0.0 - 1.0)")
    parser.add_argument('--out', type=str, default='hairline.png', help="Output file name")

    # ベーステクスチャの明るさ・レンジ
    parser.add_argument('--range', '--brightness-range', dest='brightness_range', type=str, default='0x90,0xdf',
                        help="Brightness range of base texture as 'min,max' (supports hex like '0x20,0x80' or decimal 0-255, default: '0x90,0xdf')")

    # ライティング関連オプション
    parser.add_argument('--light', action='store_true', help="Enable light reflection effect")
    parser.add_argument('--light-pos', type=str, default='0.5,0.5', help="Center position of light reflection (X,Y from 0.0 to 1.0, default: 0.5,0.5)")
    parser.add_argument('--light-color', type=str, default='255,255,255', help="Light source color (R,G,B from 0 to 255, default: 255,255,255)")
    parser.add_argument('--light-intensity', type=float, default=0.8, help="Light reflection intensity (default: 0.8)")
    parser.add_argument('--light-radius', type=float, default=0.35, help="Radius/size of light source relative to image (default: 0.35)")
    parser.add_argument('--light-spread', type=float, default=0.5, help="Softness/spread of light falloff (default: 0.5)")
    parser.add_argument('--light-stretch', type=float, default=2.5, help="Anisotropic stretch factor perpendicular to hairline (1.0 for circular, default: 2.5)")

    args = parser.parse_args()

    # 画像の端の方はブラーがうまくかからないことがあるため、少し大きめに生成して切り取る手法もあるが、
    # 今回は簡略化のためそのまま生成する
    
    # より品質を上げるために、余白を持たせて生成してから中央をクロップする
    pad = args.length
    gen_w = args.width + pad * 2
    gen_h = args.height + pad * 2

    # 明るさレンジの解析
    b_min, b_max = parse_brightness_range(args.brightness_range)
    texture = generate_hairline(gen_w, gen_h, args.direction, length=args.length, intensity=args.intensity, groove=args.groove, brightness_range=(b_min, b_max), fine=args.fine)
    
    # クロップ
    texture_cropped = texture[pad:pad+args.height, pad:pad+args.width]

    # 色付けのパラメータ決定
    r, g, b, a = 230, 240, 255, 255 # デフォルト: 軽いシルバー
    blend_intensity = 0.3

    if args.rgba:
        vals = [int(v.strip()) for v in args.rgba.split(',')]
        r, g, b = vals[0], vals[1], vals[2]
        if len(vals) >= 4:
            a = vals[3]
        blend_intensity = 0.8 # RGBA直接指定の場合は強めに反映する
    elif args.preset and args.preset in PRESETS:
        p = PRESETS[args.preset]
        r, g, b = p['r'], p['g'], p['b']
        blend_intensity = p['intensity']
        a = 255

    if args.color_intensity is not None:
        blend_intensity = args.color_intensity

    texture_color = colorize_texture(texture_cropped, r, g, b, alpha=a, intensity=blend_intensity)

    # 光の反射効果を適用
    use_light = args.light or any(arg.startswith('--light-') for arg in sys.argv[1:])
    if use_light:
        print("Applying light reflection effect...")
        pos_parts = [float(v.strip()) for v in args.light_pos.split(',')]
        light_pos = (pos_parts[0], pos_parts[1])

        col_parts = [int(v.strip()) for v in args.light_color.split(',')]
        light_color = (col_parts[0], col_parts[1], col_parts[2])

        texture_color = apply_light(
            texture_color,
            light_color=light_color,
            pos=light_pos,
            intensity=args.light_intensity,
            radius=args.light_radius,
            spread=args.light_spread,
            stretch=args.light_stretch,
            direction=args.direction
        )

    out_path = os.path.abspath(args.out)
    cv2.imwrite(out_path, texture_color)
    print(f"Texture successfully saved to {out_path}")

if __name__ == '__main__':
    main()
