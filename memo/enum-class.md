# enum → enum class への置き換えの影響調査（2026-09-30）

対象: mxv2 本体（src/）の enum のうち、値が `k` で始まるもの（mxv2 で新しく書いたもの）。
旧 mxv から引き継いだ `DISP_*`（dispqueue.h の `DispCmd`）は対象外。

## 数字

- src/ の enum は 36 個。うち `k` 始まりが 35 個、旧 mxv 由来（PLAIN）は `DispCmd` の 1 個だけ。
- 35 個の値の参照は合計およそ 700 か所。
- CMake は `CMAKE_CXX_STANDARD 11`。enum class 自体は使える（C++11）。

## enum class にすると壊れる使い方（暗黙の int 変換が無くなるため）

enum class は「値の名前が衝突しない」「int と混ざらない」が利点だが、mxv2 の enum の
多くは**意図して int として使っている**。種類ごとに数えた。

### 1. ビットマスク（3 個）— そのままでは使えない

| enum | 使い方 | 影響 |
|---|---|---|
| `Settings::Field`（30 値） | `unsigned dirt \|= kFieldX`、`fields & kFieldX`、`SaveFields(unsigned)` | 97 行。`\|` `&` の演算子オーバーロードを書くか、`unsigned` へのキャストを全部に付ける |
| `FileItemType`（8 値） | `item.type & kFileItemMdx`（type は int） | 10 か所 + 型を int で持っている |
| `ScrollBarFlag`（3 値） | フラグの OR | 6 か所 |

→ ビットマスクは enum class にする利点が無い（値を組み合わせるのが目的で、型で守りたいのは
「どの enum の値か」ではなく「どのビットか」）。**対象から外す**のが妥当。

### 2. 配列の添字・ループ変数・個数（8 個）— キャストが要る

| enum | 使い方 |
|---|---|
| `OpmOpGroup`（7） | `v[kOpmOpDT1MUL] = …`（statuswatch.cpp）、`int op[4][6]` の添字、`kNumOpmOpGroups` を配列の大きさに |
| `OpmGlobalKind`（7） | 同上（`gv[kOpmGlobalNoise]`、`kLabels[kNumOpmGlobalKinds]`） |
| `StatusItem`（38） | `kStatusItemKeys[kNumStatusItems]`、`statusPos[kNumStatusItems][2]`、`for (int i = 0; i < kNumStatusItems…` |
| `ContextMenu::Page`（4） | `pageSize_[page]`、`childWarm_[page_]`、`for (i < kCtxPageCount)`、`switchTo_ = -1` / `animFrom_ = -1` の**番兵** |
| `Tutorial::Step`（12） | `step + 1`（表示番号）、`step_ + 1 >= kNumSteps`、`Key("Title", step)` で `%d` |
| `cast::Quality`（6） | 品質テーブルの添字、ini に int で保存 |
| `FilerSide`（5） | `FilerSideName(int)`、`filerSide` を int で持つ |
| `Settings::OrientationMode` 等 | 下の 3 と同じ |

`for (int i = 0; i < kNum…; i++)` と `array[k…]` は、enum class では `static_cast<int>` か
`+e`（単項プラス）の類が全部に要る。添字用の enum は enum class にすると**読みにくくなる**
（ImGui 自身も `ImGuiCol_` などを plain enum のまま int で使う流儀）。

### 3. int で保持・受け渡ししているもの（12 個）— 型を直せば enum class にできる

| 場所 | 中身 |
|---|---|
| `Settings` | `int orientationMode / touchUi / pipMode / fileListScroll / castQuality` — ini の `GetInt/SetInt` と行き来 |
| `DrawScreen` | `int statusMode_`、`SetStatusMode(int)`、`SetFileListScroll(int)` |
| `Screen` | `int orientLock_`、`SetOrientationLock(int)` |
| `Skin` | `int filerSide` |
| `SettingsWindow` / `SettingsUi` | `int orientation_`、`SetOrientationState(bool, int)` |
| `LatencyDialog` | `int target_`、`Open(ui, int target, …)` |
| `ContextMenu` | `int page_`、`BuildPageItems(ui, int page, …)` |
| `Tutorial` | `SpotFor(int step)` など 5 本 |
| `keybind` | `HandleMouseRequest(int request, …)` |
| `pip` | `SetMode(int)` |
| `appsetup` | `OrientationForMode(int mode, …)` |

これらは「enum を int の箱に入れている」ので、enum class にするなら箱の型も enum に
変える（20 か所ほど）。ini との境界（5 か所）と JNI との境界（3 か所: nowplaying の
`(Request)r`、orientlock / pip の `(jint)`）にはキャストが要る。

### 4. 純粋に「状態の名前」で int と混ざらないもの（12 個）— そのまま enum class にできる

`BlendMode`、`cast::State`、`cast::Event`、`PlayKeyHit`、`ScrollBarHit`、`PlayKeyStatus`、
`FilerOpen`、`MouseRequest`（ただし keybind が int で受ける）、`Captured`、
`nowplaying::Request`（JNI のキャスト 1 か所）、`orientlock::Request`（同）、`AutoPause`、
`Screen::Orientation` / `ScaleMode` / `OrientationLock`、`FolderDialog::Target` / `Return`、
`SettingsUi::Request`、`updatecheck::Status`。

これらは値の参照が `switch` / `==` / 代入だけなので、`enum class X` にして参照を
`X::kFoo` に書き換えれば通る。**ただし得るものは「名前の衝突が起きない」だけ**で、
mxv2 の enum の値は `k<EnumName><Value>` の命名で既に衝突しない（`Request` という enum 名が
3 つあるが、値は `kRequestPlay` / `kRequestSetFolder` … で分かれている）。

## 判断の材料

- **得るもの**: 型の取り違え（別の enum の値を渡す）をコンパイラが弾く。名前空間の汚れが減る。
- **失うもの**: 添字・ループ・ビットマスク・ini/JNI の境界で `static_cast` が増える（見積もり
  150〜200 か所）。`Request` のように同じ enum 名が 3 つあると `nowplaying::Request::kRequestPlay`
  のように長くなる（値名の `kRequest` 接頭辞が冗長になるので、値名も `Play` に縮める
  書き換えが欲しくなり、差分が膨らむ）。
- **取り違えの実害**: 今のコードで「別の enum の値を渡していた」不具合は見つかっていない
  （値名が `k<Enum><Value>` で自己記述的なため）。

## 勧め

- **一律には置き換えない。** 種類 1（ビットマスク）と 2（添字・番兵）は plain enum のままが
  読みやすく、ImGui の流儀とも揃う。
- やるなら **種類 4 の「状態の名前」だけ**（12 個、参照 250 か所ほど）を enum class にし、
  種類 3 の「int の箱」（20 か所）をその enum の型に直す。これで「int に混ぜて使うもの」と
  「型で守るもの」の線が引ける。差分は ±400 行。
- その場合の副作用: `Request` が 3 つあるので、値名を `kRequestPlay` → `Play` のように
  縮めるかどうかを決める必要がある（縮めないと `nowplaying::Request::kRequestPlay`）。
  縮めるなら値の参照 70 か所が変わる。
- 旧 mxv 由来の `DispCmd`（`DISP_*`、62 か所）は指示どおり触らない。

## やる場合の順序

1. 種類 4 のうち JNI/ini に触れない 8 個（BlendMode / cast::State / Event / PlayKeyHit /
   ScrollBarHit / PlayKeyStatus / FilerOpen / Captured / AutoPause / Screen::* / Folder* /
   updatecheck::Status）→ 機械的に `X::kFoo` へ。
2. `MouseRequest` / `SettingsUi::Request` / `nowplaying::Request` / `orientlock::Request`
   → int で受けている関数の型を直し、JNI の境界に `static_cast`。
3. 種類 3 の Settings の 5 つは ini との境界があるので最後。`GetInt` の結果を範囲検査して
   enum にする関数を 1 つ書く。

## 追記（2026-10-01）: 本当の要求は「長い値名を名前空間で分ける」こと

ユーザーの意図は enum class そのものではなく、`kFieldSkin` のような
`k<Enum><Value>` の長い値名を `Field::Skin` の形に分けたい、というもの。方針は

- **int として使うもの**（ビットマスク・添字・番兵・ini/JNI の境界）は struct で包んだ plain enum:
  ```
  struct Field { enum Type { Skin = 1 << 0, … }; };
  unsigned dirt = Field::Skin | Field::Zoom;   // 今までどおり int に混ざる
  ```
- **int が要らないもの**（状態の名前）は enum class:
  ```
  enum class State { Idle, Discovering, … };
  ```

### この方針の影響

**どちらの enum も、値の参照は全部書き換わる**（`kFieldSkin` → `Field::Skin`）。前の見積もりの
「700 か所」はそのまま。ただし中身は**純粋な名前の付け替え**で、書き損じはすべてコンパイラが
弾く（前の 3 段のリファクタと違い、黙って動きが変わる余地が無い）。

| 観点 | struct 包み（plain enum） | enum class |
|---|---|---|
| 対象 | Field / FileItemType / ScrollBarFlag（ビットマスク）、OpmOpGroup / OpmGlobalKind / StatusItem / Page / Step / Quality / FilerSide（添字・番兵）、Settings の OrientationMode / TouchUi / PipMode / FileListScroll（ini に int で保存）＝ **15 個** | BlendMode、cast::State / Event、PlayKeyHit / ScrollBarHit / PlayKeyStatus / StatusMode、FilerOpen、MouseRequest、Captured、nowplaying::Request、orientlock::Request、AutoPause、Screen::Orientation / ScaleMode / OrientationLock、FolderDialog::Target / Return、LatencyDialog::Target、SettingsUi::Request、updatecheck::Status ＝ **20 個** |
| 値の書き換え | `kFieldSkin` → `Field::Skin` | `kIdle` → `State::Idle` |
| 型名 | **`Field::Type` になる**（`Field` は struct なので）。型として書いている所（`static const StatusItem kItems[]`、`FilerSide side` など 20〜30 か所）は `StatusItem::Type` に | `State` のまま |
| int の箱（`int page_; // Page`） | そのままでよい（int に入る） | **箱の型を enum に直す**（`int orientation_` → `Screen::Orientation`、`int target` 引数など 12 か所） |
| ビットマスク / 添字 / `-1` の番兵 / `step + 1` | そのまま通る | （対象にしない） |
| ini の GetInt/SetInt | そのまま通る | （対象にしない） |
| JNI の `(jint)` / `(Request)r` | — | `static_cast<int>` / `static_cast<Request>` に（3 か所） |
| 前方宣言 | `X::Type` は struct の定義が要る（`struct X;` だけでは書けない） | `enum class X;` で可（C++11） |
| クラスの中の enum | `Settings::Field::Skin`、型は `Settings::Field::Type`（3 段になる） | `FolderDialog::Target::Pdx`（2 段） |
| 同名の `Request` 3 つ | — | `nowplaying::Request::Play` / `SettingsUi::Request::Quit` と自然に分かれる。**前の見積もりで気にした「kRequest の二重」は消える** |
| printf / MsgNum の `%d` | そのまま | enum class を可変長引数に渡すと未定義。`(int)kNumSteps` のように今は全部キャスト済み。書き換え時に 1 度 grep で確かめる |
| ImGui の `RadioButton(&int, value)` | そのまま（Settings の 4 つはこちら） | （対象にしない） |

### 差分の見込み

値の参照 700 + 宣言 36 + 型名 30 + int の箱 12 + JNI 3 ＝ **±800 行**。機械的に当てられる
（`k<Enum><Value>` → `<Enum>::<Value>` は正規表現で一律）。動作の変更は無い。

### 決めるところ

1. **struct の名前**: ユーザーの例は `kField`（`k` 付き）。mxv2 の流儀では `k` は定数の接頭辞で、
   型は `Field` のように大文字始まり。`Field::Skin` のほうが「型::値」に見えるので、**struct は
   `Field`、中の enum は `Type`** を勧める（`kField::Skin` だと struct が定数に見える）。
2. **`Type` の名前**: `Field::Type` / `StatusItem::Type` で統一。`Enum` や `Value` でもよいが 1 つに。
3. **クラスの中の enum を外へ出すか**: `Settings::Field::Type` の 3 段を嫌うなら `Field` を
   `Settings` の外（namespace mxv2 直下）に出す。ただし `Settings::kMaxBookmarks` のような
   他の定数と場所が分かれる。**中に残す**ほうが「どのクラスの enum か」が読めるので勧める。
4. enum class 側の値名: `k` を落として `State::Idle`。落とさず `State::kIdle` にする手もあるが、
   スコープで分かれるので `k` は要らない（ユーザーの例もそう）。

### 順序

1. enum class 側の 20 個（int に混ざらないので、書き換え漏れがコンパイルエラーで全部出る）。
   JNI の 3 か所と int の箱 12 か所もこのとき。
2. struct 包みの 15 個。型名 `X::Type` の書き換えを伴う。
3. `DispCmd`（`DISP_*`）は触らない。

## 実施（2026-10-01）

上の方針で当てた。値の参照 617 行 + 型の直しで、差分は src の 50 ファイル・±700 行。
Windows Debug / Android release ともビルド通過。Windows で曲を鳴らして一時停止・再生・
CONT・REPEAT・次／前の曲・スクロールバーの矢印・停止をマウスで叩き（ヒットコードの
int 変換を通る所）、Pixel 7a でメニューと設定を開いて確認。

### 方針からのずれ

- **BlendMode / PlayKeyHit / ScrollBarHit は enum class でなく struct 包みに**した。
  BlendMode は合成の表の添字（`mode - Sub`）、ヒットコードは `capturedHit_`（PlayKeyHit と
  ScrollBarHit のどちらも入る int）と `1u << (hit - 1)` のビット位置に使っていた。
  「int が要るものは struct」の規則どおり。
- Tutorial::Step も struct 包み（`step + 1` と添字）。
- enum class にした 17 個の「int の箱」は型を直した: cast の `std::atomic<State>`、
  `DrawScreen::statusMode_` / `SetStatusMode`、`MouseInput::captured_` とその局所コピー、
  `HandleMouseRequest(MouseRequest)`、`SettingsUi::SetOrientationState(bool, Screen::Orientation)`
  と `SettingsWindow::orientation_`、`Screen::orientLock_` / `SetOrientationLock` /
  `orientationLock()`、`LatencyDialog::target_` / `Open(…, Target, …)`。
  `main.cpp` のコマンドライン（int）→ `SetOrientationLock` は static_cast。
- JNI の境界: nowplaying の `takeRequest` の戻りは jint の範囲を見てから static_cast、
  orientlock の `(jint)request` は static_cast。Java 側の定数（REQ_*）は値が同じなので不変。
- StatusItem を型として書いていた 8 か所は `StatusItem::Type`。
- keybind.h が `MouseRequest` を型として受けるようになったので mouse.h を include。

### Field のビット（ユーザーの質問への答え）

`Settings::Field` は 30 ビット使っていて `unsigned` に残り 2。増えたら
`enum Type : uint64_t { … = 1ull << 32 }` にして、マスクを持つ `unsigned` を `uint64_t` に
する（`SaveFields(unsigned)`、`changedFields_`、main.cpp の `dirt` / `dirtyFields` の 4 か所）。
struct 包みの plain enum なので、値名側は何も変わらない。ini の [Section] ごとにまとめる
案（[Play] 全体で 1 ビット）もあるが、「触った項目だけ書き戻す」粒度が粗くなるので、
先に 64 ビット化のほうがよい。
