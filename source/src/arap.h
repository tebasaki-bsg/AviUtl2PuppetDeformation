// パペット変形 for AviUtl2 - ARAP の連立方程式と反復 (格子・三角形の網目で共通)
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "geom.h"

namespace pd {

// スカイライン形式の対称行列 (下三角、行ごとに必要な幅だけ持つ) とコレスキー分解
struct SkylineMatrix {
	int n = 0;
	std::vector<int> first;     // 行 i で値を持つ最初の列
	std::vector<size_t> off;    // 行 i の先頭の位置 (off[i] + (j - first[i]))
	std::vector<double> v;
	void init(const std::vector<int>& first_);
	double& at(int i, int j) { return v[off[i] + (j - first[i])]; }
	void add(int a, int b, double val) {
		if (a < b) {
			const int t = a;
			a = b;
			b = t;
		}
		at(a, b) += val;
	}
	bool factor();
	void solve(double* x) const;
	void solve2(double* x, double* y) const;  // x, y を同時に解く
	size_t size() const { return v.size(); }
};

// 頂点を RCM (Reverse Cuthill–McKee) で並べる。戻り値 : 並べた順の元の番号
std::vector<int> rcmOrder(int n, const std::vector<std::vector<int>>& adj);
// 並べ方 order のスカイラインの大きさ (小さいほど速い)
size_t skylineSize(const std::vector<int>& order, const std::vector<std::vector<int>>& adj);

// ARAP の問題 (頂点の番号は解く順に並べたもの)
class ArapProblem {
public:
	// ---- 入力 (prepare の前に設定する) ----
	int n = 0;
	std::vector<std::array<int, 3>> tris;     // 三角形 (向きはそろっていること)
	std::vector<Vec2> rest;                   // 変形前の位置
	bool cotWeights = false;                  // true : cot の重み、false : すべて 1 (格子用)
	std::vector<std::array<int, 3>> hIdx;     // 制御点が属する三角形の頂点
	std::vector<std::array<double, 3>> hLam;  // 重心座標

	static constexpr double W = 1.0e4;     // 制御点の拘束の強さ
	static constexpr double EPS = 1.0e-4;  // 初期値に留める弱い拘束

	// 行列を作って分解する (戻り値 : 失敗したら false)
	bool prepare();
	// init : 初期値 (MLS の結果など。制御点の無い部分はここに留まる)、hCur : 制御点の変形後の位置
	// tolerance : 1回の反復での頂点の動きがこの値より小さくなったら止める (px)
	void solve(const std::vector<Vec2>& init, const std::vector<Vec2>& hCur, double tolerance, int maxIter, int andersonDepth,
			   std::vector<Vec2>& out, int& iterations) const;

private:
	std::vector<std::array<double, 6>> triRest_;  // 三角形の各辺の変形前のベクトル (辺 e : 頂点 e − 頂点 e+1)
	std::vector<std::array<double, 3>> triW_;     // 各辺の重み
	SkylineMatrix lap_, sim_;
};

}  // namespace pd
