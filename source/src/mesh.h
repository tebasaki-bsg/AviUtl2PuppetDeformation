// パペット変形 for AviUtl2 - 輪郭と網目 (三角形分割・切れ込み)
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "geom.h"

namespace pd {

// 不透明な部分の輪郭 (オブジェクト中心が原点の座標、閉じた折れ線の集まり)
struct Contour {
	std::vector<std::vector<Vec2>> loops;
	double area = 0;  // 不透明な部分 (2px 広げた後) の面積 (px²)
	bool empty() const { return loops.empty(); }
	// 偶奇規則で内側かどうか
	bool inside(const Vec2& p) const;
};

// α (w*h バイト) から輪郭を作る。大きい画像は長い辺 maxSide px (初期値 2048) まで縮めてから取る
// 不透明な画素 (α > 0) を 2px 広げ (画像の範囲内まで)、画素の境目をなぞって Douglas–Peucker (0.75px) で単純化する
// 戻り値 : 不透明な画素が無ければ false
bool buildContour(int w, int h, const uint8_t* alpha, Contour& out, int maxSide = 2048);

// 網目の作り方の設定
struct MeshParams {
	int targetTris = 3000;     // 三角形の数の目安
	int maxTris = 5440;        // 三角形の数の上限
	double minSpacing = 8;     // 頂点の間隔の最小値 (px)
};

// 三角形の網目 (切れ込みの両側は別の頂点)
struct TriMesh {
	std::vector<Vec2> pos;                  // 変形前の位置
	std::vector<std::array<int, 3>> tris;   // 三角形 (反時計回り : 数学の向き、y 下向きの画面では時計回り)
	std::vector<int> orig;                  // 分ける前の頂点の番号 (同じ位置の頂点は同じ番号)
	std::vector<uint8_t> split;             // 切れ込みで分けられた頂点
	double spacing = 0;                     // 頂点の間隔 (px)
	bool empty() const { return tris.empty(); }
};

// 輪郭と切れ込み (有効なものの点の列) から網目を作る
// 戻り値 : 網目が作れなければ false
bool buildMesh(const Contour& contour, const std::vector<std::vector<Vec2>>& cuts, const MeshParams& params, TriMesh& out);

// 網目の中で点を探す (重心座標)。網目の外の点は、最も近い三角形の重心座標 (外挿) を返す
// cuts : 切れ込みの線の上にある点は、始点から終点を見て右側の三角形に付ける
class TriLocator {
public:
	void build(const TriMesh& mesh);
	// 戻り値 : 三角形の番号 (網目が空なら -1)
	int locate(const Vec2& p, const std::vector<std::vector<Vec2>>& cuts, std::array<double, 3>& bary) const;

private:
	const TriMesh* mesh_ = nullptr;
	double x0_ = 0, y0_ = 0, cell_ = 1;
	int nx_ = 0, ny_ = 0;
	std::vector<std::vector<int>> buckets_;
};

}  // namespace pd
