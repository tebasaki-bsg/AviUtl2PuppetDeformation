// パペット変形 for AviUtl2 - 変形エンジン
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "geom.h"
#include "mesh.h"
#include "model.h"

namespace pd {

// ボーンとピンを指定時刻・ポーズで評価した結果
struct RigResult {
	std::vector<Vec2> boneCur;     // 各ボーンのジョイントの現在位置
	std::vector<double> boneRot;   // 各ボーンに累積した回転 (ラジアン)
	std::vector<Vec2> pinCur;      // 各ピンの現在位置
};

// pose : 補間済みのポーズ、time : オブジェクト先頭からの時間(秒)
void evaluateRig(const Doc& doc, const Pose& pose, double time, RigResult& out);

// 揺れによって各ボーンに加わる角度(度)を計算する (evaluateRig 内部で使用、テスト用に公開)
void computeSwing(const Doc& doc, const Pose& pose, double time, std::vector<double>& addDeg);

// 変形の制御点 (変形前の位置 → 変形後の位置)
struct Handle {
	Vec2 rest, cur;
};

// ピン、ボーン(親から子への線上の点)から制御点を作る
void buildHandles(const Doc& doc, const RigResult& rig, std::vector<Handle>& out);
bool handlesMoved(const std::vector<Handle>& h, double eps = 1e-4);

enum class Method : int { ARAP = 0, MLS = 1 };

// 画像を覆う格子メッシュの変形結果
struct MeshResult {
	int w = 0, h = 0;              // 元画像サイズ
	int cols = 0, rows = 0;        // セル数
	std::vector<Vec2> rest;        // 変形前の頂点 (オブジェクト中心が原点)
	std::vector<Vec2> cur;         // 変形後の頂点
	std::vector<uint8_t> cellActive;  // 不透明部分を含むセル (描画・変形の対象)
	double minx = 0, maxx = 0, miny = 0, maxy = 0;  // 変形後の範囲
	int index(int i, int j) const { return j * (cols + 1) + i; }
};

// 変形器 (ARAPの行列分解をキャッシュする。エフェクトごとに1つ持つ)
class Deformer {
public:
	Deformer();
	~Deformer();
	// alpha : 元画像のα (w*h バイト、nullptrなら全面を対象)
	// 戻り値 : 不透明部分が無い場合は false
	bool deform(int w, int h, const uint8_t* alpha, const std::vector<Handle>& handles, Method method, double stiffness, MeshResult& out);

	// ARAPの仕上げの反復回数 (固定回数なので結果は毎回同じ)
	int arapIterations = 60;
	// 反復を打ち切る変化量 (px、0 なら打ち切らない)
	double arapTolerance = 0.02;
	// 格子のセル数 (長い辺の分割数)
	int gridCells = 64;
	// 反復を加速する (Anderson 加速、エネルギーが下がらない場合は通常の反復に戻す)
	bool anderson = true;
	int andersonDepth = 5;
	// 直前の計算で行った反復回数 (確認用)
	int lastIterations = 0;

private:
	struct ArapCache;
	std::unique_ptr<ArapCache> arap_;
	void solveArap(int w, int h, int cols, int rows, const std::vector<uint8_t>& vActive, const std::vector<Handle>& handles,
				   MeshResult& out);
};

// ---- 三角形の網目 (輪郭と切れ込みに沿う) での変形 ----
struct TriResult {
	std::vector<Vec2> rest;                // 変形前の頂点 (オブジェクト中心が原点)
	std::vector<Vec2> cur;                 // 変形後の頂点
	std::vector<std::array<int, 3>> tris;  // 三角形
	std::vector<uint8_t> split;            // 切れ込みで分けられた頂点
	double minx = 0, maxx = 0, miny = 0, maxy = 0;  // 変形後の範囲
};

class TriDeformer {
public:
	TriDeformer();
	~TriDeformer();
	// alpha : 入力画像のα (w*h バイト)、cuts : 有効な切れ込みの点の列
	// tolerance : 剛体 (ARAP) の打ち切り (px)
	// 戻り値 : 網目が作れない (不透明な部分が無い) 場合は false
	bool deform(int w, int h, const uint8_t* alpha, const std::vector<std::vector<Vec2>>& cuts, const std::vector<Handle>& handles,
				Method method, double stiffness, double tolerance, TriResult& out);

	int arapIterations = 60;
	int andersonDepth = 5;
	MeshParams meshParams;
	int contourMaxSide = 2048;  // 輪郭を取るときの長い辺の上限 (px)
	// 確認用
	int lastIterations = 0;
	int contourBuilds = 0, meshBuilds = 0, restBuilds = 0;
	const Contour& contour() const;
	const TriMesh& mesh() const;
	const TriLocator& locator() const;

private:
	struct Impl;
	std::unique_ptr<Impl> d_;
};

// ---- グループ制御用 : オブジェクトの画像座標 ⇔ グループの座標 ----
// AviUtl2 の標準描画の X,Y / 中心X,Y / Z軸回転 / 拡大率 に相当する2次元の変換
//   グループ座標 = (x, y) + R(rz) * S(sx, sy) * (画像座標 − (cx, cy))
struct Xform2D {
	double x = 0, y = 0, cx = 0, cy = 0, rz = 0, sx = 1, sy = 1;
	Vec2 apply(const Vec2& u) const;
	Vec2 inverse(const Vec2& g) const;
	bool isIdentity() const { return x == 0 && y == 0 && cx == 0 && cy == 0 && rz == 0 && sx == 1 && sy == 1; }
};

// 変形の場 : 格子の変形結果 (rest → cur) から、任意の点の変形後の位置を求める (双線形補間、範囲外は端の移動量)
// offset : 格子の中心の位置 (格子は offset を中心に作られている)
Vec2 sampleField(const MeshResult& field, const Vec2& offset, const Vec2& p);
// 三角形の網目で作った変形の場を読む (点を含む三角形の値を重心座標で補間。網目の外は最も近い三角形で外挿)
// td : field を作った TriDeformer、offset : 場の中心のグループ座標
Vec2 sampleTriField(const TriDeformer& td, const TriResult& field, const Vec2& offset, const Vec2& p);

// 格子のセル数 (長い辺の分割数)
constexpr int kGridCells = 64;

// MLS剛体変形 (変形前の位置 v を変形後の位置へ写す)
Vec2 mlsRigid(const std::vector<Handle>& h, const Vec2& v, double alpha);

}  // namespace pd
