// mxv2_uitest - 設定 UI のダイアログの行き来を、本物のアプリで確かめる（uitest.h）
//
// 手順（Step）を 1 フレームに 1 つずつ進める。どの手順も「済んだ / 続ける /
// 失敗」を返し、待つ手順は kWaitFrames を過ぎたら失敗にする。
//
// 見るのは「いま開いているポップアップの題名の並び」（ImGui の OpenPopupStack）。
// ダイアログはすべてモーダルなので、この並びがそのまま画面の状態になる。
// 往復の途中では 1〜2 フレームだけ何も開いていない瞬間があるので、待つ手順は
// 同じ並びが kStableFrames 続いたときだけ「済んだ」にする（一瞬だけ合って、
// そのあと別のものが開き直す、という誤りを見逃さないため）。

#include "uitest.h"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "cast.h"
#include "fileutil.h"
#include "message.h"
#include "settingsui.h"
#include "settingsui_internal.h"

// ---------------------------------------------------------------------------
// ImGui のテストエンジン用のフック（imgui_internal.h が宣言している）。
// 部品が置かれるたびに ItemAdd が、見えている部品には続けて ItemInfo が呼ばれる。
// 1 フレームぶん集めて、次の AfterBuild で使う。
// ---------------------------------------------------------------------------
namespace {

struct Item {
	ImGuiWindow *root;  // 載っているウィンドウの根（ポップアップならそのもの）
	ImRect rect;
	ImRect clip;        // 置かれたときの切り抜き（スクロールで隠れたぶんを除くため）
	std::string label;  // ItemInfo が来た（＝見えている）部品だけ入る
	ImGuiItemStatusFlags flags;
	Item() : root(0), flags(0) {}
};

std::map<ImGuiID, Item> gCollecting;  // 組み立て中のフレーム
std::map<ImGuiID, Item> gItems;       // 直前に組み上がったフレーム

// いま探している名札。ItemInfo を出さない部品がある（1.92.4 の BeginCombo）ので、
// ItemAdd の時点で「この名札ならこの id」を計算して突き合わせる。部品は
// window->GetID(label) で id を作るので、同じ ID スタックの上で計算すれば一致する。
std::string gWatchLabel;

}  // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb,
                                 const ImGuiLastItemData *) {
	ImGuiWindow *w = ctx->CurrentWindow;
	Item &it = gCollecting[id];
	it.root = (w != 0) ? w->RootWindow : 0;
	it.rect = bb;
	it.clip = (w != 0) ? w->ClipRect : bb;
	// ウィンドウそのものの登録のときは ID スタックが空にしてある（imgui.cpp）。
	if (w != 0 && w->IDStack.Size > 0 && !gWatchLabel.empty() &&
	    id == w->GetID(gWatchLabel.c_str())) {
		it.label = gWatchLabel;
	}
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *, ImGuiID id, const char *label,
                                  ImGuiItemStatusFlags flags) {
	std::map<ImGuiID, Item>::iterator i = gCollecting.find(id);
	if (i == gCollecting.end() || label == 0) return;
	i->second.label = label;
	i->second.flags = flags;
}

void ImGuiTestEngineHook_Log(ImGuiContext *, const char *, ...) {}

const char *ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *, ImGuiID id) {
	std::map<ImGuiID, Item>::const_iterator i = gItems.find(id);
	return (i != gItems.end() && !i->second.label.empty()) ? i->second.label.c_str() : 0;
}

namespace mxv2 {
namespace uitest {
namespace {

using namespace settingsui;

const int kWaitFrames = 120;   // 待つ手順の上限
const int kStableFrames = 3;   // 待つ手順は、この数だけ続けて合ったら済み
const int kMaxFrames = 6000;   // 全体の上限（どこかで回り続けたとき用）

struct Env {
	SettingsUi *ui;
	SDL_Window *window;
};

// 手順。tick は 1 = 済んだ / 0 = 続ける / -1 = 失敗（理由は gFailure）。
// frame はその手順の中で何フレーム目か（0 から）。
struct Step {
	std::string name;
	std::function<int(Env &, int)> tick;
};

std::vector<Step> gSteps;
size_t gCurrent = 0;
int gStepFrame = 0;
int gFrame = 0;
bool gStarted = false;
bool gDone = false;
bool gFailed = false;
std::string gFailure;

// 題名の変数（kSettingsTitle など）。言語を替えると指す先が変わるので、
// 変数の番地で持って、使うたびに読む。
typedef const char *const *Title;

typedef std::function<std::string()> Label;

Label MsgLabel(const char *key) {
	return [key]() { return std::string(Msg(key)); };
}
Label IdLabel(const char *key, const char *id) {
	return [key, id]() { return L(key, id); };
}

// 開いているポップアップのウィンドウを、外側から順に。開いた直後でまだ
// ウィンドウが無いものがあれば false（次のフレームで決まる）。
bool OpenPopups(std::vector<ImGuiWindow *> *out) {
	ImGuiContext &g = *GImGui;
	out->clear();
	for (int i = 0; i < g.OpenPopupStack.Size; i++) {
		ImGuiWindow *w = g.OpenPopupStack[i].Window;
		if (w == 0) return false;
		out->push_back(w);
	}
	return true;
}

// 失敗したときの「いま開いているもの」。ImGui は "###" で id を共有する
// ウィンドウの名前を最初のまま持っている（題名が替わっても書き換えない。
// 題名の帯には毎フレーム渡された題名を描く）ので、フォルダ選択の 3 種類は
// どれも最初に開いたものの名前で出る。
std::string DescribePopups() {
	std::vector<ImGuiWindow *> now;
	if (!OpenPopups(&now)) return "(opening)";
	std::string s = "[";
	for (size_t i = 0; i < now.size(); i++) {
		if (i != 0) s += ", ";
		s += "\"" + std::string(now[i]->Name) + "\"";
	}
	return s + "]";
}

ImGuiWindow *TopPopup() {
	ImGuiContext &g = *GImGui;
	if (g.OpenPopupStack.Size == 0) return 0;
	ImGuiWindow *w = g.OpenPopupStack.back().Window;
	return (w != 0) ? w->RootWindow : 0;
}

void PushKey(SDL_Window *window, SDL_Keycode sym, Uint16 mod) {
	SDL_Event ev;
	SDL_zero(ev);
	ev.type = SDL_KEYDOWN;
	ev.key.windowID = SDL_GetWindowID(window);
	ev.key.state = SDL_PRESSED;
	ev.key.keysym.sym = sym;
	ev.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
	ev.key.keysym.mod = mod;
	SDL_PushEvent(&ev);
	ev.type = SDL_KEYUP;
	ev.key.state = SDL_RELEASED;
	SDL_PushEvent(&ev);
}

// ---- 手順を作る ---------------------------------------------------------

// キーを 1 回押す。SDL のイベントとして積むので、本物と同じく main の
// keybind（と ESC なら SettingsUi::CloseDialog）を通る。
Step Key(SDL_Keycode sym, const char *name, Uint16 mod = KMOD_NONE) {
	Step s;
	s.name = std::string("key ") + name;
	s.tick = [sym, mod](Env &env, int) {
		PushKey(env.window, sym, mod);
		return 1;
	};
	return s;
}

// SettingsUi の公開 API を直に呼ぶ（main が呼ぶものの代わり。起動時の警告・
// 更新チェックの結果・渡されたファイル・バナーからのメニュー）。
Step Call(const std::string &name, std::function<void(Env &)> fn) {
	Step s;
	s.name = "call " + name;
	s.tick = [fn](Env &env, int) {
		fn(env);
		return 1;
	};
	return s;
}

// 入力欄へ文字を打つ。先に Click で入力欄を掴んでおくこと。
Step Type(const std::string &name, std::function<std::string(Env &)> text) {
	Step s;
	s.name = "type " + name;
	s.tick = [text](Env &env, int) {
		ImGui::GetIO().AddInputCharactersUTF8(text(env).c_str());
		return 1;
	};
	return s;
}

// n フレーム待つ（メニューのページ送りのように、動きが収まるのを待つとき）。
Step Frames(int n) {
	Step s;
	s.name = "wait frames";
	s.tick = [n](Env &, int frame) { return (frame + 1 >= n) ? 1 : 0; };
	return s;
}

// pred が kStableFrames 続けて true になるまで待つ。describe は失敗したときの
// 「いまどうなっているか」。
Step WaitUntil(const std::string &name, std::function<bool(Env &)> pred,
               std::function<std::string(Env &)> describe) {
	std::shared_ptr<int> run(new int(0));
	Step s;
	s.name = name;
	s.tick = [pred, describe, run, name](Env &env, int frame) {
		if (frame == 0) *run = 0;
		*run = pred(env) ? *run + 1 : 0;
		if (*run >= kStableFrames) return 1;
		if (frame >= kWaitFrames) {
			gFailure = name + ": now " + describe(env);
			return -1;
		}
		return 0;
	};
	return s;
}

// ウィンドウは名前でなく id で比べる（上の DescribePopups の注記）。id は
// 題名から決まる（"###" があればそこから後ろだけ）ので、PDX とブックマークの
// フォルダ選択は同じウィンドウとして扱われる。どちらが開いたかは、閉じた
// あとの戻り先で見分ける。
bool PopupsAre(const std::vector<Title> &want) {
	std::vector<ImGuiWindow *> now;
	if (!OpenPopups(&now) || now.size() != want.size()) return false;
	for (size_t i = 0; i < want.size(); i++) {
		if (*want[i] == 0 || now[i]->ID != ImHashStr(*want[i])) return false;
	}
	return true;
}

// 開いているポップアップが、外側から順にちょうど want になるまで待つ。
Step WaitPopups(const std::string &name, const std::vector<Title> &want) {
	return WaitUntil(
	    "popups " + name, [want](Env &) { return PopupsAre(want); },
	    [](Env &) { return DescribePopups(); });
}

// 何も開いていない（SettingsUi から見ても）状態になるまで待つ。
Step WaitNothingOpen() {
	return WaitUntil(
	    "nothing open",
	    [](Env &env) { return PopupsAre(std::vector<Title>()) && !env.ui->anyDialogOpen(); },
	    [](Env &env) {
		    return DescribePopups() + (env.ui->anyDialogOpen() ? " anyDialogOpen" : "");
	    });
}

// いちばん上のポップアップの中で、名札が label の部品を押す。見えていなければ
// そのポップアップ（と中の子ウィンドウ）を上から順にスクロールして探す。
// onlyIfClosed なら、開け閉めする部品（見出し）が既に開いていれば押さない。
Step Click(const std::string &name, Label label, bool onlyIfClosed = false) {
	struct State {
		int phase;  // 0 = 探す / 1 = 押す / 2 = 離す
		bool rewound;
	};
	std::shared_ptr<State> st(new State());
	Step s;
	s.name = (onlyIfClosed ? "open " : "click ") + name;
	s.tick = [label, onlyIfClosed, st](Env &, int frame) {
		ImGuiIO &io = ImGui::GetIO();
		if (frame == 0) {
			st->phase = 0;
			st->rewound = false;
		}
		if (st->phase == 1) {
			io.AddMouseButtonEvent(0, true);
			st->phase = 2;
			return 0;
		}
		if (st->phase == 2) {
			io.AddMouseButtonEvent(0, false);
			return 1;
		}

		ImGuiWindow *top = TopPopup();
		const std::string want = label();
		gWatchLabel = want;
		if (top != 0) {
			for (std::map<ImGuiID, Item>::const_iterator i = gItems.begin(); i != gItems.end();
			     ++i) {
				const Item &it = i->second;
				if (it.root != top || it.label != want) continue;
				ImRect vis = it.rect;
				vis.ClipWith(it.clip);
				if (vis.GetWidth() <= 0.0f || vis.GetHeight() < it.rect.GetHeight() * 0.5f) {
					continue;
				}
				if (onlyIfClosed && (it.flags & ImGuiItemStatusFlags_Opened) != 0) return 1;
				io.AddMousePosEvent(vis.GetCenter().x, vis.GetCenter().y);
				st->phase = 1;
				return 0;
			}

			// 見えていない。まず上端へ戻し、そこから半画面ずつ下へ。
			ImGuiContext &g = *GImGui;
			for (int i = 0; i < g.Windows.Size; i++) {
				ImGuiWindow *w = g.Windows[i];
				if (!w->Active || w->RootWindow != top || w->ScrollMax.y <= 0.0f) continue;
				if (!st->rewound) {
					ImGui::SetScrollY(w, 0.0f);
				} else if (w->Scroll.y < w->ScrollMax.y) {
					ImGui::SetScrollY(w, w->Scroll.y + w->InnerRect.GetHeight() * 0.5f);
				}
			}
			st->rewound = true;
		}
		if (frame >= kWaitFrames) {
			// 見えていた名札を並べておく（綴りの食い違いを見つけやすいように）。
			std::string seen;
			for (std::map<ImGuiID, Item>::const_iterator i = gItems.begin(); i != gItems.end();
			     ++i) {
				if (i->second.root != top || i->second.label.empty()) continue;
				seen += " \"" + i->second.label + "\"";
			}
			gFailure = "no item \"" + want + "\" in " + DescribePopups() + "; seen:" + seen;
			return -1;
		}
		return 0;
	};
	return s;
}

// ---- 筋書き -------------------------------------------------------------
// ini は Makefile の test-ui が用意する（PDX の探索先 1 件、画面の遅れは手動、
// ブックマークは初回の既定 "assets:" 1 件）。スキンは assets:Default で、
// ユーザーフォルダに空の skin/Default を置いてある（[配色設定] の [保存] で
// 上書きの確認を出すため。確認は必ず取り消すので、中身は書かれない）。

// 待つ並びを短く書く。
std::vector<Title> Popups(Title a, Title b = 0) {
	std::vector<Title> v(1, a);
	if (b != 0) v.push_back(b);
	return v;
}

void BuildScenario() {
	std::vector<Step> &s = gSteps;
	std::vector<Title> settings(1, &kSettingsTitle);
	std::vector<Title> pdx(1, &kPdxPathsTitle);
	std::vector<Title> pdxRemove;
	pdxRemove.push_back(&kPdxPathsTitle);
	pdxRemove.push_back(&kPdxRemoveTitle);
	std::vector<Title> pdxFolder(1, &kPdxFolderTitle);
	std::vector<Title> latency(1, &kDisplayLatencyTitle);
	std::vector<Title> bm(1, &kBookmarksTitle);
	std::vector<Title> bmRemove;
	bmRemove.push_back(&kBookmarksTitle);
	bmRemove.push_back(&kBmRemoveTitle);
	std::vector<Title> bmFolder(1, &kBookmarkFolderTitle);

	// 起動時の警告（main が SetStartupWarnings で渡すもの）→ [閉じる]。
	s.push_back(Call("SetStartupWarnings", [](Env &env) {
		std::vector<SettingsUi::StartupWarning> w(1);
		w[0].text = "uitest";
		env.ui->SetStartupWarnings(w);
	}));
	s.push_back(WaitPopups("startup", Popups(&kStartupTitle)));
	s.push_back(Click("[Close] (startup)", MsgLabel("Button.Close")));
	s.push_back(WaitNothingOpen());

	// 設定 → [PDX の探索先] → 削除の確認（ESC で 1 段だけ閉じる）→ [追加…] の
	// フォルダ選択 → ESC で PDX へ戻る → ESC で設定へ戻る → ESC で閉じる。
	s.push_back(Key(SDLK_F1, "F1"));
	s.push_back(WaitPopups("settings", settings));
	s.push_back(Click("[Play] header", MsgLabel("Settings.Play"), true));
	s.push_back(Click("[Edit] (PDX)", MsgLabel("Button.Edit")));
	s.push_back(WaitPopups("pdx", pdx));
	s.push_back(Click("[Remove] (PDX)", MsgLabel("Button.Remove")));
	s.push_back(WaitPopups("pdx > remove", pdxRemove));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("pdx", pdx));
	s.push_back(Click("[Add] (PDX)", MsgLabel("Button.AddPdxPath")));
	s.push_back(WaitPopups("pdx folder", pdxFolder));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("back to pdx", pdx));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("back to settings", settings));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// 設定 → [画面の遅れ] → [閉じる] で設定へ戻る → もう一度 → ESC で戻る。
	s.push_back(Key(SDLK_F1, "F1"));
	s.push_back(WaitPopups("settings", settings));
	s.push_back(Click("[Play] header", MsgLabel("Settings.Play"), true));
	s.push_back(Click("[Setup] (latency)", IdLabel("Button.Setup", "##displaylatency")));
	s.push_back(WaitPopups("latency", latency));
	s.push_back(Click("[Close] (latency)", MsgLabel("Button.Close")));
	s.push_back(WaitPopups("back to settings", settings));
	s.push_back(Click("[Setup] (latency)", IdLabel("Button.Setup", "##displaylatency")));
	s.push_back(WaitPopups("latency", latency));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("back to settings", settings));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// [ブックマークの設定] → [追加] のフォルダ選択 → ESC で戻る → 削除の確認 →
	// [キャンセル] → もう一度 → ESC（入れ子だけ閉じる）→ ESC で閉じる。
	s.push_back(Key(SDLK_F4, "F4"));
	s.push_back(WaitPopups("bookmarks", bm));
	s.push_back(Click("[Add] (bookmark)", MsgLabel("Button.AddBookmark")));
	s.push_back(WaitPopups("bookmark folder", bmFolder));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("back to bookmarks", bm));
	s.push_back(Click("[Remove] (bookmark)", MsgLabel("Button.Remove")));
	s.push_back(WaitPopups("bookmarks > remove", bmRemove));
	s.push_back(Click("[Cancel]", MsgLabel("Button.Cancel")));
	s.push_back(WaitPopups("bookmarks", bm));
	s.push_back(Click("[Remove] (bookmark)", MsgLabel("Button.Remove")));
	s.push_back(WaitPopups("bookmarks > remove", bmRemove));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("bookmarks", bm));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// ---- 行き来の無い（入れ子だけの）ダイアログ -------------------------

	// [配色設定] (F2) → [保存] で上書きの確認 → ESC で確認だけ閉じる → もう一度 →
	// [キャンセル] → ESC で閉じる。
	s.push_back(Key(SDLK_F2, "F2"));
	s.push_back(WaitPopups("colors", Popups(&kColorsTitle)));
	s.push_back(Click("[Save] (colors)", MsgLabel("Button.Save")));
	s.push_back(WaitPopups("colors > overwrite", Popups(&kColorsTitle, &kOverwriteTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("colors", Popups(&kColorsTitle)));
	s.push_back(Click("[Save] (colors)", MsgLabel("Button.Save")));
	s.push_back(WaitPopups("colors > overwrite", Popups(&kColorsTitle, &kOverwriteTitle)));
	s.push_back(Click("[Cancel]", MsgLabel("Button.Cancel")));
	s.push_back(WaitPopups("colors", Popups(&kColorsTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// [ファイルシステムの設定] (F3) → [追加…] を ESC で閉じる → もう一度開いて
	// 実在のフォルダ（この実行ファイルの場所）を打ち込んで [追加] → 足した行
	// （選んでいた先頭に入る）の [削除] で確認 → ESC → もう一度 → 確認の [削除]
	// で本当に外す → ESC で閉じる。ini は使い捨ての -userdir のもの。
	s.push_back(Key(SDLK_F3, "F3"));
	s.push_back(WaitPopups("filesystems", Popups(&kFileSystemsTitle)));
	s.push_back(Click("[Add] (fs)", MsgLabel("Button.AddFs")));
	s.push_back(WaitPopups("filesystems > add", Popups(&kFileSystemsTitle, &kAddFsTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("filesystems", Popups(&kFileSystemsTitle)));
	s.push_back(Click("[Add] (fs)", MsgLabel("Button.AddFs")));
	s.push_back(WaitPopups("filesystems > add", Popups(&kFileSystemsTitle, &kAddFsTitle)));
	s.push_back(Click("path field", []() { return std::string("##addfspath"); }));
	s.push_back(Type("executable dir", [](Env &) { return ExecutableDir(); }));
	s.push_back(Click("[Add] (fs > add)", MsgLabel("Button.Add")));
	s.push_back(WaitPopups("filesystems (added)", Popups(&kFileSystemsTitle)));
	s.push_back(Click("[Remove] (fs)", MsgLabel("Button.Remove")));
	s.push_back(WaitPopups("filesystems > remove", Popups(&kFileSystemsTitle, &kFsRemoveTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("filesystems", Popups(&kFileSystemsTitle)));
	s.push_back(Click("[Remove] (fs)", MsgLabel("Button.Remove")));
	s.push_back(WaitPopups("filesystems > remove", Popups(&kFileSystemsTitle, &kFsRemoveTitle)));
	s.push_back(Click("[Remove] (fs > remove)", MsgLabel("Button.Remove")));
	s.push_back(WaitPopups("filesystems (removed)", Popups(&kFileSystemsTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// フォルダを選ぶ (L)。ファイラーを動かすほう。
	s.push_back(Key(SDLK_l, "L"));
	s.push_back(WaitPopups("folder", Popups(&kFolderTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// Shift+M。今の場所（初回は assets:、既定のブックマークにある）を外すかの確認。
	s.push_back(Key(SDLK_m, "Shift+M", KMOD_LSHIFT));
	s.push_back(WaitPopups("bookmark toggle", Popups(&kBmToggleTitle)));
	s.push_back(Click("[Cancel]", MsgLabel("Button.Cancel")));
	s.push_back(WaitNothingOpen());
	s.push_back(Key(SDLK_m, "Shift+M", KMOD_LSHIFT));
	s.push_back(WaitPopups("bookmark toggle", Popups(&kBmToggleTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// [操作方法] (F11 / H) と [バージョン情報] (F12 / A)。
	s.push_back(Key(SDLK_F11, "F11"));
	s.push_back(WaitPopups("help", Popups(&kHelpTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());
	s.push_back(Key(SDLK_h, "H"));
	s.push_back(WaitPopups("help", Popups(&kHelpTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());
	s.push_back(Key(SDLK_F12, "F12"));
	s.push_back(WaitPopups("about", Popups(&kAboutTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());
	s.push_back(Key(SDLK_a, "A"));
	s.push_back(WaitPopups("about", Popups(&kAboutTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// 終了の確認。何も開いていないときの ESC と Q で出る。[終了] は押さない。
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("quit", Popups(&kQuitTitle)));
	s.push_back(Click("[Cancel]", MsgLabel("Button.Cancel")));
	s.push_back(WaitNothingOpen());
	s.push_back(Key(SDLK_q, "Q"));
	s.push_back(WaitPopups("quit", Popups(&kQuitTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// [キャスト] (F9)。組み込んでいないビルドでは F9 が効かないので飛ばす。
	if (cast::Available()) {
		s.push_back(Key(SDLK_F9, "F9"));
		s.push_back(WaitPopups("cast", Popups(&kCastTitle)));
		s.push_back(Click("[Close] (cast)", MsgLabel("Button.Close")));
		s.push_back(WaitNothingOpen());
	}

	// ---- メインループが開くもの ------------------------------------------

	// 更新チェックの結果。何も開いていなければすぐ出る。
	UpdateResult newer;
	newer.status = UpdateResult::Status::Newer;
	newer.latest = "v9.9.9";
	s.push_back(Call("ShowUpdateResult", [newer](Env &env) { env.ui->ShowUpdateResult(newer); }));
	s.push_back(WaitPopups("update", Popups(&kUpdateTitle)));
	s.push_back(Click("[Close] (update)", MsgLabel("Button.Close")));
	s.push_back(WaitNothingOpen());
	// 設定ウィンドウが開いている間は預かり、閉じたら出る。
	s.push_back(Key(SDLK_F1, "F1"));
	s.push_back(WaitPopups("settings", settings));
	s.push_back(Call("ShowUpdateResult", [newer](Env &env) { env.ui->ShowUpdateResult(newer); }));
	s.push_back(WaitPopups("settings (update held)", settings));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitPopups("update", Popups(&kUpdateTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// 渡されたファイル（Android で外から MDX を渡されたときの問い）。
	s.push_back(Call("OpenHandedChoice", [](Env &env) {
		env.ui->OpenHandedChoice("content://uitest/song.mdx", "song.mdx");
	}));
	s.push_back(WaitPopups("handed", Popups(&kHandedTitle)));
	s.push_back(Click("[Cancel]", MsgLabel("Button.Cancel")));
	s.push_back(WaitNothingOpen());

	// コンテキストメニュー（バナーの長押し・右クリック）。項目から開く / ページを
	// 送ってから開く / ESC で閉じる。
	const std::function<bool(Env &)> menuOpen = [](Env &env) {
		std::vector<ImGuiWindow *> now;
		return env.ui->contextMenuOpen() && OpenPopups(&now) && now.size() == 1;
	};
	const std::function<std::string(Env &)> describe = [](Env &) { return DescribePopups(); };
	s.push_back(Call("OpenContextMenu", [](Env &env) { env.ui->OpenContextMenu(); }));
	s.push_back(WaitUntil("context menu", menuOpen, describe));
	s.push_back(Click("[Settings] (menu)", MsgLabel("Menu.Settings")));
	s.push_back(WaitPopups("settings", settings));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());
	s.push_back(Call("OpenContextMenu", [](Env &env) { env.ui->OpenContextMenu(); }));
	s.push_back(WaitUntil("context menu", menuOpen, describe));
	s.push_back(Click("[Other] (menu)", MsgLabel("Menu.Other")));
	s.push_back(Frames(30));  // ページ送りの動きが収まるまで
	s.push_back(Click("[About] (menu)", MsgLabel("Menu.About")));
	s.push_back(WaitPopups("about", Popups(&kAboutTitle)));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());
	s.push_back(Call("OpenContextMenu", [](Env &env) { env.ui->OpenContextMenu(); }));
	s.push_back(WaitUntil("context menu", menuOpen, describe));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());

	// [言語] を English に替える。設定ウィンドウはいったん閉じ、新しい題名で
	// 開き直る（SettingsWindow::ApplyPendingLocale）。
	std::shared_ptr<std::string> before(new std::string());
	s.push_back(Key(SDLK_F1, "F1"));
	s.push_back(WaitPopups("settings", settings));
	{
		Step remember;
		remember.name = "remember title";
		remember.tick = [before](Env &, int) {
			*before = kSettingsTitle;
			return 1;
		};
		s.push_back(remember);
	}
	s.push_back(Click("[Language] header", MsgLabel("Settings.Language"), true));
	s.push_back(Click("[Language] combo", IdLabel("Settings.Language", "###language")));
	s.push_back(Click("English", []() { return std::string("English"); }));
	s.push_back(WaitUntil(
	    "settings reopened in English",
	    [before, settings](Env &) {
		    return PopupsAre(settings) && *before != kSettingsTitle &&
		           std::string(kSettingsTitle) == Msg("Dialog.Settings");
	    },
	    [before](Env &) {
		    return DescribePopups() + " title \"" + kSettingsTitle + "\" (was \"" + *before +
		           "\")";
	    }));
	s.push_back(Key(SDLK_ESCAPE, "ESC"));
	s.push_back(WaitNothingOpen());
}

void Finish(bool failed) {
	gDone = true;
	gFailed = failed;
	if (failed) {
		printf("uitest: FAIL %s (step %u/%u, frame %d)\n", gFailure.c_str(),
		       (unsigned)gCurrent + 1, (unsigned)gSteps.size(), gFrame);
	} else {
		printf("uitest: all %u steps passed (%d frames)\n", (unsigned)gSteps.size(), gFrame);
	}
}

}  // namespace

bool AfterBuild(SettingsUi *ui, SDL_Window *window) {
	if (!gStarted) {
		gStarted = true;
		BuildScenario();
	}
	// ImGui の作り直し（端末のリセットなど）で消えても戻るよう、毎フレーム立てる。
	GImGui->TestEngineHookItems = true;
	gItems.swap(gCollecting);
	gCollecting.clear();
	if (gDone) return true;

	gFrame++;
	if (gFrame > kMaxFrames) {
		gFailure = "too many frames";
		Finish(true);
		return true;
	}

	Env env;
	env.ui = ui;
	env.window = window;
	Step &step = gSteps[gCurrent];
	const int r = step.tick(env, gStepFrame);
	gStepFrame++;
	if (r < 0) {
		gFailure = step.name + ": " + gFailure;
		Finish(true);
	} else if (r > 0) {
		printf("uitest: ok   %s\n", step.name.c_str());
		gCurrent++;
		gStepFrame = 0;
		if (gCurrent == gSteps.size()) Finish(false);
	}
	fflush(stdout);
	return gDone;
}

int ExitCode() {
	return (gDone && !gFailed) ? 0 : 1;
}

}  // namespace uitest
}  // namespace mxv2
