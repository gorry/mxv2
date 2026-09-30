# ピクチャー・イン・ピクチャー（Android）の計画

YouTube や Google マップのように、ホームへ戻ったときなどに mxv2 を小窓で
出し続ける。**2026-09-25 に実装し、Pixel 7a（API 37）で確認済み。**
末尾の「実装と確認（2026-09-25）」も見ること。

**小窓に出すのは「通知の mxv2」相当**（曲名・状態・時刻と、前の曲 /
一時停止・再開 / 次の曲の操作）。スキンで描いたプレーヤーの画面は出さない
（小窓では文字も鍵盤も判別できず、mxv2 では意味をなさない。ユーザーの判断）。

## 目標と範囲

- 演奏中にホームへ戻ると小窓になり、曲名・状態の行（「演奏中」＋ CONT /
  REPEAT など）・経過時間 / 演奏時間・進み具合のバーが出る。
- 小窓のボタンで前の曲・一時停止/再開・次の曲を操作できる。
- 小窓を広げると元の画面に戻る。小窓を閉じても演奏は続く（今の
  バックグラウンド演奏と同じ。止めるのは通知から）。
- 対象は Android 8.0（API 26）以降で、`FEATURE_PICTURE_IN_PICTURE` がある
  端末。minSdk 21 はそのまま。それ以外では何もしない。
- デスクトップ版は対象外。

## 方針: 小窓の中身は Java の View で作り、SDL の画面は止める

- 表示に要るもの（曲名・状態の行・演奏中か・位置・長さ）は、通知のために
  **すでに `PlaybackBridge` が持っている**（`snapshot()`）。ネイティブから
  新しく渡すものは無い。
- 小窓に入るとき、SDL の `SurfaceView` を隠し（`View.GONE`）、代わりに
  小窓用の View を `SDLActivity.mLayout` に重ねて見せる。
- **面を隠すと SDL は `surfaceDestroyed` から PAUSED に移る**
  （`SDLSurface.surfaceDestroyed` → `handleNativeState`）ので、ネイティブには
  `SDL_APP_WILLENTERBACKGROUND` が届き、mxv2 は**今のバックグラウンドの経路**
  （描かない・演奏と曲送りと通知の更新は続ける）にそのまま入る。
  出るときは面を戻す → `DIDENTERFOREGROUND` → `ForceRedrawAll` で描き直す。
  **ネイティブ側はほぼ手を入れずに済む見込み。**
- これで「小窓の大きさでキャンバスを作り直す・横向き用スキンへ切り替わる・
  行の高さを詰め直す」という問題（下調べの 2）が起きない。念のため
  ネイティブにも「PiP 中はスキンの向きの切り替えと `SyncCanvasToWindow` を
  しない」ガードを入れる（`PipBridge.inPip()` を見るだけ）。

### 小窓の中身

```
+----------------------------------+
| ARCTAN-X  Field Theme            |  曲名（1 行。収まらなければスクロール）
| 演奏中  CONT                      |  状態の行（通知と同じ文字列）
| 01:23 / 03:45                    |  経過 / 演奏時間
| [==========------------------]  |  進み具合
+----------------------------------+
```

- **曲名が収まらないときはスクロールさせる**（1 行のまま横に送る）。
  動きは本体の画面下の曲名欄（`drawscreen.cpp` の `ScrollOffsetAt`）と揃える:
  先頭で 3 秒止まる → 1 秒に「字の高さ × 40/24」の速さで末尾まで送る →
  末尾で 3 秒止まる → 先頭へ戻る、の繰り返し。2px 以下のはみ出しは送らない。
  TextView のマーキー（`ellipsize="marquee"`）は使わない——速さと止まる
  長さを決められず、つなぎ目で回り込む動きも本体と違い、選択状態
  （`setSelected(true)`）が保たれていないと止まるため。自前の View で
  `Canvas.drawText` の x をずらして描き、クリップする。
  **[ファイラー] の「曲名のスクロール」は見ない**（本体でもこの設定は
  ファイラーだけのもので、画面下の曲名欄には効かない。小窓もそれに倣い、
  収まらなければ常にスクロールする）。
- 縦横比は 2:1 前後の固定（`setAspectRatio`。許されるのは 1:2.39〜2.39:1）。
- 字は同梱の M PLUS 1p（展開済みのフォントを `Typeface.createFromFile`）。
  色は暗い地に明るい字の固定（スキンの配色には追従しない）。
- 時刻は 0.5 秒ごとに描き直す。`PlaybackBridge` の位置は「状態が変わった
  ときの値」なので（位置だけの変化では送られない）、`update()` で受けた時刻を
  覚えておき、演奏中なら経過ぶんを足して進める（通知の PlaybackState と
  同じ考え方）。シークしたときに位置が送り直されているかは確かめる。
- 小窓の中はタッチがシステムの操作パネルに取られて View には届かないので、
  **操作は PiP の `RemoteAction`** で出す: 前の曲 / 一時停止・再開 / 次の曲。
  PendingIntent は `PlaybackService` の `ACTION_PREV/PLAY/PAUSE/NEXT` を
  そのまま使う（通知と同じ経路で `PlaybackBridge` の要求に積まれる）。
  演奏中かどうかでボタンを差し替えるので、状態が変わったら
  `setPictureInPictureParams` を出し直す。
- 文言（ボタンの説明など）は message.ini の `[Notify]` から渡されたものを使う。

### 入り方

- [mxv2 の設定] の [画面] に「ホームへ戻ったら小窓で表示」:
  しない / **演奏中だけ（既定）** / 常に。ini は `[Screen] Pip=0|1|2`。
  PiP の無い端末では項目を出さない。ネイティブから Java へは
  `PipBridge.setMode(int)` で伝える。
- API 31 以降は `setAutoEnterEnabled`（ホームへ戻る動きと一緒に小窓になる）。
  モードと演奏状態から有効/無効を決めて、そのつど params を出し直す。
  このとき **面を隠すのは `onPause` で `isInPictureInPictureMode()` が真の
  とき**（`onPictureInPictureModeChanged` より先に小さなウィンドウで 1 回描かれて
  しまうのを避ける）。
- API 26〜30 は `onUserLeaveHint()` で面を隠してから
  `enterPictureInPictureMode()`。
- SAF のフォルダ選択など、自分で別の画面を開いたときも `onUserLeaveHint` が
  呼ばれるので、そのときは入らない（`SafBridge` が選択画面を開く前に印を立てる）。
- メニューに「小窓で表示」を足す（手動で入る。設定が「しない」でも使える）。
- 曲が無い（通知も出ていない）ときは入らない。「常に」でも、表示するものが
  無ければ入らない。

## 構成

- Java（新規）: `PipBridge.java` … モード・入る判断・params の組み立て・
  面の出し入れ。`PipView.java` … 小窓の中身（曲名・状態・時刻・バー）。
- Java（変更）: `MainActivity`（`onUserLeaveHint` / `onPause` /
  `onPictureInPictureModeChanged` から `PipBridge` へ）、
  `PlaybackBridge`（受けた時刻を記録し、変化を `PipBridge` にも知らせる）、
  `SafBridge`（選択画面の印）。
- Manifest: `MainActivity` に `android:supportsPictureInPicture="true"`、
  `android:resizeableActivity="true"`。`configChanges` はすでに足りている。
- ネイティブ: `src/pip.{cpp,h}`（モードを渡す・メニューから入る・PiP 中か
  を読む）。`main.cpp` に上のガード。設定は `settings.*` と
  `settingsui_settings.cpp`、メニューは `settingsui_menu.cpp`。
- 文言: 両ロケールの message.ini（`[Settings]` と `[Menu]`）。
- ドキュメント: README の Android の節、経緯は `memo/android.md`。

## 進め方

1. Manifest・`PipBridge`・`PipView` と、メニューから入る導線。
   実機で、面を隠したときにネイティブが今のバックグラウンドの経路に入ること、
   戻ったときに画面が崩れないこと（`ForceRedrawAll` で直るか）を見る。
2. 操作ボタン（RemoteAction）と時刻の進め方。
3. 自動で入る仕組みと設定の項目。
4. 実機での確認: Pixel 7a（Android 14）、タブレット（Android 15）、
   XS17（Android 8.1。PiP があるか）、古い 32bit 機（Android 8.0）。
   手掛かりは `adb shell dumpsys activity activities`（PiP のタスク）、
   `adb shell input keyevent KEYCODE_HOME`、logcat の SDL の
   `surfaceDestroyed()` / `nativePause`。

## 下調べで分かったこと（2026-09-25）

1. SDLActivity は API 24 以降、`onPause` ではなく `onStop` でネイティブを
   止める。**面を隠さずに PiP へ入ると、SDL は前面のまま**描き続ける
   （フォーカスを失ったこと・ウィンドウの大きさが変わったことだけが届く）。
2. そのまま小さなウィンドウで描かせると、`SIZE_CHANGED` → `SyncCanvasToWindow` で
   キャンバスが作り直され、`Screen::orientation()`（出力の縦横比で決まる）が
   横になって**横向き用スキンへ切り替わる**。`kTouchRowsMin` による行の
   詰め直しも走る。→ 面を隠す方針にした理由。
3. `configChanges` には `screenSize|smallestScreenSize|screenLayout|orientation`
   が入っているので、PiP の出入りで Activity は作り直されない。

## 気をつけること

- 面を隠す／戻すことで、GL の面が作り直される。今の
  `SDL_RENDER_TARGETS_RESET` / `DEVICE_RESET` と `ForceRedrawAll` の経路で
  足りるはずだが、PiP から戻った直後の画面は必ず目で見る。
- 小窓から戻るとき、ウィンドウがまだ小さいうちに面が戻ると 2. と同じことが起きうる。
  面を戻すのは `onPictureInPictureModeChanged(false)` のあと、ウィンドウの大きさが
  元に戻ってから（`onConfigurationChanged` を待つ）にする。
- `launchMode="singleInstance"` のまま、ランチャーのアイコンを押したときに
  小窓が元の大きさに戻るかを確かめる。
- 小窓を閉じたら演奏も止めるかは、使ってみてから決める（今は止めない）。

## 実装と確認（2026-09-25）

計画からの変更点:

- 面を隠すのは `PipBridge.onPause`（`isInPictureInPictureMode()` が真のとき）と
  `onPictureInPictureModeChanged(true)` の早いほう。Pixel 7a では `onPause` →
  `surfaceDestroyed` → `nativePause` の順で、小さなウィンドウで描かれることは無かった。
- 「自分で別の画面を開いた」印は `SafBridge` ではなく
  **`MainActivity.startActivityForResult` の上書き**で立てる（`startActivity` も
  ここを通るので、SAF の選択画面と `SDL_OpenURL` のブラウザを 1 か所で拾える）。
  戻ってきた `onResume` で下ろす。
- ネイティブの見張り `pip::Active()` は、小窓から戻って面を出したあと 250ms
  （`kSettleMs`）まで真。その間は `SyncCanvasToWindow` と向きの切り替えを
  止め、偽になったフレームで `windowResized` を立てて合わせ直す。
- 小窓の字の大きさは**端末の文字サイズの設定に従う**（2026-09-25、ユーザーの指示）。
  曲名 16sp・状態と時刻 14sp を `TypedValue.applyDimension(COMPLEX_UNIT_SP)` で
  ピクセルにする（Android 14 以降の非線形の拡大も効く）。3 行がバーの上に
  収まらないときだけ、同じ割合で縮める。余白・行の間隔・バーはウィンドウの高さの割合
  （8% / 4% / 5%）。縦横比は 2:1。当初は字もウィンドウの高さの割合（20% / 14%）だった。
  Pixel 7a（小窓の高さ 299px、420dpi）で文字サイズ 0.85 / 1.0 / 1.3 / 2.0 を
  見比べた: 0.85・1.0 は設定どおりの大きさでバーの上に空きが残り、1.3 で
  ほぼいっぱい、2.0 は収まるように縮められて 1.3 と同じ大きさになる。
  試すときは `settings get system font_scale` で元の値を控えてから
  `settings put system font_scale <値>` → mxv2 を起動し直す → 最後に戻す。
- 文言: `[Menu] Pip` / `[Settings] Pip, PipOff, PipPlaying, PipAlways, PipNote`。
  ini は `[Screen] Pip=0|1|2`（`Settings::kFieldPip`）。

実機（Pixel 7a、API 37、ランチャーは ADW）で確かめたこと:

- 演奏中にホームへ戻る（`input keyevent KEYCODE_HOME`）→ 自動で小窓になる
  （`dumpsys activity activities` の Task が `mode=pinned`）。音は止まらない
  （`dumpsys audio` の player が `state:started`）。
- 曲名のスクロール（3 秒止まってから送る）・時刻の進み・バー。
- 小窓のボタン: 一時停止 → 「一時停止中」、時刻が止まり、ボタンが再開に変わる。
  再開・次の曲も効く（次の曲では曲名のスクロールが先頭からやり直しになる）。
  **システムの操作メニューは 3.5 秒ほどで消える**ので、自動で押すときは
  「小窓をタップ → 0.8 秒 → ボタン」を 1 回の `adb shell` で続けて打つこと。
  メニューの中央は「広げる」ボタン。
- 広げる → 縦のスキンのまま崩れずに戻る（`surfaceChanged` は元の大きさの 1 回だけ）。
- × で閉じる → `onStop`、演奏は続く。ランチャーから開き直すと元の画面。
- 一時停止中にホームへ戻る（「演奏中だけ」）→ 小窓にならない。
- メニューの [表示] → [小窓で表示] → 小窓になる。
- [バージョン情報] の GitHub ボタン → GitHub アプリが前に出て、mxv2 は小窓に
  ならない。mxv2 へ戻ってからホームへ戻ると、また小窓になる。
- [mxv2 の設定] の [画面] に「小窓で表示」のコンボと説明が出る。

未確認: Android 8〜11 の `onUserLeaveHint` の経路（手元の Pixel 7a は 12 以降）、
XS17 など古い端末で PiP を持っているか、タブレット。

### 小窓の最中にフォントサイズを変えたら演奏が止まった（2026-09-25、ユーザーの報告・修正済み）

手順: 演奏 → ホームで小窓 → 端末の「表示サイズとテキスト」でフォントサイズを
変える → 演奏が止まり小窓が消える → mxv2 を前に出すとスキンが画面いっぱいに
広がらない → もう一度小窓にすると、スキンの画面が縮んで出る。

原因は 2 つ（Android の問題ではなかった）:

1. **`configChanges` に `fontScale` が無かった**ので、Activity が作り直された。
   SDL は `onDestroy` で `nativeSendQuit` → SDL_main が終わる（演奏が止まる）→
   新しい Activity が**同じプロセスで** SDL_main を最初から走らせる。
   logcat に `onDestroy()` → `Finished main function` → `onCreate()` が並ぶ。
   小窓とは関係なく、**フォントサイズや表示サイズを変えるといつでも演奏が
   止まっていた**（前からある不具合）。→ `density|fontScale|fontWeightAdjustment`
   を足した（SDL の雛形にも無い）。mxv2 の拡大率は物理 dpi から、文字は同梱
   フォントで描くので、どれも受け流してよい。小窓の字は描くたびに
   `getResources()` を読み直すので、その場で大きさが変わる。
2. **PipBridge の static（`sShowing` / `sInPip`）が前の Activity のまま残った。**
   `sInPip` が立ったままなのでネイティブの見張りが解けず、`SyncCanvasToWindow`
   が走らない（手順 5）。`sShowing` が立ったままなので次に小窓に入っても面を
   隠さない（手順 7）。→ `setActivity`（onCreate）で全部下ろす。

確認（Pixel 7a）: 小窓の最中に `settings put system font_scale 1.3` と
`wm density 480` → `onDestroy` は出ず、小窓はそのまま、音は `state:started`、
小窓の字が大きくなる。広げると元の画面が正しく出る。最後に
`wm density reset` と `font_scale 1.0` で戻した。
