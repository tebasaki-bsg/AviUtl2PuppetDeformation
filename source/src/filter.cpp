// パペット変形 for AviUtl2 - フィルタ効果
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deform.h"
#include "host.h"
#include "model.h"

namespace pd {

namespace {

bool procVideo(FILTER_PROC_VIDEO* video);
void* createState(int64_t effectId);
void destroyState(int64_t effectId, void* userdata);
void onOpenButton(EDIT_SECTION* edit, OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR item);

// ---- 設定項目 ----
FILTER_ITEM_SELECT::ITEM g_methodList[] = { { L"剛体（ARAP）", 0 }, { L"なめらか（MLS）", 1 }, { nullptr, 0 } };
FILTER_ITEM_SELECT g_itemMethod(kItemMethod, 0, g_methodList);
FILTER_ITEM_TRACK g_itemStiffness(kItemStiffness, 2.0, 0.1, 8.0, 0.01);
// 剛体 (ARAP) の打ち切り : プレビュー再生中 / ファイル出力中と編集中 (停止中)
constexpr double kPrecisionValues[] = { 0.02, 0.05, 0.1, 0.25, 0.5 };
FILTER_ITEM_SELECT::ITEM g_precisionList[] = { { L"0.02px", 0 }, { L"0.05px", 1 }, { L"0.1px", 2 }, { L"0.25px", 3 }, { L"0.5px", 4 }, { nullptr, 0 } };
FILTER_ITEM_SELECT g_itemPreviewPrecision(kItemPreviewPrecision, 3, g_precisionList);
FILTER_ITEM_SELECT g_itemExportPrecision(kItemExportPrecision, 0, g_precisionList);
// ポーズ補間 : 最大16個を用意し、「ポーズ補間の数」で表示する数を選ぶ
FILTER_ITEM_SELECT::ITEM g_countList[] = { { L"1", 1 },	  { L"2", 2 },	 { L"3", 3 },	{ L"4", 4 },   { L"5", 5 },	  { L"6", 6 },
										   { L"7", 7 },	  { L"8", 8 },	 { L"9", 9 },	{ L"10", 10 }, { L"11", 11 }, { L"12", 12 },
										   { L"13", 13 }, { L"14", 14 }, { L"15", 15 }, { L"16", 16 }, { nullptr, 0 } };
FILTER_ITEM_SELECT g_itemPoseCount(kItemPoseCount, 1, g_countList);
FILTER_ITEM_TRACK g_pose0(kItemPoseTracks[0], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose1(kItemPoseTracks[1], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose2(kItemPoseTracks[2], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose3(kItemPoseTracks[3], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose4(kItemPoseTracks[4], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose5(kItemPoseTracks[5], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose6(kItemPoseTracks[6], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose7(kItemPoseTracks[7], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose8(kItemPoseTracks[8], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose9(kItemPoseTracks[9], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose10(kItemPoseTracks[10], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose11(kItemPoseTracks[11], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose12(kItemPoseTracks[12], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose13(kItemPoseTracks[13], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose14(kItemPoseTracks[14], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK g_pose15(kItemPoseTracks[15], 0.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK* const g_poseTracks[kPoseTracks] = { &g_pose0, &g_pose1, &g_pose2,  &g_pose3,  &g_pose4,  &g_pose5,  &g_pose6,  &g_pose7,
														&g_pose8, &g_pose9, &g_pose10, &g_pose11, &g_pose12, &g_pose13, &g_pose14, &g_pose15 };
FILTER_ITEM_BUTTON g_itemButton(kItemButton, onOpenButton);
FILTER_ITEM_DATA<void> g_data0(kDataNames[0]);
FILTER_ITEM_DATA<void> g_data1(kDataNames[1]);
FILTER_ITEM_DATA<void> g_data2(kDataNames[2]);
FILTER_ITEM_DATA<void> g_data3(kDataNames[3]);
FILTER_ITEM_DATA<void> g_data4(kDataNames[4]);
FILTER_ITEM_DATA<void> g_data5(kDataNames[5]);
FILTER_ITEM_DATA<void> g_data6(kDataNames[6]);
FILTER_ITEM_DATA<void> g_data7(kDataNames[7]);
FILTER_ITEM_DATA<void>* const g_dataItems[kChunkCount] = { &g_data0, &g_data1, &g_data2, &g_data3,
														   &g_data4, &g_data5, &g_data6, &g_data7 };
// 剛性は なめらか（MLS） のときだけ表示
FILTER_ITEM_HIDE_RULE g_hideStiffness(kItemStiffness, kItemMethod, FILTER_ITEM_HIDE_RULE::OPERATOR::EQUAL, 0);
// プレビュー精度・書き出し精度は 剛体（ARAP） のときだけ表示
FILTER_ITEM_HIDE_RULE g_hidePreviewPrecision(kItemPreviewPrecision, kItemMethod, FILTER_ITEM_HIDE_RULE::OPERATOR::EQUAL, 1);
FILTER_ITEM_HIDE_RULE g_hideExportPrecision(kItemExportPrecision, kItemMethod, FILTER_ITEM_HIDE_RULE::OPERATOR::EQUAL, 1);
// ポーズ補間(k) は「ポーズ補間の数」が k より小さいときは隠す
#define PD_HIDE(k) FILTER_ITEM_HIDE_RULE g_hidePose##k(kItemPoseTracks[k - 1], kItemPoseCount, FILTER_ITEM_HIDE_RULE::OPERATOR::LESS, k);
PD_HIDE(2) PD_HIDE(3) PD_HIDE(4) PD_HIDE(5) PD_HIDE(6) PD_HIDE(7) PD_HIDE(8) PD_HIDE(9)
PD_HIDE(10) PD_HIDE(11) PD_HIDE(12) PD_HIDE(13) PD_HIDE(14) PD_HIDE(15) PD_HIDE(16)
#undef PD_HIDE

void* g_items[] = { &g_itemMethod, &g_itemStiffness, &g_itemPreviewPrecision, &g_itemExportPrecision, &g_itemPoseCount,
					&g_pose0,  &g_pose1,  &g_pose2,  &g_pose3,  &g_pose4,  &g_pose5,  &g_pose6,  &g_pose7,
					&g_pose8,  &g_pose9,  &g_pose10, &g_pose11, &g_pose12, &g_pose13, &g_pose14, &g_pose15,
					&g_itemButton,
					&g_data0, &g_data1, &g_data2, &g_data3, &g_data4, &g_data5, &g_data6, &g_data7,
					&g_hideStiffness, &g_hidePreviewPrecision, &g_hideExportPrecision,
					&g_hidePose2,  &g_hidePose3,  &g_hidePose4,  &g_hidePose5,  &g_hidePose6,  &g_hidePose7,  &g_hidePose8, &g_hidePose9,
					&g_hidePose10, &g_hidePose11, &g_hidePose12, &g_hidePose13, &g_hidePose14, &g_hidePose15, &g_hidePose16,
					nullptr };

FILTER_PLUGIN_TABLE g_table = {
	FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_USERDATA,
	kEffectName,
	L"変形",
	L"パペット変形 for AviUtl2 v1.0.1",
	g_items,
	procVideo,
	nullptr,
	createState,
	destroyState,
};

// ---- エフェクトごとの状態 ----
struct EffectState {
	std::mutex m;
	Deformer deformer;      // グループ制御の配下のオブジェクト用 (格子)
	TriDeformer triDeformer;  // 通常 (輪郭と切れ込みに沿った三角形の網目)
	TriResult tri;
	std::vector<PIXEL_RGBA> pixels;
	std::vector<uint8_t> alpha;
	std::vector<VERTEX_TEXTURE> vertices;
	MeshResult mesh;
	std::vector<Handle> handles;
	RigResult rig;
	// グループ制御用 : グループ全体の変形の場
	// 配下を合わせた不透明部分の輪郭に沿った三角形の網目 (切れ込みは入れない)。輪郭は長い辺 1024px まで縮めて取る
	TriDeformer fieldDeformer;
	TriResult field;
	bool fieldOk = false;
	EffectState() { fieldDeformer.contourMaxSide = 1024; }
	uint64_t fieldKey = 0;       // 制御点・設定・配下の配置と不透明部分 から作ったキー
	uint64_t fieldKeyRig = 0;    // 制御点・設定 だけのキー
	int fieldFrame = -1;         // 変形の場を確定したフレーム
	// 配下の各オブジェクトの不透明部分 (縮小したα) と位置。グループ全体の網目を不透明部分だけに作るために使う
	struct ChildMask {
		Xform2D xf;
		int w = 0, h = 0;   // 元の画像サイズ
		int ds = 1;         // 縮小率
		int mw = 0, mh = 0; // 縮小後のサイズ
		std::vector<uint8_t> a;
		uint64_t hash = 0;  // 位置と不透明部分のハッシュ
		int lastFrame = -1;
	};
	std::map<int64_t, ChildMask> children;
	int curFrame = -1;
	std::vector<uint8_t> unionAlpha;
};

void* createState(int64_t) { return new EffectState(); }
void destroyState(int64_t, void* userdata) { delete static_cast<EffectState*>(userdata); }

// 「設定ウィンドウを開く」ボタン
void onOpenButton(EDIT_SECTION* edit, OBJECT_HANDLE object, LPCWSTR effect, LPCWSTR) {
	if (!edit || !object) return;
	EFFECT_HANDLE eff = edit->find_effect(object, effect);
	if (!eff) {
		logWarn(L"[パペット変形] ボタンの対象エフェクトが見つかりません");
		return;
	}
	auto* ref = new TargetRef{ edit->get_object_id(object), edit->get_effect_id(eff) };
	if (!g_msgWnd || !PostMessageW(g_msgWnd, WM_PD_OPEN, 0, (LPARAM)ref)) delete ref;
}

constexpr int kMaxOutputSide = 8192;

bool objectHasEffect(EDIT_SECTION* e, OBJECT_HANDLE obj, EFFECT_HANDLE eff) {
	const int n = e->get_effect_list(obj, nullptr, 0);
	if (n <= 0) return false;
	std::vector<EFFECT_HANDLE> list(n);
	const int got = e->get_effect_list(obj, list.data(), n);
	for (int i = 0; i < got; i++) {
		if (list[i] == eff || (e->get_effect_id(list[i]) != 0 && e->get_effect_id(list[i]) == e->get_effect_id(eff))) return true;
	}
	return false;
}

uint64_t fnv(uint64_t h, const void* p, size_t n) {
	auto b = (const uint8_t*)p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}

// パペット変形が付いているオブジェクトを探す (グループ制御に付けた場合はグループ制御のオブジェクト)
OBJECT_HANDLE findOwner(FILTER_PROC_VIDEO* video) {
	OBJECT_HANDLE owner = video->object->handle;
	if (!video->edit || !owner || !video->object->effect_handle) return owner;
	if (objectHasEffect(video->edit, owner, video->object->effect_handle)) return owner;
	for (int i = 0; i < 16; i++) {
		OBJECT_HANDLE g = video->get_group_control_object(i);
		if (!g) break;
		if (objectHasEffect(video->edit, g, video->object->effect_handle)) return g;
	}
	return owner;
}

// 今の状態で使う剛体の打ち切り (px)
double currentTolerance() {
	int state = EDIT_HANDLE::EDIT_STATE_EDIT;
	if (g_edit && g_edit->get_edit_state) state = g_edit->get_edit_state();
	const int idx = state == EDIT_HANDLE::EDIT_STATE_PLAY ? g_itemPreviewPrecision.value : g_itemExportPrecision.value;
	return kPrecisionValues[std::clamp(idx, 0, 4)];
}

bool procVideo(FILTER_PROC_VIDEO* video) {
	const int w = video->object->width, h = video->object->height;
	if (w <= 0 || h <= 0) return true;

	static EffectState fallback;
	EffectState* st = video->userdata ? static_cast<EffectState*>(video->userdata) : &fallback;
	std::lock_guard<std::mutex> lk(st->m);

	// ---- 保存データ ----
	Doc doc;
	{
		std::vector<std::pair<const void*, int>> chunks;
		for (auto* item : g_dataItems) chunks.push_back({ item->value, item->size });
		if (!docFromChunks(chunks, doc)) doc = Doc::makeDefault();
	}

	// ---- グループ制御かどうか ----
	// グループ制御に付けた場合、このフィルタは配下のオブジェクトごとに呼ばれる。
	// ピン・ボーンはグループの座標で持ち、各オブジェクトの位置・回転・拡大率から
	// グループの中での位置を求めて同じ変形を当てはめる (= 全体を1枚の絵として変形したのと同じ結果)
	const OBJECT_HANDLE owner = findOwner(video);
	const bool group = (owner && owner != video->object->handle) || video->object->layer != video->object->effect_layer;
	Xform2D xf;
	if (group) {
		OBJECT_IMAGE_PARAM op{};
		op.sx = op.sy = op.sz = 1;
		if (video->get_output_image_param(nullptr, 0, &op, sizeof(op))) {
			xf.x = op.x;
			xf.y = op.y;
			xf.cx = op.cx;
			xf.cy = op.cy;
			xf.rz = op.rz;
			xf.sx = op.sx;
			xf.sy = op.sy;
		}
	}
	const int sceneFrame = video->object->frame_s + video->object->frame;

	// ---- 現在のポーズ (中間点の左右のポーズを「ポーズ補間」の移動方法で補間) ----
	Pose pose;
	double time = video->object->time;
	{
		SectionInfo sec;
		bool ok = false;
		if (video->edit && owner) ok = getSectionInfo(video->edit, owner, video->object->effect_handle, sceneFrame, nullptr, sec);
		if (ok && sec.pointFrames.size() >= 2) {
			auto poses = mapPoses(doc, sec.pointFrames);
			// 現在の区間で選ばれている「ポーズ補間」の補間割合だけを本体に問い合わせる
			const Pose& left = poses[sec.section];
			const Pose& right = poses[sec.section + 1];
			uint32_t mask = 0;
			for (size_t i = 0; i < doc.pins.size(); i++) {
				const PinPose& a = left.pins[i];
				const PinPose& b = right.pins[i];
				if (a.mx != b.mx) mask |= 1u << a.interp[0];
				if (a.my != b.my) mask |= 1u << a.interp[1];
			}
			for (size_t i = 0; i < doc.bones.size(); i++) {
				const BonePose& a = left.bones[i];
				const BonePose& b = right.bones[i];
				const float va[6] = { a.angle, a.stretch, a.swing, a.period, a.propagate, a.shinari };
				const float vb[6] = { b.angle, b.stretch, b.swing, b.period, b.propagate, b.shinari };
				for (int k = 0; k < 6; k++) {
					if (va[k] != vb[k]) mask |= 1u << a.interp[k];
				}
			}
			if (mask) {
				double now[kPoseTracks];
				for (int t = 0; t < kPoseTracks; t++) now[t] = g_poseTracks[t]->value;
				fillTrackFracs(video->edit, video->object->effect_handle, now, mask, sec);
			}
			blendPose(poses[sec.section], poses[sec.section + 1], sec.fracs, kPoseTracks, doc.pins.size(), doc.bones.size(), pose);
			if (video->scene->rate > 0) time = (double)sec.relFrame * video->scene->scale / video->scene->rate;
		} else {
			auto poses = mapPoses(doc, { 0 });
			pose = poses[0];
		}
	}
	evaluateRig(doc, pose, time, st->rig);
	buildHandles(doc, st->rig, st->handles);
	const bool moved = handlesMoved(st->handles);

	// ---- 入力画像 (不透明部分の判定と、専用ウィンドウへの受け渡し) ----
	auto slot = getCaptureSlot(video->object->effect_id);
	if (!slot && video->edit && owner) slot = getCaptureSlotByObject(video->edit->get_object_id(owner));
	const bool capture = slot && (sceneFrame == slot->requestedFrame.load() || sceneFrame == g_editFrame.load());
	if (!moved && !capture) return true;
	st->pixels.resize((size_t)w * h);
	video->get_image_data(st->pixels.data());
	if (capture) {
		{
			std::lock_guard<std::mutex> lk2(slot->m);
			if (slot->latestFrame != sceneFrame) {
				slot->images.clear();  // 別のフレームの画像は捨てる
				slot->latestFrame = sceneFrame;
			}
			CapturedImage& ci = slot->images[video->object->id];
			ci.pixels = st->pixels;
			ci.w = w;
			ci.h = h;
			ci.xf = xf;
			ci.frame = sceneFrame;
			ci.layer = video->object->layer;
			slot->serial++;
		}
		if (slot->notify) PostMessageW(slot->notify, WM_PD_IMAGE, 0, 0);
	}
	if (!moved) return true;  // 変形なし

	st->alpha.resize((size_t)w * h);
	for (size_t i = 0; i < st->alpha.size(); i++) st->alpha[i] = st->pixels[i].a;

	const Method method = g_itemMethod.value == 1 ? Method::MLS : Method::ARAP;
	const double tolerance = currentTolerance();
	double minx, maxx, miny, maxy;
	auto& vt = st->vertices;
	vt.clear();
	if (!group) {
		// ---- 輪郭と切れ込みに沿った三角形の網目で変形 ----
		std::vector<std::vector<Vec2>> cuts;
		for (auto& c : doc.cuts) {
			if (!c.enabled || c.points.size() < 2) continue;
			std::vector<Vec2> pts;
			for (auto& p : c.points) pts.push_back({ p.x, p.y });
			cuts.push_back(std::move(pts));
		}
		if (!st->triDeformer.deform(w, h, st->alpha.data(), cuts, st->handles, method, g_itemStiffness.value, tolerance, st->tri)) return true;
		const TriResult& r = st->tri;
		minx = r.minx;
		maxx = r.maxx;
		miny = r.miny;
		maxy = r.maxy;
		// 切れ込みで分けた頂点は、貼り付け位置を自分の側へ 0.5px 寄せる (切り口で反対側の画素がにじまないように)
		std::vector<Vec2> inset(r.rest.size(), Vec2{ 0, 0 });
		bool anySplit = false;
		for (auto s : r.split) anySplit = anySplit || s;
		if (anySplit) {
			for (auto& t : r.tris) {
				const Vec2 g = { (r.rest[t[0]].x + r.rest[t[1]].x + r.rest[t[2]].x) / 3, (r.rest[t[0]].y + r.rest[t[1]].y + r.rest[t[2]].y) / 3 };
				for (int k = 0; k < 3; k++) {
					const int v = t[k];
					if (!r.split[v]) continue;
					const double dx = g.x - r.rest[v].x, dy = g.y - r.rest[v].y, l = std::hypot(dx, dy);
					if (l > 1e-9) {
						inset[v].x += dx / l;
						inset[v].y += dy / l;
					}
				}
			}
			for (auto& d : inset) {
				const double l = std::hypot(d.x, d.y);
				if (l > 1e-9) {
					d.x *= 0.5 / l;
					d.y *= 0.5 / l;
				}
			}
		}
		vt.reserve(r.tris.size() * 3);
		for (auto& t : r.tris) {
			for (int k = 0; k < 3; k++) {
				const Vec2& c = r.cur[t[k]];
				const Vec2 rr = { r.rest[t[k]].x + inset[t[k]].x, r.rest[t[k]].y + inset[t[k]].y };
				vt.push_back({ (float)c.x, (float)c.y, 0.0f, (float)((rr.x + w * 0.5) / w), (float)((rr.y + h * 0.5) / h), 1.0f });
			}
		}
	} else {
		// ---- このオブジェクトの不透明部分を記録 ----
		if (sceneFrame != st->curFrame) {
			// フレームが変わったら、前のフレームで描かれなかったオブジェクトの記録を捨てる
			for (auto it = st->children.begin(); it != st->children.end();) {
				if (it->second.lastFrame != st->curFrame) {
					it = st->children.erase(it);
				} else {
					++it;
				}
			}
			st->curFrame = sceneFrame;
		}
		{
			auto& cm = st->children[video->object->id];
			cm.xf = xf;
			cm.w = w;
			cm.h = h;
			cm.ds = std::max(1, std::max(w, h) / 384);
			cm.mw = (w + cm.ds - 1) / cm.ds;
			cm.mh = (h + cm.ds - 1) / cm.ds;
			cm.a.assign((size_t)cm.mw * cm.mh, 0);
			for (int y = 0; y < h; y++) {
				const uint8_t* row = st->alpha.data() + (size_t)y * w;
				uint8_t* dst = cm.a.data() + (size_t)(y / cm.ds) * cm.mw;
				for (int x = 0; x < w; x++) {
					if (row[x]) dst[x / cm.ds] = 1;
				}
			}
			const double xv[7] = { xf.x, xf.y, xf.cx, xf.cy, xf.rz, xf.sx, xf.sy };
			const int dv[3] = { w, h, cm.ds };
			cm.hash = fnv(fnv(fnv(1469598103934665603ull, xv, sizeof(xv)), dv, sizeof(dv)), cm.a.data(), cm.a.size());
			cm.lastFrame = sceneFrame;
		}

		// ---- グループ全体の変形の場 (同じフレームの配下のオブジェクト間で使い回す) ----
		const double sw = video->scene->width, sh = video->scene->height;
		double x0 = -sw / 2, x1 = sw / 2, y0 = -sh / 2, y1 = sh / 2;
		for (auto& hd : st->handles) {
			x0 = std::min(x0, hd.rest.x);
			x1 = std::max(x1, hd.rest.x);
			y0 = std::min(y0, hd.rest.y);
			y1 = std::max(y1, hd.rest.y);
		}
		for (auto& kv : st->children) {
			const auto& cm = kv.second;
			for (int k = 0; k < 4; k++) {
				const Vec2 c = cm.xf.apply({ (k & 1) ? cm.w / 2.0 : -cm.w / 2.0, (k & 2) ? cm.h / 2.0 : -cm.h / 2.0 });
				x0 = std::min(x0, c.x);
				x1 = std::max(x1, c.x);
				y0 = std::min(y0, c.y);
				y1 = std::max(y1, c.y);
			}
		}
		const double margin = 0.05 * std::max(x1 - x0, y1 - y0) + 32;
		x0 -= margin;
		x1 += margin;
		y0 -= margin;
		y1 += margin;
		const int Wd = std::min(16384, std::max(16, (int)std::ceil(x1 - x0)));
		const int Hd = std::min(16384, std::max(16, (int)std::ceil(y1 - y0)));
		const Vec2 dc{ (x0 + x1) / 2, (y0 + y1) / 2 };
		// 同じフレームの配下のオブジェクトには同じ変形の場を使う (継ぎ目がずれないように)。
		// フレームが変わっても、制御点・設定・配下の配置と不透明部分が同じなら作り直さない
		uint64_t keyRig = 1469598103934665603ull;
		for (auto& hd : st->handles) keyRig = fnv(keyRig, &hd, sizeof(hd));
		const double params[7] = { (double)method, g_itemStiffness.value, (double)Wd, (double)Hd, dc.x, dc.y, tolerance };
		keyRig = fnv(keyRig, params, sizeof(params));
		uint64_t key = keyRig;
		for (auto& kv : st->children) {
			key = fnv(key, &kv.first, sizeof(kv.first));
			key = fnv(key, &kv.second.hash, sizeof(kv.second.hash));
		}
		const bool reuse = (sceneFrame == st->fieldFrame && keyRig == st->fieldKeyRig) || key == st->fieldKey;
		if (!reuse) {
			// 配下の全オブジェクトの不透明部分を、グループの座標に重ねたα (網目を作る範囲)
			st->unionAlpha.assign((size_t)Wd * Hd, 0);
			for (auto& kv : st->children) {
				const auto& cm = kv.second;
				const double scale = std::max(std::fabs(cm.xf.sx), std::fabs(cm.xf.sy));
				const int rad = (int)std::ceil(cm.ds * scale * 0.75) + 1;
				for (int my = 0; my < cm.mh; my++) {
					for (int mx = 0; mx < cm.mw; mx++) {
						if (!cm.a[(size_t)my * cm.mw + mx]) continue;
						const Vec2 u{ (mx + 0.5) * cm.ds - cm.w / 2.0, (my + 0.5) * cm.ds - cm.h / 2.0 };
						const Vec2 g = cm.xf.apply(u);
						const int px = (int)std::floor(g.x - dc.x + Wd / 2.0), py = (int)std::floor(g.y - dc.y + Hd / 2.0);
						for (int yy = std::max(0, py - rad); yy <= std::min(Hd - 1, py + rad); yy++) {
							uint8_t* row = st->unionAlpha.data() + (size_t)yy * Wd;
							for (int xx = std::max(0, px - rad); xx <= std::min(Wd - 1, px + rad); xx++) row[xx] = 255;
						}
					}
				}
			}
			std::vector<Handle> shifted = st->handles;
			for (auto& hd : shifted) {
				hd.rest.x -= dc.x;
				hd.rest.y -= dc.y;
				hd.cur.x -= dc.x;
				hd.cur.y -= dc.y;
			}
			static const std::vector<std::vector<Vec2>> noCuts;
			st->fieldOk = st->fieldDeformer.deform(Wd, Hd, st->unionAlpha.data(), noCuts, shifted, method, g_itemStiffness.value, tolerance, st->field);
			st->fieldKey = key;
		}
		st->fieldKeyRig = keyRig;
		st->fieldFrame = sceneFrame;
		// このオブジェクトの格子の各頂点を、グループの座標へ写して変形し、元の座標へ戻す
		static const std::vector<Handle> none;
		if (!st->deformer.deform(w, h, st->alpha.data(), none, Method::MLS, 1, st->mesh)) return true;
		MeshResult& mm = st->mesh;
		const int C = mm.cols + 1;
		std::vector<uint8_t> vActive(mm.rest.size(), 0);
		for (int j = 0; j < mm.rows; j++) {
			for (int i = 0; i < mm.cols; i++) {
				if (!mm.cellActive[(size_t)j * mm.cols + i]) continue;
				const int v00 = j * C + i;
				vActive[v00] = vActive[v00 + 1] = vActive[v00 + C] = vActive[v00 + C + 1] = 1;
			}
		}
		mm.minx = mm.miny = 1e300;
		mm.maxx = mm.maxy = -1e300;
		for (size_t k = 0; k < mm.rest.size(); k++) {
			if (!vActive[k]) continue;
			const Vec2 g = xf.apply(mm.rest[k]);
			const Vec2 g2 = st->fieldOk ? sampleTriField(st->fieldDeformer, st->field, dc, g) : g;
			mm.cur[k] = xf.inverse(g2);
			mm.minx = std::min(mm.minx, mm.cur[k].x);
			mm.maxx = std::max(mm.maxx, mm.cur[k].x);
			mm.miny = std::min(mm.miny, mm.cur[k].y);
			mm.maxy = std::max(mm.maxy, mm.cur[k].y);
		}
		const MeshResult& m = st->mesh;
		minx = m.minx;
		maxx = m.maxx;
		miny = m.miny;
		maxy = m.maxy;
		// ---- 変形した格子を三角形で描く ----
		const int C2 = m.cols + 1;
		auto put = [&](int k) {
			const Vec2& c = m.cur[k];
			const Vec2& r = m.rest[k];
			vt.push_back({ (float)c.x, (float)c.y, 0.0f, (float)((r.x + w * 0.5) / w), (float)((r.y + h * 0.5) / h), 1.0f });
		};
		for (int j = 0; j < m.rows; j++) {
			for (int i = 0; i < m.cols; i++) {
				if (!m.cellActive[(size_t)j * m.cols + i]) continue;
				const int v00 = j * C2 + i, v10 = v00 + 1, v01 = v00 + C2, v11 = v01 + 1;
				put(v00);
				put(v10);
				put(v11);
				put(v00);
				put(v11);
				put(v01);
			}
		}
	}

	// ---- 出力サイズ (中心を動かさないよう上下左右対称に広げる) ----
	int W2 = w, H2 = h;
	if (!video->object->is_filter_object()) {
		const double hx = std::max({ w * 0.5, -minx, maxx });
		const double hy = std::max({ h * 0.5, -miny, maxy });
		W2 = w + 2 * (int)std::ceil(hx - w * 0.5 - 1e-6);
		H2 = h + 2 * (int)std::ceil(hy - h * 0.5 - 1e-6);
		if (W2 > kMaxOutputSide) W2 = w + 2 * std::max(0, (kMaxOutputSide - w) / 2);
		if (H2 > kMaxOutputSide) H2 = h + 2 * std::max(0, (kMaxOutputSide - h) / 2);
	}
	if (vt.empty()) return true;

	const std::wstring src = L"resource:puppet_src_" + std::to_wstring(video->object->effect_id) + L"_" + std::to_wstring(video->object->id);
	if (!video->copy_image_resource(src.c_str(), L"object")) {
		logWarn(L"[パペット変形] 画像のコピーに失敗しました");
		return true;
	}
	if (W2 != w || H2 != h) video->set_image_data(nullptr, W2, H2);
	video->clear_image_resource(L"object", PIXEL_RGBA{ 0, 0, 0, 0 });
	video->set_sampler_mode(SAMPLER_MODE::CLIP);
	if (!video->draw_poly_to_resource(L"object", VERTEX_TYPE::TRIANGLE_TEXTURE, vt.data(), (int)vt.size(), src.c_str())) {
		logWarn(L"[パペット変形] 変形の描画に失敗しました");
	}
	video->release_image_resource(src.c_str());
	return true;
}

}  // namespace

FILTER_PLUGIN_TABLE* filterTable() { return &g_table; }

}  // namespace pd
