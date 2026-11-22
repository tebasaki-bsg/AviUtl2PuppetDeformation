// パペット変形 for AviUtl2 - ARAP の連立方程式と反復 (格子・三角形の網目で共通)
//   Igarashi et al. 2005 の2段階法の第1段階 (相似変換) を初期値にし、局所・大域交互法 (Sorkine & Alexa 2007) を
//   Anderson 加速 (Peng et al. 2018) で速めて解く。毎回同じ手順なので、同じ入力なら結果も同じ
#include "arap.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace pd {

// ---------------------------------------------------------------------------
// スカイライン形式

void SkylineMatrix::init(const std::vector<int>& first_) {
	n = (int)first_.size();
	first = first_;
	off.resize(n + 1);
	size_t o = 0;
	for (int i = 0; i < n; i++) {
		off[i] = o;
		o += (size_t)(i - first[i] + 1);
	}
	off[n] = o;
	v.assign(o, 0.0);
}

bool SkylineMatrix::factor() {
	for (int i = 0; i < n; i++) {
		const int fi = first[i];
		double* Li = v.data() + off[i];  // Li[j - fi] = L(i, j)
		for (int j = fi; j <= i; j++) {
			const int fj = first[j];
			const double* Lj = v.data() + off[j];
			double sum = Li[j - fi];
			const int k0 = std::max(fi, fj);
			const double* a = Li + (k0 - fi);
			const double* b = Lj + (k0 - fj);
			const int cnt = j - k0;
			for (int k = 0; k < cnt; k++) sum -= a[k] * b[k];
			if (i == j) {
				if (!(sum > 1e-12)) return false;
				Li[i - fi] = std::sqrt(sum);
			} else {
				Li[j - fi] = sum / Lj[j - fj];
			}
		}
	}
	return true;
}

void SkylineMatrix::solve(double* x) const {
	for (int i = 0; i < n; i++) {
		const int fi = first[i];
		const double* Li = v.data() + off[i];
		double s = x[i];
		for (int k = fi; k < i; k++) s -= Li[k - fi] * x[k];
		x[i] = s / Li[i - fi];
	}
	for (int i = n - 1; i >= 0; i--) {
		const int fi = first[i];
		const double* Li = v.data() + off[i];
		const double xi = x[i] / Li[i - fi];
		x[i] = xi;
		for (int k = fi; k < i; k++) x[k] -= Li[k - fi] * xi;
	}
}

void SkylineMatrix::solve2(double* x, double* y) const {
	for (int i = 0; i < n; i++) {
		const int fi = first[i];
		const double* Li = v.data() + off[i];
		double sx = x[i], sy = y[i];
		for (int k = fi; k < i; k++) {
			const double l = Li[k - fi];
			sx -= l * x[k];
			sy -= l * y[k];
		}
		const double d = Li[i - fi];
		x[i] = sx / d;
		y[i] = sy / d;
	}
	for (int i = n - 1; i >= 0; i--) {
		const int fi = first[i];
		const double* Li = v.data() + off[i];
		const double d = Li[i - fi];
		const double xi = x[i] / d, yi = y[i] / d;
		x[i] = xi;
		y[i] = yi;
		for (int k = fi; k < i; k++) {
			const double l = Li[k - fi];
			x[k] -= l * xi;
			y[k] -= l * yi;
		}
	}
}

// ---------------------------------------------------------------------------
// 並べ方

std::vector<int> rcmOrder(int n, const std::vector<std::vector<int>>& adj) {
	std::vector<int> order;
	order.reserve(n);
	std::vector<uint8_t> vis(n, 0);
	auto deg = [&](int v) { return (int)adj[v].size(); };
	// BFS で最後の段の点を返す (端に近い点を始点にするため)
	auto lastLevel = [&](int s) {
		std::vector<int> dist(n, -1);
		std::queue<int> q;
		q.push(s);
		dist[s] = 0;
		int best = s;
		while (!q.empty()) {
			const int v = q.front();
			q.pop();
			if (dist[v] > dist[best] || (dist[v] == dist[best] && deg(v) < deg(best))) best = v;
			for (int u : adj[v]) {
				if (dist[u] < 0 && !vis[u]) {
					dist[u] = dist[v] + 1;
					q.push(u);
				}
			}
		}
		return best;
	};
	for (;;) {
		int s = -1;
		for (int v = 0; v < n; v++) {
			if (!vis[v] && (s < 0 || deg(v) < deg(s))) s = v;
		}
		if (s < 0) break;
		s = lastLevel(lastLevel(s));
		std::queue<int> q;
		q.push(s);
		vis[s] = 1;
		std::vector<int> nb;
		while (!q.empty()) {
			const int v = q.front();
			q.pop();
			order.push_back(v);
			nb.clear();
			for (int u : adj[v]) {
				if (!vis[u]) {
					vis[u] = 1;
					nb.push_back(u);
				}
			}
			std::sort(nb.begin(), nb.end(), [&](int a, int b) { return deg(a) != deg(b) ? deg(a) < deg(b) : a < b; });
			for (int u : nb) q.push(u);
		}
	}
	std::reverse(order.begin(), order.end());
	return order;
}

size_t skylineSize(const std::vector<int>& order, const std::vector<std::vector<int>>& adj) {
	std::vector<int> pos(adj.size(), -1);
	for (size_t i = 0; i < order.size(); i++) pos[order[i]] = (int)i;
	size_t s = 0;
	for (size_t i = 0; i < order.size(); i++) {
		int f = (int)i;
		for (int u : adj[order[i]]) {
			if (pos[u] >= 0) f = std::min(f, pos[u]);
		}
		s += i - f + 1;
	}
	return s;
}

// ---------------------------------------------------------------------------
// ARAP

namespace {
// 三角形 (i,j,k) で、k を辺 i→j の局所座標 (x,y) で表す
inline void localXY(const Vec2& vi, const Vec2& vj, const Vec2& vk, double& x, double& y) {
	const double dx = vj.x - vi.x, dy = vj.y - vi.y, l2 = dx * dx + dy * dy;
	const double ex = vk.x - vi.x, ey = vk.y - vi.y;
	x = (ex * dx + ey * dy) / l2;
	y = (ex * -dy + ey * dx) / l2;  // R90(d) = (-dy, dx)
}
}  // namespace

bool ArapProblem::prepare() {
	const int ntri = (int)tris.size();
	triRest_.resize(ntri);
	triW_.resize(ntri);
	for (int t = 0; t < ntri; t++) {
		const auto& tr = tris[t];
		for (int e = 0; e < 3; e++) {
			const Vec2& a = rest[tr[e]];
			const Vec2& b = rest[tr[(e + 1) % 3]];
			triRest_[t][2 * e] = a.x - b.x;
			triRest_[t][2 * e + 1] = a.y - b.y;
			double w = 1.0;
			if (cotWeights) {
				// 辺 (e, e+1) の向かいの角の cot ÷ 2 (鈍角の負の値は小さな正の値にする)
				const Vec2& o = rest[tr[(e + 2) % 3]];
				const double ux = a.x - o.x, uy = a.y - o.y, vx = b.x - o.x, vy = b.y - o.y;
				const double cr = std::fabs(ux * vy - uy * vx);
				const double cot = cr > 1e-12 ? (ux * vx + uy * vy) / cr : 0.0;
				w = std::clamp(0.5 * cot, 0.01, 50.0);
			}
			triW_[t][e] = w;
		}
	}
	// 行ごとの最初の列
	std::vector<int> first(n);
	for (int i = 0; i < n; i++) first[i] = i;
	auto touch = [&](int a, int b) {
		if (a < b) std::swap(a, b);
		first[a] = std::min(first[a], b);
	};
	for (auto& t : tris) {
		touch(t[0], t[1]);
		touch(t[1], t[2]);
		touch(t[2], t[0]);
	}
	for (auto& hi : hIdx) {
		touch(hi[0], hi[1]);
		touch(hi[1], hi[2]);
		touch(hi[2], hi[0]);
	}
	const size_t nh = hIdx.size();
	// 第2段階・仕上げ用 : ラプラシアン + 制御点
	lap_.init(first);
	for (int k = 0; k < n; k++) lap_.add(k, k, EPS);
	for (int t = 0; t < ntri; t++) {
		for (int e = 0; e < 3; e++) {
			const int a = tris[t][e], b = tris[t][(e + 1) % 3];
			const double w = triW_[t][e];
			lap_.add(a, a, w);
			lap_.add(b, b, w);
			lap_.add(a, b, -w);
		}
	}
	for (size_t k = 0; k < nh; k++) {
		for (int a = 0; a < 3; a++)
			for (int b = 0; b <= a; b++) lap_.add(hIdx[k][a], hIdx[k][b], W * hLam[k][a] * hLam[k][b]);
	}
	if (!lap_.factor()) return false;

	// 第1段階 : 各三角形の形を相似変換の範囲で保つ (未知数は x0,y0,x1,y1,... の順)
	std::vector<int> first2(2 * n);
	for (int i = 0; i < n; i++) first2[2 * i] = first2[2 * i + 1] = 2 * first[i];
	sim_.init(first2);
	for (int k = 0; k < 2 * n; k++) sim_.add(k, k, EPS);
	for (int t = 0; t < ntri; t++) {
		const auto& tr = tris[t];
		for (int e = 0; e < 3; e++) {
			double x, y;
			localXY(rest[tr[e]], rest[tr[(e + 1) % 3]], rest[tr[(e + 2) % 3]], x, y);
			const int vi = tr[e], vj = tr[(e + 1) % 3], vk = tr[(e + 2) % 3];
			// 残差 r = vk' + ((x-1)I + yR) vi' + (-xI - yR) vj'   (R = 90°回転 [[0,-1],[1,0]])
			const int idx[6] = { 2 * vi, 2 * vi + 1, 2 * vj, 2 * vj + 1, 2 * vk, 2 * vk + 1 };
			const double rx[6] = { x - 1, -y, -x, y, 1, 0 };
			const double ry[6] = { y, x - 1, -y, -x, 0, 1 };
			for (int p = 0; p < 6; p++) {
				for (int q = 0; q <= p; q++) {
					const double v = rx[p] * rx[q] + ry[p] * ry[q];
					if (v != 0) sim_.add(idx[p], idx[q], v);
				}
			}
		}
	}
	for (size_t k = 0; k < nh; k++) {
		for (int a = 0; a < 3; a++)
			for (int b = 0; b <= a; b++) {
				const double v = W * hLam[k][a] * hLam[k][b];
				sim_.add(2 * hIdx[k][a], 2 * hIdx[k][b], v);
				sim_.add(2 * hIdx[k][a] + 1, 2 * hIdx[k][b] + 1, v);
			}
	}
	return sim_.factor();
}

void ArapProblem::solve(const std::vector<Vec2>& init, const std::vector<Vec2>& hCur, double tolerance, int maxIter, int andersonDepth,
						std::vector<Vec2>& out, int& iterations) const {
	const int na = n;
	const size_t nh = hIdx.size();
	// 第1段階
	std::vector<double> z(2 * na);
	for (int i = 0; i < na; i++) {
		z[2 * i] = EPS * init[i].x;
		z[2 * i + 1] = EPS * init[i].y;
	}
	for (size_t k = 0; k < nh; k++) {
		for (int a = 0; a < 3; a++) {
			z[2 * hIdx[k][a]] += W * hLam[k][a] * hCur[k].x;
			z[2 * hIdx[k][a] + 1] += W * hLam[k][a] * hCur[k].y;
		}
	}
	sim_.solve(z.data());

	// 第2段階以降 : 三角形ごとの回転を求めて位置を解く (局所・大域交互法)
	std::vector<double> X(na), Y(na), cbx(na), cby(na);
	for (int i = 0; i < na; i++) {
		X[i] = z[2 * i];
		Y[i] = z[2 * i + 1];
		cbx[i] = EPS * init[i].x;
		cby[i] = EPS * init[i].y;
	}
	for (size_t k = 0; k < nh; k++) {
		for (int a = 0; a < 3; a++) {
			cbx[hIdx[k][a]] += W * hLam[k][a] * hCur[k].x;
			cby[hIdx[k][a]] += W * hLam[k][a] * hCur[k].y;
		}
	}
	const int ntri = (int)tris.size();
	std::vector<double> rc(ntri), rs(ntri);
	// 局所ステップ : 各三角形の最適な回転を求め、その回転でのエネルギーを返す
	auto local = [&](const std::vector<double>& x, const std::vector<double>& y) {
		double E = 0;
		for (int t = 0; t < ntri; t++) {
			const auto& tr = tris[t];
			const auto& rr = triRest_[t];
			const auto& ww = triW_[t];
			double sdot = 0, scross = 0, sq = 0;
			for (int e = 0; e < 3; e++) {
				const int a = tr[e], b = tr[(e + 1) % 3];
				const double rx = rr[2 * e], ry = rr[2 * e + 1];
				const double cx = x[a] - x[b], cy = y[a] - y[b];
				const double w = ww[e];
				sdot += w * (rx * cx + ry * cy);
				scross += w * (rx * cy - ry * cx);
				sq += w * (cx * cx + cy * cy + rx * rx + ry * ry);
			}
			const double len = std::sqrt(sdot * sdot + scross * scross);
			rc[t] = len > 1e-12 ? sdot / len : 1.0;
			rs[t] = len > 1e-12 ? scross / len : 0.0;
			E += sq - 2 * len;
		}
		for (int i = 0; i < na; i++) {
			const double dx = x[i] - init[i].x, dy = y[i] - init[i].y;
			E += EPS * (dx * dx + dy * dy);
		}
		for (size_t k = 0; k < nh; k++) {
			double px = -hCur[k].x, py = -hCur[k].y;
			for (int a = 0; a < 3; a++) {
				px += hLam[k][a] * x[hIdx[k][a]];
				py += hLam[k][a] * y[hIdx[k][a]];
			}
			E += W * (px * px + py * py);
		}
		return E;
	};
	// 大域ステップ : 回転を固定して位置を解く
	std::vector<double> bx(na), by(na);
	auto global = [&] {
		bx = cbx;
		by = cby;
		for (int t = 0; t < ntri; t++) {
			const auto& tr = tris[t];
			const auto& rr = triRest_[t];
			const auto& ww = triW_[t];
			for (int e = 0; e < 3; e++) {
				const int a = tr[e], b = tr[(e + 1) % 3];
				const double rx = rr[2 * e], ry = rr[2 * e + 1];
				const double w = ww[e];
				const double ex = w * (rc[t] * rx - rs[t] * ry), ey = w * (rs[t] * rx + rc[t] * ry);
				bx[a] += ex;
				by[a] += ey;
				bx[b] -= ex;
				by[b] -= ey;
			}
		}
		lap_.solve2(bx.data(), by.data());
	};

	// Anderson 加速 : 直近の反復の結果から、収束先を推定して進む
	const int depth = std::clamp(andersonDepth, 0, 10);
	const int n2 = 2 * na;
	std::vector<std::vector<double>> dG, dF;  // 直近の g, f の差分
	std::vector<double> gPrev, fPrev, gx(n2), fx(n2);
	std::vector<double> dX = X, dY = Y;  // 加速が失敗したときに戻る位置 (直前の通常の反復の結果)
	double Eprev = 1e300;
	bool havePrev = false;
	iterations = 0;
	bool converged = false;
	for (int iter = 0; iter < maxIter; iter++) {
		iterations++;
		double E = local(X, Y);
		if (depth > 0 && E > Eprev) {
			// 加速した位置でエネルギーが増えた : 通常の反復の結果に戻してやり直す
			X = dX;
			Y = dY;
			E = local(X, Y);
			dG.clear();
			dF.clear();
			havePrev = false;
		}
		global();
		double change = 0;
		for (int i = 0; i < na; i++) change = std::max(change, std::fabs(bx[i] - X[i]) + std::fabs(by[i] - Y[i]));
		if (tolerance > 0 && change < tolerance) {
			X.swap(bx);
			Y.swap(by);
			converged = true;
			break;
		}
		if (depth == 0) {
			X.swap(bx);
			Y.swap(by);
			continue;
		}
		Eprev = E;
		dX = bx;
		dY = by;
		for (int i = 0; i < na; i++) {
			gx[2 * i] = bx[i];
			gx[2 * i + 1] = by[i];
			fx[2 * i] = bx[i] - X[i];
			fx[2 * i + 1] = by[i] - Y[i];
		}
		if (havePrev) {
			std::vector<double> a(n2), b(n2);
			for (int k = 0; k < n2; k++) {
				a[k] = gx[k] - gPrev[k];
				b[k] = fx[k] - fPrev[k];
			}
			dG.push_back(std::move(a));
			dF.push_back(std::move(b));
			if ((int)dF.size() > depth) {
				dG.erase(dG.begin());
				dF.erase(dF.begin());
			}
		}
		gPrev = gx;
		fPrev = fx;
		havePrev = true;
		const int m = (int)dF.size();
		bool accel = false;
		if (m > 0) {
			// min | f − ΔF θ | を正規方程式で解く
			double M[10][10], r[10];
			for (int p = 0; p < m; p++) {
				for (int q = 0; q <= p; q++) {
					double sum = 0;
					for (int k = 0; k < n2; k++) sum += dF[p][k] * dF[q][k];
					M[p][q] = M[q][p] = sum;
				}
				double sum = 0;
				for (int k = 0; k < n2; k++) sum += dF[p][k] * fx[k];
				r[p] = sum;
			}
			double diag = 0;
			for (int p = 0; p < m; p++) diag = std::max(diag, M[p][p]);
			for (int p = 0; p < m; p++) M[p][p] += 1e-10 * diag + 1e-300;
			bool ok = diag > 0;
			for (int p = 0; p < m && ok; p++) {
				for (int q = 0; q <= p; q++) {
					double sum = M[p][q];
					for (int k = 0; k < q; k++) sum -= M[p][k] * M[q][k];
					if (p == q) {
						if (!(sum > 0)) {
							ok = false;
							break;
						}
						M[p][p] = std::sqrt(sum);
					} else {
						M[p][q] = sum / M[q][q];
					}
				}
			}
			if (ok) {
				double th[10];
				for (int p = 0; p < m; p++) {
					double sum = r[p];
					for (int k = 0; k < p; k++) sum -= M[p][k] * th[k];
					th[p] = sum / M[p][p];
				}
				for (int p = m - 1; p >= 0; p--) {
					double sum = th[p];
					for (int k = p + 1; k < m; k++) sum -= M[k][p] * th[k];
					th[p] = sum / M[p][p];
				}
				bool finiteAll = true;
				for (int p = 0; p < m; p++) finiteAll = finiteAll && std::isfinite(th[p]);
				if (finiteAll) {
					for (int i = 0; i < na; i++) {
						double vx = gx[2 * i], vy = gx[2 * i + 1];
						for (int p = 0; p < m; p++) {
							vx -= th[p] * dG[p][2 * i];
							vy -= th[p] * dG[p][2 * i + 1];
						}
						X[i] = vx;
						Y[i] = vy;
					}
					accel = true;
				}
			}
		}
		if (!accel) {
			X = dX;
			Y = dY;
		}
	}
	if (!converged && depth > 0) {
		// 反復の上限に達した場合は、最後の通常の反復の結果 (エネルギーが下がることが保証された位置) を使う
		X = dX;
		Y = dY;
	}
	out.resize(na);
	for (int i = 0; i < na; i++) out[i] = { X[i], Y[i] };
}

}  // namespace pd
