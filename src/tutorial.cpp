// mxv2 - 初回起動時のチュートリアル（tutorial.md）

#include "tutorial.h"

#include <cstdio>

#include "drawscreen.h"
#include "fileutil.h"
#include "kinsoku.h"
#include "message.h"
#include "screen.h"
#include "skin.h"

namespace mxv2 {

namespace {

// ステップ 2 の行き先。同梱の曲が入っているフォルダ。
const char kTargetFolderRef[] = "assets:ArctanX";

// 操作待ちが満たされてから次へ進むまでの「間」（この間は UI を止める）。
const uint32_t kHoldMs = 500;

// 幕の濃さと、スポットの縁取り。
const ImU32 kDimColor = IM_COL32(0, 0, 0, 160);
const ImU32 kSpotBorder = IM_COL32(255, 220, 80, 230);

// 吹き出しの幅（倍率 1 のとき）と、スポットからの間隔・画面の余白。
const float kBubbleWidth = 400.0f;
const float kBubbleGap = 12.0f;
const float kBubbleMargin = 8.0f;

// カタログのキーを組み立てる。段は 1 始まり（tutorial.md の番号）。
std::string Key(const char *what, int step, const char *suffix = "") {
	char buf[64];
	snprintf(buf, sizeof(buf), "Tutorial.%s%d%s", what, step + 1, suffix);
	return buf;
}

// あれば文言、無ければ空（Msg は無いキーを警告つきでキー名にするので、
// 先に HasMsg で見る）。
std::string MsgOrEmpty(const std::string &key) {
	if (!HasMsg(key.c_str())) return std::string();
	return Msg(key.c_str());
}

}  // namespace

Tutorial::Tutorial()
    : active_(false),
      recordDone_(false),
      finished_(false),
      step_(kStepWelcome),
      confirmSkip_(false),
      maskReset_(false),
      stepEntered_(false),
      sawPaused_(false),
      sawMenuOpen_(false),
      sawSettingsOpen_(false),
      fsReached_(false),
      sawMasked_(false),
      contAtStart_(false),
      pressAllowed_(false),
      holding_(false),
      holdStartMs_(0),
      lastBubbleH_(0.0f) {}

void Tutorial::Start(bool recordDone) {
	active_ = true;
	recordDone_ = recordDone;
	finished_ = false;
	step_ = kStepWelcome;
	confirmSkip_ = false;
	maskReset_ = false;
	stepEntered_ = false;
	sawPaused_ = false;
	sawMenuOpen_ = false;
	sawSettingsOpen_ = false;
	fsReached_ = false;
	pressAllowed_ = false;
	holding_ = false;
	spot_ = Rect();
	bubblePos_ = ImVec2(0, 0);
	bubbleSize_ = ImVec2(0, 0);
}

void Tutorial::RequestSkip() {
	if (!active_ || holding_) return;
	confirmSkip_ = true;
}

bool Tutorial::TakeFinished() {
	const bool v = finished_;
	finished_ = false;
	return v;
}

bool Tutorial::TakeMaskReset() {
	const bool v = maskReset_;
	maskReset_ = false;
	return v;
}

void Tutorial::Finish() {
	active_ = false;
	finished_ = true;
	confirmSkip_ = false;
	spot_ = Rect();
	// 鍵盤のステップの途中で抜けたなら、マスクを残さない。
	if (step_ == kStepKeyboard) maskReset_ = true;
}

void Tutorial::Advance() {
	if (step_ == kStepKeyboard) maskReset_ = true;
	if (step_ + 1 >= kNumSteps) {
		Finish();
		return;
	}
	step_++;
	stepEntered_ = false;
	sawPaused_ = false;
	sawMenuOpen_ = false;
	sawSettingsOpen_ = false;
	fsReached_ = false;
	pressAllowed_ = false;
	holding_ = false;
}

// ---------------------------------------------------------------------------

bool Tutorial::IsAction(int step) {
	switch (step) {
		case kStepFiler:
		case kStepPlay:
		case kStepKeyboard:
		case kStepPlayKeys:
		case kStepCont:
		case kStepMenu:
		case kStepSettings:
		case kStepFsSelect:
			return true;
		default:
			return false;
	}
}

bool Tutorial::StepDone(int step, const State &st) {
	switch (step) {
		case kStepFiler:
			return CompareNoCase(st.currentRef, kTargetFolderRef) == 0;
		case kStepPlay:
			return st.playing;
		case kStepKeyboard:
			// 一度どれかのチャンネルを OFF にしたあと、すべてを ON に戻したら次へ。
			if (st.channelMask != 0) sawMasked_ = true;
			return sawMasked_ && st.channelMask == 0;
		case kStepPlayKeys:
			if (st.paused) sawPaused_ = true;
			return sawPaused_ && !st.paused;
		case kStepCont:
			// 「点ける」ではなく「切り替えた」を見る。ini の Cont=1 で既に
			// 点いていると、入った瞬間に終わって読む間が無いため。
			return st.cont != contAtStart_;
		case kStepMenu:
			// メニューから [mxv2 の設定] を開いたら次へ。
			return st.settingsOpen;
		case kStepSettings:
			// 開いているダイアログを閉じたら次へ。
			if (st.settingsOpen) sawSettingsOpen_ = true;
			return sawSettingsOpen_ && !st.settingsOpen;
		case kStepFsSelect:
			return st.fsSelect;
		default:
			return false;
	}
}

Tutorial::Rect Tutorial::SpotFor(int step, const DrawScreen &draw) const {
	const Skin &l = draw.layout();
	Rect r;
	switch (step) {
		case kStepFiler:
		case kStepPlay:
		case kStepFsSelect:
			r = Rect(l.fileListX, l.fileListY, l.fileListW, l.fileListH);
			r.unite(Rect(l.scrollHitX, l.scrollY, l.scrollHitW, l.scrollH));
			break;
		case kStepKeyboard:
			for (int ch = 0; ch < 16; ch++) {
				int x0, y0, x1, y1;
				if (draw.ChannelKeyRect(ch, &x0, &y0, &x1, &y1)) {
					r.unite(Rect(x0, y0, x1 - x0, y1 - y0));
				}
			}
			for (int row = 0; row < 9; row++) {
				int x, y, w, h;
				if (draw.StatusRect(row, &x, &y, &w, &h)) r.unite(Rect(x, y, w, h));
			}
			break;
		case kStepPlayKeys:
		case kStepCont: {
			// 操作ボタンは 0..5 が演奏、6 と 7 が CONT / REPEAT。
			const int from = (step == kStepPlayKeys) ? 0 : 6;
			const int to = (step == kStepPlayKeys) ? 6 : 8;
			for (int i = from; i < to && i < l.numPlayKeys; i++) {
				r.unite(Rect(l.playKeyX + l.playKeyPos[i][0], l.playKeyY + l.playKeyPos[i][1],
				             l.playKeyRect[i].w, l.playKeyRect[i].h));
			}
			if (step == kStepCont) {
				r.unite(Rect(l.volX, l.volY, l.volW, l.volH));
				r.unite(Rect(l.progX, l.progY, l.progW, l.progH));
			}
			break;
		}
		case kStepMenu:
			r = Rect(l.bannerX, l.bannerY, l.bannerW, l.bannerH);
			break;
		case kStepBookmark:
			// ファイルシステムの選択画面の先頭行 ("Bookmarks>")。
			r = Rect(l.fileListX, l.fileListY, l.fileListW, draw.fileListItemH());
			break;
		default:
			break;
	}
	return r;
}

// ---------------------------------------------------------------------------

std::string Tutorial::TitleFor(int step) const {
	// 見出しに通し番号 (1/11) を添える。書式はカタログ (TitleFormat)。
	return MsgF("Tutorial.TitleFormat", Msg(Key("Title", step).c_str()), MsgNum("%d", step + 1),
	            MsgNum("%d", (int)kNumSteps));
}

std::string Tutorial::TextFor(int step, const State &st) const {
	const char *side = st.touch ? "Mobile" : "Desktop";
	std::string text;
	// ステップ 8 は、選択画面に着いたら文面を差し替える。
	if (step == kStepFsSelect && fsReached_) {
		text = MsgOrEmpty(Key("Text", step, "After"));
		const std::string extra = MsgOrEmpty(Key("Text", step, (std::string("After") + side).c_str()));
		if (!extra.empty()) text += (text.empty() ? "" : "\n") + extra;
		return text;
	}
	// 本文 → 環境ごとの補足 → 「やってみる」 → その環境での操作。
	// ini の値は 1 行なので、段落はキーを分けて持つ。
	text += Msg(Key("Text", step).c_str());
	std::string extra = MsgOrEmpty(Key("Text", step, side));
	if (!extra.empty()) text += "\n" + extra;
	const std::string trying = MsgOrEmpty(Key("Try", step));
	if (!trying.empty()) {
		text += "\n\n" + trying;
		extra = MsgOrEmpty(Key("Try", step, side));
		if (!extra.empty()) text += "\n" + extra;
	}
	return text;
}

// ---------------------------------------------------------------------------

bool Tutorial::BubbleContains(int ox, int oy) const {
	if (!active_) return false;
	return ox >= bubblePos_.x && ox < bubblePos_.x + bubbleSize_.x && oy >= bubblePos_.y &&
	       oy < bubblePos_.y + bubbleSize_.y;
}

bool Tutorial::AllowsMouse(const SDL_Event &ev) {
	if (!active_) return true;
	// 動かすだけなら通す（ホバーやドラッグの続きに要る）。
	if (ev.type == SDL_MOUSEMOTION) return true;
	if (ev.type == SDL_MOUSEBUTTONUP) {
		const bool ok = pressAllowed_;
		pressAllowed_ = false;
		return ok;
	}
	if (confirmSkip_ || holding_ || spot_.empty() || !IsAction(step_) || fsReached_) return false;
	if (ev.type == SDL_MOUSEBUTTONDOWN) {
		pressAllowed_ = spot_.contains(ev.button.x, ev.button.y);
		return pressAllowed_;
	}
	if (ev.type == SDL_MOUSEWHEEL) {
		// ホイールには押した場所が無いので、ファイラーのステップだけ通す
		// （一覧のスクロールに要る）。
		return step_ == kStepFiler || step_ == kStepPlay || step_ == kStepFsSelect;
	}
	return false;
}

bool Tutorial::AllowsKey(SDL_Keycode key, Uint16 mod) const {
	if (!active_) return true;
	if (confirmSkip_ || holding_ || fsReached_) return false;
	(void)mod;
	switch (step_) {
		case kStepFiler:
		case kStepPlay:
		case kStepFsSelect:
			switch (key) {
				case SDLK_UP:
				case SDLK_DOWN:
				case SDLK_PAGEUP:
				case SDLK_PAGEDOWN:
				case SDLK_HOME:
				case SDLK_END:
				case SDLK_RETURN:
				case SDLK_KP_ENTER:
				case SDLK_BACKSPACE:
				case SDLK_BACKSLASH:
				case SDLK_TAB:
					return true;
				default:
					return false;
			}
		case kStepKeyboard:
			return (key >= SDLK_0 && key <= SDLK_8);
		case kStepPlayKeys:
			return key == SDLK_SPACE;
		case kStepCont:
			return key == SDLK_c;
		default:
			return false;
	}
}

// ---------------------------------------------------------------------------

void Tutorial::Build(const DrawScreen &draw, const Screen &screen, const State &st,
                     float uiScale, ImGuiCond placeCond) {
	(void)placeCond;  // 吹き出しは毎フレーム置き直すので使わない
	if (!active_) return;

	// ステップに入った最初のフレーム。「入ったときの状態」を覚える。
	if (!stepEntered_) {
		stepEntered_ = true;
		sawMasked_ = false;
		contAtStart_ = st.cont;
		// 設定の段に入った時点でダイアログが閉じていたら（「間」の最中に
		// 閉じられた）、「開いているのを見た」ことにして待たない。
		if (step_ == kStepSettings && !st.settingsOpen) sawSettingsOpen_ = true;
	}
	// 操作待ちが満たされたら、まず kHoldMs のあいだ UI を止めて「できた」
	// 状態を見せてから次へ進む（満たした瞬間に飛ぶと分かりにくい）。
	if (holding_) {
		if (SDL_GetTicks() - holdStartMs_ >= kHoldMs) {
			holding_ = false;
			if (step_ == kStepFsSelect) {
				fsReached_ = true;
			} else {
				Advance();
				if (!active_) return;
			}
		}
	} else if (!confirmSkip_ && IsAction(step_) && !fsReached_ && StepDone(step_, st)) {
		holding_ = true;
		holdStartMs_ = SDL_GetTicks();
	}

	spot_ = SpotFor(step_, draw);

	// ---- 幕とスポット（実ピクセル） -------------------------------------
	const ImGuiIO &io = ImGui::GetIO();
	const ImVec2 disp = io.DisplaySize;
	ImDrawList *bg = ImGui::GetBackgroundDrawList();

	ImVec2 holeMin(0, 0), holeMax(0, 0);
	bool hasHole = false;
	if (!spot_.empty() && draw.width() > 0 && draw.height() > 0) {
		const SDL_Rect cr = screen.CanvasRect();
		const float sx = (float)cr.w / (float)draw.width();
		const float sy = (float)cr.h / (float)draw.height();
		holeMin = ImVec2(cr.x + spot_.x * sx, cr.y + spot_.y * sy);
		holeMax = ImVec2(cr.x + (spot_.x + spot_.w) * sx, cr.y + (spot_.y + spot_.h) * sy);
		hasHole = true;
	}
	if (hasHole) {
		bg->AddRectFilled(ImVec2(0, 0), ImVec2(disp.x, holeMin.y), kDimColor);
		bg->AddRectFilled(ImVec2(0, holeMax.y), ImVec2(disp.x, disp.y), kDimColor);
		bg->AddRectFilled(ImVec2(0, holeMin.y), ImVec2(holeMin.x, holeMax.y), kDimColor);
		bg->AddRectFilled(ImVec2(holeMax.x, holeMin.y), ImVec2(disp.x, holeMax.y), kDimColor);
		bg->AddRect(holeMin, holeMax, kSpotBorder, 0.0f, 0, 2.0f * uiScale);
	} else {
		bg->AddRectFilled(ImVec2(0, 0), disp, kDimColor);
	}

	// ---- 吹き出し ------------------------------------------------------
	const float margin = kBubbleMargin * uiScale;
	const float gap = kBubbleGap * uiScale;
	float width = kBubbleWidth * uiScale;
	if (width > disp.x - margin * 2) width = disp.x - margin * 2;

	// 設定の段でダイアログが開いている間は、ImGui のモーダルがどのウィンドウ
	// よりも上に来て吹き出しを隠すので、説明を**最前面の描画リスト**に
	// 直接描く（ボタンは無い。閉じれば次へ進むので要らない）。画面の下端。
	if (step_ == kStepSettings && st.settingsOpen && !confirmSkip_) {
		const std::string text = TitleFor(step_) + "\n\n" + TextFor(step_, st);
		const float pad = 8.0f * uiScale;
		const float wrap = width - pad * 2;
		// 折り返しは禁則つきで自前に（kinsoku.h）。
		const std::string wrapped = WrapTextKinsoku(text, wrap);
		const ImVec2 ts = ImGui::CalcTextSize(wrapped.c_str());
		const ImVec2 size(width, ts.y + pad * 2);
		const ImVec2 pos((disp.x - size.x) * 0.5f, disp.y - margin - size.y);
		ImDrawList *fg = ImGui::GetForegroundDrawList();
		fg->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(16, 16, 16, 235),
		                  4.0f * uiScale);
		fg->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), kSpotBorder, 4.0f * uiScale, 0,
		            1.0f * uiScale);
		fg->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + pad, pos.y + pad),
		            IM_COL32(255, 255, 255, 255), wrapped.c_str(), 0, 0.0f);
		bubblePos_ = pos;
		bubbleSize_ = size;
		return;
	}

	ImVec2 pos;
	ImVec2 pivot;
	if (!hasHole) {
		pos = ImVec2(disp.x * 0.5f, disp.y * 0.5f);
		pivot = ImVec2(0.5f, 0.5f);
	} else {
		const float cx = (holeMin.x + holeMax.x) * 0.5f;
		const float holeCy = (holeMin.y + holeMax.y) * 0.5f;
		// スポットが上半分なら下へ、下半分なら上へ。上に入らなければ下。
		bool below = holeCy < disp.y * 0.5f;
		if (!below && holeMin.y - gap - lastBubbleH_ < margin) below = true;
		if (below && holeMax.y + gap + lastBubbleH_ > disp.y - margin) {
			// 下にも入らない（スポットが大きい）ときはスポットの中の下寄り。
			pos = ImVec2(cx, disp.y - margin);
			pivot = ImVec2(0.5f, 1.0f);
		} else if (below) {
			pos = ImVec2(cx, holeMax.y + gap);
			pivot = ImVec2(0.5f, 0.0f);
		} else {
			pos = ImVec2(cx, holeMin.y - gap);
			pivot = ImVec2(0.5f, 1.0f);
		}
		// 横は画面に収める。
		if (pos.x - width * 0.5f < margin) pos.x = margin + width * 0.5f;
		if (pos.x + width * 0.5f > disp.x - margin) pos.x = disp.x - margin - width * 0.5f;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, disp.y - margin * 2));
	const ImGuiWindowFlags flags =
	    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavFocus |
	    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	if (ImGui::Begin("##mxv2tutorial", 0, flags)) {
		lastBubbleH_ = ImGui::GetWindowSize().y;
		bubblePos_ = ImGui::GetWindowPos();
		bubbleSize_ = ImGui::GetWindowSize();

		// 「間」の最中は吹き出しのボタンも効かせない。
		if (holding_) ImGui::BeginDisabled();
		if (confirmSkip_) {
			TextWrappedKinsoku(Msg("Tutorial.SkipQuestion"));
			ImGui::Spacing();
			if (ImGui::Button(Msg("Tutorial.SkipYes"))) Finish();
			ImGui::SameLine();
			if (ImGui::Button(Msg("Tutorial.SkipNo"))) confirmSkip_ = false;
		} else {
			// 見出しは少し大きく。PushFont の大きさは**倍率を掛ける前**の値
			// （FontScaleDpi が後から掛かる）なので、GetFontSize() を渡すと
			// 二重に拡大される。
			ImGui::PushFont(0, ImGui::GetStyle().FontSizeBase * 1.2f);
			TextWrappedKinsoku(TitleFor(step_).c_str());
			ImGui::PopFont();
			ImGui::Separator();
			TextWrappedKinsoku(TextFor(step_, st).c_str());
			ImGui::Spacing();

			if (step_ == kStepWelcome) {
				if (ImGui::Button(Msg("Tutorial.Start"))) Advance();
				ImGui::SameLine();
				if (ImGui::Button(Msg("Tutorial.Skip"))) confirmSkip_ = true;
			} else if (step_ == kStepEnd) {
				if (ImGui::Button(Msg("Tutorial.Close"), ImVec2(-FLT_MIN, 0.0f))) Finish();
			} else {
				if (ImGui::Button(Msg("Tutorial.Next"))) Advance();
				ImGui::SameLine();
				if (ImGui::Button(Msg("Tutorial.Skip"))) confirmSkip_ = true;
			}
		}
		if (holding_) ImGui::EndDisabled();
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// 吹き出しのボタンを押すと ImGui がこのウィンドウにフォーカスを置き、
	// 次のフレームから WantCaptureKeyboard が立つ。ここで
	// SetWindowFocus(NULL) で外すと、押している最中の部品（ActiveId）まで
	// 消えてボタンが「押して離した」にならないので、外さない。
	// 本体のキーは main 側で「チュートリアル中でダイアログが無ければ
	// WantCaptureKeyboard を見ない」ことにして通す（吹き出しに入力欄は無い）。
}

}  // namespace mxv2
