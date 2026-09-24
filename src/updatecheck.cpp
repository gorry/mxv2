// mxv2 - 更新チェック（GitHub Releases API）

#include "updatecheck.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <SDL.h>

#include "appprofile.h"  // CMake が Profile.ini から生成する
#include "httpget.h"

namespace mxv2 {

namespace {

const char kGitHubPrefix[] = "https://github.com/";
const char kApiPrefix[] = "https://api.github.com/repos/";
// GitHub の REST API の応答形式（文書の勧める Accept）。
const char kAccept[] = "application/vnd.github+json";

// 公開場所 (https://github.com/<owner>/<repo>) の <owner>/<repo>。
// GitHub でなければ空。
std::string RepoPath() {
	const std::string project = MXV2_URL_PROJECT;
	const size_t n = sizeof(kGitHubPrefix) - 1;
	if (project.compare(0, n, kGitHubPrefix) != 0) return std::string();
	std::string repo = project.substr(n);
	while (!repo.empty() && repo[repo.size() - 1] == '/') repo.erase(repo.size() - 1);
	if (repo.find('/') == std::string::npos) return std::string();
	return repo;
}

// ---------------------------------------------------------------------------
// JSON の最上位の文字列を拾う
//
// 応答には入れ子の "author" などにも同じ名前 ("html_url") があるので、
// 最上位のキーだけを見る。値の中身は読み飛ばすだけの最小の走査。
// ---------------------------------------------------------------------------
struct JsonScan {
	const std::string &s;
	size_t i;
	explicit JsonScan(const std::string &text) : s(text), i(0) {}

	void SkipSpace() {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
	}

	static void PutUtf8(unsigned cp, std::string *out) {
		if (cp < 0x80) {
			out->push_back((char)cp);
		} else if (cp < 0x800) {
			out->push_back((char)(0xC0 | (cp >> 6)));
			out->push_back((char)(0x80 | (cp & 0x3F)));
		} else if (cp < 0x10000) {
			out->push_back((char)(0xE0 | (cp >> 12)));
			out->push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out->push_back((char)(0x80 | (cp & 0x3F)));
		} else {
			out->push_back((char)(0xF0 | (cp >> 18)));
			out->push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
			out->push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out->push_back((char)(0x80 | (cp & 0x3F)));
		}
	}

	bool Hex4(unsigned *v) {
		if (i + 4 > s.size()) return false;
		*v = 0;
		for (int k = 0; k < 4; k++) {
			const char c = s[i++];
			*v <<= 4;
			if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
			else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
			else return false;
		}
		return true;
	}

	// s[i] が '"' の位置から読む。out が 0 なら読み飛ばすだけ。
	bool String(std::string *out) {
		if (i >= s.size() || s[i] != '"') return false;
		i++;
		while (i < s.size()) {
			const char c = s[i++];
			if (c == '"') return true;
			if (c != '\\') {
				if (out != 0) out->push_back(c);
				continue;
			}
			if (i >= s.size()) return false;
			const char e = s[i++];
			char plain = 0;
			switch (e) {
				case '"': plain = '"'; break;
				case '\\': plain = '\\'; break;
				case '/': plain = '/'; break;
				case 'b': plain = '\b'; break;
				case 'f': plain = '\f'; break;
				case 'n': plain = '\n'; break;
				case 'r': plain = '\r'; break;
				case 't': plain = '\t'; break;
				case 'u': {
					unsigned cp = 0;
					if (!Hex4(&cp)) return false;
					// サロゲートの組は 1 文字に直す。
					if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' &&
					    s[i + 1] == 'u') {
						i += 2;
						unsigned lo = 0;
						if (!Hex4(&lo)) return false;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					if (out != 0) PutUtf8(cp, out);
					continue;
				}
				default:
					return false;
			}
			if (out != 0) out->push_back(plain);
		}
		return false;
	}

	// 値を 1 つ読み飛ばす（入れ子は括弧の対応だけ見る）。
	bool SkipValue() {
		SkipSpace();
		if (i >= s.size()) return false;
		const char c = s[i];
		if (c == '"') return String(0);
		if (c == '{' || c == '[') {
			int depth = 0;
			while (i < s.size()) {
				const char d = s[i];
				if (d == '"') {
					if (!String(0)) return false;
					continue;
				}
				i++;
				if (d == '{' || d == '[') depth++;
				if (d == '}' || d == ']') {
					if (--depth == 0) return true;
				}
			}
			return false;
		}
		// 数・true・false・null
		while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') i++;
		return true;
	}
};

// 最上位のオブジェクトから、名前が keys[k] の文字列を values[k] へ入れる。
bool JsonTopStrings(const std::string &text, const char *const *keys, std::string *values,
                    int count) {
	JsonScan j(text);
	j.SkipSpace();
	if (j.i >= text.size() || text[j.i] != '{') return false;
	j.i++;
	for (;;) {
		j.SkipSpace();
		if (j.i >= text.size()) return false;
		if (text[j.i] == '}') return true;
		std::string key;
		if (!j.String(&key)) return false;
		j.SkipSpace();
		if (j.i >= text.size() || text[j.i] != ':') return false;
		j.i++;
		j.SkipSpace();
		int hit = -1;
		for (int k = 0; k < count; k++) {
			if (key == keys[k]) hit = k;
		}
		if (hit >= 0 && j.i < text.size() && text[j.i] == '"') {
			values[hit].clear();
			if (!j.String(&values[hit])) return false;
		} else if (!j.SkipValue()) {
			return false;
		}
		j.SkipSpace();
		if (j.i < text.size() && text[j.i] == ',') j.i++;
	}
}

// 版の文字列を数の並びにする。頭の "v" は読み飛ばし、数字以外で区切る。
std::vector<long> VersionParts(const std::string &v) {
	std::vector<long> parts;
	size_t i = 0;
	if (i < v.size() && (v[i] == 'v' || v[i] == 'V')) i++;
	while (i < v.size()) {
		if (v[i] >= '0' && v[i] <= '9') {
			long n = 0;
			while (i < v.size() && v[i] >= '0' && v[i] <= '9') n = n * 10 + (v[i++] - '0');
			parts.push_back(n);
		} else {
			i++;
		}
	}
	return parts;
}

}  // namespace

// 作業スレッドとの受け渡し。どちらが先に手を離しても大丈夫なように、
// 参照の数で持つ（本体が先に消えたら作業スレッドが最後に片付ける）。
struct UpdateChecker::State {
	SDL_atomic_t refs;
	SDL_atomic_t done;
	UpdateResult result;
};

UpdateChecker::UpdateChecker() : state_(0) {}

UpdateChecker::~UpdateChecker() {
	if (state_ != 0) {
		Release(state_);
		state_ = 0;
	}
}

bool UpdateChecker::Available() {
	return httpget::Available() && !RepoPath().empty();
}

void UpdateChecker::Release(State *s) {
	if (SDL_AtomicAdd(&s->refs, -1) == 1) delete s;
}

bool UpdateChecker::Start(bool manual) {
	if (state_ != 0) return false;
	httpget::Prepare();  // メインスレッドでの下ごしらえ（Android の FindClass）

	State *s = new State;
	SDL_AtomicSet(&s->refs, 2);  // 本体と作業スレッド
	SDL_AtomicSet(&s->done, 0);
	s->result.manual = manual;

	SDL_Thread *th = SDL_CreateThread(ThreadMain, "mxv2Update", s);
	if (th == 0) {
		s->result.status = UpdateResult::kFailed;
		s->result.error = std::string("SDL_CreateThread failed: ") + SDL_GetError();
		SDL_AtomicSet(&s->refs, 1);
		SDL_AtomicSet(&s->done, 1);
	} else {
		// 待ち合わせはしない（Poll で拾う）。
		SDL_DetachThread(th);
	}
	state_ = s;
	return true;
}

bool UpdateChecker::Poll(UpdateResult *out) {
	if (state_ == 0 || SDL_AtomicGet(&state_->done) == 0) return false;
	*out = state_->result;
	Release(state_);
	state_ = 0;
	return true;
}

int UpdateChecker::CompareVersions(const std::string &a, const std::string &b) {
	const std::vector<long> pa = VersionParts(a);
	const std::vector<long> pb = VersionParts(b);
	const size_t n = (pa.size() > pb.size()) ? pa.size() : pb.size();
	for (size_t k = 0; k < n; k++) {
		const long x = (k < pa.size()) ? pa[k] : 0;
		const long y = (k < pb.size()) ? pb[k] : 0;
		if (x != y) return (x > y) ? 1 : -1;
	}
	return 0;
}

int UpdateChecker::ThreadMain(void *arg) {
	State *s = (State *)arg;
	UpdateResult &r = s->result;

	const std::string repo = RepoPath();
	const std::string api = std::string(kApiPrefix) + repo + "/releases/latest";
	const std::string ua = std::string(MXV2_APP_SHORT_NAME) + "/" + MXV2_APP_VERSION;

	int status = 0;
	std::string body;
	std::string err;
	if (repo.empty()) {
		r.error = "the project URL is not on GitHub";
	} else if (!httpget::Get(api, ua, kAccept, &status, &body, &err)) {
		r.error = err;
	} else if (status != 200) {
		// 404 はリリースが 1 つも無いとき。403 / 429 は回数制限。
		char buf[64];
		snprintf(buf, sizeof(buf), "HTTP %d", status);
		r.error = buf;
	} else {
		static const char *const kKeys[] = { "tag_name", "html_url" };
		std::string values[2];
		if (!JsonTopStrings(body, kKeys, values, 2) || values[0].empty()) {
			r.error = "unexpected response (no tag_name)";
		} else {
			r.latest = values[0];
			// 開くページは公開場所の下にあるものだけ使う（応答の URL を
			// そのままブラウザへ渡さない）。無ければ最新リリースのページ。
			const std::string project = MXV2_URL_PROJECT;
			if (values[1].compare(0, project.size() + 1, project + "/") == 0) {
				r.pageUrl = values[1];
			} else {
				r.pageUrl = project + "/releases/latest";
			}
			r.status = (CompareVersions(r.latest, MXV2_APP_VERSION) > 0) ? UpdateResult::kNewer
			                                                            : UpdateResult::kLatest;
		}
	}

	SDL_AtomicSet(&s->done, 1);
	Release(s);
	return 0;
}

}  // namespace mxv2
