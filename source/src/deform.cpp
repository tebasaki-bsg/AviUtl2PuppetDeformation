// パペット変形 for AviUtl2 - 変形エンジン
#include "deform.h"

#include "arap.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <array>
#include <cmath>
#include <cstring>
#include <thread>

namespace pd {

namespace {

constexpr double kPi = 3.14159265358979323846;
inline double rad(double deg) { return deg * kPi / 180.0; }

template <class F>
// work : 処理量の目安 (少ない場合はスレッドを作る手間の方が大きいので、そのまま処理する)
void parallelFor(int n, F&& f, size_t work) {
	unsigned hc = std::thread::hardware_concurrency();
	int threads = (int)std::min<unsigned>(hc ? hc : 1, 16);
	if (n < 512 || work < 200000 || threads <= 1) {
		for (int i = 0; i < n; i++) f(i);
		return;
	}
	std::vector<std::thread> pool;
	const int chunk = (n + threads - 1) / threads;
	for (int t = 0; t < threads; t++) {
		const int s = t * chunk, e = std::min(n, s + chunk);
		if (s >= e) break;
		pool.emplace_back([&f, s, e] {
			for (int i = s; i < e; i++) f(i);
		});
	}
	for (auto& th : pool) th.join();
}

// 親より先に子を処理しないよう、深さ順に並べる
std::vector<int> boneOrder(const Doc& doc, std::vector<int>* depthOut = nullptr) {
	const int n = (int)doc.bones.size();
	std::vector<int> depth(n, 0);
	for (int i = 0; i < n; i++) {
		int d = 0, p = doc.bones[i].parent;
		while (p >= 0 && d <= n) {
			d++;
			p = doc.bones[p].parent;
		}
		depth[i] = d;
	}
	std::vector<int> order(n);
	for (int i = 0; i < n; i++) order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth[a] < depth[b]; });
	if (depthOut) *depthOut = std::move(depth);
	return order;
}

uint64_t fnv(uint64_t h, const void* p, size_t n) {
	auto b = (const uint8_t*)p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// 揺れ
//   揺れているボーン : 揺れ角度 × sin(2π t / 周期 + 位相)
//   k世代下の子孫     : 揺れ角度 × 伝播^k × sin(2π t / 周期 + 位相 − k × しなり)
void computeSwing(const Doc& doc, const Pose& pose, double time, std::vector<double>& addDeg) {
	const int n = (int)doc.bones.size();
	addDeg.assign(n, 0.0);
	std::vector<std::vector<int>> children(n);
	for (int i = 0; i < n; i++) {
		const int p = doc.bones[i].parent;
		if (p >= 0 && p < n) children[p].push_back(i);
	}
	for (int a = 0; a < n; a++) {
		if (!doc.bones[a].enabled) continue;
		const BonePose& bp = pose.bones[a];
		if (bp.swing == 0 || !(bp.period > 0)) continue;
		const double base = 2.0 * kPi * time / bp.period + rad(doc.bones[a].phase);
		const double prop = bp.propagate / 100.0;
		const double lag = rad(bp.shinari);
		addDeg[a] += bp.swing * std::sin(base);
		// 子孫へ伝える (深さ優先)
		struct Item { int bone, depth; };
		std::vector<Item> stack;
		for (int c : children[a]) stack.push_back({ c, 1 });
		while (!stack.empty()) {
			const Item it = stack.back();
			stack.pop_back();
			if (it.depth > n) continue;
			const double amp = bp.swing * std::pow(prop, it.depth);
			addDeg[it.bone] += amp * std::sin(base - it.depth * lag);
			for (int c : children[it.bone]) stack.push_back({ c, it.depth + 1 });
		}
	}
}

void evaluateRig(const Doc& doc, const Pose& pose, double time, RigResult& out) {
	const int nb = (int)doc.bones.size();
	out.boneCur.assign(nb, Vec2{});
	out.boneRot.assign(nb, 0.0);
	std::vector<double> swing;
	computeSwing(doc, pose, time, swing);

	for (int b : boneOrder(doc)) {
		const Bone& bone = doc.bones[b];
		const int p = bone.parent;
		if (p < 0) {
			// 親が無いボーンは角度・伸縮の効果なし
			out.boneCur[b] = { bone.x, bone.y };
			out.boneRot[b] = 0;
			continue;
		}
		const BonePose& bp = pose.bones[b];
		const double own = bone.enabled ? bp.angle : 0.0;
		const double s = bone.enabled ? bp.stretch / 100.0 : 1.0;
		const double rot = out.boneRot[p] + rad(own + swing[b]);
		const double dx = (bone.x - doc.bones[p].x) * s;
		const double dy = (bone.y - doc.bones[p].y) * s;
		const double c = std::cos(rot), sn = std::sin(rot);
		out.boneRot[b] = rot;
		out.boneCur[b] = { out.boneCur[p].x + c * dx - sn * dy, out.boneCur[p].y + sn * dx + c * dy };
	}

	const int np = (int)doc.pins.size();
	out.pinCur.assign(np, Vec2{});
	for (int i = 0; i < np; i++) {
		const Pin& pin = doc.pins[i];
		Vec2 c{ pin.x, pin.y };
		if (pin.enabled) {
			const int b = pin.bone;
			if (b >= 0 && b < nb) {
				const double rx = pin.x - doc.bones[b].x, ry = pin.y - doc.bones[b].y;
				const double cs = std::cos(out.boneRot[b]), sn = std::sin(out.boneRot[b]);
				c = { out.boneCur[b].x + cs * rx - sn * ry, out.boneCur[b].y + sn * rx + cs * ry };
			}
			c.x += pose.pins[i].mx;
			c.y += pose.pins[i].my;
		}
		out.pinCur[i] = c;
	}
}

void buildHandles(const Doc& doc, const RigResult& rig, std::vector<Handle>& out) {
	out.clear();
	for (size_t i = 0; i < doc.pins.size(); i++) {
		out.push_back({ { doc.pins[i].x, doc.pins[i].y }, rig.pinCur[i] });
	}
	const int nb = (int)doc.bones.size();
	std::vector<int> childCount(nb, 0);
	for (int b = 0; b < nb; b++) {
		const int p = doc.bones[b].parent;
		if (p >= 0) childCount[p]++;
	}
	// 親から子への線を、まとまって動く点列として加える
	static const double kSamples[] = { 0.25, 0.5, 0.75, 1.0 };
	for (int b = 0; b < nb; b++) {
		const int p = doc.bones[b].parent;
		if (p < 0) {
			if (childCount[b] > 0) out.push_back({ { doc.bones[b].x, doc.bones[b].y }, rig.boneCur[b] });
			continue;
		}
		const Vec2 r0{ doc.bones[p].x, doc.bones[p].y }, r1{ doc.bones[b].x, doc.bones[b].y };
		const Vec2 c0 = rig.boneCur[p], c1 = rig.boneCur[b];
		for (double u : kSamples) {
			out.push_back({ { r0.x + (r1.x - r0.x) * u, r0.y + (r1.y - r0.y) * u },
							{ c0.x + (c1.x - c0.x) * u, c0.y + (c1.y - c0.y) * u } });
		}
	}
}

Vec2 Xform2D::apply(const Vec2& u) const {
	const double px = (u.x - cx) * sx, py = (u.y - cy) * sy;
	const double c = std::cos(rad(rz)), s = std::sin(rad(rz));
	return { x + c * px - s * py, y + s * px + c * py };
}

Vec2 Xform2D::inverse(const Vec2& g) const {
	const double dx = g.x - x, dy = g.y - y;
	const double c = std::cos(rad(rz)), s = std::sin(rad(rz));
	const double px = c * dx + s * dy, py = -s * dx + c * dy;
	return { (sx != 0 ? px / sx : 0) + cx, (sy != 0 ? py / sy : 0) + cy };
}

Vec2 sampleField(const MeshResult& f, const Vec2& offset, const Vec2& p) {
	if (f.cols <= 0 || f.rows <= 0 || f.cur.empty()) return p;
	const double lx = p.x - offset.x + f.w * 0.5, ly = p.y - offset.y + f.h * 0.5;
	const double fx = std::clamp(lx / f.w * f.cols, 0.0, (double)f.cols);
	const double fy = std::clamp(ly / f.h * f.rows, 0.0, (double)f.rows);
	const int i = std::min((int)fx, f.cols - 1), j = std::min((int)fy, f.rows - 1);
	const double u = fx - i, v = fy - j;
	auto disp = [&](int a, int b) {
		const int k = f.index(a, b);
		return Vec2{ f.cur[k].x - f.rest[k].x, f.cur[k].y - f.rest[k].y };
	};
	const Vec2 d00 = disp(i, j), d10 = disp(i + 1, j), d01 = disp(i, j + 1), d11 = disp(i + 1, j + 1);
	const double dx = (d00.x * (1 - u) + d10.x * u) * (1 - v) + (d01.x * (1 - u) + d11.x * u) * v;
	const double dy = (d00.y * (1 - u) + d10.y * u) * (1 - v) + (d01.y * (1 - u) + d11.y * u) * v;
	return { p.x + dx, p.y + dy };
}

bool handlesMoved(const std::vector<Handle>& h, double eps) {
	for (auto& x : h) {
		if (std::fabs(x.cur.x - x.rest.x) > eps || std::fabs(x.cur.y - x.rest.y) > eps) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// MLS剛体変形 (Schaefer et al. 2006)
// 精度のため v を原点とした相対座標で計算し、重みは最近傍の制御点で正規化する
Vec2 mlsRigid(const std::vector<Handle>& h, const Vec2& v, double alpha) {
	const size_t n = h.size();
	if (n == 0) return v;
	if (n == 1) return { v.x + h[0].cur.x - h[0].rest.x, v.y + h[0].cur.y - h[0].rest.y };
	double dmin = 1e300;
	size_t imin = 0;
	for (size_t i = 0; i < n; i++) {
		const double dx = h[i].rest.x - v.x, dy = h[i].rest.y - v.y;
		const double d2 = dx * dx + dy * dy;
		if (d2 < dmin) {
			dmin = d2;
			imin = i;
		}
	}
	if (dmin < 1e-10) return { h[imin].cur.x + v.x - h[imin].rest.x, h[imin].cur.y + v.y - h[imin].rest.y };

	double sw = 0, psx = 0, psy = 0, qsx = 0, qsy = 0, ar = 0, ai = 0;
	const bool alpha1 = std::fabs(alpha - 1.0) < 1e-9;
	for (size_t i = 0; i < n; i++) {
		const double px = h[i].rest.x - v.x, py = h[i].rest.y - v.y;
		const double qx = h[i].cur.x - v.x, qy = h[i].cur.y - v.y;
		const double r = dmin / std::max(px * px + py * py, dmin);
		const double w = alpha1 ? r : std::pow(r, alpha);
		sw += w;
		psx += w * px;
		psy += w * py;
		qsx += w * qx;
		qsy += w * qy;
		ar += w * (qx * px + qy * py);  // q * conj(p)
		ai += w * (qy * px - qx * py);
	}
	psx /= sw;
	psy /= sw;
	qsx /= sw;
	qsy /= sw;
	ar -= sw * (qsx * psx + qsy * psy);
	ai -= sw * (qsy * psx - qsx * psy);
	const double hx = -psx, hy = -psy;  // v − p*
	const double fx = ar * hx - ai * hy, fy = ar * hy + ai * hx;
	const double lf = std::sqrt(fx * fx + fy * fy);
	double sx = hx, sy = hy;
	if (lf > 1e-12) {
		const double lv = std::sqrt(hx * hx + hy * hy);
		sx = fx * lv / lf;
		sy = fy * lv / lf;
	}
	return { sx + qsx + v.x, sy + qsy + v.y };
}

// ---------------------------------------------------------------------------
// ARAP
//   Igarashi et al. 2005 "As-Rigid-As-Possible Shape Manipulation" の2段階法で
//   回転を含む解を直接求め、その後に局所・大域交互法で仕上げる (毎回同じ手順なので結果は決定的)

struct Deformer::ArapCache {
	uint64_t key = 0;
	std::vector<int> act;  // 解く頂点 (不透明部分の頂点) の格子での番号。スカイラインが小さくなる順に並べる
	ArapProblem prob;
	// 前回の結果 (同じ入力なら計算を省く)
	uint64_t resultKey = 0;
	std::vector<Handle> lastHandles;
	std::vector<Vec2> lastCur;
};

Deformer::Deformer() : arap_(std::make_unique<ArapCache>()) {}
Deformer::~Deformer() = default;

// ARAP で out.cur を求める (out.rest, out.cellActive は設定済み)
void Deformer::solveArap(int w, int h, int cols, int rows, const std::vector<uint8_t>& vActive, const std::vector<Handle>& handles,
						 MeshResult& out) {
	const int C = cols + 1;
	const int nv = (int)out.rest.size();
	const double cw = (double)w / cols, ch = (double)h / rows;

	// 初期値 : MLS(α=1)  (制御点の無い部分の置き場所として使う)
	parallelFor(nv, [&](int k) { out.cur[k] = vActive[k] ? mlsRigid(handles, out.rest[k], 1.0) : out.rest[k]; }, (size_t)nv * handles.size());

	ArapCache& A = *arap_;
	uint64_t key = 1469598103934665603ull;
	const int dims[4] = { w, h, cols, rows };
	key = fnv(key, dims, sizeof(dims));
	key = fnv(key, out.cellActive.data(), out.cellActive.size());
	for (auto& hd : handles) key = fnv(key, &hd.rest, sizeof(Vec2));
	const size_t nh = handles.size();
	key = fnv(key, &nh, sizeof(nh));

	if (A.key != key) {
		A.key = 0;
		std::vector<std::array<int, 3>> trisFull;
		for (int j = 0; j < rows; j++) {
			for (int i = 0; i < cols; i++) {
				if (!out.cellActive[(size_t)j * cols + i]) continue;
				const int v00 = j * C + i, v10 = v00 + 1, v01 = v00 + C, v11 = v01 + 1;
				trisFull.push_back({ v00, v10, v11 });
				trisFull.push_back({ v00, v11, v01 });
			}
		}
		// 解く頂点の順番 : 不透明部分の頂点だけを、行ごと または 列ごと のうちスカイラインが小さくなる方で並べる
		// (透明な部分の頂点は他とつながらず初期値のままなので、解く必要がない)
		std::vector<std::vector<int>> adj(nv);
		for (auto& t : trisFull) {
			for (int e = 0; e < 3; e++) {
				adj[t[e]].push_back(t[(e + 1) % 3]);
				adj[t[(e + 1) % 3]].push_back(t[e]);
			}
		}
		std::vector<int> actR, actC;
		for (int j = 0; j <= rows; j++)
			for (int i = 0; i < C; i++)
				if (vActive[j * C + i]) actR.push_back(j * C + i);
		for (int i = 0; i < C; i++)
			for (int j = 0; j <= rows; j++)
				if (vActive[j * C + i]) actC.push_back(j * C + i);
		A.act = skylineSize(actC, adj) < skylineSize(actR, adj) ? std::move(actC) : std::move(actR);
		std::vector<int> pos(nv, -1);
		for (size_t i = 0; i < A.act.size(); i++) pos[A.act[i]] = (int)i;
		const int na = (int)A.act.size();

		ArapProblem& P = A.prob;
		P = ArapProblem{};
		P.n = na;
		P.cotWeights = false;
		P.rest.resize(na);
		for (int i = 0; i < na; i++) P.rest[i] = out.rest[A.act[i]];
		for (auto& t : trisFull) P.tris.push_back({ pos[t[0]], pos[t[1]], pos[t[2]] });
		// 制御点 : 最寄りの対象セルの三角形の重心座標で表す (セル外は外挿)
		P.hIdx.resize(nh);
		P.hLam.resize(nh);
		for (size_t k = 0; k < nh; k++) {
			const double fx = (handles[k].rest.x + w * 0.5) / cw;
			const double fy = (handles[k].rest.y + h * 0.5) / ch;
			int bi = std::clamp((int)std::floor(fx), 0, cols - 1);
			int bj = std::clamp((int)std::floor(fy), 0, rows - 1);
			if (!out.cellActive[(size_t)bj * cols + bi]) {
				double best = 1e300;
				for (int j = 0; j < rows; j++) {
					for (int i = 0; i < cols; i++) {
						if (!out.cellActive[(size_t)j * cols + i]) continue;
						const double dx = i + 0.5 - fx, dy = j + 0.5 - fy, d = dx * dx + dy * dy;
						if (d < best) {
							best = d;
							bi = i;
							bj = j;
						}
					}
				}
			}
			const double u = fx - bi, v = fy - bj;
			const int v00 = bj * C + bi, v10 = v00 + 1, v01 = v00 + C, v11 = v01 + 1;
			if (u >= v) {
				P.hIdx[k] = { pos[v00], pos[v10], pos[v11] };
				P.hLam[k] = { 1 - u, u - v, v };
			} else {
				P.hIdx[k] = { pos[v00], pos[v11], pos[v01] };
				P.hLam[k] = { 1 - v, u, v - u };
			}
		}
		if (!P.prepare()) return;  // 解けない場合は初期値 (MLS) のまま
		A.key = key;
	}

	const int na = (int)A.act.size();
	std::vector<Vec2> init(na), hCur(nh), res;
	for (int i = 0; i < na; i++) init[i] = out.cur[A.act[i]];
	for (size_t k = 0; k < nh; k++) hCur[k] = handles[k].cur;
	A.prob.solve(init, hCur, arapTolerance, arapIterations, anderson ? andersonDepth : 0, res, lastIterations);
	for (int i = 0; i < na; i++) out.cur[A.act[i]] = res[i];
}

bool Deformer::deform(int w, int h, const uint8_t* alpha, const std::vector<Handle>& handles, Method method, double stiffness,
					  MeshResult& out) {
	// 格子
	const double longSide = std::max(w, h);
	const double cell = std::max(4.0, longSide / std::max(1, gridCells));
	const int cols = std::max(1, (int)std::ceil(w / cell - 1e-9));
	const int rows = std::max(1, (int)std::ceil(h / cell - 1e-9));
	const double cw = (double)w / cols, ch = (double)h / rows;
	const int C = cols + 1;
	const int nv = C * (rows + 1);
	out.w = w;
	out.h = h;
	out.cols = cols;
	out.rows = rows;
	out.rest.resize(nv);
	out.cur.resize(nv);
	for (int j = 0; j <= rows; j++) {
		for (int i = 0; i <= cols; i++) {
			out.rest[j * C + i] = { -w * 0.5 + i * cw, -h * 0.5 + j * ch };
		}
	}

	// 不透明部分を含むセルだけを対象にする (指の間など透明な所でつながらないように)
	out.cellActive.assign((size_t)cols * rows, alpha ? 0 : 1);
	if (alpha) {
		for (int j = 0; j < rows; j++) {
			const int y0 = (int)std::floor(j * ch), y1 = std::min(h, (int)std::ceil((j + 1) * ch));
			for (int i = 0; i < cols; i++) {
				const int x0 = (int)std::floor(i * cw), x1 = std::min(w, (int)std::ceil((i + 1) * cw));
				bool any = false;
				for (int y = y0; y < y1 && !any; y++) {
					const uint8_t* row = alpha + (size_t)y * w;
					for (int x = x0; x < x1; x++) {
						if (row[x]) {
							any = true;
							break;
						}
					}
				}
				out.cellActive[(size_t)j * cols + i] = any ? 1 : 0;
			}
		}
	}
	std::vector<uint8_t> vActive(nv, 0);
	int activeCells = 0;
	for (int j = 0; j < rows; j++) {
		for (int i = 0; i < cols; i++) {
			if (!out.cellActive[(size_t)j * cols + i]) continue;
			activeCells++;
			const int v00 = j * C + i;
			vActive[v00] = vActive[v00 + 1] = vActive[v00 + C] = vActive[v00 + C + 1] = 1;
		}
	}
	if (activeCells == 0) return false;

	if (handles.empty()) {
		out.cur = out.rest;
	} else {
		ArapCache& A = *arap_;
		// 前回と同じ入力 (画像の不透明部分・制御点・設定) なら、前回の結果をそのまま使う
		uint64_t rkey = 1469598103934665603ull;
		const int dims[4] = { w, h, cols, rows };
		rkey = fnv(rkey, dims, sizeof(dims));
		rkey = fnv(rkey, out.cellActive.data(), out.cellActive.size());
		const double settings[4] = { (double)method, method == Method::MLS ? stiffness : 0.0, (double)arapIterations, arapTolerance };
		rkey = fnv(rkey, settings, sizeof(settings));
		auto sameHandles = [&] {
			if (A.lastHandles.size() != handles.size()) return false;
			for (size_t k = 0; k < handles.size(); k++) {
				const Handle &p = A.lastHandles[k], &q = handles[k];
				if (p.rest.x != q.rest.x || p.rest.y != q.rest.y || p.cur.x != q.cur.x || p.cur.y != q.cur.y) return false;
			}
			return true;
		};
		if (rkey == A.resultKey && A.lastCur.size() == (size_t)nv && sameHandles()) {
			out.cur = A.lastCur;
		} else {
			A.resultKey = 0;
			if (method == Method::MLS) {
				const double a = std::clamp(stiffness, 0.1, 8.0);
				parallelFor(nv, [&](int k) { out.cur[k] = vActive[k] ? mlsRigid(handles, out.rest[k], a) : out.rest[k]; }, (size_t)nv * handles.size());
			} else {
				solveArap(w, h, cols, rows, vActive, handles, out);
			}
			A.resultKey = rkey;
			A.lastHandles = handles;
			A.lastCur = out.cur;
		}
	}

	out.minx = out.miny = 1e300;
	out.maxx = out.maxy = -1e300;
	for (int k = 0; k < nv; k++) {
		if (!vActive[k]) continue;
		const Vec2& p = out.cur[k];
		out.minx = std::min(out.minx, p.x);
		out.maxx = std::max(out.maxx, p.x);
		out.miny = std::min(out.miny, p.y);
		out.maxy = std::max(out.maxy, p.y);
	}
	return true;
}


// ---------------------------------------------------------------------------
// 三角形の網目での変形

struct TriDeformer::Impl {
	// 入力の α (同じなら輪郭を作り直さない)
	int w = -1, h = -1;
	std::vector<uint8_t> alpha;
	std::vector<uint8_t> mask;  // α > 0
	Contour contour;
	bool hasContour = false;
	uint64_t contourVer = 0;
	// 網目
	uint64_t meshKey = 0;
	bool meshOk = false;
	TriMesh mesh;
	TriLocator loc;
	std::vector<std::vector<Vec2>> cuts;
	std::vector<std::vector<int>> adj;
	std::vector<int> order;  // ARAP の解く順 (RCM)
	// 制御点の変形前の位置に依るもの
	uint64_t restKey = 0;
	std::vector<std::array<int, 3>> hIdx;
	std::vector<std::array<double, 3>> hLam;
	std::vector<float> dist;  // [頂点 * nh + 制御点] 網目に沿った距離 (届かなければ無限大)
	uint64_t probKey = 0;
	bool probOk = false;
	ArapProblem prob;
	// 前回の結果
	uint64_t resultKey = 0;
	std::vector<Handle> lastHandles;
	std::vector<Vec2> lastCur;
};

TriDeformer::TriDeformer() : d_(std::make_unique<Impl>()) {}
TriDeformer::~TriDeformer() = default;
const Contour& TriDeformer::contour() const { return d_->contour; }
const TriMesh& TriDeformer::mesh() const { return d_->mesh; }
const TriLocator& TriDeformer::locator() const { return d_->loc; }

Vec2 sampleTriField(const TriDeformer& td, const TriResult& f, const Vec2& offset, const Vec2& p) {
	if (f.cur.empty() || f.tris.empty()) return p;
	static const std::vector<std::vector<Vec2>> noCuts;
	std::array<double, 3> b{};
	const Vec2 q{ p.x - offset.x, p.y - offset.y };
	const int t = td.locator().locate(q, noCuts, b);
	if (t < 0 || t >= (int)f.tris.size()) return p;
	const auto& tri = f.tris[t];
	double x = 0, y = 0;
	for (int k = 0; k < 3; k++) {
		x += b[k] * f.cur[tri[k]].x;
		y += b[k] * f.cur[tri[k]].y;
	}
	return { x + offset.x, y + offset.y };
}

namespace {

// 距離を指定した MLS 剛体変形 (d2[k] : 制御点 k までの距離の2乗。届かない制御点は無限大)
Vec2 mlsRigidDist(const std::vector<Handle>& h, const Vec2& v, const float* d, double alpha) {
	const size_t n = h.size();
	double dmin = std::numeric_limits<double>::infinity();
	size_t imin = 0;
	for (size_t i = 0; i < n; i++) {
		const double di = d[i];
		if (di < dmin) {
			dmin = di;
			imin = i;
		}
	}
	if (!std::isfinite(dmin)) return mlsRigid(h, v, alpha);  // どの制御点からも網目をたどって届かない : 直線距離
	const double dmin2 = dmin * dmin;
	if (dmin2 < 1e-10) return { h[imin].cur.x + v.x - h[imin].rest.x, h[imin].cur.y + v.y - h[imin].rest.y };
	double sw = 0, psx = 0, psy = 0, qsx = 0, qsy = 0, ar = 0, ai = 0;
	const bool alpha1 = std::fabs(alpha - 1.0) < 1e-9;
	for (size_t i = 0; i < n; i++) {
		const double di = d[i];
		if (!std::isfinite(di)) continue;
		const double r = dmin2 / std::max(di * di, dmin2);
		const double w = alpha1 ? r : std::pow(r, alpha);
		const double px = h[i].rest.x - v.x, py = h[i].rest.y - v.y;
		const double qx = h[i].cur.x - v.x, qy = h[i].cur.y - v.y;
		sw += w;
		psx += w * px;
		psy += w * py;
		qsx += w * qx;
		qsy += w * qy;
		ar += w * (qx * px + qy * py);
		ai += w * (qy * px - qx * py);
	}
	psx /= sw;
	psy /= sw;
	qsx /= sw;
	qsy /= sw;
	ar -= sw * (qsx * psx + qsy * psy);
	ai -= sw * (qsy * psx - qsx * psy);
	const double hx = -psx, hy = -psy;
	const double fx = ar * hx - ai * hy, fy = ar * hy + ai * hx;
	const double lf = std::sqrt(fx * fx + fy * fy);
	double sx = hx, sy = hy;
	if (lf > 1e-12) {
		const double lv = std::sqrt(hx * hx + hy * hy);
		sx = fx * lv / lf;
		sy = fy * lv / lf;
	}
	return { sx + qsx + v.x, sy + qsy + v.y };
}

}  // namespace

bool TriDeformer::deform(int w, int h, const uint8_t* alpha, const std::vector<std::vector<Vec2>>& cuts, const std::vector<Handle>& handles,
						 Method method, double stiffness, double tolerance, TriResult& out) {
	out = TriResult{};
	if (w <= 0 || h <= 0 || !alpha) return false;
	Impl& D = *d_;
	const size_t npx = (size_t)w * h;

	// ---- 1. 輪郭 (不透明な部分が変わったときだけ作り直す) ----
	const bool sameSize = D.w == w && D.h == h && D.alpha.size() == npx;
	if (!sameSize || std::memcmp(D.alpha.data(), alpha, npx) != 0) {
		bool sameMask = sameSize;
		if (sameMask) {
			for (size_t i = 0; i < npx; i++) {
				if ((alpha[i] != 0) != (D.mask[i] != 0)) {
					sameMask = false;
					break;
				}
			}
		}
		D.alpha.assign(alpha, alpha + npx);
		if (!sameMask) {
			D.w = w;
			D.h = h;
			D.mask.resize(npx);
			for (size_t i = 0; i < npx; i++) D.mask[i] = alpha[i] != 0;
			D.hasContour = buildContour(w, h, alpha, D.contour, contourMaxSide);
			D.contourVer++;
			contourBuilds++;
		}
	}
	if (!D.hasContour) return false;

	// ---- 2. 網目 (輪郭か切れ込みが変わったときだけ) ----
	uint64_t mkey = fnv(1469598103934665603ull, &D.contourVer, sizeof(D.contourVer));
	for (auto& c : cuts) {
		const size_t n = c.size();
		mkey = fnv(mkey, &n, sizeof(n));
		mkey = fnv(mkey, c.data(), n * sizeof(Vec2));
	}
	const int mp[3] = { meshParams.targetTris, meshParams.maxTris, (int)(meshParams.minSpacing * 1000) };
	mkey = fnv(mkey, mp, sizeof(mp));
	if (mkey != D.meshKey) {
		D.meshKey = mkey;
		D.restKey = D.probKey = D.resultKey = 0;
		D.cuts = cuts;
		D.meshOk = buildMesh(D.contour, cuts, meshParams, D.mesh);
		meshBuilds++;
		if (D.meshOk) {
			D.loc.build(D.mesh);
			const int nv = (int)D.mesh.pos.size();
			D.adj.assign(nv, {});
			for (auto& t : D.mesh.tris) {
				for (int e = 0; e < 3; e++) {
					const int a = t[e], b = t[(e + 1) % 3];
					D.adj[a].push_back(b);
					D.adj[b].push_back(a);
				}
			}
			for (auto& l : D.adj) {
				std::sort(l.begin(), l.end());
				l.erase(std::unique(l.begin(), l.end()), l.end());
			}
			D.order = rcmOrder(nv, D.adj);
		}
	}
	if (!D.meshOk) return false;
	const TriMesh& M = D.mesh;
	const int nv = (int)M.pos.size();
	const size_t nh = handles.size();

	// ---- 3. 制御点の場所と、網目に沿った距離 (制御点の変形前の位置が変わったときだけ) ----
	uint64_t rkey = fnv(D.meshKey, &nh, sizeof(nh));
	for (auto& hd : handles) rkey = fnv(rkey, &hd.rest, sizeof(Vec2));
	if (rkey != D.restKey) {
		D.restKey = rkey;
		D.probKey = D.resultKey = 0;
		restBuilds++;
		D.hIdx.assign(nh, { 0, 0, 0 });
		D.hLam.assign(nh, { 1, 0, 0 });
		for (size_t k = 0; k < nh; k++) {
			std::array<double, 3> b{};
			const int t = D.loc.locate(handles[k].rest, D.cuts, b);
			if (t < 0) continue;
			D.hIdx[k] = M.tris[t];
			D.hLam[k] = b;
		}
		D.dist.assign((size_t)nv * nh, std::numeric_limits<float>::infinity());
		parallelFor(
			(int)nh,
			[&](int k) {
				std::vector<double> dd(nv, std::numeric_limits<double>::infinity());
				using QE = std::pair<double, int>;
				std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
				for (int a = 0; a < 3; a++) {
					const int v = D.hIdx[k][a];
					const double d0 = std::hypot(M.pos[v].x - handles[k].rest.x, M.pos[v].y - handles[k].rest.y);
					if (d0 < dd[v]) {
						dd[v] = d0;
						q.push({ d0, v });
					}
				}
				while (!q.empty()) {
					const auto [dv, v] = q.top();
					q.pop();
					if (dv > dd[v]) continue;
					for (int u : D.adj[v]) {
						const double nd = dv + std::hypot(M.pos[u].x - M.pos[v].x, M.pos[u].y - M.pos[v].y);
						if (nd < dd[u]) {
							dd[u] = nd;
							q.push({ nd, u });
						}
					}
				}
				for (int v = 0; v < nv; v++) D.dist[(size_t)v * nh + k] = (float)dd[v];
			},
			nh * (size_t)nv * 20);
	}

	// ---- 4. 変形 ----
	uint64_t reskey = rkey;
	const double settings[5] = { (double)method, method == Method::MLS ? stiffness : 0.0, method == Method::ARAP ? tolerance : 0.0,
								 (double)arapIterations, (double)andersonDepth };
	reskey = fnv(reskey, settings, sizeof(settings));
	auto sameHandles = [&] {
		if (D.lastHandles.size() != nh) return false;
		for (size_t k = 0; k < nh; k++) {
			const Handle &p = D.lastHandles[k], &q = handles[k];
			if (p.rest.x != q.rest.x || p.rest.y != q.rest.y || p.cur.x != q.cur.x || p.cur.y != q.cur.y) return false;
		}
		return true;
	};
	std::vector<Vec2> cur;
	if (reskey == D.resultKey && D.lastCur.size() == (size_t)nv && sameHandles()) {
		cur = D.lastCur;
	} else {
		D.resultKey = 0;
		cur = M.pos;
		if (nh > 0) {
			const double a = method == Method::MLS ? std::clamp(stiffness, 0.1, 8.0) : 1.0;
			parallelFor(nv, [&](int v) { cur[v] = mlsRigidDist(handles, M.pos[v], D.dist.data() + (size_t)v * nh, a); }, (size_t)nv * nh);
			if (method == Method::ARAP) {
				if (D.probKey != rkey) {
					D.probKey = rkey;
					std::vector<int> pos(nv);
					for (int i = 0; i < nv; i++) pos[D.order[i]] = i;
					ArapProblem& P = D.prob;
					P = ArapProblem{};
					P.n = nv;
					P.cotWeights = true;
					P.rest.resize(nv);
					for (int i = 0; i < nv; i++) P.rest[i] = M.pos[D.order[i]];
					P.tris.reserve(M.tris.size());
					for (auto& t : M.tris) P.tris.push_back({ pos[t[0]], pos[t[1]], pos[t[2]] });
					P.hIdx.resize(nh);
					P.hLam = D.hLam;
					for (size_t k = 0; k < nh; k++) P.hIdx[k] = { pos[D.hIdx[k][0]], pos[D.hIdx[k][1]], pos[D.hIdx[k][2]] };
					D.probOk = P.prepare();
				}
				if (D.probOk) {
					std::vector<Vec2> init(nv), hCur(nh), res;
					for (int i = 0; i < nv; i++) init[i] = cur[D.order[i]];
					for (size_t k = 0; k < nh; k++) hCur[k] = handles[k].cur;
					D.prob.solve(init, hCur, tolerance, arapIterations, andersonDepth, res, lastIterations);
					for (int i = 0; i < nv; i++) cur[D.order[i]] = res[i];
				}
			}
		}
		D.resultKey = reskey;
		D.lastHandles = handles;
		D.lastCur = cur;
	}

	out.rest = M.pos;
	out.cur = std::move(cur);
	out.tris = M.tris;
	out.split = M.split;
	out.minx = out.miny = 1e300;
	out.maxx = out.maxy = -1e300;
	for (auto& p : out.cur) {
		out.minx = std::min(out.minx, p.x);
		out.maxx = std::max(out.maxx, p.x);
		out.miny = std::min(out.miny, p.y);
		out.maxy = std::max(out.maxy, p.y);
	}
	return true;
}

}  // namespace pd
