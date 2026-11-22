// パペット変形 for AviUtl2 - データモデル
#include "model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace pd {

Doc Doc::makeDefault() {
	Doc d;
	d.normalize();
	return d;
}

void Doc::normalize() {
	if (pins.size() > (size_t)kMaxPins) pins.resize(kMaxPins);
	if (bones.size() > (size_t)kMaxBones) bones.resize(kMaxBones);
	if (cuts.size() > (size_t)kMaxCuts) cuts.resize(kMaxCuts);
	cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [](const Cut& c) { return c.points.size() < 2; }), cuts.end());
	for (auto& c : cuts) {
		if (c.points.size() > (size_t)kMaxCutPoints) c.points.resize(kMaxCutPoints);
	}
	if (poses.empty()) poses.push_back(Pose{});
	for (auto& p : poses) {
		p.pins.resize(pins.size());
		p.bones.resize(bones.size());
	}
	sanitize();
}

void Doc::sanitize() {
	const int nb = (int)bones.size();
	for (auto& b : bones) {
		if (b.parent < -1 || b.parent >= nb) b.parent = -1;
	}
	// 循環している場合はその場で切る
	for (int i = 0; i < nb; i++) {
		int cur = bones[i].parent;
		int steps = 0;
		while (cur >= 0 && steps <= nb) {
			if (cur == i) {
				bones[i].parent = -1;
				break;
			}
			cur = bones[cur].parent;
			steps++;
		}
		if (steps > nb) bones[i].parent = -1;
	}
	for (auto& p : pins) {
		if (p.bone < -1 || p.bone >= nb) p.bone = -1;
	}
}

// ---------------------------------------------------------------------------
namespace {

constexpr uint8_t kMagic[4] = { 'P', 'D', 'F', '2' };
constexpr uint16_t kVersion = 3;  // 2 : ポーズ補間の選択を追加 / 3 : 揺れ位相をボーンの共通値に移動
// v0.4.0 : 番号は 3 のまま、末尾に切れ込みの塊を足す (v0.3.2 は末尾の余分なデータを無視するので、ピン・ボーンは読める)
constexpr uint8_t kCutTag[4] = { 'C', 'U', 'T', '1' };

struct Writer {
	std::vector<uint8_t> buf;
	template <class T> void put(T v) {
		const size_t n = buf.size();
		buf.resize(n + sizeof(T));
		std::memcpy(buf.data() + n, &v, sizeof(T));
	}
};

struct Reader {
	const uint8_t* p;
	size_t n;
	size_t pos = 0;
	bool ok = true;
	template <class T> T get() {
		T v{};
		if (pos + sizeof(T) > n) {
			ok = false;
			return v;
		}
		std::memcpy(&v, p + pos, sizeof(T));
		pos += sizeof(T);
		return v;
	}
};

float finite(float v, float def) { return std::isfinite(v) ? v : def; }

}  // namespace

std::vector<uint8_t> serialize(const Doc& d) {
	Writer w;
	for (auto c : kMagic) w.put<uint8_t>(c);
	w.put<uint16_t>(kVersion);
	w.put<uint16_t>((uint16_t)d.pins.size());
	w.put<uint16_t>((uint16_t)d.bones.size());
	w.put<uint16_t>((uint16_t)d.poses.size());
	for (auto& p : d.pins) {
		w.put<float>(p.x);
		w.put<float>(p.y);
		w.put<uint8_t>(p.enabled ? 1 : 0);
		w.put<int16_t>(p.bone);
	}
	for (auto& b : d.bones) {
		w.put<float>(b.x);
		w.put<float>(b.y);
		w.put<uint8_t>(b.enabled ? 1 : 0);
		w.put<int16_t>(b.parent);
		w.put<float>(b.phase);
	}
	for (auto& ps : d.poses) {
		w.put<int32_t>(ps.frame);
		for (size_t i = 0; i < d.pins.size(); i++) {
			const PinPose pp = i < ps.pins.size() ? ps.pins[i] : PinPose{};
			w.put<float>(pp.mx);
			w.put<float>(pp.my);
			for (auto v : pp.interp) w.put<uint8_t>(v);
		}
		for (size_t i = 0; i < d.bones.size(); i++) {
			const BonePose bp = i < ps.bones.size() ? ps.bones[i] : BonePose{};
			w.put<float>(bp.angle);
			w.put<float>(bp.stretch);
			w.put<float>(bp.swing);
			w.put<float>(bp.period);
			w.put<float>(bp.propagate);
			w.put<float>(bp.shinari);
			for (auto v : bp.interp) w.put<uint8_t>(v);
		}
	}
	if (!d.cuts.empty()) {
		for (auto c : kCutTag) w.put<uint8_t>(c);
		w.put<uint16_t>((uint16_t)d.cuts.size());
		for (auto& c : d.cuts) {
			w.put<uint8_t>(c.enabled ? 1 : 0);
			w.put<uint8_t>((uint8_t)c.points.size());
			for (auto& pt : c.points) {
				w.put<float>(pt.x);
				w.put<float>(pt.y);
			}
		}
	}
	return std::move(w.buf);
}

bool deserialize(const uint8_t* data, size_t size, Doc& out) {
	if (!data || size < 12) return false;
	Reader r{ data, size };
	for (auto c : kMagic) {
		if (r.get<uint8_t>() != c) return false;
	}
	const uint16_t ver = r.get<uint16_t>();
	if (ver < 1 || ver > kVersion) return false;
	const int np = r.get<uint16_t>();
	const int nb = r.get<uint16_t>();
	const int nps = r.get<uint16_t>();
	if (!r.ok || np > kMaxPins || nb > kMaxBones || nps > 1000) return false;
	Doc d;
	d.pins.resize(np);
	d.bones.resize(nb);
	for (auto& p : d.pins) {
		p.x = finite(r.get<float>(), 0);
		p.y = finite(r.get<float>(), 0);
		p.enabled = r.get<uint8_t>() & 1;
		p.bone = r.get<int16_t>();
	}
	for (auto& b : d.bones) {
		b.x = finite(r.get<float>(), 0);
		b.y = finite(r.get<float>(), 0);
		b.enabled = r.get<uint8_t>() & 1;
		b.parent = r.get<int16_t>();
		if (ver >= 3) b.phase = finite(r.get<float>(), 0);
	}
	d.poses.resize(nps);
	bool firstPose = true;
	for (auto& ps : d.poses) {
		ps.frame = r.get<int32_t>();
		ps.pins.resize(np);
		ps.bones.resize(nb);
		for (auto& pp : ps.pins) {
			pp.mx = finite(r.get<float>(), 0);
			pp.my = finite(r.get<float>(), 0);
			if (ver >= 2) {
				for (auto& v : pp.interp) v = std::min<uint8_t>(r.get<uint8_t>(), kPoseTracks - 1);
			}
		}
		for (auto& bp : ps.bones) {
			bp.angle = finite(r.get<float>(), 0);
			bp.stretch = finite(r.get<float>(), 100);
			bp.swing = finite(r.get<float>(), 0);
			bp.period = finite(r.get<float>(), 1);
			if (ver < 3) {
				// 以前は中間点ごとの値だった揺れ位相は、開始点の値を使う
				const float ph = finite(r.get<float>(), 0);
				if (firstPose) d.bones[&bp - ps.bones.data()].phase = ph;
			}
			bp.propagate = finite(r.get<float>(), 100);
			bp.shinari = finite(r.get<float>(), 60);
			if (ver >= 3) {
				for (auto& v : bp.interp) v = std::min<uint8_t>(r.get<uint8_t>(), kPoseTracks - 1);
			} else if (ver == 2) {
				uint8_t old[7];
				for (auto& v : old) v = std::min<uint8_t>(r.get<uint8_t>(), kPoseTracks - 1);
				const int map[6] = { 0, 1, 2, 3, 5, 6 };  // 揺れ位相(4)を除く
				for (int k = 0; k < 6; k++) bp.interp[k] = old[map[k]];
			}
		}
		firstPose = false;
	}
	if (!r.ok) return false;
	// 切れ込み (v0.4.0〜)
	if (r.pos + 6 <= r.n && std::memcmp(r.p + r.pos, kCutTag, 4) == 0) {
		r.pos += 4;
		const int nc = r.get<uint16_t>();
		for (int i = 0; i < nc && r.ok; i++) {
			Cut c;
			c.enabled = r.get<uint8_t>() & 1;
			const int npt = r.get<uint8_t>();
			c.points.resize(npt);
			for (auto& pt : c.points) {
				pt.x = finite(r.get<float>(), 0);
				pt.y = finite(r.get<float>(), 0);
			}
			if (r.ok) d.cuts.push_back(std::move(c));
		}
	}
	d.normalize();
	out = std::move(d);
	return true;
}

// ---------------------------------------------------------------------------

void lerpPose(const Pose& a, const Pose& b, double t, size_t npins, size_t nbones, Pose& out) {
	auto L = [t](float x, float y) { return (float)(x + (y - x) * t); };
	out.frame = (int32_t)std::lround(a.frame + (b.frame - a.frame) * t);
	out.pins.resize(npins);
	out.bones.resize(nbones);
	for (size_t i = 0; i < npins; i++) {
		const PinPose pa = i < a.pins.size() ? a.pins[i] : PinPose{};
		const PinPose pb = i < b.pins.size() ? b.pins[i] : PinPose{};
		out.pins[i].mx = L(pa.mx, pb.mx);
		out.pins[i].my = L(pa.my, pb.my);
		std::copy(std::begin(pa.interp), std::end(pa.interp), out.pins[i].interp);
	}
	for (size_t i = 0; i < nbones; i++) {
		const BonePose ba = i < a.bones.size() ? a.bones[i] : BonePose{};
		const BonePose bb = i < b.bones.size() ? b.bones[i] : BonePose{};
		BonePose& o = out.bones[i];
		o.angle = L(ba.angle, bb.angle);
		o.stretch = L(ba.stretch, bb.stretch);
		o.swing = L(ba.swing, bb.swing);
		o.period = L(ba.period, bb.period);
		o.propagate = L(ba.propagate, bb.propagate);
		o.shinari = L(ba.shinari, bb.shinari);
		std::copy(std::begin(ba.interp), std::end(ba.interp), o.interp);
	}
}

void blendPose(const Pose& a, const Pose& b, const double* fracs, int nfrac, size_t npins, size_t nbones, Pose& out) {
	auto F = [&](uint8_t k) { return (k < nfrac) ? fracs[k] : (nfrac > 0 ? fracs[0] : 0.0); };
	auto L = [](float x, float y, double t) { return (float)(x + (y - x) * t); };
	out.frame = a.frame;
	out.pins.resize(npins);
	out.bones.resize(nbones);
	for (size_t i = 0; i < npins; i++) {
		const PinPose pa = i < a.pins.size() ? a.pins[i] : PinPose{};
		const PinPose pb = i < b.pins.size() ? b.pins[i] : PinPose{};
		PinPose& o = out.pins[i];
		o = pa;
		o.mx = L(pa.mx, pb.mx, F(pa.interp[0]));
		o.my = L(pa.my, pb.my, F(pa.interp[1]));
	}
	for (size_t i = 0; i < nbones; i++) {
		const BonePose ba = i < a.bones.size() ? a.bones[i] : BonePose{};
		const BonePose bb = i < b.bones.size() ? b.bones[i] : BonePose{};
		BonePose& o = out.bones[i];
		o = ba;
		o.angle = L(ba.angle, bb.angle, F(ba.interp[0]));
		o.stretch = L(ba.stretch, bb.stretch, F(ba.interp[1]));
		o.swing = L(ba.swing, bb.swing, F(ba.interp[2]));
		o.period = L(ba.period, bb.period, F(ba.interp[3]));
		o.propagate = L(ba.propagate, bb.propagate, F(ba.interp[4]));
		o.shinari = L(ba.shinari, bb.shinari, F(ba.interp[5]));
	}
}

std::vector<Pose> mapPoses(const Doc& doc, const std::vector<int>& pointFrames) {
	const size_t np = doc.pins.size(), nb = doc.bones.size();
	std::vector<Pose> out(pointFrames.size());
	if (pointFrames.empty()) return out;

	std::vector<Pose> src = doc.poses;
	if (src.empty()) src.push_back(Pose{});

	if (src.size() == pointFrames.size()) {
		for (size_t i = 0; i < src.size(); i++) {
			lerpPose(src[i], src[i], 0, np, nb, out[i]);
			out[i].frame = pointFrames[i];
		}
		return out;
	}

	// フレーム順に並べたうえで、各点のフレームでの値を求める
	std::stable_sort(src.begin(), src.end(), [](const Pose& a, const Pose& b) { return a.frame < b.frame; });
	for (size_t i = 0; i < pointFrames.size(); i++) {
		const int f = pointFrames[i];
		if (f <= src.front().frame) {
			lerpPose(src.front(), src.front(), 0, np, nb, out[i]);
		} else if (f >= src.back().frame) {
			lerpPose(src.back(), src.back(), 0, np, nb, out[i]);
		} else {
			size_t k = 0;
			while (k + 1 < src.size() && src[k + 1].frame <= f) k++;
			const Pose& a = src[k];
			const Pose& b = src[std::min(k + 1, src.size() - 1)];
			const double span = (double)b.frame - a.frame;
			const double t = span > 0 ? (f - a.frame) / span : 0.0;
			lerpPose(a, b, t, np, nb, out[i]);
		}
		out[i].frame = f;
	}
	return out;
}

}  // namespace pd
