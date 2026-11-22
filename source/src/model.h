// パペット変形 for AviUtl2 - データモデル
#pragma once
#include <cstdint>
#include <vector>

namespace pd {

constexpr int kMaxPins = 256;
constexpr int kMaxBones = 256;
constexpr int kChunkCount = 8;          // 汎用データ項目の数
constexpr int kChunkSize = 16384;       // 1項目あたりの最大サイズ(16KB)
constexpr int kPoseTracks = 16;         // 「ポーズ補間」の最大数
constexpr int kMaxCuts = 64;            // 切れ込みの最大本数
constexpr int kMaxCutPoints = 32;       // 切れ込み1本の点の最大数 (端を含む)

// ---- 全区間で共通の値 ----
struct Pin {
	float x = 0, y = 0;     // 基準X/Y (オブジェクト中心が原点、y下向き、px)
	bool enabled = true;    // 有効
	int16_t bone = -1;      // 紐付いたボーン (-1 = なし)
};

struct Bone {
	float x = 0, y = 0;     // ジョイントX/Y
	bool enabled = true;    // 有効
	int16_t parent = -1;    // 親ボーン (-1 = なし)
	float phase = 0;        // 揺れ位相 (度、初期位相。中間点ごとには変わらない)
};

// 切れ込み (折れ線。始点 → 中間の点 → 終点。全区間で共通)
struct CutPoint {
	float x = 0, y = 0;
};
struct Cut {
	bool enabled = true;
	std::vector<CutPoint> points;  // 2〜kMaxCutPoints 点
};

// ---- 中間点ごとの値 (ポーズ) ----
struct PinPose {
	float mx = 0, my = 0;   // 移動X/Y (グローバル軸)
	// この点から次の点までの区間で使う「ポーズ補間」の番号 (0 = ポーズ補間1) [移動X, 移動Y]
	uint8_t interp[2] = { 0, 0 };
};

struct BonePose {
	float angle = 0;        // 角度 (度)
	float stretch = 100;    // 伸縮 (%)
	float swing = 0;        // 揺れ角度 (度、最大振幅)
	float period = 1;       // 揺れ周期 (秒)
	float propagate = 100;  // 揺れ伝播 (%)
	float shinari = 60;     // しなり (度、世代ごとの遅れ)
	// この点から次の点までの区間で使う「ポーズ補間」の番号
	// [角度, 伸縮, 揺れ角度, 揺れ周期, 揺れ伝播, しなり]
	uint8_t interp[6] = { 0, 0, 0, 0, 0, 0 };
};

struct Pose {
	int32_t frame = 0;      // この点のフレーム (オブジェクト先頭からの相対)
	std::vector<PinPose> pins;
	std::vector<BonePose> bones;
};

struct Doc {
	std::vector<Pin> pins;
	std::vector<Bone> bones;
	std::vector<Pose> poses;    // 開始点・中間点・終了点の順
	std::vector<Cut> cuts;      // 切れ込み

	// 新しく付けたエフェクトの初期状態 (v0.4.0 からピンなし。パペット変形改と同じ)
	static Doc makeDefault();
	// ピン・ボーンの数に合わせて各ポーズの配列長をそろえる (切れ込みの数・点の数も制限内に収める)
	void normalize();
	// 親子関係の破損(範囲外・循環)を修正する
	void sanitize();
};

// ---- 保存形式 ----
// 失敗時は false (データが空の場合も false)
bool deserialize(const uint8_t* data, size_t size, Doc& out);
std::vector<uint8_t> serialize(const Doc& doc);
// 保存できる最大サイズ
constexpr size_t kMaxBlob = (size_t)kChunkCount * kChunkSize;

// ---- 区間とポーズの対応 ----
// 保存済みポーズ (各々に保存時のフレームあり) を、現在の点 (開始点・中間点・終了点) の並びに合わせる
//  ・点の数が同じなら順番どおりに対応 (中間点の移動に追従)
//  ・数が違う場合は、同じフレームの保存済みポーズを使い、無ければ前後の保存済みポーズを時間で補間
std::vector<Pose> mapPoses(const Doc& doc, const std::vector<int>& pointFrames);

// 2つのポーズを補間 (t=0でa、t=1でb)。補間の選択は a のものを引き継ぐ
void lerpPose(const Pose& a, const Pose& b, double t, size_t npins, size_t nbones, Pose& out);
// 区間の左の点 a から右の点 b へ、各項目が選んだ「ポーズ補間」の割合で補間する
// fracs[k] : ポーズ補間(k+1) の割合 (nfrac 個)
void blendPose(const Pose& a, const Pose& b, const double* fracs, int nfrac, size_t npins, size_t nbones, Pose& out);

}  // namespace pd
