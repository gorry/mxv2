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
