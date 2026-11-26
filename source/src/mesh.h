// パペット変形 for AviUtl2 - 輪郭と網目 (三角形分割・切れ込み)
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "geom.h"

namespace pd {

struct ContourSource;

// 不透明な部分の輪郭 (オブジェクト中心が原点の座標、閉じた折れ線の集まり)
struct Contour {
	std::vector<std::vector<Vec2>> loops;
	double area = 0;  // 不透明な部分 (広げた後) の面積 (px²)
	int level = 0;    // 粗さの段階 (0 がいちばん細かい)
	std::shared_ptr<ContourSource> src;  // 粗い段階を作るための元の情報 (buildContour で作ったものだけ。座標を書き換えたら外す)
	bool empty() const { return loops.empty(); }
	// 偶奇規則で内側かどうか
	bool inside(const Vec2& p) const;
};

// α (w*h バイト) から輪郭を作る (段階 0)。大きい画像は長い辺 maxSide px (初期値 2048) まで縮めてから取る
// 不透明な画素 (α > 0) を 2px 広げ (画像の外にもはみ出す)、画素の境目をなぞって Douglas–Peucker (1.5px) で単純化する
// (パペット変形改 v1.0.1 と同じ。v1.0.0 までは画像の範囲内まで広げ、0.75px で単純化していた)
// 戻り値 : 不透明な画素が無ければ false
bool buildContour(int w, int h, const uint8_t* alpha, Contour& out, int maxSide = 2048);

// base と同じ画像の、段階 level の粗い輪郭 (1〜maxContourLevel())。間引きを粗くし、広げる幅も大きくする。
// 面積 64 画素以下の小さな島 (ほぼ透明な点など) はさらに大きく広げ、近くのものとまとめる。
// 網目の三角形が上限を超えるときに使う。作ったものは base の元の情報の中に残して使い回す。作れなければ nullptr
const Contour* coarserContour(const Contour& base, int level);
int maxContourLevel();

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
	int contourLevel = 0;                   // 使った輪郭の粗さの段階
	bool empty() const { return tris.empty(); }
};

// 輪郭と切れ込み (有効なものの点の列) から網目を作る
// 三角形が上限を超えるときは、まず輪郭を粗い段階にし、いちばん粗い段階でも超えるときだけ内側の間隔を広げる
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
