# SettingsUi のメンバ変数を、ダイアログごとに分割統治する計画（2026-09-30）

## いまの状態

settingsui.cpp は 11 本に小分けしてあるが、**状態は全部 SettingsUi のメンバ**として
1 つに集まったまま（settingsui.h 875 行、コンストラクタの初期化子が 100 行超）。

- ダイアログごとに `show*_` / `*OpenPending_` / `*ReturnTo*_` / `*Open_` / `*Close_` /
  一覧の選択行 / エラー文 … と同じ形のフラグが並び、名前の接頭辞（bm / pdx / fs / cast …）
  で見分けている。
- どのファイルがどれを触っているかは接頭辞でおおむね分かるが、**他のファイルの
  変数へ手を伸ばしている所**がある（下の「越境」）。これがグローバル変数に近い
  状態の正体で、直すときにどこまで影響するかが読めない。

### 越境（別ファイルの変数を触っている所）

| ファイル | 触っている他所の変数 | 理由 |
|---|---|---|
| bookmarks.cpp | folderOpenPending_ folderReturnToBookmarks_ folderTarget_ | [追加…] でフォルダ選択へ往復 |
| pdx.cpp | folderOpenPending_ folderReturnToPdx_ folderTarget_ | 同上 |
| folder.cpp | bmSelected_ pdxSelected_ | 選んだフォルダを一覧の選択位置へ挿す |
| settings.cpp | pdx* latency* updateNested_ | [編集…]/[設定…] の往復、入れ子の更新の知らせ |
| info.cpp | safPicking_ safRegrantRef_ safRegrantFromFiler_ | 起動時の警告の [許可を取り直す…] |
| menu.cpp | visible_ showAbout_ showCast_ showFolder_ showHelp_ bmOpenToggle_ quit* | メニューから各ダイアログを開く |
| update.cpp | visible_ updateNested_ | 設定ウィンドウの中に入れ子で開く |
| settingsui.cpp Build() | folder/pdx/latency の *ReturnTo* 一式 | 閉じたら元へ戻す連鎖 |

menu.cpp が各 `show*_` を立てるのは「開く」という正当な入口なので、越境というより
**開く API が無いから直接触っている**だけ。API を通せば済む。

## 目標の形

```
class SettingsUi {
  // 共通（このまま残す）
  ready_ / フォント / styleScale_ / touch* / dialogGrow_ / inputScale_ / relayout_ /
  vfs_ / songLoader_ / changedFields_ / openedModal_ / request_ / dragScroll_ / dragMoved_

  // ダイアログごとの状態。1 ファイル 1 struct。
  SettingsWindowState settings_;   // settingsui_settings.cpp（visible_ もここ）
  LatencyDialogState  latency_;    // settingsui_settings.cpp（→ 後で settingsui_latency.cpp へ）
  ColorsDialogState   colors_;     // settingsui_colors.cpp
  FolderDialogState   folder_;     // settingsui_folder.cpp
  FileSystemsState    fs_;         // settingsui_filesystems.cpp（addFs / handed / saf も）
  BookmarksState      bm_;         // settingsui_bookmarks.cpp
  PdxPathsState       pdx_;        // settingsui_pdx.cpp
  CastDialogState     cast_;       // settingsui_cast.cpp
  UpdateDialogState   update_;     // settingsui_update.cpp
  InfoDialogsState    info_;       // settingsui_info.cpp（about / help / startup）
  ContextMenuState    ctx_;        // settingsui_menu.cpp（quit の確認も）
};
```

- **struct は「データ + 自分だけで完結する小さな関数」**（Reset、CloseTop など）。
  Build 関数は今までどおり SettingsUi のメンバのまま（SyncModal・DialogFlags・
  placeCond・Msg など共通の道具にそのまま手が届く）。クラス化して SettingsUi への
  参照を持ち回る作りは、まずやらない（往復の連鎖があるので、今そこまで切ると
  かえって配線が増える）。
- struct はそれぞれ**自分のコンストラクタで初期化**する。SettingsUi のコンストラクタ
  からは該当の初期化子が消える（これが「明らかにグローバル」を解く見た目の効果）。
- 宣言は settingsui.h の中に nested struct として置く（ファイルは増やさない。
  private なので外からは見えない）。

## 同じ形のフラグをまとめる型（先に用意する）

接頭辞違いで何度も出てくる 2 つの形を小さな型にする。これでフラグの数がまず減る。

```
// 入れ子で開く確認ダイアログ（上書き・削除・終了・渡されたファイル・更新の知らせ）
//   ask / open / close の 3 つ組: 次のフレームで開く / いま開いている / ESC で閉じてほしい
struct NestedPopup {
  bool ask, open, close;
  NestedPopup() : ask(false), open(false), close(false) {}
  void Request() { ask = true; }
  // CloseDialog() 用。開いていれば閉じる印を付けて true。
  bool CloseTop() { if (!open) return false; close = true; return true; }
};
// 対象: overwrite / fsConfirm / bmRemove / bmToggle / pdxRemove / quit / handed / addFs / update
//（addFs は addFsOpen_/addFsShow_/addFsClose_、update は updateAsk_/updateOpen_/updateClose_ が同じ形）

// 設定ウィンドウから開き、閉じたら戻る往復（pdx / latency / folder の一部）
struct Handoff {
  bool openPending;      // 呼び出し元が閉じきったら開く
  bool returnToCaller;   // 閉じたら呼び出し元を開き直す
};
```

folder の戻り先は 3 択（settings / bookmarks / pdx）なので、`folderReturnToSettings_ /
Bookmarks_ / Pdx_` の bool 3 つは **enum 1 つ（`FolderReturn`）**にする。同時に 2 つ
立つことは無いので、意味も正確になる。

## 手順

各手順は「ビルド両方（Windows Debug / Android release）が通り、対象ダイアログを
開閉して同じ動きをする」ところまでで 1 区切り。**手順ごとにコミットできる粒度**に
してある（コミットはユーザー）。順番は、他所との絡みが少ないものから。

| # | 対象 | 動かすもの | 絡み |
|---|---|---|---|
| 0 | 下ごしらえ | `NestedPopup` / `Handoff` / `FolderReturn` を settingsui.h に足す（まだ使わない） | なし |
| 1 | cast_ | showCast_ castWasOpen_ castSelected_ castLastSize_ castRecenter_ castScrollY_ | menu.cpp が開く → `OpenCast()` を作って通す |
| 2 | update_ | update* 9 個（ask/open/close は NestedPopup に） | settings.cpp の updateNested_ 参照は `update_.nested` |
| 3 | info_ | showAbout_ aboutHeader_ aboutText_ showHelp_ helpRows_ showStartup_ startupLines_ | info.cpp が saf* を触るのは fs 側の API（`RegrantSaf(ref, fromFiler)`）にして越境を消す |
| 4 | pdx_ | showPdxPaths_ pdxOpenPending_ pdxReturnToSettings_（Handoff）pdxSelected_ pdxError_ pdxRemove（NestedPopup） | settings.cpp の [編集…] は `OpenPdxPathsFromSettings()`。folder.cpp の pdxSelected_ 参照は `pdx_.InsertAt()` のような API に |
| 5 | latency_ | showLatency_ latencyOpenPending_ latencyReturnToSettings_（Handoff）latencyTarget_ latencyDevice_ | ついでに BuildLatencyWindow を settingsui_latency.cpp へ切り出す（settings.cpp が 880 行で一番大きい） |
| 6 | bm_ | showBookmarks_ bmSelected_ bmError_ bmRemove/bmToggle（NestedPopup）bmToggleRef_ bmJumpPending_ bmJumpRef_ | folder.cpp の bmSelected_ 参照は pdx と同じ API に。menu.cpp の bmOpenToggle_ は `AskBookmarkToggle()` |
| 7 | colors_ | showColors_ skinNameBuf_ skinNameReset_ saveError_ saveErrorFresh_ overwrite（NestedPopup + overwriteName_） | OpenColors() が中を触るだけ |
| 8 | ctx_ | openContextMenu_ contextMenuOpen_ closeContextMenu_ ctxPage_ ctxSwitchTo_ ctxAnim* ctxPageSize_ ctxChildWarm_ ctxNoShortcut_ quit（NestedPopup） | menu.cpp が他を開くのは API 経由に揃える（Open*） |
| 9 | folder_ | showFolder_ folderTarget_ FolderReturn folderOpenPending_ folderDir_ folderSelected_ folderEntries_ folderLister_ folderLoading_ folderTicks_ folderHasPrev_ folderPrev* folderError_ requestedFolder_ folderPathBuf_ | bookmarks/pdx からの往復は `OpenFolderFor(target, returnTo)` 1 本に。Build() の戻り連鎖もここで整理 |
| 10 | fs_ | showFileSystems_ fsSelected_ fsError_ fsRemoveRevoke_ fsConfirm（NestedPopup）addFs（NestedPopup + addFsPathBuf_ addFsError_）pendingBrowse_ browseStart_ handed（NestedPopup + handedUri_ handedName_ handedRef_）safHandedUri_ safPicking_ safRegrantRef_ safRegrantFromFiler_ | 一番大きい。handed / saf / addFs を struct の中でさらに小分け（`fs_.handed` `fs_.saf` `fs_.addFs`） |
| 11 | settings_ | visible_ settingsWasVisible_ settingsClosed_ pendingZoom_ zoomApplyAtMs_ zoomStepRequest_ skins_ pendingSkin_ pendingSampleRate_ orientEnabled_ orientation_ locales_ pendingLocale_ localeApplyPending_ localeChanged_ | `visible_` は menu/update/settings から読まれるので `settingsVisible()` を通す。pending*（main への出力）は今の public API のまま、置き場だけ移す |
| 12 | 仕上げ | `busy()` と `CloseDialog()` を、各 struct の `IsOpen()` / `CloseTop()` を並べる形に書き直す。順番（入れ子の内側から）はここ 1 か所に残す | 動きは変えない |

### 手順の中の作法

- 1 手順の中では**名前の付け替えだけ**をする（`showCast_` → `cast_.show`）。動きを
  変えたくなっても（例: Handoff の共通化で見つかる重複）別の手順に回す。
- 各 struct の中のフラグの名前は接頭辞を落とす（`cast_.wasOpen`、`pdx_.remove.open`）。
- struct のコンストラクタで全部初期化する。SettingsUi のコンストラクタの初期化子は
  手順が進むたびに短くなる（最後は共通のものだけ）。
- 越境しているアクセスは**必ず SettingsUi の private メンバ関数（API）に置き換える**。
  struct の中身を別ファイルから直接触るのは禁止（これを守らないと元に戻る）。

### 確かめかた（各手順で）

- Windows: `make build-windows`、Android: `make TARGET=android BUILD=release build-android`。
- 動き: 対象ダイアログを「開く → 入れ子の確認を出す → ESC で 1 段ずつ閉じる」。
  往復のあるもの（pdx / latency / bookmarks の [追加…]）は往って戻るまで。
  ESC の順番（CloseDialog）と busy() は手順 12 まで手を入れないので、途中の手順では
  名前の付け替えが漏れていればコンパイルで分かる。
- r77（480x640・指で操作）で × と題名の見た目が変わっていないか 1 度は見る
  （BeginDialog は共通側なので影響しないはずだが、念のため）。

## 期待する効果

- settingsui.h: メンバの並びが「共通」＋「ダイアログ 11 個」になり、どの状態が
  どのファイルのものかが宣言から読める。
- SettingsUi(): 初期化子が 100 行超 → 共通の 20 行ほど。
- フラグの数: NestedPopup 9 組で 27 個 → 9 個、folder の戻り先 3 個 → 1 個。
- 越境が API 経由になるので、ダイアログを 1 つ直すときに見る範囲が 1 ファイル＋
  API の呼び出し元だけになる。

## やらないこと（今回）

- ダイアログを別クラスにして Build を移す（上の理由。struct 化が済んでからなら、
  往復の少ない cast / update / info はクラスにしても配線が少ない。その時に判断）。
- SyncModal / openedModal_ の作り直し。モーダルの掛け替えの制約（1 度に 1 つ・
  閉じきってから開く）は今の設計の前提なのでそのまま。
- 文言・見た目の変更。

## 実施の記録（2026-09-30、手順 0〜12 まで完了）

- 全手順を当てた。Windows Debug / Android release ともビルド通過。動きは Windows
  （ud2 の検証用 userdir、指で操作する ON）で [mxv2 の設定]→[PDX の探索先]→[追加…]→
  フォルダ選択→ESC→[PDX の探索先]→×→[mxv2 の設定] の往復、[画面の遅れ] の往復、
  F2/F3/F4/F9 の開閉、ブックマークの削除確認を ESC で 1 段ずつ、右クリックメニューを
  ESC で閉じる、を確認。r77 でも設定→PDX→フォルダ選択→戻る、を確認。
- 計画からの差分:
  - `requestedFolder_`（kRequestSetFolder の行き先）と `pendingSkin_` は、ブックマーク／
    配色設定からも積む「main への要求」なので struct に入れず共通側に残した。
  - `folderReturnToSettings_` はどこも立てていなかった（死んでいた）ので、FolderReturn には
    Settings を入れていない。
  - `SettingsWindowState::orientation` の既定値は SettingsUi のコンストラクタ本体で入れる
    （settingsui.h では Screen が前方宣言だけで `Screen::kLandscape` が見えない）。
  - MsToFrames / FramesToMs は settingsui_internal.h の宣言 + settingsui_settings.cpp の定義に
    した（settingsui_latency.cpp と 2 か所で使う）。
- 手順ごとのコミットはできていない（作業ツリーに全部まとまっている。コミットはユーザー）。
- 手順に使ったスクリプトは scratchpad の step01.py〜step12.py（rf.py が共通）。使い捨て。

## ダイアログを別クラスにする価値の見積もり（2026-09-30）

struct 化が済んだ状態で、Build 関数まで各ダイアログのクラスへ移す（`class FolderDialog { Build(); ... }`）と
どうなるかを、各ファイルが SettingsUi の何に手を伸ばしているかで数えた（scratchpad の dep.py）。

### いま各ダイアログが SettingsUi から借りているもの

| 種類 | 中身 | 使っている数 |
|---|---|---|
| 共通の道具（全ダイアログ） | SyncModal / BeginDialog / DialogFlags / DialogSize / placeCond / CenterNextWindow、`openedModal_`、`dragScroll_` `dragMoved_`、`touchUi_` | 11 ファイル全部 |
| main への出力 | `changedFields_`（10 ファイル）、`request_` + `requestedFolder_`（5）、`pendingSkin_`（2） | ほぼ全部 |
| 環境 | `vfs_`（bookmarks 21・fs 45・folder 16・pdx 4）、`songLoader_`、`paths_` | 5 ファイル |
| 他のダイアログへの依頼 | OpenFolderFor / SetSettingsVisible / OpenFileSystemsFor / StartSafRegrant / InsertBookmark / InsertPdxPath / OpenLatencyWindow / OpenPdxPathsFromSettings、menu からの Open* 9 本 | 8 ファイル・約 25 か所 |
| 往復の制御 | Build() の handoff / returnTo / jumpPending の配線（約 70 行） | settingsui.cpp に集中 |

クラスにするには、上の 1〜3 段目を **`DialogHost`（SettingsUi への参照か、道具と出力をまとめた context）** として
全クラスへ渡し、4 段目を host のメソッドとして呼び直すことになる。5 段目はどのみち SettingsUi に残る
（ダイアログ同士が直接知り合わない作りにするなら、むしろ残すべきもの）。

### 得られるもの

- settingsui.h（今 1030 行）を「SettingsUi 本体 + ダイアログごとのヘッダ 11 本」に割れる。
- 各ダイアログの補助関数（fs: 12 本、folder: 7 本、menu: 5 本、colors: 3 本）が、そのクラスの private に
  なる。今は SettingsUi の private として全ファイルから見えている。
- 新しいダイアログを足すときの型が決まる（state + Build + host の 3 点セット）。

### 掛かるもの

- 11 クラス + host の型。呼び出しの書き換えは `placeCond()` → `host.placeCond()` の類が数百か所で、
  今回の struct 化と同じくらいの差分（±1000 行）。動作は変えないので危険は小さいが、手間は同じだけ掛かる。
- `changedFields_` や `request_` は「各ダイアログが host に積む」形になるだけで、構造は変わらない。
- ImGui のポップアップ ID は開いた場所の ID スタックで決まるので、`update_.nested`（設定ウィンドウの
  中で組む更新の知らせ）のような「別のダイアログの中で組む」ものは、クラスを分けても呼び出し順は
  SettingsUi が持ち続ける。

### 見立て

- **効果が出るのは大きい 4 つだけ**（filesystems 614 行 / settings 754 行 / menu 455 行 / folder 346 行）。
  cast / update / latency / info / pdx（150〜280 行、補助関数 0〜2 本）は、クラスにしても host 経由の
  呼び出しが増えるぶん、むしろ読みにくくなる。
- 「グローバルに近い」問題は struct 化 + API 経由で解けているので、クラス化で新たに解ける問題は
  **ヘッダの長さ**と**補助関数の可視範囲**の 2 つ。どちらも困っている度合いは小さい。

### もしやるなら（軽い順）

1. **ヘッダだけ割る**（最小）: nested struct を namespace スコープの `struct FolderDialogState` に出し、
   `settingsui_folder.h` などへ移す。settingsui.h はそれらを include して持つだけ。Build 関数は
   SettingsUi のまま。差分は小さく（宣言の移動だけ）、ヘッダの見通しは得られる。
2. **大きい 4 つだけクラス化**: fs / settings / menu / folder を `DialogHost` 経由のクラスに。残り 7 つは
   struct のまま。host の型はこの 4 つのために作る。
3. **全部クラス化**: 上の見積もりどおり。

推奨は **1 か「やらない」**。2・3 は、次にダイアログを大きく作り替える予定（例: ファイルシステムの設定を
分割する）が出てきたときに、そのダイアログだけ手を付けるのがよい。

## 第 2 段: ダイアログを別クラスに（2026-09-30、実施済み）

ユーザーの目的は「人間がメソッドやメンバを見たとき、個々のダイアログの private か
SettingsUi のものかを手早く分かる」こと（例: 配色設定の Rebuild が SettingsUi の
private に並んでいた）。SettingsUi の private 関数 60 本のうち共通の道具は 5 本だけで、
45 本は自分のファイルからしか呼ばれていなかった。

### 形

- 1 ダイアログ 1 クラス、1 ヘッダ（`settingsui_<name>.h`）。状態（末尾 `_`）・Build・
  補助関数・CloseTop はそのクラスの中。ヘッダを開けば持ち物が全部そこにある。
  | クラス | ヘッダ | 中身 |
  |---|---|---|
  | SettingsWindow | settingsui_settings.h | [mxv2 の設定]、スキン一覧、言語、倍率の適用待ち |
  | LatencyDialog | settingsui_latency.h | [画面の遅れ]/[遅延時間]（enum Target もここ） |
  | ColorsDialog | settingsui_colors.h | [配色設定]、SaveAs / Rebuild / 上書き確認 |
  | FolderDialog | settingsui_folder.h | フォルダ選択（enum Target/Return、DirLister の所有） |
  | FileSystemsDialog | settingsui_filesystems.h | [ファイルシステムの設定]・SAF・渡されたファイル・追加 |
  | BookmarksDialog | settingsui_bookmarks.h | [ブックマークの設定]・Shift+M・ジャンプ |
  | PdxPathsDialog | settingsui_pdx.h | [PDX の探索先] |
  | ContextMenu | settingsui_menu.h | コンテキストメニュー（enum Page）・終了の確認 |
  | InfoDialogs | settingsui_info.h | バージョン情報・操作方法・起動時の警告（StartupWarning） |
  | UpdateDialog | settingsui_update.h | 更新チェックの知らせ・[通信] |
  | CastDialog | settingsui_cast.h | [キャスト] |
  共通の型 NestedPopup / Handoff は settingsui_dialog.h。
- SettingsUi に残したもの: 外向きの API（Open* などは各クラスの Open() へ委ねる薄い
  wrapper）、共通の道具（SyncModal / BeginDialog / DialogFlags / DialogSize / placeCond /
  ドラッグスクロール）、main への出力（changedFields_ / request_ / requestedFolder_ /
  pendingSkin_）、環境（vfs_ / songLoader_ / paths_）、**Build() の往復の配線**、
  CanBookmark / FindBookmark / OpenBookmarkList（複数のダイアログから使う）。
- 各クラスは `Build(SettingsUi &ui, …)` で SettingsUi への参照を受け、道具と出力は
  `ui.placeCond()` `ui.changedFields_ |= …` で使う。SettingsUi は 11 クラスを friend にし、
  各クラスも `friend class SettingsUi` にしている（往復の配線で中を触るため）。
  ui を使わない関数（Title / Insert / SelectEntry / Rebuild / MenuItem / PageItem など）は
  引数を持たない。
- 他のダイアログへの依頼は `ui.folder_.OpenFor(...)` `ui.fs_.StartSafRegrant(ui, ...)` のように
  相手のクラスの public を呼ぶ（SettingsUi の中継は Open* の wrapper だけ）。

### 数字

settingsui.h: 1030 行 → 533 行。ヘッダ全体（12 本）で 1678 行。SettingsUi の private
関数は 60 本 → 共通の道具と往復まわりの十数本。

### 進めかた（scratchpad の cls.py、使い捨て）

1 ダイアログずつ: settingsui.h から struct と関数宣言を取り除き、ヘッダを書き、
機械的に (1) `SettingsUi::Old(` → `Cls::New(SettingsUi &ui, ` (2) 自分のファイル内の呼び出し
(3) `own_.field` → `field_` (4) 残りの SettingsUi メンバに `ui.` (5) 他のファイルの呼び出しを
`own_.New(*this, …)` / `ui.own_.New(ui, …)` に (6) ui を使わない関数から引数を外す、を当て、
ビルドして直す。踏んだ罠: 型名（ImVec2）や入れ子 struct のコンストラクタが「関数」に
見えて `ui.` が付く／旧名と新名が同じ関数の引数無し呼び出しが 2 度書き換わる／先に足した
wrapper の宣言まで書き換わる／bash の heredoc がバックスラッシュを壊す（`'\0'` が NUL に）。

### 確認

Windows Debug / Android release ビルド通過。動きは Windows（ud2）で設定→PDX→フォルダ選択→
ESC→×、F2/F3/F4/F9、削除確認の ESC、右クリックメニュー、Pixel 7a で設定→PDX→フォルダ→戻る、
F3、メニューを確認（このあとの節に結果）。

## 第 3 段の見積もり: 各ダイアログの共通部を括り出す（2026-09-30）

第 2 段の 11 クラスを並べて、各 Build() の中で**同じ形で繰り返している部分**を数えた。
「インターフェース（抽象基底）」より、**定型を関数に括り出す**ほうが効く、という見立て。

### 繰り返している定型（3 つ）

1. **モーダルの開き方**（9 か所: settings / colors / pdx / bookmarks / folder / filesystems /
   latency / info×2、cast は幅の決め方が独自）
   ```
   if (!ui.SyncModal(title, &show_)) return;
   CenterNextWindow(ui.placeCond());
   ImGui::SetNextWindowSize(ui.DialogSize(w, h), ui.placeCond());
   if (!ui.BeginDialog(title, &show_, ui.DialogFlags())) return;
   ... 中身 ...
   DragToScroll(&ui.dragScroll_, &ui.dragMoved_, ...);   // 7 か所
   ImGui::EndPopup();
   ```
   違うのは title / show_ / 大きさ / DragToScroll の引数 / 追加フラグ（latency は
   AlwaysAutoResize、置き場所もファイラー側）だけ。

2. **入れ子の確認（NestedPopup）の開閉**（7 か所: bookmarks×2 / colors / filesystems×3 /
   menu の quit / update）。**7 か所が字句まで同じ**:
   ```
   if (p.ask) { p.ask = false; ImGui::OpenPopup(title); }
   p.open = ImGui::IsPopupOpen(title);
   if (!p.open) { p.close = false; return; }
   CenterNextWindow(ui.placeCond());
   if (!ImGui::BeginPopupModal(title, NULL, ui.DialogFlags() | AlwaysAutoResize)) return;
   ... 中身 ...
   if (ImGui::Button(Cancel) || p.close) { p.close = false; ImGui::CloseCurrentPopup(); }
   ImGui::EndPopup();
   ```

3. **各クラスの `show_` / `isOpen()` / `CloseTop()`**（11 クラスで同じ 3 行）。

### 「インターフェース」で括るとどうなるか

`class Dialog { virtual void Build(SettingsUi&) = 0; virtual bool CloseTop() = 0; ... }` を
基底にして 11 クラスが継承する形。得られるのは SettingsUi::Build の呼び出し列と
CloseDialog() の 11 連結を `for (Dialog *d : dialogs_)` に畳めること。ただし:
- Build の引数がクラスごとに違う（settings/draw/player/filer/screen の組み合わせが 5 通り）。
  揃えるには全部渡す「BuildContext」を作ることになり、使わない引数が並ぶ。
- **順番が意味を持つ**（メニュー→about→cast→colors→往復の配線→folder→…→settings 本体、
  入れ子の update は設定ウィンドウの中）。配列で回すと順番が配列の並びに隠れて、今の
  Build() の「なぜこの順か」のコメントが付けにくい。
- CloseDialog の順番も同じ（内側の入れ子から）。
- 仮想関数にする実益（実行時に入れ替える相手）が無い。
→ 抽象基底は**畳める行が少ない割に、順番の意図を隠す**ので勧めない。

### 勧める形: 定型 1・2 を関数に、3 を小さな基底に

- **(A) 入れ子の確認 → `NestedPopup` に開閉を持たせる**（settingsui_dialog.h）
  ```
  struct NestedPopup {
    bool ask, open, close;
    // 開く・閉じるの定型。true を返したら中身を組んで EndPopup() すること。
    bool Begin(SettingsUi &ui, const char *title);   // ask→OpenPopup、open の更新、BeginPopupModal
    // [キャンセル] と ESC を 1 か所で。押されたら CloseCurrentPopup して true。
    bool CancelButton(const char *label);
  };
  ```
  7 か所 × 約 12 行 → 各 2 行。ImGui に依るので実装は .cpp（settingsui_dialog.cpp）。
- **(B) モーダルの開き方 → SettingsUi の 1 関数に**
  ```
  // SyncModal + 置き場所 + 大きさ + BeginDialog をまとめる。false なら何もせず return。
  bool BeginModal(const char *title, bool *show, float w, float h,
                  ImGuiWindowFlags extra = 0, Placement place = kCenter);
  ```
  9 か所 × 4〜5 行 → 各 1 行。latency の「ファイラー側」は Placement で、cast の幅の
  決め方だけは今のまま。
- **(C) `show_` / `isOpen()` / `CloseTop()` → 小さな基底 `ModalDialog`**
  ```
  class ModalDialog {          // 仮想関数は持たない。共通の 3 行を置くだけ
  public:
    bool isOpen() const { return show_; }
    bool CloseTop() { if (!show_) return false; show_ = false; return true; }
  protected:
    bool show_ = false;
  };
  ```
  入れ子を持つクラス（bm / pdx / colors / fs）は CloseTop を上書きして先に入れ子を閉じる
  （非仮想で隠す。SettingsUi は具象型で持っているので問題ない）。update / info / ctx は
  `show_` 1 つで表せないので継承しない。

### 見込み

- 差分は合計 −150 行ほど。危険は小さい（字句が同じ所を関数に置き換えるだけ）。
- 新しいダイアログを足すときの型が「ModalDialog を継承、Build の頭は BeginModal 1 行、
  確認は NestedPopup::Begin / CancelButton」に決まる。
- (A) が一番効く（7 か所の完全な重複が消える）。(B) は読みやすさ、(C) は型の統一。

### やらないこと

- 抽象基底 + 配列で Build/CloseDialog を回す（上の理由）。
- Build の引数を BuildContext にまとめる（使わない引数が並ぶ。順番の問題も解けない）。

### 第 3 段の実施（2026-09-30）

(A)(B)(C) を全部当てた。差分 20 ファイル、+150 / −316 行。
- (A) `NestedPopup::Begin(ui, title)` / `CancelButton(label)` / `TakeClose()`（settingsui_dialog.cpp）。
  7 か所の入れ子（bm の削除・Shift+M、colors の上書き、fs の削除・追加・渡されたファイル、
  menu の終了、update）が「Begin で開く → 中身 → CancelButton」の形に。開く前に支度がある
  もの（Shift+M で控えられない場所、渡されたファイルが許可済みで尋ねない）は、支度の中で
  `ask = false` にして Begin に開かせない。update は [閉じる] とまとめるので TakeClose。
  NestedPopup は SettingsUi の friend（placeCond / DialogFlags を使う）。
- (B) `SettingsUi::BeginModal(title, &show, w, h, extra)`。9 か所（settings / colors / pdx /
  bookmarks / folder / filesystems / info×3）が 1 行に。h=0 なら AlwaysAutoResize を足す。
  latency（ファイラー側に置く）と cast（幅を中身で決める）は SyncModal + 自前の配置のまま。
- (C) `ModalDialog`（show_ / isOpen / CloseTop、仮想関数なし）を cast / latency / folder /
  pdx / bookmarks / colors / filesystems が継承。入れ子を持つ 4 つは CloseTop を隠して先に
  入れ子を閉じる。settings（visible_ の意味が違う）/ update / info / ctx は継承しない。
- 確認: Windows Debug / Android release ビルド通過。Windows（ud2）で設定→PDX→フォルダ→ESC→×、
  [画面の遅れ] の往復、F4 の削除確認（ESC で 1 段ずつ）、F2、終了の確認、F12。Pixel 7a で F4 の
  削除確認、Shift+M の確認、設定→PDX→フォルダ→戻る。ログに例外なし。

### 第 4 段: 片方向の friend にする（2026-10-01）

第 3 段までは、SettingsUi が 11 クラスを friend にし、各クラスも `friend class SettingsUi` に
していた（相互 friend）。後者をすべて外し、FileSystemsDialog → InfoDialogs の friend も外した。
残る friend は「ダイアログ → SettingsUi」の片方向だけ（共通の道具と main への出力を使うため）。
SettingsUi もダイアログ同士も、相手の中へは public の関数でしか触らない。

コンパイラに越境を列挙させたら、settingsui.cpp / settingsui.h / settingsui_filesystems.cpp の
3 ファイルだけだった。置き換え:
- 往復: `Handoff::OpenWhenCallerClosed(&show_, callerTitle)` / `TakeReturn(show, title, blocked)`
  （settingsui_dialog.cpp）。pdx / latency は `PollOpen()` / `TakeReturn(...)`。folder は
  `PollOpen(ui, settings, filer)`（開く位置の計算ごと移した）/ `TakeReturn()` / `BusyFor(caller)`。
  戻ってきたときの `show_ = true` は `ModalDialog::Reopen()`（中身は保つ。Open は選択を戻す）。
- bm: `PollJump(ui, settings)`。JumpToRef は private へ。
- settings: `WatchClosed()` / `ApplyPendingZoom(screen)` / `ApplyPendingLocale(ui)` / `SetLocales()`。
- update: `SetState()` / `TakeCheckNow()` / `Show(result)`。`anyDialogOpen` は `update_.isOpen()`。
- info: `MarkRegranted(mountRef, text)`（起動時の警告の行を書き換える）。
- 外から使われなくなった FolderDialog::Title / SetDir も private へ。

Build() の中の呼び出し順は変えていない（条件式をそのまま関数へ移しただけ）。
Windows Debug / Android release ビルド通過。
