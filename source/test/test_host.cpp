// host.cpp を模擬の AviUtl2 で試験する
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "../src/host.h"
using namespace pd;

// テスト用 : ±100 の4本のピン (v0.3.2 までの初期状態)
static pd::Doc fourPins() {
	pd::Doc d;
	const float p[4][2] = { { -100, -100 }, { 100, -100 }, { -100, 100 }, { 100, 100 } };
	for (auto& q : p) {
		pd::Pin pin;
		pin.x = q[0];
		pin.y = q[1];
		d.pins.push_back(pin);
	}
	d.normalize();
	return d;
}


// ---- 模擬オブジェクト ----
struct MockEffect { int64_t id; std::wstring name; std::map<std::wstring, std::vector<uint8_t>> data; std::string poseTrack = "0.00"; };
struct MockObject { int64_t id; int layer, start, end; std::vector<int> sections; std::vector<MockEffect> effects; std::vector<double> trackPoints; bool trackMoves = false; };
static std::vector<MockObject> g_objs;
static bool g_sceneBasedSections = true;   // get_object_section_frame がシーン基準か
static bool g_sceneBasedTrack = true;      // get_effect_track_value がシーン基準か
static std::string g_strbuf;

static MockObject* objOf(OBJECT_HANDLE h) { return (MockObject*)h; }
static MockEffect* effOf(EFFECT_HANDLE h) { return (MockEffect*)h; }
static MockObject* ownerOf(MockEffect* e) { for (auto& o : g_objs) for (auto& x : o.effects) if (&x == e) return &o; return nullptr; }

int main() {
	int fails = 0;
	auto check = [&](bool c, const char* m) { std::printf("%s %s\n", c ? "[OK]  " : "[FAIL]", m); if (!c) fails++; };

	g_objs.push_back({ 101, 0, 0, 50, { 0 }, { { 1, L"ぼかし" } } });
	g_objs.push_back({ 202, 2, 100, 220, { 100, 140, 180 }, { { 7, kEffectName }, { 8, L"ぼかし" }, { 9, kEffectName } } });

	EDIT_SECTION e{};
	e.find_object = [](int layer, int frame) -> OBJECT_HANDLE {
		for (auto& o : g_objs) if (o.layer == layer && o.end >= frame) return &o;
		return nullptr;
	};
	e.get_object_id = [](OBJECT_HANDLE h) -> int64_t { return objOf(h)->id; };
	e.get_object_layer_frame = [](OBJECT_HANDLE h) { auto o = objOf(h); return OBJECT_LAYER_FRAME{ o->layer, o->start, o->end }; };
	e.get_effect_list = [](OBJECT_HANDLE h, EFFECT_HANDLE* list, int n) -> int {
		auto o = objOf(h);
		if (!list) return (int)o->effects.size();
		int k = 0; for (auto& x : o->effects) { if (k < n) list[k++] = &x; } return k;
	};
	e.get_effect_id = [](EFFECT_HANDLE h) -> int64_t { return effOf(h)->id; };
	e.get_effect_name = [](EFFECT_HANDLE h) -> LPCWSTR { return effOf(h)->name.c_str(); };
	e.get_effect_data_value = [](EFFECT_HANDLE h, LPCWSTR item, void* data, int size) -> int {
		auto& d = effOf(h)->data[item];
		if (!data) return (int)d.size();
		int n = std::min<int>(size, (int)d.size()); std::memcpy(data, d.data(), n); return n;
	};
	e.set_effect_data_value = [](EFFECT_HANDLE h, LPCWSTR item, void* data, int size) -> bool {
		if (size > kChunkSize) return false;
		auto& d = effOf(h)->data[item]; d.assign((uint8_t*)data, (uint8_t*)data + size); return true;
	};
	e.get_object_section_num = [](OBJECT_HANDLE h) -> int { return (int)objOf(h)->sections.size(); };
	e.get_object_section_frame = [](OBJECT_HANDLE h, int i) -> int { auto o = objOf(h); return g_sceneBasedSections ? o->sections[i] : o->sections[i] - o->start; };
	e.get_effect_track_value = [](EFFECT_HANDLE h, LPCWSTR item, double frame, double* v) -> bool {
		auto o = ownerOf(effOf(h));
		if (item && std::wcscmp(item, kItemPoseTracks[1]) == 0) { *v = 5.0; return true; }  // ポーズ補間2 : 移動なし
		double rel = g_sceneBasedTrack ? frame - o->start : frame;
		std::vector<int> pts = o->sections; for (auto& p : pts) p -= o->start; pts.push_back(o->end - o->start);
		if (!o->trackMoves) { *v = o->trackPoints[0]; return true; }
		int k = 0; for (size_t i = 0; i + 1 < pts.size(); i++) if (pts[i] <= rel) k = (int)i;
		double t = (rel - pts[k]) / double(pts[k + 1] - pts[k]); t = std::fmin(1, std::fmax(0, t));
		double eased = t * t * (3 - 2 * t);  // 加減速のような移動方法を模擬
		*v = o->trackPoints[k] + (o->trackPoints[k + 1] - o->trackPoints[k]) * eased; return true;
	};
	e.get_effect_item_value = [](EFFECT_HANDLE h, LPCWSTR item) -> LPCSTR {
		if (std::wcscmp(item, kItemPoseCount) == 0) { g_strbuf = "3"; return g_strbuf.c_str(); }
		if (std::wcscmp(item, kItemPoseTracks[0]) != 0) { g_strbuf = "0.00"; return g_strbuf.c_str(); }
		g_strbuf = effOf(h)->poseTrack; return g_strbuf.c_str(); };
	e.set_effect_item_value = [](EFFECT_HANDLE h, LPCWSTR item, LPCSTR v) -> bool { if (std::wcscmp(item, kItemPoseTracks[0]) == 0) effOf(h)->poseTrack = v; return true; };

	// ---- 対象の検索 ----
	OBJECT_HANDLE obj = nullptr; EFFECT_HANDLE eff = nullptr;
	check(findTarget(&e, { 202, 9 }, 3, obj, eff) && effOf(eff)->id == 9, "IDからオブジェクトとエフェクトを探せる");
	check(effectOrdinal(&e, obj, eff) == 2, "2番目のパペット変形と判定できる");
	OBJECT_HANDLE o2; EFFECT_HANDLE e2;
	check(!findTarget(&e, { 202, 999 }, 3, o2, e2), "存在しないエフェクトは見つからない");

	// ---- 保存と読み込み (16KBを超えるデータを複数項目に分割) ----
	Doc d; d.pins.resize(256); d.bones.resize(100); d.poses.resize(4); d.normalize();
	d.poses[3].bones[50].angle = 33; d.pins[200].x = 12.5f;
	check(writeDoc(&e, eff, d), "書き込み");
	int used = 0; for (auto& kv : effOf(eff)->data) if (!kv.second.empty()) used++;
	std::printf("       使ったデータ項目: %d 個\n", used);
	Doc r; check(readDoc(&e, eff, r) && r.pins.size() == 256 && r.poses[3].bones[50].angle == 33 && r.pins[200].x == 12.5f, "読み込みで元に戻る");
	Doc small = fourPins(); writeDoc(&e, eff, small);
	Doc r2; check(readDoc(&e, eff, r2) && r2.pins.size() == 4, "小さいデータで上書きしても正しく読める");

	// ---- 区間の情報 ----
	for (int mode = 0; mode < 4; mode++) {
		g_sceneBasedSections = mode & 1; g_sceneBasedTrack = mode & 2;
		auto& o = g_objs[1];
		o.trackMoves = true; o.trackPoints = { 0, 1, 2, 3 };
		SectionInfo s;
		// 現在 = シーン160 (オブジェクト基準60) → 区間1 (140〜180) の中央
		const int rel = 60;
		double now[kPoseTracks];
		for (int t = 0; t < kPoseTracks; t++) e.get_effect_track_value(eff, kItemPoseTracks[t], g_sceneBasedTrack ? 100 + rel : rel, &now[t]);
		getSectionInfo(&e, obj, eff, 100 + rel, now, s);
		char m[160];
		std::snprintf(m, sizeof m, "区間の判定 (区間:%s基準 / トラック:%s基準) 点=%d,%d,%d,%d 区間=%d 割合=%.3f",
			g_sceneBasedSections ? "シーン" : "オブジェクト", g_sceneBasedTrack ? "シーン" : "オブジェクト",
			s.pointFrames[0], s.pointFrames[1], s.pointFrames[2], s.pointFrames[3], s.section, s.fracs[0]);
		check(s.pointFrames == std::vector<int>({ 0, 40, 80, 120 }) && s.section == 1 && std::fabs(s.fracs[0] - 0.5) < 1e-9, m);
		// 区間の1/4の位置: 時間なら0.25、移動方法(加減速)なら 0.15625
		double now2[kPoseTracks];
		for (int t = 0; t < kPoseTracks; t++) e.get_effect_track_value(eff, kItemPoseTracks[t], g_sceneBasedTrack ? 100 + 50 : 50, &now2[t]);
		getSectionInfo(&e, obj, eff, 150, now2, s);
		std::snprintf(m, sizeof m, "ポーズ補間1は移動方法で補間 (1/4の位置で %.4f、期待 0.1562) / ポーズ補間2は移動なしなので時間で直線 (%.4f、期待 0.25)", s.fracs[0], s.fracs[1]);
		check(std::fabs(s.fracs[0] - 0.15625) < 1e-6 && std::fabs(s.fracs[1] - 0.25) < 1e-9, m);
		// フィルタ処理と同じ手順 : 区間だけ先に求め、使う補間だけを問い合わせる
		SectionInfo s2;
		getSectionInfo(&e, obj, eff, 150, nullptr, s2);
		fillTrackFracs(&e, eff, now2, 1u, s2);
		check(std::fabs(s2.fracs[0] - 0.15625) < 1e-6 && std::fabs(s2.fracs[1] - 0.25) < 1e-9, "使う補間だけを問い合わせても同じ結果");
	}
	{
		auto& o = g_objs[1]; o.trackMoves = false; o.trackPoints = { 0 };
		double now3[kPoseTracks] = {}; now3[1] = 5.0;
		SectionInfo s; getSectionInfo(&e, obj, eff, 150, now3, s);
		check(std::fabs(s.fracs[0] - 0.25) < 1e-9, "移動方法なしなら時間で直線補間");
		check(getPoseTrackCount(&e, eff) == 3, "「ポーズ補間の数」を読み取れる");
	}

	// ---- ポーズ補間の値の振り直し ----
	effOf(eff)->poseTrack = "0.00,0.00,0.00,0.00,直線移動,0";
	renumberPoseTrack(&e, eff, 4);
	check(effOf(eff)->poseTrack == "0.00,1.00,2.00,3.00,直線移動,0", "同じ値が並んでいたら 0,1,2,3 に振り直す");
	effOf(eff)->poseTrack = "0.00,5.00,7.50,9.00,加減速移動,3";
	renumberPoseTrack(&e, eff, 4);
	check(effOf(eff)->poseTrack == "0.00,5.00,7.50,9.00,加減速移動,3", "異なる値なら手を付けない");
	effOf(eff)->poseTrack = "0.00";
	renumberPoseTrack(&e, eff, 4);
	check(effOf(eff)->poseTrack == "0.00", "移動方法なしなら手を付けない");

	std::printf(fails ? "失敗 %d 件\n" : "すべて成功\n", fails);
	return fails ? 1 : 0;
}
