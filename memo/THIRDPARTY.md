# mxv2 が同梱している第三者のソフトウェアと素材

配布物に含まれるもののライセンス一覧。ライセンス全文はそれぞれの場所にある。

| 名前 | 版 | 場所 | ライセンス | 全文 |
|---|---|---|---|---|
| SDL2 | 2.32.10 | `third_party/SDL2-2.32.10/` | zlib | `third_party/SDL2-2.32.10/LICENSE.txt` |
| Dear ImGui | 1.92.4 | `third_party/imgui/` | MIT | `third_party/imgui/LICENSE.txt` |
| portable_mdx | - | `third_party/portable_mdx/` | `third_party/portable_mdx/readme.md` を参照 | 同左 |
| M PLUS 1p Regular | 2016 | `assets/MPLUS1p-Regular.ttf` | SIL Open Font License 1.1 | `assets/MPLUS1p-OFL.txt` |
| Get Ultimate Sound Amusement with G. | 2000 | `assets/mdx/ArctanX` | 個別 | `third_party/GUSA-CDg/ArctanX/readme.txt` |

いずれも無改変で置いてある。更新するときは差し替えるだけでよい。
場所はすべて `mxv2/` から見た相対パス。

## それぞれの採用理由

**SDL2 2.32.10** — Dear ImGui の SDL_Renderer バックエンドが `SDL_RenderGeometry`
(SDL 2.0.18 以降) を要求するため、portable_mdx 同梱の 2.0.7 から差し替えた。
CMake の `SDL2_ROOT` で別の SDL2 に切り替えられる。

**Dear ImGui 1.92.4** — 設定ウィンドウ (F1)。コアと SDL2 / SDL_Renderer
バックエンドだけを取り込んでいる。

**M PLUS 1p Regular** — 設定ウィンドウと、ファイラ・曲名の日本語表示。
JIS 第1+2水準を含み (8,676 グリフ / 1.68 MB)、Shift-JIS を CP932 で
変換したときに出る U+FF5E `～` や U+2015 `―` も持っている。
Reserved Font Name が設定されていないため、名前の制約なく再配布できる。
ファイラ側は stb_truetype (Dear ImGui 同梱) で焼いているので、
文字描画に OS のフォント API は使っていない。

`assets/font.ttf` を置くと、同梱フォントより優先してそちらが使われる。
それも無ければシステムの日本語フォント (Windows は meiryo など) を探す。

**portable_mdx** — 演奏モジュール (MXDRV / x68sound)。2026-08-18 に
`mxv2/third_party/portable_mdx/` へ移した（それまでは mxv2 の外に置いて
参照していた）。**参照するだけで一切改変していない**ので、更新はフォルダごと
差し替えればよい。CMake の `PORTABLE_MDX_DIR` で別の場所を指せる。
付属サンプル (`examples/simple_mdx_player`, `simple_mdx2wav`) も
そのままビルドしていて、土台の動作確認に使っている。

**Get Ultimate Sound Amusement with G.** — サンプルのmdxファイルとして、
独自に同梱許可を得ている。

## 同梱していないもの

`mxv/` は移植元の原典で、仕様の参照用。mxv2 の配布物には含めない。
