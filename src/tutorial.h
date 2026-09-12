// mxv2 - 初回起動時のチュートリアル（tutorial.md）
//
// 用意したシナリオに沿って進む案内。本体の上に半透明の幕を掛け、注目する
// 部品だけ幕を抜いて（スポットライト）、小さな吹き出しで説明する。
// 「やってみる」ステップは本体の状態を見て自動で進む（イベントは横取り
// しない。本体が動いた結果だけを見る）。
//
// 幕が出ている間の入力はメインループが AllowsMouse / AllowsKey で選ぶ:
// 期待している操作（スポットの中のマウス、そのステップのキー）だけ通し、
// それ以外は幕が吸い取る。ESC / 戻るキーは RequestSkip()。
//
// ImGui のフレームの中（SettingsUi::Build のあと、Render の前）で Build()
// を呼ぶ。幕は背景の描画リストに、吹き出しは普通のウィンドウとして描く。

#ifndef MXV2_TUTORIAL_H
#define MXV2_TUTORIAL_H

#include <SDL.h>

#include <string>

#include "imgui.h"

namespace mxv2 {

class DrawScreen;
class Screen;

class Tutorial {
public:
	Tutorial();

	// 本体の状態。毎フレーム main が埋めて Build() に渡す。
	struct State {
		std::string currentRef;  // ファイラーの今の場所（ref）
		bool fsSelect;           // ファイルシステムの選択画面にいる
		bool playing;
		bool paused;
		uint16_t channelMask;
		bool cont;               // CONT が点いている
		bool menuOpen;           // コンテキストメニューが開いている
		bool settingsOpen;       // [mxv2 の設定] ダイアログが開いている
		bool touch;              // 指で操作する（文面の出し分け）
		State()
		    : fsSelect(false), playing(false), paused(false), channelMask(0), cont(false),
		      menuOpen(false), settingsOpen(false), touch(false) {}
	};

	// 始める。recordDone は「終わったら ini に Done を書く」（初回起動の
	// 自動表示だけ true。メニューや -tutorial からは false）。
	void Start(bool recordDone);
	bool active() const { return active_; }
	bool recordDone() const { return recordDone_; }

	// ESC / 戻るキー。確認を出す（[スキップ] と同じ）。
	void RequestSkip();

	// 1 フレームぶんを描く。uiScale は SettingsUi の styleScale_（ダイアログの
	// 字の大きさ）、placeCond は回転・リサイズのフレームで Always。
	void Build(const DrawScreen &draw, const Screen &screen, const State &st, float uiScale,
	           ImGuiCond placeCond);

	// 終わった（最後まで見た／スキップした）瞬間を 1 度だけ返す。
	bool TakeFinished();
	// ステップ 4 を抜けるときに 1 度だけ true。main がマスクを全解除する。
	bool TakeMaskReset();

	// 入力の許可。座標はキャンバスの論理 px。
	bool AllowsMouse(const SDL_Event &canvasEv);
	bool AllowsKey(SDL_Keycode key, Uint16 mod) const;
	// 実出力の座標 (ox, oy) が吹き出しの上か。
	// 指で操作するとき、前の触りが吹き出しの上で終わっていると、ImGui は
	// 次の押下まで「まだそこにある」と見なして WantCaptureMouse を立てる。
	// main はチュートリアル中（他のダイアログが無いとき）、押した位置が
	// 吹き出しの中のときだけ ImGui に渡し、外なら本体へ通す。
	bool BubbleContains(int ox, int oy) const;

private:
	enum Step {
		kStepWelcome = 0,
		kStepFiler,      // ArctanX を開く
		kStepPlay,       // 曲を開く
		kStepKeyboard,   // 鍵盤を押す
		kStepPlayKeys,   // 一時停止と再開
		kStepCont,       // CONT を点ける
		kStepMenu,       // メニューから [mxv2 の設定] を開く
		kStepSettings,   // 設定の説明。ダイアログを閉じたら次へ
		kStepFsSelect,   // ファイルシステムの選択まで戻る
		kStepBookmark,
		kStepEnd,
		kNumSteps
	};

	struct Rect {
		int x, y, w, h;  // 論理 px。w <= 0 なら「無し」
		Rect() : x(0), y(0), w(0), h(0) {}
		Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
		bool empty() const { return w <= 0 || h <= 0; }
		bool contains(int px, int py) const {
			return px >= x && px < x + w && py >= y && py < y + h;
		}
		void unite(const Rect &o) {
			if (o.empty()) return;
			if (empty()) { *this = o; return; }
			const int x1 = (x + w > o.x + o.w) ? x + w : o.x + o.w;
			const int y1 = (y + h > o.y + o.h) ? y + h : o.y + o.h;
			if (o.x < x) x = o.x;
			if (o.y < y) y = o.y;
			w = x1 - x;
			h = y1 - y;
		}
	};

	// ステップごとのスポット（論理 px）。無ければ空。
	Rect SpotFor(int step, const DrawScreen &draw) const;
	// ステップの操作待ちが満たされたか。状態の変わり目も見るので const ではない。
	bool StepDone(int step, const State &st);
	// 操作待ちがあるステップか。
	static bool IsAction(int step);
	void Advance();
	void Finish();

	// 文面（カタログの [Tutorial]）。
	std::string TitleFor(int step) const;
	std::string TextFor(int step, const State &st) const;

	bool active_;
	bool recordDone_;
	bool finished_;
	int step_;
	bool confirmSkip_;
	bool maskReset_;
	// 操作待ちの判定に使う「前のフレーム」。
	bool stepEntered_;       // このステップの最初のフレームを過ぎた
	bool sawPaused_;         // ステップ 5: 一時停止を見た
	bool sawMenuOpen_;       // ステップ 7: メニューが開いたのを見た
	bool sawSettingsOpen_;   // ステップ 8: 設定ダイアログが開いているのを見た
	bool fsReached_;         // ステップ 8: 選択画面に着いた（文面を差し替えて [次へ] 待ち）
	bool sawMasked_;         // ステップ 4: どれかのチャンネルが OFF になったのを見た
	bool contAtStart_;       // ステップ 6: 入ったときの CONT
	// 押した場所を許可したら、離すまで通す（MouseInput の押下を宙に浮かせない）。
	bool pressAllowed_;
	// 操作待ちが満たされた直後の「間」。満たした瞬間に次へ飛ぶと何が
	// 起きたか分かりにくいので、kHoldMs のあいだ UI を止めてから進む。
	bool holding_;
	uint32_t holdStartMs_;
	Rect spot_;              // 今のスポット（AllowsMouse 用。Build が更新）
	float lastBubbleH_;      // 前のフレームの吹き出しの高さ（置き場所の判断に使う）
	ImVec2 bubblePos_;       // 吹き出しの矩形（実出力 px。BubbleContains 用）
	ImVec2 bubbleSize_;
};

}  // namespace mxv2

#endif  // MXV2_TUTORIAL_H
