// パペット変形 for AviUtl2 - 本体(AviUtl2)とのやり取り
#include "host.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace pd {

const wchar_t* const kEffectName = L"パペット変形";
const wchar_t* const kItemMethod = L"変形方式";
const wchar_t* const kItemStiffness = L"剛性";
const wchar_t* const kItemPreviewPrecision = L"プレビュー精度";
const wchar_t* const kItemExportPrecision = L"書き出し精度";
const wchar_t* const kItemPoseCount = L"ポーズ補間の数";
const wchar_t* const kItemPoseTracks[kPoseTracks] = {
	L"ポーズ補間1",  L"ポーズ補間2",  L"ポーズ補間3",  L"ポーズ補間4",  L"ポーズ補間5",  L"ポーズ補間6",
	L"ポーズ補間7",  L"ポーズ補間8",  L"ポーズ補間9",  L"ポーズ補間10", L"ポーズ補間11", L"ポーズ補間12",
	L"ポーズ補間13", L"ポーズ補間14", L"ポーズ補間15", L"ポーズ補間16",
};
const wchar_t* const kItemButton = L"設定ウィンドウを開く";
const wchar_t* const kDataNames[kChunkCount] = { L"PuppetData0", L"PuppetData1", L"PuppetData2", L"PuppetData3",
												 L"PuppetData4", L"PuppetData5", L"PuppetData6", L"PuppetData7" };

EDIT_HANDLE* g_edit = nullptr;
LOG_HANDLE* g_logger = nullptr;
HINSTANCE g_hinst = nullptr;
std::atomic<int> g_editFrame{ -1 };

HWND g_msgWnd = nullptr;

static std::mutex g_captureMutex;
static std::map<int64_t, std::shared_ptr<CaptureSlot>> g_captures;

void setCaptureSlot(int64_t effectId, std::shared_ptr<CaptureSlot> slot) {
	std::lock_guard<std::mutex> lk(g_captureMutex);
	if (slot) {
		g_captures[effectId] = std::move(slot);
	} else {
		g_captures.erase(effectId);
	}
}

std::shared_ptr<CaptureSlot> getCaptureSlotByObject(int64_t objectId) {
	std::lock_guard<std::mutex> lk(g_captureMutex);
	for (auto& kv : g_captures) {
		if (kv.second && kv.second->objectId == objectId) return kv.second;
	}
	return nullptr;
}

std::shared_ptr<CaptureSlot> getCaptureSlot(int64_t effectId) {
	std::lock_guard<std::mutex> lk(g_captureMutex);
	auto it = g_captures.find(effectId);
	return it == g_captures.end() ? nullptr : it->second;
}

void logInfo(const std::wstring& s) {
	if (g_logger) g_logger->info(g_logger, s.c_str());
}
void logWarn(const std::wstring& s) {
	if (g_logger) g_logger->warn(g_logger, s.c_str());
}

static bool matchEffect(EDIT_SECTION* e, OBJECT_HANDLE o, int64_t effectId, EFFECT_HANDLE& eff) {
	const int n = e->get_effect_list(o, nullptr, 0);
	if (n <= 0) return false;
	std::vector<EFFECT_HANDLE> list(n);
	const int got = e->get_effect_list(o, list.data(), n);
	for (int i = 0; i < got; i++) {
		if (e->get_effect_id(list[i]) == effectId) {
			eff = list[i];
			return true;
		}
	}
	return false;
}

bool findTarget(EDIT_SECTION* e, const TargetRef& t, int layerMax, OBJECT_HANDLE& obj, EFFECT_HANDLE& eff) {
	static int lastLayer = -1;
	auto scanLayer = [&](int layer) -> bool {
		int frame = 0;
		for (int guard = 0; guard < 100000; guard++) {
			OBJECT_HANDLE o = e->find_object(layer, frame);
			if (!o) return false;
			if (e->get_object_id(o) == t.objectId) {
				if (matchEffect(e, o, t.effectId, eff)) {
					obj = o;
					lastLayer = layer;
					return true;
				}
				return false;
			}
			const OBJECT_LAYER_FRAME lf = e->get_object_layer_frame(o);
			if (lf.end < frame) return false;
			frame = lf.end + 1;
		}
		return false;
	};
	if (lastLayer >= 0 && scanLayer(lastLayer)) return true;
	for (int layer = 0; layer <= std::max(layerMax, 0); layer++) {
		if (layer != lastLayer && scanLayer(layer)) return true;
	}
	return false;
}

bool docFromChunks(const std::vector<std::pair<const void*, int>>& chunks, Doc& doc) {
	std::vector<uint8_t> blob;
	for (auto& c : chunks) {
		if (c.first && c.second > 0) {
			const auto* p = (const uint8_t*)c.first;
			blob.insert(blob.end(), p, p + c.second);
		}
	}
	return deserialize(blob.data(), blob.size(), doc);
}

bool readDoc(EDIT_SECTION* e, EFFECT_HANDLE eff, Doc& doc) {
	std::vector<std::vector<uint8_t>> bufs(kChunkCount);
	std::vector<std::pair<const void*, int>> chunks;
	for (int i = 0; i < kChunkCount; i++) {
		const int size = e->get_effect_data_value(eff, kDataNames[i], nullptr, 0);
		if (size > 0) {
			bufs[i].resize(size);
			const int got = e->get_effect_data_value(eff, kDataNames[i], bufs[i].data(), size);
			chunks.push_back({ bufs[i].data(), got });
		}
	}
	return docFromChunks(chunks, doc);
}

bool writeDoc(EDIT_SECTION* e, EFFECT_HANDLE eff, const Doc& doc) {
	std::vector<uint8_t> blob = serialize(doc);
	if (blob.size() > kMaxBlob) return false;
	bool ok = true;
	uint8_t dummy = 0;
	for (int i = 0; i < kChunkCount; i++) {
		const size_t off = (size_t)i * kChunkSize;
		const int n = off < blob.size() ? (int)std::min<size_t>(kChunkSize, blob.size() - off) : 0;
		if (n > 0) {
			ok &= e->set_effect_data_value(eff, kDataNames[i], blob.data() + off, n);
		} else if (e->get_effect_data_value(eff, kDataNames[i], nullptr, 0) > 0) {
			e->set_effect_data_value(eff, kDataNames[i], &dummy, 0);
		}
	}
	return ok;
}

bool getSectionInfo(EDIT_SECTION* e, OBJECT_HANDLE obj, EFFECT_HANDLE eff, int sceneFrame, const double* trackNow, SectionInfo& out) {
	if (!e || !obj) return false;
	const OBJECT_LAYER_FRAME lf = e->get_object_layer_frame(obj);
	out.layer = lf.layer;
	out.frameStart = lf.start;
	const int endRel = std::max(0, lf.end - lf.start);
	int num = std::max(1, e->get_object_section_num(obj));
	std::vector<int> f(num, 0);
	for (int i = 0; i < num; i++) f[i] = e->get_object_section_frame(obj, i);
	// 区間の開始フレームがシーン基準か、オブジェクト基準かを判別する
	const bool sceneBased = (f[0] == lf.start);
	out.pointFrames.clear();
	for (int i = 0; i < num; i++) {
		int r = f[i] < 0 ? 0 : (sceneBased ? f[i] - lf.start : f[i]);
		r = std::clamp(r, 0, endRel);
		if (!out.pointFrames.empty()) r = std::max(r, out.pointFrames.back());
		out.pointFrames.push_back(r);
	}
	out.pointFrames.push_back(std::max(endRel, out.pointFrames.back()));

	const int cur = std::clamp(sceneFrame - lf.start, 0, endRel);
	out.relFrame = cur;
	int k = 0;
	for (int i = 0; i < num; i++) {
		if (out.pointFrames[i] <= cur) k = i;
	}
	out.section = k;
	const int fa = out.pointFrames[k], fb = out.pointFrames[k + 1];
	const double timeFrac = fb > fa ? (double)(cur - fa) / (fb - fa) : 0.0;
	for (int t = 0; t < kPoseTracks; t++) out.fracs[t] = timeFrac;

	if (trackNow && eff) fillTrackFracs(e, eff, trackNow, 0xFFFFFFFFu, out);
	return true;
}

void fillTrackFracs(EDIT_SECTION* e, EFFECT_HANDLE eff, const double* trackNow, uint32_t trackMask, SectionInfo& out) {
	if (!e || !eff || !trackNow || out.pointFrames.size() < 2) return;
	const int k = std::clamp(out.section, 0, (int)out.pointFrames.size() - 2);
	const int fa = out.pointFrames[k], fb = out.pointFrames[k + 1];
	const int cur = out.relFrame;
	const int start = out.frameStart;
	int base = -1;  // get_effect_track_value のフレーム指定の基準 (判別できたら start か 0)
	for (int t = 0; t < kPoseTracks; t++) {
		if (!(trackMask & (1u << t))) continue;
		const wchar_t* item = kItemPoseTracks[t];
		if (base < 0) {
			// シーン基準かオブジェクト基準かを、現在値と比べて判別する
			double vScene = NAN, vRel = NAN;
			const bool mS = e->get_effect_track_value(eff, item, (double)(start + cur), &vScene) && std::fabs(vScene - trackNow[t]) < 1e-6;
			const bool mR = e->get_effect_track_value(eff, item, (double)cur, &vRel) && std::fabs(vRel - trackNow[t]) < 1e-6;
			if (!mS && !mR) continue;
			if (mS != mR) base = mS ? start : 0;
			// 両方一致 (値が一定など) の場合は、この補間では判別しない
			const int b0 = mS ? start : 0;
			double a = NAN, b = NAN;
			if (e->get_effect_track_value(eff, item, (double)(b0 + fa), &a) && e->get_effect_track_value(eff, item, (double)(b0 + fb), &b) &&
				std::fabs(b - a) > 1e-9) {
				out.fracs[t] = (trackNow[t] - a) / (b - a);
			}
		} else {
			double a = NAN, b = NAN;
			if (e->get_effect_track_value(eff, item, (double)(base + fa), &a) && e->get_effect_track_value(eff, item, (double)(base + fb), &b) &&
				std::fabs(b - a) > 1e-9) {
				out.fracs[t] = (trackNow[t] - a) / (b - a);
			}
		}
	}
}

int getPoseTrackCount(EDIT_SECTION* e, EFFECT_HANDLE eff) {
	LPCSTR v = e ? e->get_effect_item_value(eff, kItemPoseCount) : nullptr;
	if (!v) return 1;
	const int n = std::atoi(v);
	return std::clamp(n, 1, kPoseTracks);
}

static void renumberOne(EDIT_SECTION* e, EFFECT_HANDLE eff, const wchar_t* item, int pointCount) {
	LPCSTR cur = e->get_effect_item_value(eff, item);
	if (!cur) return;
	std::string s(cur);
	std::vector<std::string> tok;
	size_t p = 0;
	while (true) {
		const size_t q = s.find(',', p);
		tok.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
		if (q == std::string::npos) break;
		p = q + 1;
	}
	std::vector<double> vals;
	size_t i = 0;
	for (; i < tok.size(); i++) {
		char* end = nullptr;
		const double v = std::strtod(tok[i].c_str(), &end);
		if (tok[i].empty() || !end || *end) break;
		vals.push_back(v);
	}
	// 移動方法が設定されていない(値が1つだけ)場合や、点の数が合わない場合は何もしない
	if ((int)vals.size() != pointCount || i >= tok.size()) return;
	bool needs = false;
	for (size_t k = 0; k + 1 < vals.size(); k++) {
		if (std::fabs(vals[k + 1] - vals[k]) < 1e-9) needs = true;
	}
	if (!needs) return;
	std::string out;
	char buf[32];
	for (int k = 0; k < pointCount; k++) {
		std::snprintf(buf, sizeof buf, "%d.00", k);
		out += buf;
		out += ',';
	}
	for (size_t k = i; k < tok.size(); k++) {
		out += tok[k];
		if (k + 1 < tok.size()) out += ',';
	}
	e->set_effect_item_value(eff, item, out.c_str());
}

void renumberPoseTrack(EDIT_SECTION* e, EFFECT_HANDLE eff, int pointCount) {
	if (pointCount < 2) return;
	for (int t = 0; t < kPoseTracks; t++) renumberOne(e, eff, kItemPoseTracks[t], pointCount);
}

int effectOrdinal(EDIT_SECTION* e, OBJECT_HANDLE obj, EFFECT_HANDLE eff) {
	const int n = e->get_effect_list(obj, nullptr, 0);
	if (n <= 0) return 1;
	std::vector<EFFECT_HANDLE> list(n);
	const int got = e->get_effect_list(obj, list.data(), n);
	int ord = 0;
	for (int i = 0; i < got; i++) {
		LPCWSTR name = e->get_effect_name(list[i]);
		if (name && std::wcscmp(name, kEffectName) == 0) ord++;
		if (list[i] == eff) return std::max(ord, 1);
	}
	return 1;
}

}  // namespace pd
