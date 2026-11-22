// パペット変形 for AviUtl2 - 輪郭と網目 (三角形分割・切れ込み)
#include "mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace pd {

namespace {

inline Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 operator*(const Vec2& a, double s) { return { a.x * s, a.y * s }; }
inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double crs(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
// a→b→c が反時計回り (数学の向き) なら正
inline double orient(const Vec2& a, const Vec2& b, const Vec2& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

double distPointSeg(const Vec2& p, const Vec2& a, const Vec2& b) {
	const Vec2 d = b - a;
	const double l2 = dot(d, d);
	double t = l2 > 0 ? dot(p - a, d) / l2 : 0;
	t = std::clamp(t, 0.0, 1.0);
	const Vec2 q = a + d * t;
	return std::hypot(p.x - q.x, p.y - q.y);
}

// 閉じた折れ線の Douglas–Peucker
std::vector<Vec2> simplifyLoop(const std::vector<Vec2>& pts, double tol) {
	const int n = (int)pts.size();
	if (n < 4) return pts;
	// 先頭から最も遠い点で2つに分ける
	int far = 0;
	double best = -1;
	for (int i = 1; i < n; i++) {
		const double d = std::hypot(pts[i].x - pts[0].x, pts[i].y - pts[0].y);
		if (d > best) {
			best = d;
			far = i;
		}
	}
	std::vector<uint8_t> keep(n, 0);
	keep[0] = keep[far] = 1;
	// 区間 [a, b] (b は n を超えたら折り返し)
	std::vector<std::pair<int, int>> stack = { { 0, far }, { far, n } };
	while (!stack.empty()) {
		auto [a, b] = stack.back();
		stack.pop_back();
		if (b - a < 2) continue;
		const Vec2& pa = pts[a % n];
		const Vec2& pb = pts[b % n];
		int idx = -1;
		double dmax = tol;
		for (int i = a + 1; i < b; i++) {
			const double d = distPointSeg(pts[i % n], pa, pb);
			if (d > dmax) {
				dmax = d;
				idx = i;
			}
		}
		if (idx >= 0) {
			keep[idx % n] = 1;
			stack.push_back({ a, idx });
			stack.push_back({ idx, b });
		}
	}
	std::vector<Vec2> out;
	for (int i = 0; i < n; i++) {
		if (keep[i]) out.push_back(pts[i]);
	}
	return out;
}

// ---- Bowyer–Watson の Delaunay 分割 ----
class Delaunay {
public:
	struct Tri {
		int v[3];
		int n[3];  // n[k] : 頂点 v[k] の向かいの辺 (v[k+1], v[k+2]) の隣
		bool alive;
	};
	std::vector<Vec2> P;
	std::vector<Tri> T;
	std::vector<int> alias;  // 同じ位置の点の付け替え先
	int super[3] = { -1, -1, -1 };

	void init(double minx, double miny, double maxx, double maxy) {
		const double cx = (minx + maxx) * 0.5, cy = (miny + maxy) * 0.5;
		const double R = std::max({ maxx - minx, maxy - miny, 1.0 }) * 10;
		P.clear();
		T.clear();
		alias.clear();
		P.push_back({ cx - 3 * R, cy - 3 * R });
		P.push_back({ cx + 3 * R, cy - 3 * R });
		P.push_back({ cx, cy + 3 * R });
		alias = { 0, 1, 2 };
		super[0] = 0;
		super[1] = 1;
		super[2] = 2;
		T.push_back({ { 0, 1, 2 }, { -1, -1, -1 }, true });
		last_ = 0;
		eps_ = R * 1e-12;
	}

	// 点を足す。戻り値 : 点の番号 (同じ位置の点があればその番号)
	int add(const Vec2& p) {
		const int pi = (int)P.size();
		P.push_back(p);
		alias.push_back(pi);
		const int t = locate(p);
		if (t < 0) {
			alias[pi] = -1;
			return -1;
		}
		for (int k = 0; k < 3; k++) {
			const Vec2& q = P[T[t].v[k]];
			if (std::fabs(q.x - p.x) < 1e-9 && std::fabs(q.y - p.y) < 1e-9) {
				alias[pi] = T[t].v[k];
				return T[t].v[k];
			}
		}
		if (!insertAt(pi, t)) {
			alias[pi] = -1;
			return -1;
		}
		return pi;
	}

	int resolve(int i) const { return i >= 0 && i < (int)alias.size() ? alias[i] : -1; }

private:
	int last_ = 0;
	double eps_ = 0;
	std::vector<int> mark_;
	int stamp_ = 0;

	double incircle(const Tri& t, const Vec2& p) const {
		const Vec2& a = P[t.v[0]];
		const Vec2& b = P[t.v[1]];
		const Vec2& c = P[t.v[2]];
		const double adx = a.x - p.x, ady = a.y - p.y;
		const double bdx = b.x - p.x, bdy = b.y - p.y;
		const double cdx = c.x - p.x, cdy = c.y - p.y;
		const double ad = adx * adx + ady * ady, bd = bdx * bdx + bdy * bdy, cd = cdx * cdx + cdy * cdy;
		return adx * (bdy * cd - bd * cdy) - ady * (bdx * cd - bd * cdx) + ad * (bdx * cdy - bdy * cdx);
	}

	int locate(const Vec2& p) {
		int t = (last_ >= 0 && last_ < (int)T.size() && T[last_].alive) ? last_ : -1;
		if (t < 0) {
			for (int i = (int)T.size() - 1; i >= 0; i--) {
				if (T[i].alive) {
					t = i;
					break;
				}
			}
		}
		for (int steps = 0; t >= 0 && steps < 100000; steps++) {
			bool moved = false;
			for (int k = 0; k < 3; k++) {
				const Vec2& a = P[T[t].v[(k + 1) % 3]];
				const Vec2& b = P[T[t].v[(k + 2) % 3]];
				if (orient(a, b, p) < 0 && T[t].n[k] >= 0) {
					t = T[t].n[k];
					moved = true;
					break;
				}
			}
			if (!moved) return t;
		}
		// 歩いて見つからなければ総当たり
		for (int i = 0; i < (int)T.size(); i++) {
			if (!T[i].alive) continue;
			bool in = true;
			for (int k = 0; k < 3 && in; k++) {
				if (orient(P[T[i].v[(k + 1) % 3]], P[T[i].v[(k + 2) % 3]], p) < 0) in = false;
			}
			if (in) return i;
		}
		return -1;
	}

	bool insertAt(int pi, int seed) {
		const Vec2 p = P[pi];
		if (mark_.size() < T.size()) mark_.resize(T.size() * 2 + 16, 0);
		stamp_++;
		std::vector<int> cav = { seed };
		mark_[seed] = stamp_;
		for (size_t i = 0; i < cav.size(); i++) {
			const int x = cav[i];
			for (int k = 0; k < 3; k++) {
				const int nb = T[x].n[k];
				if (nb < 0 || mark_[nb] == stamp_) continue;
				if (incircle(T[nb], p) > 0) {
					mark_[nb] = stamp_;
					cav.push_back(nb);
				}
			}
		}
		struct Edge {
			int a, b, outer;
		};
		std::vector<Edge> bound;
		// 空洞が p から見て星形になっているかを確かめ、そうでなければ直す
		for (int round = 0; round < 16; round++) {
			bound.clear();
			int bad = -1, badOuter = -1;
			bool onEdge = false;
			for (int x : cav) {
				for (int k = 0; k < 3; k++) {
					const int nb = T[x].n[k];
					if (nb >= 0 && mark_[nb] == stamp_) continue;
					const int a = T[x].v[(k + 1) % 3], b = T[x].v[(k + 2) % 3];
					const double o = orient(P[a], P[b], p);
					const double thr = eps_ * std::max(1.0, std::hypot(P[b].x - P[a].x, P[b].y - P[a].y));
					if (o <= thr && bad < 0) {
						bad = x;
						badOuter = nb;
						onEdge = o >= -thr;
					}
					bound.push_back({ a, b, nb });
				}
			}
			if (bad < 0) break;
			if (onEdge && badOuter >= 0) {
				// p が辺の上 : 隣の三角形も空洞に入れる
				mark_[badOuter] = stamp_;
				cav.push_back(badOuter);
			} else if (bad != seed) {
				// 空洞が星形でない : その三角形を外す
				mark_[bad] = 0;
				cav.erase(std::find(cav.begin(), cav.end(), bad));
			} else {
				return false;
			}
			if (round == 15) return false;
		}
		for (int x : cav) T[x].alive = false;
		const int base = (int)T.size();
		std::unordered_map<int, int> byA, byB;
		byA.reserve(bound.size() * 2);
		byB.reserve(bound.size() * 2);
		for (size_t i = 0; i < bound.size(); i++) {
			const Edge& e = bound[i];
			const int id = base + (int)i;
			T.push_back({ { pi, e.a, e.b }, { e.outer, -1, -1 }, true });
			byA[e.a] = id;
			byB[e.b] = id;
			if (e.outer >= 0) {
				Tri& o = T[e.outer];
				for (int k = 0; k < 3; k++) {
					if (o.v[(k + 1) % 3] == e.b && o.v[(k + 2) % 3] == e.a) o.n[k] = id;
				}
			}
		}
		for (size_t i = 0; i < bound.size(); i++) {
			Tri& t = T[base + i];
			auto ib = byA.find(t.v[2]);  // 辺 (b, p) の隣 : a == b の新しい三角形
			auto ia = byB.find(t.v[1]);  // 辺 (p, a) の隣 : b == a の新しい三角形
			t.n[1] = ib != byA.end() ? ib->second : -1;
			t.n[2] = ia != byB.end() ? ia->second : -1;
		}
		last_ = base;
		if (mark_.size() < T.size()) mark_.resize(T.size() * 2 + 16, 0);
		return true;
	}
};

inline uint64_t edgeKey(int a, int b) {
	if (a > b) std::swap(a, b);
	return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b;
}

struct Constraint {
	int a, b;
	bool cut;
};

// 1回分の網目作り (間隔 h)
bool buildOnce(const Contour& contour, const std::vector<std::vector<Vec2>>& cuts, double h, TriMesh& out) {
	out = TriMesh{};
	double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
	for (auto& lp : contour.loops) {
		for (auto& p : lp) {
			minx = std::min(minx, p.x);
			miny = std::min(miny, p.y);
			maxx = std::max(maxx, p.x);
			maxy = std::max(maxy, p.y);
		}
	}
	if (!(minx < maxx && miny < maxy)) return false;

	// ---- 制約の線分 (輪郭・切れ込み) と、その交点 ----
	std::vector<Vec2> pts;  // 制約の点
	struct Seg {
		int a, b;
		bool cut;
		std::vector<std::pair<double, int>> on;  // 線分の上の点 (割合, 点の番号)
	};
	std::vector<Seg> segs;
	for (auto& lp : contour.loops) {
		const int base = (int)pts.size();
		for (auto& p : lp) pts.push_back(p);
		const int n = (int)lp.size();
		for (int i = 0; i < n; i++) segs.push_back({ base + i, base + (i + 1) % n, false, {} });
	}
	const size_t ncontour = segs.size();
	for (auto& c : cuts) {
		if (c.size() < 2) continue;
		const int base = (int)pts.size();
		for (auto& p : c) pts.push_back(p);
		for (size_t i = 0; i + 1 < c.size(); i++) {
			if (std::hypot(c[i + 1].x - c[i].x, c[i + 1].y - c[i].y) < 1e-6) continue;
			segs.push_back({ base + (int)i, base + (int)i + 1, true, {} });
		}
	}
	// 交点 (切れ込みと輪郭、切れ込みどうし)
	for (size_t i = ncontour; i < segs.size(); i++) {
		for (size_t j = 0; j < segs.size(); j++) {
			if (j >= ncontour && j <= i) continue;
			Seg& s1 = segs[i];
			Seg& s2 = segs[j];
			if (s1.a == s2.a || s1.a == s2.b || s1.b == s2.a || s1.b == s2.b) continue;
			const Vec2 p = pts[s1.a], r = pts[s1.b] - pts[s1.a];
			const Vec2 q = pts[s2.a], s = pts[s2.b] - pts[s2.a];
			const double den = crs(r, s);
			if (std::fabs(den) < 1e-12) continue;
			const double t = crs(q - p, s) / den;
			const double u = crs(q - p, r) / den;
			if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) continue;
			int idx;
			if (u <= 1e-9) idx = s2.a;
			else if (u >= 1 - 1e-9) idx = s2.b;
			else if (t <= 1e-9) idx = s1.a;
			else if (t >= 1 - 1e-9) idx = s1.b;
			else {
				idx = (int)pts.size();
				pts.push_back(p + r * t);
			}
			if (t > 1e-9 && t < 1 - 1e-9) s1.on.push_back({ t, idx });
			if (u > 1e-9 && u < 1 - 1e-9) s2.on.push_back({ u, idx });
		}
	}
	// 区切りと細分 (長さ h 以下に)
	std::vector<Constraint> cons;
	for (auto& sg : segs) {
		auto on = sg.on;
		on.push_back({ 0.0, sg.a });
		on.push_back({ 1.0, sg.b });
		std::sort(on.begin(), on.end());
		for (size_t k = 0; k + 1 < on.size(); k++) {
			int a = on[k].second, b = on[k + 1].second;
			if (a == b) continue;
			const Vec2 pa = pts[a], pb = pts[b];
			if (sg.cut) {
				// 輪郭の外の部分は使わない
				const Vec2 mid = (pa + pb) * 0.5;
				if (!contour.inside(mid)) continue;
			}
			const double len = std::hypot(pb.x - pa.x, pb.y - pa.y);
			if (len < 1e-9) continue;
			const int m = std::max(1, (int)std::ceil(len / h - 1e-9));
			int prev = a;
			for (int s = 1; s <= m; s++) {
				int cur;
				if (s == m) cur = b;
				else {
					cur = (int)pts.size();
					pts.push_back(pa + (pb - pa) * ((double)s / m));
				}
				cons.push_back({ prev, cur, sg.cut });
				prev = cur;
			}
		}
	}

	// ---- 内側の格子の点 (ほぼ正三角形) ----
	std::vector<Vec2> lattice;
	{
		const double dy = h * std::sqrt(3.0) * 0.5;
		const int rows = (int)std::ceil((maxy - miny) / dy) + 1;
		const int cols = (int)std::ceil((maxx - minx) / h) + 2;
		// 制約の線分の近くは除く (細い三角形を作らないため)。線分をます目に分けて近くだけ調べる
		const double cell = h;
		const int gx = std::max(1, (int)std::ceil((maxx - minx) / cell) + 1);
		const int gy = std::max(1, (int)std::ceil((maxy - miny) / cell) + 1);
		std::vector<std::vector<int>> grid((size_t)gx * gy);
		for (size_t i = 0; i < cons.size(); i++) {
			const Vec2 &a = pts[cons[i].a], &b = pts[cons[i].b];
			const int x0 = std::clamp((int)std::floor((std::min(a.x, b.x) - minx) / cell) - 1, 0, gx - 1);
			const int x1 = std::clamp((int)std::floor((std::max(a.x, b.x) - minx) / cell) + 1, 0, gx - 1);
			const int y0 = std::clamp((int)std::floor((std::min(a.y, b.y) - miny) / cell) - 1, 0, gy - 1);
			const int y1 = std::clamp((int)std::floor((std::max(a.y, b.y) - miny) / cell) + 1, 0, gy - 1);
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++) grid[(size_t)y * gx + x].push_back((int)i);
		}
		for (int j = 0; j < rows; j++) {
			for (int i = -1; i < cols; i++) {
				// わずかにずらして、同じ円の上に4点が並ぶのを避ける (決まった値なので結果は毎回同じ)
				const uint32_t hsh = (uint32_t)(i * 73856093) ^ (uint32_t)(j * 19349663);
				const double jx = ((hsh & 1023) / 1023.0 - 0.5) * 1e-3 * h;
				const double jy = (((hsh >> 10) & 1023) / 1023.0 - 0.5) * 1e-3 * h;
				const Vec2 p = { minx + i * h + ((j & 1) ? h * 0.5 : 0.0) + jx, miny + j * dy + jy };
				if (p.x < minx || p.x > maxx || p.y < miny || p.y > maxy) continue;
				if (!contour.inside(p)) continue;
				const int cx = std::clamp((int)std::floor((p.x - minx) / cell), 0, gx - 1);
				const int cy = std::clamp((int)std::floor((p.y - miny) / cell), 0, gy - 1);
				bool near = false;
				for (int k : grid[(size_t)cy * gx + cx]) {
					if (distPointSeg(p, pts[cons[k].a], pts[cons[k].b]) < 0.5 * h) {
						near = true;
						break;
					}
				}
				if (!near) lattice.push_back(p);
			}
		}
	}

	// ---- Delaunay 分割 ----
	Delaunay dt;
	dt.init(minx, miny, maxx, maxy);
	std::vector<int> map(pts.size(), -1);
	std::vector<uint8_t> usedPt(pts.size(), 0);
	for (auto& c : cons) usedPt[c.a] = usedPt[c.b] = 1;
	for (size_t i = 0; i < pts.size(); i++) {
		if (usedPt[i]) map[i] = dt.add(pts[i]);
	}
	for (auto& p : lattice) dt.add(p);
	for (auto& c : cons) {
		c.a = map[c.a];
		c.b = map[c.b];
	}
	cons.erase(std::remove_if(cons.begin(), cons.end(), [](const Constraint& c) { return c.a < 0 || c.b < 0 || c.a == c.b; }), cons.end());

	// ---- 制約の線分が辺になるように、足りないところに中点を足す ----
	const double minLen = std::max(0.05 * h, 0.25);
	int inserted = 0;
	for (int round = 0; round < 40; round++) {
		std::unordered_set<uint64_t> edges;
		edges.reserve(dt.T.size() * 3);
		for (auto& t : dt.T) {
			if (!t.alive) continue;
			for (int k = 0; k < 3; k++) edges.insert(edgeKey(t.v[k], t.v[(k + 1) % 3]));
		}
		std::vector<Constraint> next;
		bool changed = false;
		for (auto& c : cons) {
			if (edges.count(edgeKey(c.a, c.b))) {
				next.push_back(c);
				continue;
			}
			const Vec2 pa = dt.P[c.a], pb = dt.P[c.b];
			if (std::hypot(pb.x - pa.x, pb.y - pa.y) < minLen || inserted > 20000) {
				next.push_back(c);  // 諦める (まれ)
				continue;
			}
			const int m = dt.add((pa + pb) * 0.5);
			inserted++;
			if (m < 0 || m == c.a || m == c.b) {
				next.push_back(c);
				continue;
			}
			next.push_back({ c.a, m, c.cut });
			next.push_back({ m, c.b, c.cut });
			changed = true;
		}
		cons.swap(next);
		if (!changed) break;
	}

	// ---- 輪郭の内側の三角形を取り出す ----
	std::vector<std::array<int, 3>> tris;
	for (auto& t : dt.T) {
		if (!t.alive) continue;
		if (t.v[0] < 3 || t.v[1] < 3 || t.v[2] < 3) continue;
		const Vec2 &a = dt.P[t.v[0]], &b = dt.P[t.v[1]], &c = dt.P[t.v[2]];
		if (orient(a, b, c) <= 1e-12) continue;
		const Vec2 g = { (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3 };
		if (!contour.inside(g)) continue;
		tris.push_back({ t.v[0], t.v[1], t.v[2] });
	}
	if (tris.empty()) return false;

	// ---- 切れ込みの辺で頂点を分ける ----
	std::unordered_set<uint64_t> cutEdges;
	for (auto& c : cons) {
		if (c.cut) cutEdges.insert(edgeKey(c.a, c.b));
	}
	const int np = (int)dt.P.size();
	std::vector<std::vector<int>> inc(np);  // 頂点 → 三角形
	for (int t = 0; t < (int)tris.size(); t++) {
		for (int k = 0; k < 3; k++) inc[tris[t][k]].push_back(t);
	}
	// 三角形の各頂点に、新しい頂点の番号を付ける
	std::vector<std::array<int, 3>> newIdx(tris.size(), { -1, -1, -1 });
	std::vector<Vec2> pos;
	std::vector<int> orig;
	std::vector<uint8_t> split;
	std::vector<int> parent;
	auto find = [&](int x) {
		while (parent[x] != x) x = parent[x] = parent[parent[x]];
		return x;
	};
	for (int v = 0; v < np; v++) {
		auto& L = inc[v];
		if (L.empty()) continue;
		const int m = (int)L.size();
		parent.resize(m);
		std::iota(parent.begin(), parent.end(), 0);
		// まわりの三角形のうち、切れ込みでない辺 (v, w) を共有するものをつなぐ
		std::unordered_map<int, int> firstByW;
		for (int i = 0; i < m; i++) {
			const auto& t = tris[L[i]];
			for (int k = 0; k < 3; k++) {
				const int w = t[k];
				if (w == v) continue;
				if (cutEdges.count(edgeKey(v, w))) continue;
				auto it = firstByW.find(w);
				if (it == firstByW.end()) firstByW[w] = i;
				else parent[find(i)] = find(it->second);
			}
		}
		std::unordered_map<int, int> groupId;
		for (int i = 0; i < m; i++) {
			const int r = find(i);
			auto it = groupId.find(r);
			int id;
			if (it == groupId.end()) {
				id = (int)pos.size();
				groupId[r] = id;
				pos.push_back(dt.P[v]);
				orig.push_back(v);
				split.push_back(0);
			} else {
				id = it->second;
			}
			const auto& t = tris[L[i]];
			for (int k = 0; k < 3; k++) {
				if (t[k] == v) newIdx[L[i]][k] = id;
			}
		}
		if (groupId.size() > 1) {
			for (auto& kv : groupId) split[kv.second] = 1;
		}
	}
	// 元の頂点番号を 0 から詰める
	std::unordered_map<int, int> origMap;
	for (auto& o : orig) {
		auto it = origMap.find(o);
		if (it == origMap.end()) {
			const int id = (int)origMap.size();
			origMap[o] = id;
			o = id;
		} else {
			o = it->second;
		}
	}
	out.pos = std::move(pos);
	out.tris = std::move(newIdx);
	out.orig = std::move(orig);
	out.split = std::move(split);
	out.spacing = h;
	return true;
}

}  // namespace

bool Contour::inside(const Vec2& p) const {
	bool c = false;
	for (auto& lp : loops) {
		const size_t n = lp.size();
		for (size_t i = 0, j = n - 1; i < n; j = i++) {
			const Vec2& a = lp[i];
			const Vec2& b = lp[j];
			if ((a.y > p.y) != (b.y > p.y)) {
				const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
				if (p.x < x) c = !c;
			}
		}
	}
	return c;
}

bool buildContour(int w, int h, const uint8_t* alpha, Contour& out, int maxSide) {
	out = Contour{};
	if (w <= 0 || h <= 0 || !alpha) return false;
	const int L = std::max(w, h);
	maxSide = std::max(16, maxSide);
	const double s = L > maxSide ? (double)maxSide / L : 1.0;
	const int mw = std::max(1, (int)std::ceil(w * s));
	const int mh = std::max(1, (int)std::ceil(h * s));
	std::vector<uint8_t> m((size_t)mw * mh, 0);
	bool any = false;
	for (int y = 0; y < h; y++) {
		const uint8_t* row = alpha + (size_t)y * w;
		uint8_t* dst = m.data() + (size_t)std::min(mh - 1, (int)(y * s)) * mw;
		if (s == 1.0) {
			for (int x = 0; x < w; x++) {
				if (row[x]) {
					dst[x] = 1;
					any = true;
				}
			}
		} else {
			for (int x = 0; x < w; x++) {
				if (row[x]) {
					dst[std::min(mw - 1, (int)(x * s))] = 1;
					any = true;
				}
			}
		}
	}
	if (!any) return false;
	// 2px 広げる (3x3 を2回。画像の範囲内まで)
	for (int pass = 0; pass < 2; pass++) {
		std::vector<uint8_t> src = m;
		for (int y = 0; y < mh; y++) {
			for (int x = 0; x < mw; x++) {
				if (src[(size_t)y * mw + x]) continue;
				bool on = false;
				for (int dy = -1; dy <= 1 && !on; dy++) {
					const int yy = y + dy;
					if (yy < 0 || yy >= mh) continue;
					for (int dx = -1; dx <= 1; dx++) {
						const int xx = x + dx;
						if (xx >= 0 && xx < mw && src[(size_t)yy * mw + xx]) {
							on = true;
							break;
						}
					}
				}
				if (on) m[(size_t)y * mw + x] = 1;
			}
		}
	}
	size_t cnt = 0;
	for (auto v : m) cnt += v;
	out.area = cnt / (s * s);

	// 画素の境目の辺 (不透明な側を右に見る向き)。方向 : 0=+x 1=+y 2=-x 3=-y
	const int VW = mw + 1;
	std::vector<uint8_t> outMask((size_t)VW * (mh + 1), 0), used((size_t)VW * (mh + 1), 0);
	auto in = [&](int i, int j) { return i >= 0 && j >= 0 && i < mw && j < mh && m[(size_t)j * mw + i]; };
	for (int j = 0; j < mh; j++) {
		for (int i = 0; i < mw; i++) {
			if (!m[(size_t)j * mw + i]) continue;
			if (!in(i, j - 1)) outMask[(size_t)j * VW + i] |= 1;
			if (!in(i + 1, j)) outMask[(size_t)j * VW + i + 1] |= 2;
			if (!in(i, j + 1)) outMask[(size_t)(j + 1) * VW + i + 1] |= 4;
			if (!in(i - 1, j)) outMask[(size_t)(j + 1) * VW + i] |= 8;
		}
	}
	const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };
	for (int sv = 0; sv < (int)outMask.size(); sv++) {
		for (int sd = 0; sd < 4; sd++) {
			if (!(outMask[sv] & (1 << sd)) || (used[sv] & (1 << sd))) continue;
			std::vector<Vec2> loop;
			int v = sv, d = sd, prevD = -1;
			for (int guard = 0; guard < (int)outMask.size() * 4 + 8; guard++) {
				used[v] |= (uint8_t)(1 << d);
				if (d != prevD) loop.push_back({ (double)(v % VW), (double)(v / VW) });
				prevD = d;
				const int nx = v % VW + DX[d], ny = v / VW + DY[d];
				v = ny * VW + nx;
				// 右折 → 直進 → 左折 の順に選ぶ
				const int order[3] = { (d + 1) & 3, d, (d + 3) & 3 };
				int nd = -1;
				for (int c : order) {
					if (outMask[v] & (1 << c)) {
						nd = c;
						break;
					}
				}
				if (nd < 0 || (used[v] & (1 << nd))) break;
				d = nd;
			}
			auto simp = simplifyLoop(loop, 0.75);
			if (simp.size() < 3) continue;
			for (auto& p : simp) {
				p.x = p.x / s - w * 0.5;
				p.y = p.y / s - h * 0.5;
			}
			out.loops.push_back(std::move(simp));
		}
	}
	return !out.loops.empty();
}

bool buildMesh(const Contour& contour, const std::vector<std::vector<Vec2>>& cuts, const MeshParams& params, TriMesh& out) {
	out = TriMesh{};
	if (contour.empty() || contour.area <= 0) return false;
	double h = std::max(params.minSpacing, std::sqrt(4.0 * contour.area / (std::sqrt(3.0) * std::max(1, params.targetTris))));
	for (int attempt = 0; attempt < 8; attempt++) {
		TriMesh m;
		if (!buildOnce(contour, cuts, h, m)) return false;
		if ((int)m.tris.size() <= params.maxTris || attempt == 7) {
			out = std::move(m);
			return true;
		}
		h *= std::sqrt((double)m.tris.size() / params.targetTris) * 1.05;
	}
	return false;
}

// ---------------------------------------------------------------------------

void TriLocator::build(const TriMesh& mesh) {
	mesh_ = &mesh;
	buckets_.clear();
	if (mesh.tris.empty()) return;
	double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
	for (auto& p : mesh.pos) {
		minx = std::min(minx, p.x);
		miny = std::min(miny, p.y);
		maxx = std::max(maxx, p.x);
		maxy = std::max(maxy, p.y);
	}
	cell_ = std::max(1.0, mesh.spacing * 2);
	x0_ = minx;
	y0_ = miny;
	nx_ = std::max(1, (int)std::ceil((maxx - minx) / cell_) + 1);
	ny_ = std::max(1, (int)std::ceil((maxy - miny) / cell_) + 1);
	buckets_.assign((size_t)nx_ * ny_, {});
	for (int t = 0; t < (int)mesh.tris.size(); t++) {
		double a = 1e300, b = 1e300, c = -1e300, d = -1e300;
		for (int k = 0; k < 3; k++) {
			const Vec2& p = mesh.pos[mesh.tris[t][k]];
			a = std::min(a, p.x);
			b = std::min(b, p.y);
			c = std::max(c, p.x);
			d = std::max(d, p.y);
		}
		const int i0 = std::clamp((int)std::floor((a - x0_) / cell_), 0, nx_ - 1);
		const int i1 = std::clamp((int)std::floor((c - x0_) / cell_), 0, nx_ - 1);
		const int j0 = std::clamp((int)std::floor((b - y0_) / cell_), 0, ny_ - 1);
		const int j1 = std::clamp((int)std::floor((d - y0_) / cell_), 0, ny_ - 1);
		for (int j = j0; j <= j1; j++)
			for (int i = i0; i <= i1; i++) buckets_[(size_t)j * nx_ + i].push_back(t);
	}
}

int TriLocator::locate(const Vec2& p0, const std::vector<std::vector<Vec2>>& cuts, std::array<double, 3>& bary) const {
	if (!mesh_ || mesh_->tris.empty()) return -1;
	const TriMesh& M = *mesh_;
	// 切れ込みの線の上の点は、始点から終点を見て右側へわずかにずらす
	Vec2 p = p0;
	for (auto& c : cuts) {
		for (size_t i = 0; i + 1 < c.size(); i++) {
			const Vec2 a = c[i], d = c[i + 1] - c[i];
			const double l = std::hypot(d.x, d.y);
			if (l < 1e-9) continue;
			const double t = dot(p - a, d) / (l * l);
			if (t < -1e-9 || t > 1 + 1e-9) continue;
			const double dist = crs(d, p - a) / l;
			if (std::fabs(dist) < 1e-3) {
				// y 下向きの画面で「右」は (−dy, dx)
				p = p + Vec2{ -d.y / l, d.x / l } * 2e-3;
			}
		}
	}
	auto baryOf = [&](int t, std::array<double, 3>& b) {
		const Vec2& a = M.pos[M.tris[t][0]];
		const Vec2& bb = M.pos[M.tris[t][1]];
		const Vec2& c = M.pos[M.tris[t][2]];
		const double area = orient(a, bb, c);
		if (std::fabs(area) < 1e-18) return false;
		b[0] = orient(p, bb, c) / area;
		b[1] = orient(a, p, c) / area;
		b[2] = 1 - b[0] - b[1];
		return true;
	};
	const int ci = (int)std::floor((p.x - x0_) / cell_), cj = (int)std::floor((p.y - y0_) / cell_);
	if (ci >= 0 && cj >= 0 && ci < nx_ && cj < ny_) {
		int best = -1;
		double bestMin = -1e300;
		std::array<double, 3> bb{};
		for (int t : buckets_[(size_t)cj * nx_ + ci]) {
			if (!baryOf(t, bb)) continue;
			const double mn = std::min({ bb[0], bb[1], bb[2] });
			if (mn > bestMin) {
				bestMin = mn;
				best = t;
				bary = bb;
			}
		}
		if (best >= 0 && bestMin >= -1e-9) return best;
	}
	// 網目の外 : 最も近い三角形
	int best = -1;
	double bestD = 1e300;
	for (int t = 0; t < (int)M.tris.size(); t++) {
		double d = 1e300;
		for (int k = 0; k < 3; k++) d = std::min(d, distPointSeg(p, M.pos[M.tris[t][k]], M.pos[M.tris[t][(k + 1) % 3]]));
		if (d < bestD) {
			bestD = d;
			best = t;
		}
	}
	if (best >= 0) baryOf(best, bary);
	return best;
}

}  // namespace pd
