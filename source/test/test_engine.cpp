// 変形エンジンの単体テスト (Linux上で実行、結果をPPM画像に出力)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/deform.h"
#include "../src/model.h"

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


struct Img {
	int w, h;
	std::vector<float> a;  // 白一色の図形のα
	std::vector<uint8_t> mask() const {
		std::vector<uint8_t> m(a.size());
		for (size_t i = 0; i < a.size(); i++) m[i] = (uint8_t)std::lround(a[i] * 255);
		return m;
	}
	float at(double x, double y) const {  // バイリニア、範囲外は0
		x -= 0.5;
		y -= 0.5;
		int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
		double fx = x - x0, fy = y - y0, s = 0;
		for (int dy = 0; dy < 2; dy++)
			for (int dx = 0; dx < 2; dx++) {
				int xi = x0 + dx, yi = y0 + dy;
				double wgt = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
				if (xi >= 0 && yi >= 0 && xi < w && yi < h) s += wgt * a[yi * w + xi];
			}
		return (float)s;
	}
};

// 手の形 (手のひら + 5本の指)
Img makeHand(int w, int h) {
	Img im{ w, h, std::vector<float>(w * h, 0) };
	auto capsule = [](double px, double py, double ax, double ay, double bx, double by, double r) {
		double vx = bx - ax, vy = by - ay, t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy);
		t = std::fmin(1, std::fmax(0, t));
		double dx = px - (ax + vx * t), dy = py - (ay + vy * t);
		return std::sqrt(dx * dx + dy * dy) - r;
	};
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			double px = x + 0.5 - w / 2.0, py = y + 0.5 - h / 2.0, d = 1e9;
			double ex = px / 150.0, ey = (py - 120) / 140.0;
			d = std::fmin(d, (std::sqrt(ex * ex + ey * ey) - 1) * 140);
			d = std::fmin(d, capsule(px, py, 0, 0, 0, -230, 32));       // 中指
			d = std::fmin(d, capsule(px, py, -75, 10, -110, -190, 30));  // 人差し指
			d = std::fmin(d, capsule(px, py, 75, 10, 105, -160, 30));    // 薬指
			d = std::fmin(d, capsule(px, py, 120, 50, 190, -80, 27));    // 小指
			d = std::fmin(d, capsule(px, py, -120, 120, -230, 20, 32));  // 親指
			im.a[y * w + x] = (float)std::fmin(1, std::fmax(0, 0.5 - d));
		}
	return im;
}

Img makeBar(int w, int h) {
	Img im{ w, h, std::vector<float>(w * h, 1.0f) };
	return im;
}

// 変形メッシュを描画 (三角形ごとにアフィン逆写像でサンプリング)
std::vector<float> render(const Img& src, const MeshResult& m, int W, int H) {
	std::vector<float> out(W * H, 0);
	auto tri = [&](int a, int b, int c) {
		const Vec2 P[3] = { m.cur[a], m.cur[b], m.cur[c] };
		const Vec2 S[3] = { m.rest[a], m.rest[b], m.rest[c] };
		double minx = std::fmin(P[0].x, std::fmin(P[1].x, P[2].x)), maxx = std::fmax(P[0].x, std::fmax(P[1].x, P[2].x));
		double miny = std::fmin(P[0].y, std::fmin(P[1].y, P[2].y)), maxy = std::fmax(P[0].y, std::fmax(P[1].y, P[2].y));
		double den = (P[1].y - P[2].y) * (P[0].x - P[2].x) + (P[2].x - P[1].x) * (P[0].y - P[2].y);
		if (std::fabs(den) < 1e-12) return;
		for (int y = (int)std::floor(miny + H / 2.0); y <= (int)std::ceil(maxy + H / 2.0); y++)
			for (int x = (int)std::floor(minx + W / 2.0); x <= (int)std::ceil(maxx + W / 2.0); x++) {
				if (x < 0 || y < 0 || x >= W || y >= H) continue;
				double px = x + 0.5 - W / 2.0, py = y + 0.5 - H / 2.0;
				double l0 = ((P[1].y - P[2].y) * (px - P[2].x) + (P[2].x - P[1].x) * (py - P[2].y)) / den;
				double l1 = ((P[2].y - P[0].y) * (px - P[2].x) + (P[0].x - P[2].x) * (py - P[2].y)) / den;
				double l2 = 1 - l0 - l1;
				if (l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9) continue;
				double sx = l0 * S[0].x + l1 * S[1].x + l2 * S[2].x + src.w / 2.0;
				double sy = l0 * S[0].y + l1 * S[1].y + l2 * S[2].y + src.h / 2.0;
				out[y * W + x] = std::fmax(out[y * W + x], src.at(sx, sy));
			}
	};
	for (int j = 0; j < m.rows; j++)
		for (int i = 0; i < m.cols; i++) {
			if (!m.cellActive[(size_t)j * m.cols + i]) continue;
			int v00 = m.index(i, j), v10 = v00 + 1, v01 = m.index(i, j + 1), v11 = v01 + 1;
			tri(v00, v10, v11);
			tri(v00, v11, v01);
		}
	return out;
}

void savePGM(const std::string& path, const std::vector<float>& a, int W, int H) {
	FILE* f = std::fopen(path.c_str(), "wb");
	std::fprintf(f, "P5\n%d %d\n255\n", W, H);
	for (float v : a) std::fputc((int)std::lround(std::fmin(1, std::fmax(0, v)) * 255), f);
	std::fclose(f);
}

int main(int argc, char** argv) {
	std::string dir = argc > 1 ? argv[1] : ".";
	int fails = 0;
	auto check = [&](bool c, const char* msg) {
		std::printf("%s %s\n", c ? "[OK]  " : "[FAIL]", msg);
		if (!c) fails++;
	};

	// ---- 1. 保存形式の往復 ----
	{
		Doc d = fourPins();
		d.bones.push_back({ 0, 0, true, -1 });
		d.bones.push_back({ 0, -100, false, 0 });
		d.pins[1].bone = 1;
		d.normalize();
		d.poses[0].bones[1].angle = 12.5f;
		d.poses[0].pins[2].mx = -3;
		Pose p2 = d.poses[0];
		p2.frame = 30;
		d.poses.push_back(p2);
		auto blob = serialize(d);
		Doc e;
		bool ok = deserialize(blob.data(), blob.size(), e);
		check(ok && e.pins.size() == 4 && e.bones.size() == 2 && e.poses.size() == 2 && e.poses[1].frame == 30 &&
				  e.poses[0].bones[1].angle == 12.5f && e.pins[1].bone == 1 && !e.bones[1].enabled,
			  "保存形式の往復");
		// 最大構成のサイズ
		Doc big;
		big.pins.resize(kMaxPins);
		big.bones.resize(kMaxBones);
		big.poses.resize(7);
		big.normalize();
		size_t sz = serialize(big).size();
		std::printf("       最大構成(ピン256・ボーン256・ポーズ7)のサイズ: %zu バイト / 上限 %zu\n", sz, kMaxBlob);
		check(sz <= kMaxBlob, "最大構成が保存容量に収まる");
	}

	// ---- 1a. 切れ込みの保存 ----
	{
		Doc d = fourPins();
		Cut c;
		c.points = { { -10, 0 }, { 0, 5 }, { 20, 0 } };
		c.enabled = false;
		d.cuts.push_back(c);
		d.cuts.push_back(Cut{ true, { { 1, 2 }, { 3, 4 } } });
		auto blob = serialize(d);
		Doc e;
		check(deserialize(blob.data(), blob.size(), e) && e.cuts.size() == 2 && !e.cuts[0].enabled && e.cuts[0].points.size() == 3 &&
				  e.cuts[0].points[1].y == 5 && e.cuts[1].points[1].x == 3 && e.pins.size() == 4,
			  "切れ込みの保存と読み込み");
		// v0.3.2 の読み込み (末尾の切れ込みを読まない) でもピン・ボーンは読める : 番号は 3 のまま
		check(blob[4] == 3 && blob[5] == 0, "保存の形式の番号は 3 のまま");
		// 切れ込みの無いデータには塊を書かない (v0.3.2 と同じバイト列)
		Doc f = fourPins();
		auto b2 = serialize(f);
		Doc g;
		check(deserialize(b2.data(), b2.size(), g) && g.cuts.empty() && std::memcmp(b2.data() + b2.size() - 4, "CUT1", 4) != 0,
			  "切れ込みが無ければ以前と同じ形");
		// 最大 (64本 × 32点)
		Doc big = fourPins();
		for (int i = 0; i < kMaxCuts + 3; i++) {
			Cut cc;
			cc.points.resize(kMaxCutPoints + 5);
			big.cuts.push_back(cc);
		}
		big.normalize();
		check(big.cuts.size() == (size_t)kMaxCuts && big.cuts[0].points.size() == (size_t)kMaxCutPoints, "切れ込みの数・点の数の上限");
		std::printf("       切れ込み最大のサイズ: %zu バイト\n", serialize(big).size());
		check(Doc::makeDefault().pins.empty(), "新しいエフェクトはピンなし");
	}

	// ---- 1b. 補間の選択の保存と、項目ごとの補間 ----
	{
		Doc d = fourPins();
		d.bones.push_back({ 0, 0, true, -1 });
		d.normalize();
		d.poses.resize(2);
		d.normalize();
		d.poses[0].pins[0].interp[1] = 3;
		d.poses[0].bones[0].interp[0] = 15;
		d.poses[0].pins[0].mx = 0; d.poses[1].pins[0].mx = 100;
		d.poses[0].pins[0].my = 0; d.poses[1].pins[0].my = 100;
		d.poses[0].bones[0].angle = 0; d.poses[1].bones[0].angle = 90;
		auto blob = serialize(d);
		Doc e;
		check(deserialize(blob.data(), blob.size(), e) && e.poses[0].pins[0].interp[1] == 3 && e.poses[0].bones[0].interp[0] == 15, "補間の選択の保存と読み込み");
		double fr[kPoseTracks] = {};
		fr[0] = 0.5; fr[3] = 0.1; fr[15] = 0.9;
		Pose out;
		blendPose(e.poses[0], e.poses[1], fr, kPoseTracks, e.pins.size(), e.bones.size(), out);
		check(std::fabs(out.pins[0].mx - 50) < 1e-4 && std::fabs(out.pins[0].my - 10) < 1e-4 && std::fabs(out.bones[0].angle - 81) < 1e-4,
			  "項目ごとに選んだポーズ補間の割合で補間される");
		// 旧形式(v1)のデータも読める
		std::vector<uint8_t> v1 = { 'P', 'D', 'F', '2', 1, 0, 1, 0, 0, 0, 1, 0 };
		float fx = 5, fy = 6; uint8_t* px = (uint8_t*)&fx; uint8_t* py = (uint8_t*)&fy;
		v1.insert(v1.end(), px, px + 4); v1.insert(v1.end(), py, py + 4); v1.push_back(1); v1.push_back(0xff); v1.push_back(0xff);
		int32_t fr0 = 0; uint8_t* pf = (uint8_t*)&fr0; v1.insert(v1.end(), pf, pf + 4);
		float mx = 7, my = 8; v1.insert(v1.end(), (uint8_t*)&mx, (uint8_t*)&mx + 4); v1.insert(v1.end(), (uint8_t*)&my, (uint8_t*)&my + 4);
		Doc old;
		check(deserialize(v1.data(), v1.size(), old) && old.pins[0].x == 5 && old.poses[0].pins[0].mx == 7 && old.poses[0].pins[0].interp[0] == 0,
			  "以前の形式(v1)のデータも読める");
	}

	// ---- 1c. 揺れ位相は全区間で共通 (開始点の値) ----
	{
		Doc d;
		d.bones.push_back({ 0, 0, true, -1 });
		d.normalize();
		d.poses.resize(2);
		d.normalize();
		d.bones[0].phase = 90;
		d.poses[0].bones[0].propagate = 10; d.poses[1].bones[0].propagate = 30;
		d.poses[0].bones[0].interp[4] = 2;  // 揺れ伝播
		auto blob = serialize(d);
		Doc e;
		check(deserialize(blob.data(), blob.size(), e) && e.bones[0].phase == 90 && e.poses[0].bones[0].interp[4] == 2, "揺れ位相の保存と読み込み");
		// v2 形式 (揺れ位相が中間点ごと) からの読み込み : 開始点の値を使い、補間の選択は位相の分を除いて詰める
		std::vector<uint8_t> v2 = { 'P', 'D', 'F', '2', 2, 0, 0, 0, 1, 0, 2, 0 };
		auto putf = [&](float f) { uint8_t* q = (uint8_t*)&f; v2.insert(v2.end(), q, q + 4); };
		putf(0); putf(0); v2.push_back(1); v2.push_back(0xff); v2.push_back(0xff);  // ボーン
		for (int k = 0; k < 2; k++) {
			int32_t f = k * 10; v2.insert(v2.end(), (uint8_t*)&f, (uint8_t*)&f + 4);
			putf(0); putf(100); putf(0); putf(1);
			putf(k == 0 ? 45.f : 135.f);  // 揺れ位相
			putf(100); putf(60);
			for (uint8_t v : { 1, 2, 3, 4, 5, 6, 7 }) v2.push_back(v);
		}
		Doc old;
		check(deserialize(v2.data(), v2.size(), old) && old.bones[0].phase == 45 && old.poses[1].bones[0].interp[3] == 4 &&
				  old.poses[1].bones[0].interp[4] == 6 && old.poses[1].bones[0].interp[5] == 7,
			  "以前の形式(v2)の揺れ位相は開始点の値になる");
	}

	// ---- 2. ポーズの対応付け ----
	{
		Doc d = fourPins();
		d.poses.resize(3);
		d.normalize();
		d.poses[0].frame = 0;
		d.poses[1].frame = 50;
		d.poses[2].frame = 100;
		d.poses[0].pins[0].mx = 0;
		d.poses[1].pins[0].mx = 10;
		d.poses[2].pins[0].mx = 30;
		auto same = mapPoses(d, { 0, 70, 100 });  // 中間点の移動
		check(same[1].pins[0].mx == 10 && same[1].frame == 70, "点の数が同じなら順番で対応 (中間点の移動)");
		auto ins = mapPoses(d, { 0, 25, 50, 100 });  // 中間点の追加
		check(std::fabs(ins[1].pins[0].mx - 5) < 1e-6 && ins[2].pins[0].mx == 10 && ins[3].pins[0].mx == 30,
			  "中間点の追加では前後を時間で補間");
		auto del = mapPoses(d, { 0, 100 });  // 中間点の削除
		check(del[0].pins[0].mx == 0 && del[1].pins[0].mx == 30, "中間点の削除");
	}

	// ---- 3. 揺れ (YMM4の計測結果と同じ式になっているか) ----
	{
		Doc d;
		for (int i = 0; i < 4; i++) d.bones.push_back({ 0, (float)(-100 * i), true, (int16_t)(i - 1) });
		d.normalize();
		BonePose& s = d.poses[0].bones[1];
		s.swing = 45;
		s.period = 3;
		d.bones[1].phase = 0;
		s.propagate = 50;
		for (float sh : { 0.f, 30.f, 60.f }) {
			s.shinari = sh;
			double maxErr = 0;
			for (int k = 0; k < 60; k++) {
				double t = k * 0.05;
				std::vector<double> add;
				computeSwing(d, d.poses[0], t, add);
				const double w = 2 * 3.14159265358979 * t / 3;
				const double e1 = 45 * std::sin(w), e2 = 22.5 * std::sin(w - sh * 3.14159265358979 / 180),
							 e3 = 11.25 * std::sin(w - 2 * sh * 3.14159265358979 / 180);
				maxErr = std::fmax(maxErr, std::fabs(add[1] - e1) + std::fabs(add[2] - e2) + std::fabs(add[3] - e3) + std::fabs(add[0]));
			}
			char msg[128];
			std::snprintf(msg, sizeof msg, "揺れの式 (しなり %.0f°) 最大誤差 %.2e", sh, maxErr);
			check(maxErr < 1e-9, msg);
		}
		// 計測値との比較 : 一番上の区間の、下の区間に対する相対角の振幅と遅れ
		const double meas[3][2] = { { 32.8, 2.4 }, { 33.2, 42.7 }, { 32.1, 80.7 } };
		int idx = 0;
		for (float sh : { 0.f, 30.f, 60.f }) {
			s.shinari = sh;
			double a = 0, b = 0;
			const int N = 600;
			for (int k = 0; k < N; k++) {
				double t = 3.0 * k / N;
				std::vector<double> add;
				computeSwing(d, d.poses[0], t, add);
				double rel = add[2] + add[3];
				a += rel * std::sin(2 * 3.14159265358979 * t / 3) * 2 / N;
				b += rel * std::cos(2 * 3.14159265358979 * t / 3) * 2 / N;
			}
			double amp = std::hypot(a, b), lag = std::atan2(-b, a) * 180 / 3.14159265358979;
			std::printf("       しなり%2.0f°: 計算 振幅%.1f° 遅れ%.1f° / YMM4の動画の計測 振幅%.1f° 遅れ%.1f°\n", sh, amp, lag,
						meas[idx][0], meas[idx][1]);
			idx++;
		}
	}

	// ---- 4. ボーンの順運動学 ----
	{
		Doc d;
		d.bones.push_back({ 0, 0, true, -1 });
		d.bones.push_back({ 0, -100, true, 0 });
		d.bones.push_back({ 0, -200, true, 1 });
		d.pins.push_back({ 10, -150, true, 1 });
		d.normalize();
		d.poses[0].bones[1].angle = 90;  // 親(0)を中心に時計回り90° → (100,0)へ
		RigResult r;
		evaluateRig(d, d.poses[0], 0, r);
		check(std::fabs(r.boneCur[1].x - 100) < 1e-9 && std::fabs(r.boneCur[1].y) < 1e-9, "角度は親のジョイントを中心に回す (時計回り)");
		check(std::fabs(r.boneCur[2].x - 200) < 1e-9 && std::fabs(r.boneCur[2].y) < 1e-9, "子ボーンも一緒に回る");
		check(std::fabs(r.pinCur[0].x - 150) < 1e-9 && std::fabs(r.pinCur[0].y - 10) < 1e-9, "紐付いたピンも一緒に回る");
		d.poses[0].bones[1].angle = 0;
		d.poses[0].bones[1].stretch = 150;
		evaluateRig(d, d.poses[0], 0, r);
		check(std::fabs(r.boneCur[1].y + 150) < 1e-9 && std::fabs(r.boneCur[2].y + 250) < 1e-9 && std::fabs(r.pinCur[0].y + 200) < 1e-9,
			  "伸縮は親からの距離を変え、子とピンは平行移動");
		d.poses[0].bones[0].angle = 45;
		d.poses[0].bones[0].stretch = 300;
		d.poses[0].bones[1].stretch = 100;
		evaluateRig(d, d.poses[0], 0, r);
		check(std::fabs(r.boneCur[1].y + 100) < 1e-9 && std::fabs(r.boneCur[1].x) < 1e-9, "根元のボーンの角度・伸縮は効果なし");
		d.poses[0].bones[1].angle = 90;
		d.bones[1].enabled = false;
		evaluateRig(d, d.poses[0], 0, r);
		check(std::fabs(r.boneCur[1].x) < 1e-9 && std::fabs(r.boneCur[2].y + 200) < 1e-9, "無効なボーンは角度0°・伸縮100%と同じ");
		d.bones[1].enabled = true;
	}

	// ---- 5. 手の画像で中指のボーンを回す (YMM4の画面と比較) ----
	{
		const int W = 560, H = 640;
		Img hand = makeHand(W, H);
		Doc d;
		d.bones.push_back({ 0, 0, true, -1 });        // 中指の付け根
		d.bones.push_back({ 0, -230, true, 0 });      // 中指の先
		d.pins.push_back({ -110, -190, true, -1 });   // 人差し指の先
		d.pins.push_back({ 105, -160, true, -1 });    // 薬指の先
		d.pins.push_back({ 190, -80, true, -1 });     // 小指の先
		d.pins.push_back({ -230, 20, true, -1 });     // 親指の先
		d.pins.push_back({ 0, 120, true, -1 });       // 手のひらの中央
		d.normalize();
		savePGM(dir + "/hand_orig.pgm", hand.a, W, H);
		Deformer def;
		for (double ang : { -15.0, 15.0 }) {
			d.poses[0].bones[1].angle = (float)ang;
			RigResult r;
			evaluateRig(d, d.poses[0], 0, r);
			std::vector<Handle> hs;
			buildHandles(d, r, hs);
			for (Method m : { Method::ARAP, Method::MLS }) {
				MeshResult mr;
				auto t0 = std::chrono::steady_clock::now();
				def.deform(W, H, hand.mask().data(), hs, m, 2.0, mr);
				auto t1 = std::chrono::steady_clock::now();
				double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
				auto img = render(hand, mr, W, H);
				char name[128];
				std::snprintf(name, sizeof name, "/hand_%s_%+.0f.pgm", m == Method::ARAP ? "arap" : "mls", ang);
				savePGM(dir + name, img, W, H);
				// 中指の先の位置 (期待: 付け根を中心に回転)
				Vec2 tip = r.boneCur[1];
				std::printf("       %s 角度%+.0f°: 処理 %.1f ms, 指先 (%.1f, %.1f)\n", m == Method::ARAP ? "ARAP" : "MLS ", ang, ms, tip.x, tip.y);
			}
		}
	}

	// ---- 6. 揺れるバー (YMM4の動画と同じ構成) ----
	{
		const int W = 40, H = 600;
		Img bar = makeBar(W, H);
		Doc d;
		for (int i = 0; i < 4; i++) d.bones.push_back({ 0, (float)(280 - 186.67 * i), true, (int16_t)(i - 1) });
		d.normalize();
		BonePose& s = d.poses[0].bones[1];
		s.swing = 45;
		s.period = 3;
		s.propagate = 50;
		s.shinari = 60;
		Deformer def;
		int k = 0;
		for (double t : { 0.0, 0.375, 0.75, 1.5, 2.25 }) {
			RigResult r;
			evaluateRig(d, d.poses[0], t, r);
			std::vector<Handle> hs;
			buildHandles(d, r, hs);
			MeshResult mr;
			def.deform(W, H, bar.mask().data(), hs, Method::ARAP, 2.0, mr);
			const int OW = 900, OH = 700;
			auto img = render(bar, mr, OW, OH);
			char name[64];
			std::snprintf(name, sizeof name, "/bar_%d.pgm", k++);
			savePGM(dir + name, img, OW, OH);
		}
	}

	// ---- 6b. ARAPの精度 (十分反復して収束させた結果との差) と連続フレームの処理時間 ----
	{
		const int W = 560, H = 640;
		Img hand = makeHand(W, H);
		auto mask = hand.mask();
		Doc d;
		d.bones.push_back({ 0, 0, true, -1 });
		d.bones.push_back({ 0, -230, true, 0 });
		d.pins.push_back({ -110, -190, true, -1 });
		d.pins.push_back({ 105, -160, true, -1 });
		d.pins.push_back({ 190, -80, true, -1 });
		d.pins.push_back({ -230, 20, true, -1 });
		d.pins.push_back({ 0, 120, true, -1 });
		d.normalize();
		d.poses[0].bones[1].swing = 30;
		d.poses[0].bones[1].period = 1;
		Deformer fast, ref;
		ref.arapIterations = 600;
		double total = 0, maxDiff = 0;
		const int N = 30;
		for (int f = 0; f < N; f++) {
			RigResult r;
			evaluateRig(d, d.poses[0], f / 30.0, r);
			std::vector<Handle> hs;
			buildHandles(d, r, hs);
			MeshResult a, b;
			auto t0 = std::chrono::steady_clock::now();
			fast.deform(W, H, mask.data(), hs, Method::ARAP, 2, a);
			auto t1 = std::chrono::steady_clock::now();
			total += std::chrono::duration<double, std::milli>(t1 - t0).count();
			ref.deform(W, H, mask.data(), hs, Method::ARAP, 2, b);
			for (size_t k = 0; k < a.cur.size(); k++) maxDiff = std::fmax(maxDiff, std::hypot(a.cur[k].x - b.cur[k].x, a.cur[k].y - b.cur[k].y));
		}
		std::printf("       連続30フレームのARAP: 平均 %.1f ms / フレーム、収束させた結果との最大差 %.3f px\n", total / N, maxDiff);
		check(maxDiff < 3.0, "ARAPが収束値に十分近い (ピン5本の手で3px未満)");
	}

	// ---- 6c. グループ制御 : 2つのオブジェクトに分けて変形しても、1枚の絵として変形したのと同じになるか ----
	{
		// 手の画像を左右2つのオブジェクトに分け、右側は回転・拡大して配置したものとして扱う
		const int W = 560, H = 640;
		Img hand = makeHand(W, H);
		Doc d;
		d.bones.push_back({ 0, 0, true, -1 });
		d.bones.push_back({ 0, -230, true, 0 });
		d.pins.push_back({ -110, -190, true, -1 });
		d.pins.push_back({ 190, -80, true, -1 });
		d.pins.push_back({ 0, 120, true, -1 });
		d.normalize();
		d.poses[0].bones[1].angle = 20;
		d.poses[0].pins[1].mx = 30;
		RigResult r;
		evaluateRig(d, d.poses[0], 0, r);
		std::vector<Handle> hs;
		buildHandles(d, r, hs);
		// グループ全体の変形の場 (シーン 1280x720 を含む範囲)
		const Vec2 dc{ 0, 0 };
		Deformer fd;
		MeshResult field;
		fd.deform(1400, 900, nullptr, hs, Method::MLS, 2.0, field);
		// オブジェクト2 : 右半分を切り出した画像。グループ上では (140,0) を中心に置かれ、画像は −30°回転・0.5倍で用意されている
		Xform2D xf;
		xf.x = 140; xf.y = 0; xf.rz = 30; xf.sx = xf.sy = 2.0;  // 画像座標 → グループ座標
		double maxErr = 0;
		for (int k = 0; k < 2000; k++) {
			const Vec2 u{ std::fmod(k * 37.0, 140.0) - 70, std::fmod(k * 53.0, 320.0) - 160 };  // オブジェクト2の画像座標
			const Vec2 g = xf.apply(u);
			const Vec2 viaObject = xf.apply(xf.inverse(sampleField(field, dc, g)));  // オブジェクト側で変形 → 標準描画で配置
			const Vec2 direct = mlsRigid(hs, g, 2.0);                                 // 1枚の絵として直接変形
			maxErr = std::fmax(maxErr, std::hypot(viaObject.x - direct.x, viaObject.y - direct.y));
		}
		char msg[160];
		std::snprintf(msg, sizeof msg, "グループ制御: オブジェクトごとに変形しても1枚の絵の変形と一致 (最大差 %.2f px)", maxErr);
		check(maxErr < 1.5, msg);
		// 座標変換の往復
		const Vec2 p{ 12.3, -45.6 };
		const Vec2 q = xf.inverse(xf.apply(p));
		check(std::hypot(q.x - p.x, q.y - p.y) < 1e-9, "座標変換 (位置・回転・拡大率) の往復");
	}

	// ---- 7. 処理時間 (最大構成に近い場合) ----
	{
		Doc d;
		for (int i = 0; i < 256; i++) d.pins.push_back({ (float)(std::fmod(i * 37.0, 1800) - 900), (float)(std::fmod(i * 53.0, 1000) - 500), true, -1 });
		for (int i = 0; i < 256; i++) d.bones.push_back({ (float)(-900 + 7 * i), (float)(std::sin(i * 0.1) * 300), true, (int16_t)(i % 16 == 0 ? -1 : i - 1) });
		d.normalize();
		for (int i = 0; i < 256; i++) d.poses[0].bones[i].angle = 2;
		RigResult r;
		evaluateRig(d, d.poses[0], 0, r);
		std::vector<Handle> hs;
		buildHandles(d, r, hs);
		Deformer def;
		for (Method m : { Method::ARAP, Method::MLS }) {
			MeshResult mr;
			for (int rep = 0; rep < 2; rep++) {
				auto t0 = std::chrono::steady_clock::now();
				def.deform(1920, 1080, nullptr, hs, m, 2.0, mr);
				auto t1 = std::chrono::steady_clock::now();
				std::printf("       1920x1080 制御点%zu個 %s %s: %.1f ms\n", hs.size(), m == Method::ARAP ? "ARAP" : "MLS ",
							rep == 0 ? "初回" : "2回目", std::chrono::duration<double, std::milli>(t1 - t0).count());
			}
		}
	}

	std::printf(fails ? "失敗 %d 件\n" : "すべて成功\n", fails);
	return fails ? 1 : 0;
}
