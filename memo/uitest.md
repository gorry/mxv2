# 設定 UI の自動テスト（mxv2_uitest、2026-10-01）

`make test-ui`（`make test` からも走る）。設定 UI のダイアログの行き来を、本物のアプリで
確かめる。SDL の dummy ドライバで走るのでウィンドウは出ない。

## 作り

- mxv2 と同じソースを `MXV2_UITEST` 付きでもう 1 本ビルドする（CMake の `mxv2_uitest`。
  ALL から外してあり、test-ui が名指しでビルドする）。main.cpp は `ui.Build()` の直後に
  `uitest::AfterBuild()` を呼び、終了コードを `uitest::ExitCode()` にする（#ifdef の 3 か所だけ）。
- 部品の位置と名札は ImGui のテストエンジン用フック（`IMGUI_ENABLE_TEST_ENGINE`）で集める。
  ImGui も定義付きで別にビルドする（`imgui_uitest`。定義で構造体の形が変わるので PUBLIC で
  mxv2 側にも伝える）。
- キーは `SDL_PushEvent`（本物と同じ道: keybind・ESC → CloseDialog）。マウスは ImGui の
  io へ直に渡す（座標の変換を逆算しなくて済む。dummy ドライバでは ImGui の SDL バックエンドが
  グローバルなマウス位置を読まないので、上書きされない）。
- 状態は「開いているポップアップの並び」（OpenPopupStack）で見る。待つ手順は同じ並びが
  3 フレーム続いたら済み（往復の途中で一瞬だけ合うのを見逃さないため）。
- 設定は使い捨ての `-userdir`（`build/<TARGET>/uitest-userdir`、毎回 tools/uitest.ini から
  作り直す）。`-multi` で、手元で動いている mxv2 へ渡さない。

## 筋書き（56 手順）

1. F1 → [演奏] → [編集…] で PDX → 削除の確認を ESC → [追加…] のフォルダ選択 → ESC で PDX →
   ESC で設定 → ESC で何も無し
2. F1 → [設定…] で画面の遅れ → [閉じる] で設定 → もう一度 → ESC で設定 → ESC
3. F4 → [追加…] のフォルダ選択 → ESC でブックマーク → 削除の確認を [キャンセル] → もう一度 →
   ESC（入れ子だけ閉じる）→ ESC
4. F1 → [言語] を English → 設定ウィンドウが新しい題名で開き直る → ESC

わざと壊して捕まえることも確かめた: PDX を閉じたあと設定へ戻らない（16 手順目で落ちる）、
ブックマークの CloseTop が入れ子を先に閉じない（45 手順目で落ちる）。

## 踏んだ罠

- **ImGui は "###" で id を共有するウィンドウの名前を書き換えない**（題名の帯には毎フレーム
  渡された題名を描くが、`window->Name` は最初に開いたときのまま）。フォルダ選択の 3 種類は
  `###mxv2folder` を共有するので、名前で比べると「ブックマークのフォルダ選択が PDX の名前」に
  見える。比べるのは `window->ID == ImHashStr(title)`。
- **1.92.4 の BeginCombo は ItemInfo（名札）を出さない**。探している名札を `gWatchLabel` に
  置き、ItemAdd の中で `window->GetID(label)` と突き合わせて補う。
- 見出し（CollapsingHeader）は開け閉めを ImGui がウィンドウごとに覚えているので、2 度目は
  押すと畳んでしまう。`Click(..., onlyIfClosed=true)` は Opened の印を見て、開いていれば押さない。
- 見えていない部品は ItemInfo が来ない。いちばん上のポップアップ（と中の子ウィンドウ）を
  上端へ戻してから半画面ずつ下へスクロールして探す。

## 足すとき

- 手順は `BuildScenario()` に並べる。名札は `MsgLabel("Button.Xxx")` / `IdLabel(key, "##id")`
  （表示の言語に追従する）。題名は settingsui_internal.h の `k*Title` の番地で渡す（言語を
  替えると指す先が変わるので、使うたびに読む）。
- 失敗すると、その時点で開いているポップアップと、見えていた名札の一覧が出る。
