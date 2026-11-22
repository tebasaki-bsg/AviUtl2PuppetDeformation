// 腕の先にボールが付いた絵で、ボーンを回したときにボールが歪まないかを測る
#define main main_unused
#include "test_engine.cpp"
#undef main
#include <functional>

// 体(右側の縦長)・腕(斜め)・ボール(円) の絵
static Img makeArmBall(int W, int H) {
	Img im{ W, H, std::vector<float>(W * H, 0) };
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			double px = x + 0.5 - W / 2.0, py = y + 0.5 - H / 2.0, d = 1e9;
			d = std::fmin(d, std::hypot(px + 100, py + 120) - 75);                          // ボール (中心 -100,-120)
			// 腕 : (-60,-60) から (60,140) への太さ28の線
			double ax = -60, ay = -60, bx = 60, by = 140, vx = bx - ax, vy = by - ay;
			double t = std::fmin(1, std::fmax(0, ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy)));
			d = std::fmin(d, std::hypot(px - (ax + vx * t), py - (ay + vy * t)) - 28);
			// 体 : x 110〜260
			d = std::fmin(d, std::fmax(110 - px, px - 260));
			im.a[y * W + x] = (float)std::fmin(1, std::fmax(0, 0.5 - d));
		}
	return im;
}

int main(int argc, char** argv) {
	std::string dir = argc > 1 ? argv[1] : ".";
	const int W = 560, H = 480;
	Img img = makeArmBall(W, H);
	auto mask = img.mask();
	savePGM(dir + "/armball_orig.pgm", img.a, W, H);
	Doc d;
	d.bones.push_back({ 60, 140, true, -1 });     // 腕の付け根
	d.bones.push_back({ -100, -120, true, 0 });   // ボールの中心
	d.pins.push_back({ 180, -180, true, -1 });    // 体の上
	d.pins.push_back({ 180, 0, true, -1 });       // 体の中
	d.normalize();
	d.poses[0].bones[1].angle = 25;
	RigResult r;
	evaluateRig(d, d.poses[0], 0, r);
	std::vector<Handle> hs;
	buildHandles(d, r, hs);
	// 理想 : ボールと腕は 腕の付け根を中心に 25°回転
	const double th = 25 * 3.14159265358979 / 180, c = std::cos(th), s = std::sin(th);
	auto ideal = [&](Vec2 p) { double dx = p.x - 60, dy = p.y - 140; return Vec2{ 60 + c * dx - s * dy, 140 + s * dx + c * dy }; };
	auto measure = [&](const MeshResult& m, const char* name) {
		double maxErr = 0;
		for (size_t k = 0; k < m.rest.size(); k++) {
			const Vec2 p = m.rest[k];
			if (std::hypot(p.x + 100, p.y + 120) > 70) continue;  // ボールの内側の頂点だけ
			const Vec2 q = ideal(p);
			maxErr = std::fmax(maxErr, std::hypot(m.cur[k].x - q.x, m.cur[k].y - q.y));
		}
		std::printf("  %-34s ボールの歪み (理想の回転との最大差) %6.2f px\n", name, maxErr);
		return maxErr;
	};
	auto timed = [&](const char* name, std::function<void(MeshResult&)> f, const char* file) {
		MeshResult m;
		auto t0 = std::chrono::steady_clock::now();
		f(m);
		auto t1 = std::chrono::steady_clock::now();
		std::printf("  (%5.1f ms)", std::chrono::duration<double, std::milli>(t1 - t0).count());
		const double e = measure(m, name);
		savePGM(dir + "/" + file, render(img, m, W, H), W, H);
		return e;
	};
	std::printf("[1枚の画像として変形]\n");
	timed("以前 (粗い格子なし)", [&](MeshResult& m) { Deformer df; df.deform(W, H, mask.data(), hs, Method::ARAP, 2, m); }, "armball_old.pgm");
	const double eNew = timed("今回 (粗い格子で先に計算)", [&](MeshResult& m) { Deformer df; df.deform(W, H, mask.data(), hs, Method::ARAP, 2, m); }, "armball_new.pgm");
	timed("参考: 収束するまで計算", [&](MeshResult& m) { Deformer df; df.arapIterations = 3000; df.deform(W, H, mask.data(), hs, Method::ARAP, 2, m); }, "armball_ref.pgm");
	std::printf("[グループ制御: 網目を不透明部分に限定しない場合 (v0.3.0)]\n");
	{
		Deformer df;
		MeshResult field;
		df.deform(W + 200, H + 200, nullptr, hs, Method::ARAP, 2, field);
		MeshResult m;
		Deformer base;
		base.deform(W, H, mask.data(), {}, Method::MLS, 1, m);
		for (size_t k = 0; k < m.rest.size(); k++) m.cur[k] = sampleField(field, { 0, 0 }, m.rest[k]);
		measure(m, "v0.3.0 のグループ制御");
		savePGM(dir + "/armball_group_old.pgm", render(img, m, W, H), W, H);
	}
	// ---- 今回のグループ制御 : 2つのオブジェクトに分けた場合 ----
	// オブジェクトA : 左半分 (ボール・腕) を、0.5倍に縮小し −30°回転した画像として持ち、標準描画で元の位置に戻す
	// オブジェクトB : 右半分 (体) をそのまま
	double eGroup = 0;
	{
		struct Child { Xform2D xf; int w, h; std::vector<uint8_t> a; };
		std::vector<Child> ch;
		{
			Child A;
			A.w = 300; A.h = 300;  // 画像A (縮小・回転済み) の大きさ
			A.xf.x = -130; A.xf.y = 0; A.xf.rz = 30; A.xf.sx = A.xf.sy = 2.0;
			A.a.assign(A.w * A.h, 0);
			for (int y = 0; y < A.h; y++) for (int x = 0; x < A.w; x++) {
				Vec2 g = A.xf.apply({ x + 0.5 - A.w / 2.0, y + 0.5 - A.h / 2.0 });
				if (g.x > 100) continue;  // 左側だけ
				A.a[y * A.w + x] = (uint8_t)std::lround(img.at(g.x + W / 2.0, g.y + H / 2.0) * 255);
			}
			ch.push_back(A);
			Child B;
			B.w = 180; B.h = H;
			B.xf.x = 190; B.xf.y = 0;
			B.a.assign(B.w * B.h, 0);
			for (int y = 0; y < B.h; y++) for (int x = 0; x < B.w; x++) {
				Vec2 g = B.xf.apply({ x + 0.5 - B.w / 2.0, y + 0.5 - B.h / 2.0 });
				B.a[y * B.w + x] = (uint8_t)std::lround(img.at(g.x + W / 2.0, g.y + H / 2.0) * 255);
			}
			ch.push_back(B);
		}
		// 合成したα (フィルタ処理と同じ作り方 : 縮小した画素をグループ座標に置く)
		const int Wd = W + 120, Hd = H + 120;
		std::vector<uint8_t> uni(Wd * Hd, 0);
		for (auto& c : ch) {
			const int ds = std::max(1, std::max(c.w, c.h) / 384);
			const double scale = std::max(std::fabs(c.xf.sx), std::fabs(c.xf.sy));
			const int rad = (int)std::ceil(ds * scale * 0.75) + 1;
			for (int y = 0; y < c.h; y += ds) for (int x = 0; x < c.w; x += ds) {
				bool any = false;
				for (int yy = y; yy < std::min(c.h, y + ds); yy++) for (int xx = x; xx < std::min(c.w, x + ds); xx++) any |= c.a[yy * c.w + xx] != 0;
				if (!any) continue;
				Vec2 g = c.xf.apply({ x + ds / 2.0 - c.w / 2.0, y + ds / 2.0 - c.h / 2.0 });
				int px = (int)std::floor(g.x + Wd / 2.0), py = (int)std::floor(g.y + Hd / 2.0);
				for (int yy = std::max(0, py - rad); yy <= std::min(Hd - 1, py + rad); yy++)
					for (int xx = std::max(0, px - rad); xx <= std::min(Wd - 1, px + rad); xx++) uni[yy * Wd + xx] = 255;
			}
		}
		// v0.4.0 : 合成したαの輪郭 (長い辺 1024px まで縮めて取る) に沿った三角形の網目
		TriDeformer fd;
		fd.contourMaxSide = 1024;
		TriResult field;
		const std::vector<std::vector<Vec2>> noCuts;
		auto t0 = std::chrono::steady_clock::now();
		fd.deform(Wd, Hd, uni.data(), noCuts, hs, Method::ARAP, 2, 0.02, field);
		auto t1 = std::chrono::steady_clock::now();
		std::vector<Handle> hs2 = hs;
		for (auto& h : hs2) h.cur.x += 0.5;
		fd.deform(Wd, Hd, uni.data(), noCuts, hs2, Method::ARAP, 2, 0.02, field);
		fd.deform(Wd, Hd, uni.data(), noCuts, hs, Method::ARAP, 2, 0.02, field);
		auto t2 = std::chrono::steady_clock::now();
		std::printf("[グループ制御: v0.4.0 (配下を合わせた輪郭に沿った三角形の網目)] 初回 %.1f ms / 制御点だけ動いたとき %.1f ms\n",
			std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count() / 2);
		// オブジェクトAの頂点を変形 (画像座標 → グループ → 変形 → 画像座標) し、グループ座標でボールの歪みを測る
		Deformer base;
		MeshResult m;
		base.deform(ch[0].w, ch[0].h, ch[0].a.data(), {}, Method::MLS, 1, m);
		MeshResult inGroup = m;
		for (size_t k = 0; k < m.rest.size(); k++) {
			const Vec2 g = ch[0].xf.apply(m.rest[k]);
			const Vec2 g2 = sampleTriField(fd, field, { 0, 0 }, g);
			m.cur[k] = ch[0].xf.inverse(g2);
			inGroup.rest[k] = g;
			inGroup.cur[k] = ch[0].xf.apply(m.cur[k]);  // 標準描画で配置した結果
		}
		eGroup = measure(inGroup, "今回のグループ制御 (オブジェクトA)");
		// 2つのオブジェクトを描いて保存
		std::vector<float> out(W * H, 0);
		auto drawChild = [&](const Child& c) {
			Img ci{ c.w, c.h, std::vector<float>(c.w * c.h) };
			for (int i = 0; i < c.w * c.h; i++) ci.a[i] = c.a[i] / 255.0f;
			Deformer bd; MeshResult cm; bd.deform(c.w, c.h, c.a.data(), {}, Method::MLS, 1, cm);
			for (size_t k = 0; k < cm.rest.size(); k++) {
				const Vec2 g = c.xf.apply(cm.rest[k]);
				cm.cur[k] = sampleTriField(fd, field, { 0, 0 }, g);  // グループ座標に直接置く
			}
			// 描画用に rest を画像座標のまま、cur をグループ座標として render
			auto img2 = render(ci, cm, W, H);
			for (int i = 0; i < W * H; i++) out[i] = std::fmax(out[i], img2[i]);
		};
		for (auto& c : ch) drawChild(c);
		savePGM(dir + "/armball_group_new.pgm", out, W, H);
	}
	return (eNew < 3 && eGroup < 3) ? 0 : 1;
}
