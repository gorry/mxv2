mxv2をAndroidへ移植します。Phase 7の前半にあたります。

移植そのものの下ごしらえは済んでいます。文字描画はOS非依存（stb_truetype＋同梱フォント）、
Shift_JIS変換は自前の変換表、素材の置き場所は「同梱は読むだけ／書くのはユーザーフォルダ」に
分離済み、ファイルシステムはVFS化済みで、フォルダ読み・曲名読み・曲の読み込みはすべて
別スレッドに乗っています。**したがってAndroid固有の作業は「土台・素材・ライフサイクル・
操作・SAF」に集中し、移植そのもののやり直しはありません。**

## ビルド環境（2026-08-31に確認）

- NDK r28c (28.2.13676358) … `D:\dev\android-ndk`
- SDK … `D:\dev\android-sdk`（platforms 〜android-36 / build-tools 36.1.0）
- JDK OpenJDK 21.0.2 … `D:\dev\jdk`
- Gradle 8.7 … `D:\dev\gradle`
- adb … 実機1台が接続済み

足りないのは**SDL2のソース**だけです。`third_party/SDL2-2.32.10`はVC用のバイナリ配布で
`include`と`lib`しかなく、Androidは本体をソースからビルドし、その`android-project`の
Java側も必要になります。既存のフォルダはWindowsのビルドが使っているのでそのまま残し、
ソースは別に置きます。取得手順は`BUILD.md`へ書きます（third_partyは配布物に含めません）。

## Phase A — 土台（Gradleプロジェクトとcmakeのandroid対応）

- `third_party`にSDL2 2.32.10のソースを置きます。
- `android/`にGradleプロジェクトを作ります。SDLの`android-project`を土台にし、
  `SDLActivity`を継承したActivityを1つ用意します。
- `externalNativeBuild`から既存の`CMakeLists.txt`を呼びます。
- CMakeLists.txtのWindows前提を切り分けます。
  - **Androidでは`add_executable(mxv2)`ではなく`add_library(main SHARED)`**とします
    （`SDLActivity`が`libmain.so`をdlopenするため）。
  - `WIN32_EXECUTABLE`、`ole32` `shell32`のリンク、`SDL2.lib`の直リンク、
    `SDL2.dll`のコピーはWindows限定にします。
  - `simple_mdx_player`、`simple_mdx2wav`、`mxv2_chunktest`はAndroidでは作りません。
- **出口**: arm64-v8aの`libmain.so`がリンクまで通ること。まだ動かなくてかまいません。

## Phase B — 起動して画面が出る

- 画面サイズと向き。スキンは既存の`Phone`（480x720）を使い、`SDL_RenderSetLogicalSize`で
  端末の解像度に合わせます。
- **表示倍率は今「システム拡大率を拾う」作り**なので、Androidでは画面サイズから決める
  分岐が要ります。
- ログはSDLがlogcatへ流します。`SetupConsole`と`-console`はWindows専用として畳みます。
- **出口**: mxv2の画面が出て、logcatにログが出ること。

## Phase C — 素材の届け方（最初の山）

APKの`assets/`は**`fopen`で読めず、ディレクトリ列挙もできません**。今の`bmpfile`、
`textrender`、`ini`はすべて`fileutil`の`FILE*`経由なので、そのままでは何も読めません。

- 方針は**内部ストレージへ展開**します。読み出しは`SDL_RWops`（相対パスならAPKの
  assetsを読みます）。**実装では版番号は持たず、索引そのものを毎回突き合わせて、
  変わったファイルだけ書き直します**（消えたものは削除します）。
- 展開する顔ぶれは**ビルド時に生成した索引ファイル**で持ちます
  （`AAssetManager_openDir`のJNI列挙を避けるため）。
- `AssetPaths.bundledDir`を展開先へ向けます。**`ExecutableDir()`はAndroidでは意味を
  持たない**ので、`SDL_AndroidGetInternalStoragePath()`に差し替えます
  （実装では**その下の`bundled/`**。根へ展開すると`SDL_RWFromFile`が相対パスで
  そちらを先に開いてしまい、APKの中身が読めなくなります）。
  `UserDataDir()`はすでに`SDL_GetPrefPath`なのでそのままです。
- **出口**: スキン・フォント・配色・メッセージカタログ・同梱MDXが読め、画面が正しく
  描かれること。

## Phase D — 音を出す

- SDL audio（AAudio / OpenSL ES）。**`want.samples`を端末のnative frames-per-bufferに
  合わせます**（外すとプチプチ言います）。表示遅延は`have.samples`から自動追従する作りなので
  そのまま効くはずです。
- 96kHzは端末次第です。`X68SOUND_SUPPORT_96KHZ`の分岐は入っているので、出せなければ
  48000に落ちます。
- ARMでのデコード負荷を実測します（Windowsの`mxv2_chunktest`に相当するものを1回だけ）。
- **出口**: 同梱MDXが正しい速度・音程で鳴り、ビジュアライザが同期すること。

## Phase E — ライフサイクルと画面の作り直し

- `SDL_APP_WILLENTERBACKGROUND` / `SDL_APP_DIDENTERFOREGROUND`。バックグラウンド中は
  **描きません**（描くとAndroidに停止させられます）。
  （**2026-09-07 に変更**: 当初は音も止めていましたが、バックグラウンドでも
  演奏を続けるようにしました。末尾の「バックグラウンド演奏と通知」を見ること）
- **`SDL_RENDER_TARGETS_RESET` / `SDL_RENDER_DEVICE_RESET`**。GLコンテキストが失われると
  テクスチャの中身が飛びます。mxv2は**差分更新**なので、ここで「まだ描いていない」印まで
  含めて全面再描画させる必要があります（`chromeRefresh` / `fileListRefresh` /
  textLayerの全再描画）。
- 解像度変更（`SDL_WINDOWEVENT_SIZE_CHANGED`）で倍率を取り直します。
- 戻るキー（`SDLK_AC_BACK`）。今のESC=終了をそのまま当てるのは乱暴なので、
  「ダイアログを閉じる → 親フォルダへ → 確認して終了」に割り当てます。
- **出口**: ホームに戻して復帰しても、画面が変わっても壊れないこと。

## Phase F — 操作（タッチ）

- タッチ→マウスの合成はSDLが既定で行うので、**ファイラー・操作ボタン・ドラッグスクロール・
  慣性はそのまま動くはず**です。
- 足りないのは2つです。
  - **右クリック（コンテキストメニュー）を長押しに割り当てます。**
  - **キーボードにしか無い機能への導線**を用意します（チャンネルミュート1-8、シーク、
    TABの文字サイズ、`\`、N・Bなど）。ImGuiのメニューか、Phoneスキンのボタンへ出します。
- ImGuiの入力欄はソフトキーボードが出ます。**入力欄がキーボードに隠れないよう**、
  ダイアログの位置を見直します。
- **出口**: 物理キーボード無しで全機能に手が届くこと。

### Phase F の残り 3 つの結論（2026-09-10、ユーザーの判断）

- **長押しでのメニュー表示は無し。** バナーのタップで足りる。
- **キーボード専用機能**は洗い直したところ、タッチで届かないのは `F`（フェードアウト）と
  `\`（ファイルシステムのルートへ）の 2 つだけだった（`Ctrl+0` はステータス欄 2 回、
  `TAB` は [設定] のチェックボックス、シークはプログレスバー、終了は戻るキーで届く）。
  - **STOP の長押し → フェードアウト**、**ファイラーの長押し → 文字サイズ切り替え**を
    足した（`MouseInput::Poll` で 500ms。Android の設定で選べる最短の長押しが 400ms なので、それより短くはしない。成立した
    押下は離しても何もしない）。[操作方法] のマウス欄にも書いた。
  - **ルートへ行く機能はタッチ UI では用意しない**（左へはじく操作の繰り返しで足りる）。
- **ImGui の入力欄がソフトキーボードに隠れる件は再現していない**ので、このままにする。

## Phase G — ファイルシステム（SAF）

- `localfs`はAndroidで`available()`がfalseなので、**初回に「フォルダを追加してください」の
  導線**が要ります。
- `ACTION_OPEN_DOCUMENT_TREE`で選んだツリーを`saf:`マウントとして
  `Vfs::CreateFromMountRef`に足します。JNI越しに`List` / `Read` / `Exists` / `IsDir`を
  実装します。URIはpersistable permissionを取ってiniに残します（認証情報は書きません）。
- **ここが非同期化の投資が効く場所**です。フォルダ読み・曲名読み・曲の読み込みがすべて
  別スレッドに乗っているので、遅いSAFでも画面は止まりません。
- **出口**: 端末の任意のフォルダのMDXが鳴ること。

## Phase H — 仕上げと配布

- アイコン、アプリ名、パーミッションの最小化、minSdk / targetSdk、ABI（arm64-v8a、
  必要ならarmeabi-v7aも）。
- `NOTICE`と`LICENSE`は**バージョン情報が実行時に読む**ので、assetsに必ず入れます。
- 署名とapk / aabの作り方を`BUILD.md`へ書きます。

## 画面の向きとスキン（2026-09-10 に実装。仕様は `screen_orientation.md`）

**やった。** 縦画面用と横画面用の 2 つのスキンを設定し、端末の向きで取り替える。
機能の ON/OFF は起動オプション `-orient`（Android は既定で ON）で、ini には
残さない。切り替えかたは「縦画面のみ / 横画面のみ / 起動時の方向で切り替える /
常に切り替える」の 4 つ。

実装で分かった、Android 側の要点を 2 つ。

- **向きを `SDL_GetDisplayOrientation` で採ってはいけない。** SDLActivity の
  `getCurrentOrientation()` が `display.getRotation()`（＝端末の**自然な向き**からの
  回転量）をそのまま写しているので、**横が自然な向きのタブレットでは逆に出る**。
  mxv2 は出力（ウィンドウ）の `w > h` で決めている（`Screen::orientation()`）。
- **端末の向きの固定は JNI で行う。** `SDL_HINT_ORIENTATIONS` はウィンドウを
  作るときと `SetWindowResizable` のときにしか読まれず、しかも
  `SDL_SetWindowResizable` はフラグが変わるときしかバックエンドを呼ばないので、
  実行中に変えられない。`OrientationBridge`（`src/orientlock.cpp`）から
  `Activity.setRequestedOrientation` を直に叩く。上下反転を許すために
  `SCREEN_ORIENTATION_SENSOR_PORTRAIT` / `SENSOR_LANDSCAPE` を使い、
  「常に切り替える」は `SCREEN_ORIENTATION_FULL_USER`（＝何もしないときと同じ）。

- **`SDL_SetWindowSize` を Android で呼んではいけない**（この作業で踏んだ）。
  Android のバックエンドは `SetWindowSize` を持たないので、SDL は
  **自分の記録 (`window->w/h`) だけを書き換えて**リサイズ済みとみなす。
  レンダラの出力サイズもその記録から来るため、実際の描画面 (2400x1080) より
  小さいビューポートに描かれ、**絵が画面の左下へ寄る**（実機で 720x480 が
  左下に出た）。スキンを切り替えると `Screen::Resize` がこれを呼んでいた。
  → `Screen::CanResizeWindow()` を足して、窓の大きさを決めてよい
  プラットフォームでだけ呼ぶようにした（表示倍率の変更と、`[Position]` からの
  大きさの復元も同じ）。Android ではキャンバスだけ変え、あとは
  `SyncCanvasToWindow` が実際の出力に合わせ直す。

以下は着手前に書いていた見込み。

## 保留事項（当時） — 画面の向きとスキン

**縦横を切り替えたときにスキンをどうするか。** 以下を選べるようにしておくとよいはずです。

- **縦画面固定** / **横画面固定**
- **起動時の向きで固定**
- **縦と横で別のスキンを使用できる**

スキンはフォルダ形式で、`layout.ini`に画面サイズまで持っています。実行中の切り替えも
すでに実装済みなので、「縦と横で別のスキン」は既存の仕組みに乗せられます
（切り替え時の順番には注意が要ります）。向きの固定そのものはAndroidの
`screenOrientation`と、実行時の`SDL_HINT_ORIENTATIONS`のどちらで行うかを決めます。
今あるスキンは`Default`（640x480＝横）と`Phone`（480x720＝縦）なので、
縦横で別スキンにする形とは相性がよいはずです。

**これはPhase Eの画面まわりと重なりますが、いつ着手するかは別に指示します。**

## 通しで気を付けること

- **どのフェーズでもWindows版を壊さないこと。** 同じCMakeLists.txt、同じソースで進め、
  `#ifdef`は最小限にします。区切りごとにWindowsでビルドし、`mxv2_chunktest`で
  波形一致を確認します。
- 順番はA→B→Cが「画面が出る」までのひとかたまりで、**Cが最初の山**です。Dは比較的軽く、
  Eはハマると長引きます。FとGは互いに独立して進められます。

## 進捗（2026-08-31）

**Phase A〜C は完了。実機（Pixel 7a / arm64-v8a）で画面が出て、同梱 MDX が
演奏できるところまで確認した。** Windows 版は壊れていない（警告ゼロ、
`mxv2_chunktest` は testdata 35 本すべて一致）。

やったこと:

- `third_party/SDL2-2.32.10-src/` に SDL2 のソースを置いた（VC 用パッケージは
  そのまま残してある）。取得手順は `BUILD.md` へ書いた。
- **アプリ識別子は `net.gorry.mxv2`**（ユーザーが決めた。Activity は
  `net.gorry.mxv2.MainActivity`）。
- `android/` に Gradle プロジェクト（AGP 8.5.1 / Gradle 8.7 / JDK 21 /
  NDK r28c / compileSdk 34 / minSdk 21 / CMake は SDK 同梱の 3.22.1）。
  `externalNativeBuild` から既存の `CMakeLists.txt` を呼ぶ。
- CMakeLists.txt を `if(ANDROID)` で切り分けた（SDL2 をソースから
  `add_subdirectory` / 本体を共有ライブラリに / サンプルと chunktest は作らない /
  「実行ファイルの隣へコピー」を飛ばす / `log` をリンク）。
- 素材は Gradle が apk へ入れ（`prepareMxv2Assets`）、索引を作り
  （`generateMxv2AssetIndex`）、起動時に `src/androidassets.cpp` が内部
  ストレージへ展開する。**変わったものだけ**書き直す。
- `ExecutableDir()` は Android では `SDL_AndroidGetInternalStoragePath()`。

**計画から変えたところ**:

- **`add_library(main SHARED)` にはしなかった。** ターゲット名を Windows と
  揃えて `mxv2` のままにし、`MainActivity.getLibraries()` を
  `{"SDL2", "mxv2"}` に上書きして `libmxv2.so` を読ませている。
- **ログは printf をパイプで受けて `__android_log_write`（タグ `mxv2`）へ流す。**
  最初 `SDL_Log` へ渡したら、SDL の既定のログ出力が logcat のあと **stderr にも
  同じものを書く**ので輪ができて止まらなくなった。stderr は差し替えず、
  書き出しにも SDL_Log を使わないこと。
- **表示倍率は Android では 100% 固定**にした（`Screen::SystemZoomPercent`）。
  窓は画面いっぱいで、拡大は `SDL_RenderSetLogicalSize` が面倒を見る。
  DPI から出すと 400% などになって意味を持たない。
- **既定のスキンは Android では `Phone`**（`settings.cpp`）。縦画面に合う。
  保留事項（向きとスキン）に手は付けていない。
- **コマンドライン引数をインテントの extra `args` で渡せるようにした**
  （`MainActivity.getArguments()`）。端末にはコマンドラインが無いので、
  `-skin` や曲の指定を試すための開発用の受け皿。

**その場で直した不具合**: `settingsui.cpp` の `ImGui::TextDisabled(Msg(...))` が
NDK の `-Werror=format-security` で止まった。`"%s"` を足した（Windows でも
より正しい）。

**Phase D（音）の残り**: 実機で 48000 Hz / バッファ 512 frames で開き、演奏時間も
ビジュアライザも進んでいる（＝オーディオのコールバックが回っている）。
**耳での確認と、`want.samples` を端末の native frames-per-buffer に合わせる話、
ARM でのデコード負荷の実測はまだ。**

## Phase E の進捗（2026-08-31）

**Phase E は完了。** 実機で確認した。

- **バックグラウンドでは 1 フレームも描かない**（`SDL_APP_WILLENTERBACKGROUND` /
  `SDL_APP_DIDENTERFOREGROUND`）。`SDL_WaitEventTimeout` で寝て待つので CPU は 0%。
  音は `Player::SetAudioSuspended()` でオーディオ装置ごと止める。**一時停止
  (Pause) とは別物**で、装置を止めると演奏位置も止まるため、復帰しても表示との
  対応がずれない。
- **`SDL_RENDER_DEVICE_RESET` / `SDL_RENDER_TARGETS_RESET` と復帰で全面再描画**
  （`main.cpp` の `ForceRedrawAll`）。テクスチャは中身だけでなく**器ごと**
  無効になるので `Screen::ResetTextures()` で作り直し、ImGui にも
  `SettingsUi::HandleDeviceReset()` でフォントアトラスを作り直させ、差分更新の
  「もう描いた」印まで戻す。
- **戻るキー (`SDLK_AC_BACK`)** … ダイアログを閉じる → 親フォルダへ →
  **終了の確認ダイアログ**。確認は `SettingsUi::OpenQuitConfirm()`（文言は
  `Dialog.Quit` / `Dialog.QuitQuestion` / `Button.Quit`）。
  **この確認はユーザーの指示で全プラットフォーム共通の作法にした**（後述）。
- 解像度変更 (`SDL_WINDOWEVENT_SIZE_CHANGED`) は元から拾っていたのでそのまま。

**ここで見つけた不具合（Windows にもあったもの）**: ImGui のダイアログを
**押しても反応しない**。ImGui は実解像度で描くが、mxv2 はマウス座標を
「論理座標 × 倍率」でしか直しておらず、**レターボックス（キャンバスが窓より
小さいときの帯）のぶんがずれていた**。Android は窓が画面いっぱいなので必ず
ずれる。Windows でも窓を引き伸ばして縦横比を変えれば同じことが起きる。
`Screen::GetRenderOffset()` を足し、`io.DisplaySize` も実出力そのものにして、
入力にも帯のぶんを足した。ダイアログは画面の真ん中に出るようになった。

**次は Phase F（タッチ）か G（SAF）。** タッチは単押しでカーソルが動き、
ImGui のボタンも押せるところまで確認した。フォルダを開くのはダブルクリック
扱いなので `adb shell input tap` を 2 回打つ方法では確認できていない
（Phase F でタッチの導線を作る）。ダイアログの部品は指には小さいので、
そこも Phase F で見直す。

## 終了の確認は全プラットフォーム共通（2026-08-31、ユーザーの指示）

Android の戻るキーだけでなく、**ESC と Q でも終了の前に確認を出す**。
押し間違いで演奏が止まるのを防ぐため。

- **確認するのはキーだけ**。ウィンドウの × （`SDL_QUIT`）と、コンテキスト
  メニューの [終了] は、意図してそこを選んでいるので**確認しない**
  （× は明示的に不要と指示された）。
- ダイアログは **Enter でも [終了]**。ESC で開いて ESC で閉じられる一方、
  「はい」がマウスでしか押せないと、キーボードだけでは終われなくなるため。
- 文言 `[HelpKeys] ESC / Q` も「終了（確認してから終わる）」に直した。

**ただし、右クリックメニューの [終了] はモバイルでは出さない**（2026-09-10、
ユーザーの指示）。区切り線ごと消える。終わらせるのは OS の仕事で、メニューに
[終了] を置くのはモバイルの作法に合わず、**あってはならないと定めている
配布先もある**ため。判定は `Screen::CanQuitApp()`（Android / iOS で false）。

**戻るキーからの終了は残す。** あちらは OS 側の作法そのもので、確認
ダイアログも今までどおり出る。物理キーボードを繋いだときの ESC / Q も
そのまま効く（消えるのはメニューの項目だけ）。

## Phase G の進捗（2026-08-31）

**SAF (Storage Access Framework) を実装した。** 実機で、端末のフォルダを
マウントして一覧・曲名・演奏まで確認済み。ドラッグスクロールの確認に
大量の MDX を置けるようにするため、F より先に着手した。

- **Java 側**: `android/app/src/main/java/net/gorry/mxv2/SafBridge.java`。
  `ACTION_OPEN_DOCUMENT_TREE` で選んでもらい、**persistable permission を
  取って次の起動でも読めるようにする**。`list` / `read` / `stat` /
  `rootName` / `hasPermission` を `DocumentsContract` + `ContentResolver` で。
  SAF はパスではなくドキュメント ID で辿るので、**相対パス → ドキュメント ID の
  対応づけを覚えておく**（一覧を読んだときに子のぶんもまとめて覚える）。
- **ネイティブ側**: `src/safaccess.*`（Android 以外では「使えない」と
  返すだけの実装が入る）。`saf:` の `FileSystem` を JNI で被せたもので、
  ref は `saf:<ツリーの URI>/<相対パス>`、`mountRef()` は `saf:<ツリーの URI>`。
  `parentIsCheap()` は false。**FindClass はメインスレッドから先に呼ぶこと**
  （作業スレッドのクラスローダからはアプリのクラスが見えない。掴んだ
  jclass はグローバル参照で持ち続けるので、以降はどのスレッドからでも呼べる）。
- **UI**: [ファイルシステムの設定] の [追加...] が、Android では SAF の
  選択画面を出す（他のプラットフォームは従来どおり `dir:` の追加ダイアログ）。
  選択画面が出ている間 mxv2 はバックグラウンドなので、結果は
  `SettingsUi::PollSafPicked()` が毎フレーム拾ってマウントする。
- 文字列は JNI の「修正 UTF-8」を避けて UTF-16 経由で変換している
  （絵文字入りのファイル名でも崩れない）。

**仕様書との違い**: `filesystem.md` は「SAF> は曲名の代わりに URI を表示」と
書いてあるが、**選んだフォルダの表示名**（例 `SAF> mxv2test`）にした。
URI は 90 文字前後あって読めないため。戻すのは `SafFileSystem::label()` の
1 行。

**ここで直した大きな不具合（Windows にも影響）**: **タッチが効いたり
効かなかったりする**。原因は `SDL_RenderSetLogicalSize`。SDL は「いま論理
サイズが入っているか」を見てマウス座標を論理座標へ直すが、mxv2 は文字と
ImGui を実解像度で描くために**毎フレーム論理サイズを外して戻していた**。
Android はタッチが Java の UI スレッドから飛んでくるので、外している隙に
届いたイベントだけ変換されずに素の窓の座標で入る（同じ場所を叩いても
1/3 くらいの割合で別の場所に当たる）。

→ **論理サイズを使うのをやめた。** キャンバスの拡大は `Screen::CanvasRect()` を
自分で計算して `SDL_RenderCopy` の行き先に渡し、マウス座標は
`Screen::WindowEventToCanvas()` で自分で論理座標へ直す。ImGui へ渡すのは
窓の座標のまま（実ピクセル）。`BeginNativeScale` / `EndNativeScale` は不要に
なったので消した。**当たり判定は全部キャンバスの論理座標**という約束は変わらない。

**残り**: 種類が 2 つ以上ある環境が出てきたら [追加] に種類の選択を挟むこと。
SAF の `forget()`（覚えた対応づけを捨てる）はまだどこからも呼んでいない
（ファイラーの再読込に繋げるとよい）。Web / SMB は未着手。

## 指で押せる大きさ（2026-08-31、ユーザーの指示）

「設定ダイアログの行が小さすぎて指で操作できない。指で押すには最低 6mm
くらい要る」との指摘。Android だけでなく、**今後 Web からモバイル端末で
触るときにも効く共通の仕組み**として入れた。

- `Screen::PixelsPerMm()` … `SDL_GetDisplayDPI` の対角 dpi から出す。
  取れなければ Android は 160dpi (mdpi)、それ以外は 96dpi を仮に使う。
- `Screen::TouchPreferred()` … 「自動」の既定。Android / iOS は true、
  Emscripten は `SDL_GetNumTouchDevices() > 0`、それ以外は false。
- 設定は `[Screen] TouchUI`（0 = 自動 / 1 = する / 2 = しない）。
  設定ウィンドウの [画面] に combo を足した。触れる画面の PC や、
  逆に Android にマウスを繋いだときのために手で決められる。

**広げ方**（`SettingsUi::ApplyScale`）。**字の大きさは変えない**
（携帯の狭い画面で字まで大きくすると、一度に読める項目が減るため）。

- ボタン・チェックボックス・入力欄の高さ = 字の高さ + `FramePadding.y * 2`
- 一覧の行 (Selectable) の当たり判定 = 字の高さ + `ItemSpacing.y`
  ImGui は行同士に隙間ができないよう Selectable の箱を `ItemSpacing.y` の
  半分ずつ上下へ広げる作りなので、**`ItemSpacing.y` を足すとそのまま行が
  太くなり、行間に死んだ隙間もできない**。ここが要点で、Selectable に
  高さを渡して回る必要はなかった。
- スクロールバーの幅とつまみは 6mm の 2/3 (4mm)。幅まで 6mm にすると
  横幅を食いすぎる。
- 行が太くなるとダイアログに中身が入らなくなるので、既定の大きさを
  `dialogGrow_`（= 6mm ÷ ふつうの行の高さ）倍にしてから画面へ詰める
  (`SettingsUi::DialogSize`)。携帯ではほぼ全画面になる。

**字も一緒に大きくする**（同日の追加指示。「行だけ 6mm でも文字が 1.5mm では
使いにくい。文字は行の 60% 程度ほしい」）。`kTouchFontRatio = 0.6`。
`style.FontScaleDpi` を上げて、字が行の 60%（6mm の行に 3.6mm の字）になる
ようにする。**この 0.6 倍は 2026-09-08 に `kTouchFontMm = 3.1f` へ変えた**
（後述「ラベルが横幅で切れる件」）。日本語は仮想ボディいっぱいに書かれるので、漢字の実寸はほぼ
この値になる。`ScaleAllSizes()` も**キャンバスの拡大率ではなく字の倍率**で
掛ける（字だけ大きくすると、窓の内側の余白や区切りが相対的に痩せて見える）。

**字を大きくしたら横が足りなくなった**ので、狭い画面向けに 3 つ足した。
どれも「入らないときだけ効く」ので、PC の見た目は変わらない。
- `TextNote()` / `TextError()` … 補足と注意の行を `PushTextWrapPos(0.0f)` で
  折り返す。部品のラベル（チェックボックスの文言など）は ImGui が折り返して
  くれないので、そちらは短いままにしておくこと。
- `SameLineOrWrap(次のボタンの文言)` … 残り幅に入らなければ `SameLine()` を
  やめて次の行へ落とす。[再読込] と [システムに合わせる] に入れた。
- `ConfirmText()` … `AlwaysAutoResize` の確認ダイアログは、長いパスが 1 行で
  伸びて画面からはみ出す。24 文字ぶんと画面幅の小さい方で折り返して頭打ちにする。

**確認**: Pixel 7a で 6.0mm = 99px、字 3.6mm。一覧の行の下端（文字から外れた位置、
y=480）を叩いても正しくその行が選ばれる＝当たり判定が 99px ある。
Windows でも `TouchUI=1` で 6.0mm = 40px になり、行の上端 (y=90) と
下端 (y=126) のどちらを押しても同じ行が選ばれた。「しない」に戻すと
元の詰まった表示に戻る（設定ウィンドウはその場で作り直される）。

### ラベルが横幅で切れる件（2026-09-08 に解決）

2026-08-31 に「文言そのものを短くして直す」方針で後回しにしていたが、
**Android の縦画面ではラベルに 6 文字しか入らず、短くするだけでは足りない**
ことが分かった。字を小さくするほうへ切り替えた。

`kTouchFontRatio = 0.6`（行の高さの 60% = 3.6mm）をやめ、**行の高さとは
切り離した絶対値** `kTouchFontMm` にした。**押せるところの高さは 6mm の
まま**なので操作しやすさは変わらず、字が小さくなったぶんは上下の余白へ回る。

Pixel 7a（1080x2400、6mm = 99px、Phone スキンでキャンバス倍率 2.25）での
実測。ImGui のコンボやスライダーは既定で幅が**残り幅の 65%**なので、
ラベルに使えるのは残りの 35%。

| 字 | px | ラベルに入る全角 | |
|---|---|---|---|
| 3.6mm | 59 | 約 6 文字 | 切れる |
| 2.6mm | 43 | 約 8〜9 文字 | **小さすぎる**（ユーザー判断） |
| **3.1mm（今）** | 51 | **約 7 文字** | 採用 |

**3.1mm は 3.6mm と 2.6mm の中間で、実機を見たユーザーが決めた値**
（Android の 19sp 相当。本文の 14sp = 約 2.2mm より大きい）。いま一番長い
ラベルは全角 7 文字（「カーソルの強さ」「ボタンの明るさ」「曲名スクロール」）で、
実測すると**右端に 38px（1 文字ぶんに満たない）しか余っていない**。
**つまり全角 8 文字のラベルを新しく足すと切れる**——文言を足すときはここを見ること。

字は**下限**としてしか使っていない（`touchFontPx_ > kFontSizePx * fontScale`
のときだけ上げる）ので、キャンバスの拡大率のほうが大きい環境では今までどおり。
低 DPI の Windows（96dpi）では 3.1mm = 11.7px < 15px なので**何も変わらない**。
HiDPI の Windows で `TouchUI=1` にしたときだけ効く。

なお、これでも足りなければ次の手は**コンボの幅**（既定の 65% を
`SetNextItemWidth` で詰める）で、字をさらに小さくするより効く。

**残り**: 文言を短くする話そのものは有効なので、いま切れているラベルがあれば
`assets/locale/*/message.ini` の `[Settings]` を短くして直す（コード側で
折り返す仕組みは入れない）。

## タブレットでの確認（2026-08-31）

Xiaomi Redmi Pad Pro（2405CRPFDG / dizi、Android 15、arm64-v8a、
1600x2560、12.1 インチ）で確認。**そのまま動いた**（起動・素材の展開・
Phone スキンの描画・演奏（PCM8 の PDX 込み）・設定ダイアログ）。
ここで 2 つ直した。

**1. 索引に消えた素材が残り続けていた**（起動時に
「cannot read from apk: assets/extract-test.txt」）。`generateMxv2AssetIndex` が
**Copy** で素材を別の場所へ通してから数えていたが、Gradle の Copy は
**destination から消えたファイルを消さない**ので、素材を減らすと索引にだけ
残る。apk のほうは Sync なので実物は無く、起動のたびに読めないと言われる。

→ 索引は `prepareMxv2Assets`（Sync）が並べ終えた木を直接数えるようにした。
複製が 1 つ減るので速くもなった。なお `ExtractBundledAssets` は
**1 つでも読めないと索引を置かない**作り（途中で落ちたら次の起動でやり直す
ため）なので、この状態では毎回 44 個を展開し直していた。直ったあとは
2 回目の起動で何も展開しない。

**2. `Screen::PixelsPerMm()` が実寸から離れていた**。`SDL_GetDisplayDPI` の
`ddpi` は、Android では画面の実寸ではなく **密度の区分**（160/320/480…）を
丸めた値。この端末は区分 320 に対して実寸は約 250dpi なので、6mm のつもりが
4.7mm しか無かった。

→ `hdpi` と `vdpi` の平均（実寸から出た値）を使うようにした。結果、この
タブレットで 6.0mm = 59px（実寸どおり）。Pixel 7a は区分 420 に対し実寸 429
なので、ほぼ変わらない。**判断の基準は下の 2 台目で改めた。**

**未確認**: 横向き。Phone スキンは 480x720 の縦長なので、横向きにすると
左右に大きな帯が出るはず（`保留事項 — 画面の向きとスキン` の話）。
ファイラーの字も、12 インチではスキンの既定が小さめに見える
（[大きい文字で表示する] はある）。

## タブレット 2 台目での確認（2026-08-31）

Alldocube iPlay 60 mini Pro（Android 14、arm64-v8a、1200x1920、8.4 インチ）。
**そのまま動いた**（起動・素材の展開・演奏（PCM8）・設定・ファイルシステムの
設定）。索引の修正も効いていて、初回は 44 個を展開し、2 回目の起動と再
インストール後は何も展開しない。

**ここで `ddpi` を当てにする書き方をやめた。** この端末は `wm density` が
「Physical 320 / **Override 189**」。つまり **ddpi はユーザーが「画面サイズ」の
設定でいつでも変えられる**値で、実寸の物差しにはできない（1 台目は区分の
丸めでずれ、2 台目はユーザー設定でずれた）。

→ `hdpi` / `vdpi` を第一に使い、**ありえない値かどうかだけ**を見る形に改めた。
  ・40〜1000 dpi に収まっているか（大画面のテレビから高精細の携帯まで）
  ・横と縦がかけ離れていないか（画素はふつう正方形）
外れたときだけ `ddpi` へ落とし、それも駄目なら決め打ち（Android 160 /
その他 96）。前の「`ddpi` の 0.6〜1.7 倍」という基準だと、この端末は
284/189 = 1.50 で**たまたま通っていただけ**だった。もう少し強い拡大表示に
していたら、誤って 189dpi を採ってしまう。

結果、この端末で 6.0mm = 67px（実寸 約 270dpi に対して 284dpi 相当。
数 % のずれ）。

## Pixel 7a へ戻して再確認（2026-08-31）

3 台目の確認を終えて Pixel 7a（1080x2400 / 6.1"）へ戻し、同じ apk で確認。

- **索引の後始末が効いた**: 「extracted 0 file(s), **removed 1**」。この端末には
  古い索引で展開された `assets/extract-test.txt` が残っていたが、新しい索引に
  無いので消された（`ExtractBundledAssets` の「索引から消えたものは展開先
  からも消す」経路）。
- **SAF の権限は再インストールを越えて残る**: ユーザーの `SAF> MXDRV` と
  検証用の `SAF> mxv2test` が両方そのまま。`SAF>MXDRV/mdx` まで降りて
  一覧できた。
- 6.0mm = **95px**（前の `ddpi` 基準では 99px）。

**dpi の実測まとめ**（`hdpi`/`vdpi` の平均を使った値と、画素数と公称
インチから出した実寸）:

| 端末 | 画面 | 6.0mm | 採った dpi | 実寸 dpi | ずれ |
|---|---|---|---|---|---|
| Pixel 7a | 1080x2400 / 6.1" | 95px | 402 | 429 | -6% |
| Redmi Pad Pro | 1600x2560 / 12.1" | 59px | 250 | 249 | ±0% |
| iPlay 60 mini Pro | 1200x1920 / 8.4" | 67px | 284 | 270 | +5% |

**どれも 1 割以内**に収まる。`ddpi` は区分の丸めで 3 割ずれ、しかも
ユーザーが変えられるので、こちらのほうが確実によい。ずれが気になるなら
`kTouchTargetMm` を上げる（1 行）。

## ユーザーフォルダを外から見える場所へ（2026-08-31、ユーザーの指摘）

「ユーザーフォントを試そうとしたが `/Android/data/net.gorry.mxv2` が開けない」。
**そもそもそのフォルダが存在していなかった。** `UserDataDir()` は
`SDL_GetPrefPath` を通していて、Android ではこれが**内部**ストレージ
（`/data/data/<パッケージ>/files/`）を返す。外から見えないどころか、
`adb shell ls` すら Permission denied になる場所で、ユーザーが font.ttf を
置けるはずがなかった。

→ Android の `UserDataDir()` を `SDL_AndroidGetExternalStoragePath()`
（= `getExternalFilesDir(null)` = `/sdcard/Android/data/<パッケージ>/files/`）
に変えた。USB でパソコンから見え、`adb push` でも入り、権限は要らず、
アンインストールで消える。**このフォルダは `getExternalFilesDir` を呼ぶまで
作られない**ので、「呼ぶこと自体が作ること」でもある（呼ばないアプリの
フォルダは、ファイラーで見ても無い）。外部が使えない端末では内部へ落とす。

**設定の引き取り**: `MigrateLegacySettings()` に、実行ファイルの隣に加えて
`LegacyUserDataDir()`（Android の内部ストレージ）も見させた。1 度だけ
`mxv2.ini` を引き取り、元は残す。実機で SAF のマウント 2 つを含めて
そのまま引き継げた。

**確認**: `adb push <フォント>.ttf …/files/font.ttf` で、ImGui のダイアログも
キャンバスの文字（ファイラー）も置いたフォントに変わった。
なお **字が無いときに同梱フォントへ落ちることはしない**（欧文だけの
フォントを置いたら日本語が全部 `?` になった）。仕様としてそれでよいと
思うが、BUILD.md に注意として書いた。

## mdx フォルダに外から書けない（2026-08-31、ユーザーの報告）

ユーザーフォルダが外から見えるようになったあと、「エクスプローラや端末の
ファイルマネージャに net.gorry.mxv2 が出るようになった。フォントの置き換えも
できた。**ただし mdx フォルダにファイルを書き込めない**」。

原因は `MakeDirectories()` の `mkdir(path, 0755)`。**0755 にはグループの
書き込みが無い。** /sdcard/Android/data/… は、パソコンや端末のファイル
マネージャからは **ext_data_rw グループ**として見えるので、書き込みは
グループ権限で通る。Android 自身が作る `files/` は `drwxrws---` (0770+setgid)
なのに、その中に mxv2 が作った `mdx/` だけ `drwxr-s---` (0750) だった
——「フォルダは見えるのに中に置けない」という症状。

→ Android では 0770 で作り、既にあるものも直す (`FixSharedDirMode`)。

**ここで 1 度やらかした**: 直すときに `chmod(path, 0770)` としたら、
`files/` の **setgid が落ちた**（`drwxrws---` → `drwxrwx---`）。setgid は
「中に作られたファイルが ext_data_rw グループを引き継ぐ」ための印で、
落ちると外から置いたファイルが置いた人のグループ（例 `shell:shell`）に
なり、モードによっては **mxv2 から読めなくなる**。しかも **FUSE 越しでは
setgid を立て直せない**（`chmod 02770` が通らない）。

→ `FixSharedDirMode()` は **足りないビットを足すだけ**にした。
`stat` して `S_IWGRP` が立っていれば何もしない。立っていなければ
`(st_mode & 07777) | S_IWGRP` を渡す。モードを丸ごと渡してはいけない。
落としてしまった `files/` は、`/sdcard/Android/data/net.gorry.mxv2` を消して
Android に作り直させて戻した（内部ストレージは消えないので、設定は
`MigrateLegacySettings` が引き取り直す）。

**確認**: `files/` も `mdx/` も `drwxrws---` に揃った。外から置いたファイルは
`ext_data_rw` グループを引き継ぎ、`UserDir>` から MDX（PDX 込み）を演奏できた。

## 演奏中に [■] を押すと PLAY TIME が止まらない（2026-08-31、修正済み）

上の確認中に見つけた**別件**。演奏中にメイン画面の [■] を押すと、
鍵盤とレベルメーターは止まるのに **PLAY TIME とシークバーだけが進み続ける**。
0 にも戻らない。プラットフォーム共通の不具合で、Windows でも同じだった。
ユーザーの指示は「停止したら 00:00 に戻り、そこで止まる」。

**原因は 2 つ**。

1. `MXDRV_Stop` は音を止めるだけ。ワークの `PLAYTIME` は `MXDRV_GetPCM` を
   回しているかぎり進み続けるので、デコードスレッドとオーディオ装置を
   止めないと演奏位置だけが進む。
   → `Player::Stop()` を `PlaySong` / `SeekMs` と同じ手順にした
   （`SDL_PauseAudioDevice(1)` → `StopDecodeThread()` → `MXDRV_Stop` →
   `dispQueue_.Clear()` → `ResetClocks()` → `nowTimeMs_ = 0`）。

2. 止めたあとも**鍵盤とレベルメーターが点いたまま残る**。ここで 2 回間違えた。
   - `MXDRV_Stop` の直後に `watch_.Poll()` を 1 回呼べば消えると思ったが、
     ワークの「鳴っている」印はその場では消えない。
   - `StatusWatch` が覚えている鍵から「消す指示」を作ろうとしたが、これも駄目。
     **イベントはデコード位置で打刻され、画面に出るのは再生位置まで
     追いついた分だけ**で、両者はリングバッファのぶんだけずれている。
     `StatusWatch` が知っているのは**デコード位置**の状態、画面に出ているのは
     **再生位置**の状態。しかも `dispQueue_.Clear()` は、その差分にある
     「消す指示」まで捨ててしまう。
   → **描いた側で覚えるのが正解**。`Visualizer` に `noteOn_[段][鍵]` を持たせ、
     `AllOff()` で消す。呼ぶのは `UpdateChrome()` の中で、
     `player.playing()` が落ちたフレーム（[■] でもメニューの [停止] でも通る）。
     曲の終わりでは `playing` は落ちないので、そちらの見た目は変わらない。

**確認**: 実機と Windows の両方で、[■] → PLAY TIME 00:00 で停止・鍵盤と
レベルメーターが消える・[▶] で頭から再生。

## バックグラウンド演奏と通知（2026-09-07）

ユーザーの指示: 「バックグラウンドに回ると演奏を中断するが、演奏し続ける
ようにしてほしい。CONT / REPEAT もバックグラウンドで効くこと。通知での
演奏状態の表示とアプリ画面への誘導も」。

### SDL 側 — 止まらないようにする

SDL2 は既定でアプリが止まると**イベントループごと止め、オーディオ装置も
止める**。`SDL_Init` の前に 2 つのヒントを切る（`src/main.cpp`）。

```c
SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0");
```

前者を切ると `Android_PumpEvents_Blocking` ではなく `..._NonBlocking` が
選ばれ、止まっているあいだもメインループが回り続ける。後者は
「止まっているあいだオーディオ装置も止めるか」で、こちらも切る。
**後者は前者を切っていないと意味がない**（SDL_hints.h にそう書いてある）。

`Player::SetAudioSuspended()` は要らなくなったので消した。

### メインループ — 描かないが、演奏の面倒は見る

バックグラウンドの分岐は今まで「寝るだけ」だったのを、次を回すようにした。

- `PollSong()` … 読み終わった曲を演奏へ渡す（自動送りの着地点）
- `PollNotifyRequests()` … 通知やメディアキーからの操作
- `visualizer.Consume()` … **描かないがイベントは食べる。**ためると
  64K のキューが溢れ、Push が失敗する側＝**新しいものが捨てられる**ので、
  前面へ戻ったときの描き直しの指示まで消える
- `PollSongEnd()` … CONT / REPEAT。**前面のフレーム末尾にあった処理を
  そのまま関数へ出して共用する**（曲送りの条件を二重に持たないため）
- `UpdateNowPlaying()` … 通知の中身

間隔は `kBackgroundTickMs = 100`（`SDL_WaitEventTimeout(NULL, 100)`）。
NULL を渡すとイベントは**取り出さずに覗くだけ**なので、次の周回の
`SDL_PollEvent` がそのまま拾う。

### 前面サービスと通知

**前面サービスが立っていないと、画面が消えたあとに OS がプロセスを止める。**
演奏しているあいだだけ走らせる。

- `android/.../PlaybackService.java` … 通知・MediaSession・ウェイクロック・
  オーディオフォーカス・ヘッドホンが抜けたときの受け口
- `android/.../PlaybackBridge.java` … ネイティブとの窓口（状態を預かる／
  操作を待ち行列に積む）
- `src/nowplaying.{h,cpp}` … その JNI 側。**Android 以外では何もしない**

決めたこと:

- **文言は Java 側に持たせない。** `message.ini` の `[Notify]` から引いた
  ものを `SetLabels()` で渡す（`strings.xml` に日本語を置かない）。
- 通知を出すのは `playing && player.playing()` のあいだだけ。[■] で
  止めたときと、演奏し終えて次が無いときは通知ごと消える。
- ボタンは [前の曲] [一時停止 / 再開] [次の曲] [停止]。畳んだときに
  見えるのは先頭 3 つ (`setShowActionsInCompactView`)。
- `Notification.MediaStyle` + `MediaSession` なので、ロック画面や
  Bluetooth のリモコンからも同じ操作ができる。**枠組みだけで済むよう
  androidx は使っていない**（この APK は依存ライブラリ無しのまま）。
- 小さいアイコンは白抜き単色で描かれるので、アプリのアイコンではなく
  OS の `ic_media_play` / `ic_media_pause` を使う。
- オーディオフォーカスを失ったら止める（ダッキングはしない。MDX は
  音量を絞ると聴き取れない）。**自分で止めた曲を勝手に鳴らさない**ように、
  「フォーカスを失って止めた」ときだけ印を立て、返ってきたらその印の
  ぶんだけ再開する（`pausedByFocus`）。
- 演奏中は `PARTIAL_WAKE_LOCK` を持つ。画面が消えてもデコードスレッドを
  回し続けるため。

**前面サービスはアプリが前面にいるあいだしか起こせない**（Android 12 以降）。
演奏を始めるのは画面を見ているときなので、ふつうはそこで起きる。曲が
変わるだけならサービスは動いたままなので、バックグラウンドでの自動送りでも
起こし直しは要らない。起こせなかったときは logcat に警告を出して**演奏だけ
続ける**（通知は次に前面へ戻ったときに出る）。

マニフェストに足したもの: `FOREGROUND_SERVICE` /
`FOREGROUND_SERVICE_MEDIA_PLAYBACK` / `POST_NOTIFICATIONS` / `WAKE_LOCK` と、
`android:foregroundServiceType="mediaPlayback"` の `<service>`。
Android 13 以降は通知に実行時の許可が要るので、`MainActivity.onCreate` で
一度だけ尋ねる（断られても演奏は続くので結果は見ない）。

### バッファ（2026-09-07、ユーザーの報告「ノイズが入る・テンポが不安定」）

音が途切れる（デコードが間に合わず無音を差し込む）と、雑音とテンポの乱れに
なって聞こえる。**前面にいないアプリのスレッドは前面のときほど優先されない**
ので、前面と同じ薄さのバッファでは足りない。守るところが違う 3 つを直した。

1. **リングバッファ（デコード → 装置）を 43ms → 250ms**。ブロック数ではなく
   **時間で決める**ようにした（`Player::Open` が `kRingMs` と出力レートから
   ブロック数を計算する。`Config::numAudioBlocks` は下限になった）。
   デコードスレッドが待たされたときの余裕。
   **Android だけ**——パソコンでは今までどおり浅い（`kRingMs = 0`）。深くすると
   音量・チャンネルマスク・一時停止が音に届くのがそのぶん遅れるので、
   掴んで動かす音量バーがある環境では浅いほうがよい。
2. **装置のバッファ (`want.samples`) を 512 → 2048 フレーム**（42.7ms。Android
   だけ）。**リングバッファでは守れない**「SDL のオーディオスレッドが装置へ
   渡すのが遅れたとき」のための余裕。表示の遅れは自動でこの長さに合わせるので、
   音と画面はずれない。
3. **デコードスレッドの優先度を上げる**。`setpriority(PRIO_PROCESS, 0, -16)`
   ＝ Android の `THREAD_PRIORITY_AUDIO`（SDL 自身もオーディオスレッドに
   これを与えている）。**`SDL_SetThreadPriority` は Android では効かない**
   ——SDL_platform.h が `__LINUX__` を undef するので、`SCHED_OTHER` のまま
   `pthread_setschedparam` を呼ぶだけの経路に落ちる。
   確認は `/proc/<pid>/task/*/stat` の 19 番目（nice）。`mxv2Decode` が
   `-16` になっていればよい。

合わせて、**音が途切れたら logcat に出す**ようにした（`PollUnderruns`。
終了時のまとめだけでは、鳴らしっぱなしのバックグラウンド演奏で気付けない）。
5 秒に 1 回まで `warning  : audio underrun xN`。

**この環境では再現できなかった**（画面を消して 120 秒、直す前の設定でも
アンダーラン 0）。ユーザーの環境（他のアプリが動いている・発熱・長時間）で
出たものなので、**次に出たら logcat の `audio underrun` を見ること**。

### AAudio へ切り替えた（2026-09-07、同じ報告の続き）

上の 3 つを入れてもユーザーから「他のアプリを操作しているとまだ相当出ている」
との報告。**装置側の作りを調べたら、根っこはこちらだった。**

`adb shell dumpsys media.audio_flinger` の Tracks の行を見ると、mxv2 の
トラックが **`F1`＝fast track、`FrmCnt=256`（5.3ms）** になっていた。
SDL は Android で **OpenSL ES を先に選ぶ**（`SDL_audio.c` の bootstrap の
並びが openslES → aaudio）。SDL の OpenSL ES バックエンドは性能モードを
指定しないので、**OpenSL ES の既定＝`SL_ANDROID_PERFORMANCE_LATENCY`
（低遅延＝fast track）**になる。**こちらが `want.samples` をいくら大きく
しても、AudioFlinger 側のバッファは 256 フレームのまま**で、5ms ごとに
渡し続けないと途切れる。前面にいないアプリには厳しい。

そこで `SDL_Init` の前に `SDL_SetHint(SDL_HINT_AUDIODRIVER, "aaudio")` で
**AAudio を先に試す**ようにした（`src/main.cpp`）。SDL の AAudio バックエンドも
性能モードを指定しないが、**AAudio の既定は `PERFORMANCE_MODE_NONE`
（deep buffer）**なので、狙いどおり普通のトラックになる。
**AAudio は Android 8 以降**なので、開けなかったら指定を外して開き直す
（`SDL_InitSubSystem(SDL_INIT_AUDIO)` を分けたのはこのため）。
どちらで鳴っているかは起動ログの `audiodrv :` に出す。

実機での違い（Pixel 7a）:

| | OpenSL ES（既定） | AAudio |
|---|---|---|
| AudioFlinger のトラック | `F1` fast track | ふつうのトラック |
| `FrmCnt` | 256 フレーム (5.3ms) | 3848 フレーム (80ms) |
| Latency | 48ms | 170〜196ms |

**測り方**（次に同じ話が来たときのために）:
- 自分のリングバッファの取りこぼしは logcat の `warning  : audio underrun xN`
- **装置側の取りこぼしは `dumpsys media.audio_flinger`** の Tracks の
  `Underruns` 欄。**これは回数ではなくフレーム数**（AudioFlinger の
  `getUnderrunFrames()`）。トラックが消えたあとも "Local log" の
  `AT::remove` の行に最後の値が残る
- システム全体の詰まりは同じ出力の `Fifo frame underruns:` と
  `FastMixer ... underruns=`。**これが増えていたら端末側の問題**で、
  mxv2 だけの話ではない

**このときの端末の状態も記録しておく**: 調べている最中、この Pixel 7a は
**3.1GB の Android の OTA を適用中**（`update_engine` が 53%→98%）で、
Play ストアのアプリ更新も走り、メモリは 7.2/7.5GB 使用・スワップ
3.6/3.8GB 使用・`/data` は 88% だった。ユーザーがノイズを聞いたのは
この最中で、**`Fifo frame underruns` が n=11（最大 1529 フレーム）出て
いた**＝端末全体が音を落としている状態でもあった。mxv2 側の計測
（リング 2 回／装置側 128〜256 フレーム＝ほぼ 1 バッファ）では、
**この負荷でも mxv2 自身の取りこぼしはほとんど無かった**。

### 非力な端末での確認（2026-09-07、Xperia Ace III / A203SO）

Pixel 7a より非力で、画面を消すとさらに性能を落とす端末（Snapdragon 480 /
Android 14）で確認した。**ここで見つかった一番大きい無駄は、演奏でも
描画でもなくメインループの待ち方だった。**

- **`SDL_WaitEventTimeout(NULL, N)` を待ちに使ってはいけない。** 中で
  **1ms ごとに起きて `SDL_PumpEvents` を回す**作りなので、この端末では
  **バックグラウンドで何もしていないのにコアの 38.6%** を食っていた
  （MDX のデコードそのものより重い）。**`SDL_Delay` で 1 回眠るだけ**に
  変えたら **2.7%** になった。イベントは次の周回の `SDL_PollEvent` が
  拾うので取りこぼさない（気付くのが最大 100ms 遅れるだけ）。
- **鳴らし始める前にリングバッファを半分ほど溜める**ようにした
  （`Player::WaitForPrefill`、上限 300ms）。今までは装置を動かした瞬間は
  まだ 1 ブロックも無く、**曲を替えるたびに頭で必ず 1 回途切れて**いた
  （`audio underrun x1` が毎回出ていたのはこれ）。入れたあとは曲の
  切り替えでも 0 になった。

計測（バックグラウンド＋画面消灯、`/proc/<pid>/task/*/stat` の
utime+stime を 30 秒差分）:

| スレッド | 直す前 | 直したあと |
|---|---|---|
| SDLThread（メインループ） | 38.6% | **2.7〜3.1%** |
| mxv2Decode | 11〜37%（曲による） | 同じ（40.7%: PCM8 の重い曲） |
| SDLAudioP2 | 0.2% | 0.6% |

**結果**: 画面を消したまま 10 分以上（CONT で曲をまたいで）演奏して、
自分のリングも装置側 (`Underruns`) も **0**。プロセスの cpuset は
前面サービスのおかげで `/foreground`（`/background` に落とされない）。
AAudio のトラックは `FrmCnt=2886`（60ms）。

### さらに非力な端末での確認（2026-09-07、AQUOS 603SH / Android 8.0 / 32bit）

MSM8952・3GB・**armeabi-v7a**・Android 8.0（API 26）。画面を消すとクロックが
落ちる（cpu0 が 1.52GHz → 0.96GHz、cpu4 が 1.21GHz → 1.09GHz）。

- **`mxv2.abiFilters` に `armeabi-v7a` を足した**（`gradle.properties`）。
  32bit ビルドでは `third_party/portable_mdx` が
  「4294967296 との比較は常に真」の警告を出すが、**third_party は無改変で
  置く方針**なのでそのままにしてある（mxv2 自身の警告はゼロ）。
- **AAudio が無く、OpenSL ES へ落ちた**（`warning  : AAudio is not available ()`
  → `audiodrv : openslES`）。AAudio は API 26 からのはずだが、この端末には
  実体が無い。**用意しておいたフォールバックが実機で効くことを確認できた。**
- そのため AudioFlinger のトラックは **fast track（`fCount=384`＝8ms）**。
  それでも **`UndFrmCnt` は 0 のまま**だった（画面消灯で背面 14 分、
  CONT で 3 曲）。自分のリングのアンダーランも 0。
- CPU（1 コアぶん）: **前面 93.5%**（描画。この端末では重い）→
  **背面 4.1%**。デコードは 38〜48%（曲による）。
  つまり**この端末でも実時間デコードには余裕がある**。
- Android 8.0 の `dumpsys media.audio_flinger` は列の並びが違う。
  取りこぼしの欄は **`UndFrmCnt`**（新しい版の `Underruns` にあたる）。

### 実機で確認したこと（Pixel 7a / Android 14）

- ホームに戻しても `dumpsys audio` の player が `state:started` のまま
  （**以前は `event:paused` になっていた**）
- 通知に曲名・「演奏中」・4 つのボタンが出る。CONT を入れると
  「演奏中  CONT」になる
- バックグラウンドでメディアキーの [次の曲] → 次の MDX が始まり、
  通知の曲名も変わる。[再生/一時停止] → 「一時停止中」とボタンの
  入れ替わり
- **バックグラウンドで CONT / REPEAT が効く**（フェードアウト `F` を
  押してからホームへ戻し、曲が終わったところで次の曲 / 同じ曲が
  始まることを logcat で確認）
- 画面を消しても `state:started` のまま。`dumpsys power` に
  `PARTIAL_WAKE_LOCK 'mxv2:playback'`
- メディアキーの [停止] → サービスも通知も消える
- 前面へ戻すと画面が正しく描き直される（スクリーンショットで確認）
