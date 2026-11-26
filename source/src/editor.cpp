// パペット変形 for AviUtl2 - 専用設定ウィンドウ
#include "editor.h"

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "deform.h"
#include "mesh.h"

namespace pd {

namespace G = Gdiplus;
void updateKeyboardHook();

namespace {

const wchar_t* const kClassName = L"PuppetDeformationEditorWindow";

// ---- 色 ----
const G::Color kBg(255, 32, 32, 32);
const G::Color kPanel(255, 38, 38, 38);
const G::Color kTab(255, 52, 52, 52);
const G::Color kTabSel(255, 58, 82, 106);
const G::Color kTabSelLine(255, 86, 156, 214);
const G::Color kBorder(255, 72, 72, 72);
const G::Color kText(255, 228, 228, 228);
const G::Color kTextDim(255, 150, 150, 150);
const G::Color kBar(255, 74, 74, 74);
const G::Color kBarSide(255, 60, 60, 60);
const G::Color kBtn(255, 64, 64, 64);
const G::Color kBtnDisabled(255, 48, 48, 48);
const G::Color kPin(255, 52, 132, 246);
const G::Color kOrange(255, 245, 158, 11);
const G::Color kWhite(255, 255, 255, 255);
const G::Color kChk1(255, 60, 60, 60);
const G::Color kChk2(255, 48, 48, 48);
const G::Color kToggleOn(255, 52, 132, 246);
const G::Color kToggleOff(255, 90, 90, 90);
const G::Color kScroll(160, 140, 140, 140);
const G::Color kCut(255, 239, 68, 68);
const G::Color kContour(255, 34, 197, 94);

// ---- 設定項目 ----
enum Field {
	F_PinEnabled,
	F_PinX,
	F_PinY,
	F_MoveX,
	F_MoveY,
	F_BoneEnabled,
	F_BoneX,
	F_BoneY,
	F_Angle,
	F_Stretch,
	F_Swing,
	F_Period,
	F_Phase,
	F_Prop,
	F_Shinari,
	F_CutEnabled,
	F_CutX,
	F_CutY,
	F_Count
};

struct FieldMeta {
	const wchar_t* label;
	const wchar_t* unit;
	double min, max;
	int decimals;
	double dragRate;  // ドラッグ1pxあたりの変化量
	double step;      // −/+ ボタンの変化量
	bool perPose;     // 中間点ごとの値か
	bool toggle;
};

const FieldMeta kFields[F_Count] = {
	{ L"有効", L"", 0, 1, 0, 0, 0, false, true },
	{ L"基準X", L"px", -100000, 100000, 1, 1.0, 1.0, false, false },
	{ L"基準Y", L"px", -100000, 100000, 1, 1.0, 1.0, false, false },
	{ L"移動X", L"px", -100000, 100000, 1, 1.0, 1.0, true, false },
	{ L"移動Y", L"px", -100000, 100000, 1, 1.0, 1.0, true, false },
	{ L"有効", L"", 0, 1, 0, 0, 0, false, true },
	{ L"ジョイントX", L"px", -100000, 100000, 1, 1.0, 1.0, false, false },
	{ L"ジョイントY", L"px", -100000, 100000, 1, 1.0, 1.0, false, false },
	{ L"角度", L"°", -3600, 3600, 1, 0.5, 1.0, true, false },
	{ L"伸縮", L"%", 0, 1000, 1, 0.5, 1.0, true, false },
	{ L"揺れ角度", L"°", 0, 360, 1, 0.5, 1.0, true, false },
	{ L"揺れ周期", L"秒", 0.01, 60, 2, 0.01, 0.05, true, false },
	{ L"揺れ位相", L"°", -360, 360, 0, 1.0, 1.0, false, false },
	{ L"揺れ伝播", L"%", 0, 200, 0, 1.0, 1.0, true, false },
	{ L"しなり", L"°", -180, 180, 0, 1.0, 1.0, true, false },
	{ L"有効", L"", 0, 1, 0, 0, 0, false, true },
	{ L"X", L"px", -100000, 100000, 1, 1.0, 1.0, false, false },
	{ L"Y", L"px", -100000, 100000, 1, 1.0, 1.0, false, false },
};

enum ButtonId { B_AddPin = 1, B_DelPin, B_ResetPins, B_AddBone, B_DelBone };

enum class WType { Button, Toggle, Number, Label, Header, Group, Interp };

struct Widget {
	WType type;
	RECT rc{};
	int id = 0;
	Field field = F_Count;
	int side = -1;  // -1 共通値 / 0 左の点 / 1 右の点
	std::wstring text;
	bool enabled = true;
	RECT rcMinus{}, rcPlus{}, rcValue{};
};

inline bool inRect(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline G::RectF toRectF(const RECT& r) { return G::RectF((G::REAL)r.left, (G::REAL)r.top, (G::REAL)(r.right - r.left), (G::REAL)(r.bottom - r.top)); }

double dpiScale(HWND hwnd) {
	using Fn = UINT(WINAPI*)(HWND);
	static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	const UINT dpi = fn && hwnd ? fn(hwnd) : 96;
	return dpi ? dpi / 96.0 : 1.0;
}

void addRoundRect(G::GraphicsPath& path, const G::RectF& r, float rad) {
	const float d = rad * 2;
	if (d <= 0.5f) {
		path.AddRectangle(r);
		return;
	}
	path.AddArc(r.X, r.Y, d, d, 180, 90);
	path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
	path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
	path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
	path.CloseFigure();
}

void fillRound(G::Graphics& g, const RECT& rc, float rad, const G::Color& c) {
	G::GraphicsPath p;
	addRoundRect(p, toRectF(rc), rad);
	G::SolidBrush b(c);
	g.FillPath(&b, &p);
}

std::wstring formatValue(Field f, double v) {
	const FieldMeta& m = kFields[f];
	wchar_t buf[64];
	std::swprintf(buf, 64, L"%.*f %ls", m.decimals, v, m.unit);
	return buf;
}

class Editor;
std::map<int64_t, Editor*> g_editors;

// ===========================================================================
class Editor {
public:
	explicit Editor(const TargetRef& t) : target_(t) {}

	bool create();
	HWND hwnd() const { return hwnd_; }
	void reload();

	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

	// データ
	bool commit();  // 戻り値 : 本体に保存できたら true
public:
	void undo();
	void redo();
	HWND editControl() const { return edit_; }
private:
	void requestImage();
	void takeImage();
	Pose& pose(int side);
	double getField(Field f, int side);
	void setField(Field f, int side, double v);
	void addPin(double x, double y);
	void deletePin(int i);
	void addBone(double x, double y, int parent);
	void insertBone(int child, double x, double y);
	void deleteBone(int b);

	// 表示
	void layout();
	void paint(HDC hdc);
	void paintCanvas(G::Graphics& g);
	void paintPanel(G::Graphics& g);
	void fitView();
	G::PointF toScreen(double x, double y) const;
	void toImage(int sx, int sy, double& x, double& y) const;
	// 表示する画像全体の範囲 (ピン・ボーンの座標)
	double conW() const { return std::max(1.0, conR_ - conL_); }
	double conH() const { return std::max(1.0, conB_ - conT_); }
	bool hScrollVisible() const;
	bool vScrollVisible() const;
	void scrollGeometry(bool horizontal, RECT& track, RECT& thumb, double& lo, double& hi) const;

	// 操作
	int hitPin(int sx, int sy) const;
	int hitBone(int sx, int sy) const;
	int hitSegment(int sx, int sy, double& ix, double& iy) const;
	bool hitCutPoint(int sx, int sy, int& cut, int& pt) const;
	bool hitCutSegment(int sx, int sy, int& cut, int& seg, double& ix, double& iy) const;
	bool cutEndOpen(const CutPoint& p) const;  // 輪郭の外にある端か
	bool cutSelected() const { return selCut_ >= 0 && selCut_ < (int)doc_.cuts.size() && selCutPt_ >= 0 && selCutPt_ < (int)doc_.cuts[selCut_].points.size(); }
	void addCut();
	void deleteCutPoint();
	void onLButtonDown(int x, int y, WPARAM keys);
	void onMouseMove(int x, int y, WPARAM keys);
	void onLButtonUp(int x, int y);
	void onButton(int id);
	void beginTextEdit(size_t widgetIndex);
	void endTextEdit(bool apply);
	void zoomAt(int sx, int sy, double factor);

	TargetRef target_;
	HWND hwnd_ = nullptr;
	HWND edit_ = nullptr;
	WNDPROC editOrigProc_ = nullptr;
	std::shared_ptr<CaptureSlot> capture_;

	Doc doc_;
	Doc committed_;                 // 本体に保存済みの状態
	bool hasCommitted_ = false;
	// 元に戻す (Ctrl+Z) とやり直し (Ctrl+Y) の記録。各要素は1回の操作の前 (やり直しは後) の状態
	static constexpr size_t kMaxUndo = 100;
	std::vector<Doc> undoStack_, redoStack_;
	bool restoreDoc(const Doc& src);
	bool undoing_ = false;
	SectionInfo sec_;
	bool found_ = false;
	bool loaded_ = false;
	std::wstring status_;

	int tab_ = 0;  // 0 = ピン, 1 = ボーン
	int selPin_ = -1;
	int selBone_ = -1;
	int selCut_ = -1;    // 選択中の切れ込み (両方のタブで共通。選んでいる間は値の欄が切れ込みになる)
	int selCutPt_ = -1;  // 選択中の点
	bool hoverCut_ = false;  // 切れ込みの線にカーソルがある (点が増える位置を表示)
	double hoverX_ = 0, hoverY_ = 0;
	bool group_ = false;     // グループ制御に付けたエフェクト
	Contour contour_;        // 変形前の画像の輪郭 (グループ制御・複数の画像では重ねた画像の輪郭)
	int poseCount_ = 1;  // 設定画面の「ポーズ補間の数」
	uint8_t* interpRef(Field f);  // 現在の区間で、その項目が使うポーズ補間の番号
	void chooseInterp(size_t widgetIndex);

	// 変形前の画像 (グループ制御の場合は配下のオブジェクトごと)
	struct ViewImage {
		std::unique_ptr<G::Bitmap> bmp;
		int w = 0, h = 0;
		Xform2D xf;
		int layer = 0;
	};
	std::vector<ViewImage> images_;
	uint64_t imageSerial_ = 0;
	double conL_ = -320, conT_ = -180, conR_ = 320, conB_ = 180;
	bool needFit_ = true;

	double zoom_ = 1.0, viewX_ = 0, viewY_ = 0;
	double s_ = 1.0;  // DPI倍率

	RECT rcClient_{}, rcTabs_{}, rcTabPin_{}, rcTabBone_{}, rcCanvas_{}, rcPanel_{}, rcFit_{}, rcRefresh_{}, rcCut_{};
	std::vector<Widget> widgets_;

	enum class Drag { None, Pin, Bone, CutPt, Pan, Number, HScroll, VScroll } drag_ = Drag::None;
	int dragIndex_ = -1;
	size_t dragWidget_ = 0;
	double dragStartValue_ = 0;
	int dragStartX_ = 0, dragStartY_ = 0;
	double dragOffX_ = 0, dragOffY_ = 0;
	double dragViewX_ = 0, dragViewY_ = 0;
	bool dragMoved_ = false;
	bool pendingReload_ = false;
	size_t editingWidget_ = (size_t)-1;

	HDC memDC_ = nullptr;
	HBITMAP memBmp_ = nullptr;
	HGDIOBJ memOld_ = nullptr;
	int memW_ = 0, memH_ = 0;
	std::unique_ptr<G::FontFamily> family_;
};

// ---------------------------------------------------------------------------
bool Editor::create() {
	HWND owner = g_edit ? g_edit->get_host_app_window() : nullptr;
	hwnd_ = CreateWindowExW(0, kClassName, kEffectName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 600,
							860, owner, nullptr, g_hinst, this);
	if (!hwnd_) return false;
	s_ = dpiScale(hwnd_);
	RECT wr;
	GetWindowRect(hwnd_, &wr);
	SetWindowPos(hwnd_, nullptr, 0, 0, (int)(560 * s_), (int)(860 * s_), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	// タイトルバーを暗い色に (Windows 10 20H1 以降)
	if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
		using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		if (auto fn = (Fn)GetProcAddress(dwm, "DwmSetWindowAttribute")) {
			BOOL on = TRUE;
			fn(hwnd_, 20, &on, sizeof(on));
		}
	}
	edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_, nullptr, g_hinst, nullptr);
	SetWindowLongPtrW(edit_, GWLP_USERDATA, (LONG_PTR)this);
	editOrigProc_ = (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC, (LONG_PTR)&Editor::editProc);

	for (auto name : { L"Yu Gothic UI", L"Meiryo UI", L"MS UI Gothic" }) {
		auto f = std::make_unique<G::FontFamily>(name);
		if (f->GetLastStatus() == G::Ok) {
			family_ = std::move(f);
			break;
		}
	}
	if (!family_) family_.reset(G::FontFamily::GenericSansSerif()->Clone());
	if (HFONT hf = CreateFontW((int)(-13 * s_), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI")) {
		SendMessageW(edit_, WM_SETFONT, (WPARAM)hf, FALSE);
	}

	capture_ = std::make_shared<CaptureSlot>();
	capture_->notify = hwnd_;
	capture_->objectId = target_.objectId;
	setCaptureSlot(target_.effectId, capture_);

	reload();
	layout();
	ShowWindow(hwnd_, SW_SHOW);
	requestImage();
	return true;
}

// ---- データの読み込み ----
void Editor::reload() {
	if (!g_edit) return;
	EDIT_INFO info{};
	g_edit->get_edit_info(&info, sizeof(info));
	g_editFrame = info.frame;
	struct Ctx {
		Editor* self;
		EDIT_INFO info;
		bool found = false;
		Doc doc;
		bool hasData = false;
		SectionInfo sec;
		int ordinal = 1;
		int poseCount = 1;
	} ctx{ this, info };
	const bool called = g_edit->call_read_section_param(&ctx, [](void* p, EDIT_SECTION* e) {
		auto* c = (Ctx*)p;
		OBJECT_HANDLE obj = nullptr;
		EFFECT_HANDLE eff = nullptr;
		if (!findTarget(e, c->self->target_, c->info.layer_max, obj, eff)) return;
		c->found = true;
		c->hasData = readDoc(e, eff, c->doc);
		getSectionInfo(e, obj, eff, c->info.frame, nullptr, c->sec);
		c->poseCount = getPoseTrackCount(e, eff);
		c->ordinal = effectOrdinal(e, obj, eff);
	});
	if (!called) return;  // 出力中など。表示はそのまま
	found_ = ctx.found;
	if (!found_) {
		status_ = L"対象のエフェクトが見つかりません（削除されたか、別のシーンにあります）";
		InvalidateRect(hwnd_, nullptr, FALSE);
		return;
	}
	status_.clear();
	Doc d = ctx.hasData ? ctx.doc : Doc::makeDefault();
	sec_ = ctx.sec;
	poseCount_ = ctx.poseCount;
	if (sec_.pointFrames.size() < 2) sec_.pointFrames = { 0, 0 };
	d.poses = mapPoses(d, sec_.pointFrames);
	d.normalize();
	doc_ = std::move(d);
	loaded_ = true;
	// 本体側で変更された(自分の保存以外)場合は、元に戻す・やり直しの記録を破棄する
	if (hasCommitted_ && serialize(doc_) != serialize(committed_)) {
		undoStack_.clear();
		redoStack_.clear();
	}
	committed_ = doc_;
	hasCommitted_ = true;
	if (selPin_ >= (int)doc_.pins.size()) selPin_ = doc_.pins.empty() ? -1 : (int)doc_.pins.size() - 1;
	if (selBone_ >= (int)doc_.bones.size()) selBone_ = -1;
	if (!cutSelected()) selCut_ = selCutPt_ = -1;
	wchar_t title[128];
	if (ctx.ordinal > 1) {
		std::swprintf(title, 128, L"%ls - レイヤー%d（%d番目）", kEffectName, sec_.layer + 1, ctx.ordinal);
	} else {
		std::swprintf(title, 128, L"%ls - レイヤー%d", kEffectName, sec_.layer + 1);
	}
	SetWindowTextW(hwnd_, title);
	layout();
	InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- データの保存 (1回の操作 = 1回の元に戻す) ----
bool Editor::commit() {
	if (!g_edit || !found_) return false;
	doc_.normalize();
	for (size_t i = 0; i < doc_.poses.size() && i < sec_.pointFrames.size(); i++) doc_.poses[i].frame = sec_.pointFrames[i];
	if (serialize(doc_).size() > kMaxBlob) {
		MessageBoxW(hwnd_, L"保存できるデータの容量を超えました。ピン・ボーン・中間点の数を減らしてください。", kEffectName, MB_OK | MB_ICONWARNING);
		reload();
		return false;
	}
	struct Ctx {
		Editor* self;
		bool ok = false;
	} ctx{ this };
	const bool called = g_edit->call_edit_section_param(&ctx, [](void* p, EDIT_SECTION* e) {
		auto* c = (Ctx*)p;
		OBJECT_HANDLE obj = nullptr;
		EFFECT_HANDLE eff = nullptr;
		if (!findTarget(e, c->self->target_, e->info ? e->info->layer_max : 100, obj, eff)) return;
		c->ok = writeDoc(e, eff, c->self->doc_);
		renumberPoseTrack(e, eff, (int)c->self->sec_.pointFrames.size());
	});
	if (!called || !ctx.ok) {
		status_ = L"変更を保存できませんでした（再生中・出力中などは編集できません）";
		reload();
		return false;
	}
	if (!undoing_ && hasCommitted_ && serialize(doc_) != serialize(committed_)) {
		undoStack_.push_back(committed_);
		if (undoStack_.size() > kMaxUndo) undoStack_.erase(undoStack_.begin());
		redoStack_.clear();
	}
	committed_ = doc_;
	hasCommitted_ = true;
	return true;
}

// 操作を1つ取り消す (ドラッグはドラッグを始める前の状態へ)。最大 kMaxUndo 回
void Editor::undo() {
	if (undoStack_.empty() || drag_ != Drag::None || !found_) return;
	if (edit_ && IsWindowVisible(edit_)) endTextEdit(false);
	const Doc before = committed_;
	if (!restoreDoc(undoStack_.back())) return;  // 保存できなければ記録はそのまま
	undoStack_.pop_back();
	redoStack_.push_back(before);
}

// 取り消した操作を1つやり直す
void Editor::redo() {
	if (redoStack_.empty() || drag_ != Drag::None || !found_) return;
	if (edit_ && IsWindowVisible(edit_)) endTextEdit(false);
	const Doc before = committed_;
	if (!restoreDoc(redoStack_.back())) return;
	redoStack_.pop_back();
	undoStack_.push_back(before);
	if (undoStack_.size() > kMaxUndo) undoStack_.erase(undoStack_.begin());
}

bool Editor::restoreDoc(const Doc& src) {
	Doc d = src;
	d.poses = mapPoses(d, sec_.pointFrames);
	d.normalize();
	doc_ = std::move(d);
	if (selPin_ >= (int)doc_.pins.size()) selPin_ = doc_.pins.empty() ? -1 : (int)doc_.pins.size() - 1;
	if (selBone_ >= (int)doc_.bones.size()) selBone_ = -1;
	if (!cutSelected()) selCut_ = selCutPt_ = -1;
	undoing_ = true;
	const bool ok = commit();
	undoing_ = false;
	layout();
	InvalidateRect(hwnd_, nullptr, FALSE);
	return ok;
}

void Editor::requestImage() {
	if (!g_edit || !capture_) return;
	EDIT_INFO info{};
	g_edit->get_edit_info(&info, sizeof(info));
	struct Ctx {
		Editor* self;
		EDIT_INFO info;
		OBJECT_HANDLE obj = nullptr;
		int frame = 0;
		bool group = false;
	} ctx{ this, info };
	g_edit->call_read_section_param(&ctx, [](void* p, EDIT_SECTION* e) {
		auto* c = (Ctx*)p;
		EFFECT_HANDLE eff = nullptr;
		if (!findTarget(e, c->self->target_, c->info.layer_max, c->obj, eff)) return;
		const OBJECT_LAYER_FRAME lf = e->get_object_layer_frame(c->obj);
		c->frame = std::clamp(c->info.frame, lf.start, lf.end);
		// グループ制御かどうか (グループ制御そのものは画像を持たないので、シーン全体を描画させる)
		const int n = e->get_effect_list(c->obj, nullptr, 0);
		std::vector<EFFECT_HANDLE> list(std::max(n, 0));
		const int got = n > 0 ? e->get_effect_list(c->obj, list.data(), n) : 0;
		for (int i = 0; i < got; i++) {
			LPCWSTR name = e->get_effect_name(list[i]);
			if (name && std::wcscmp(name, L"グループ制御") == 0) c->group = true;
		}
	});
	// オブジェクトを描画させて、フィルタ処理の中で入力画像を受け取る
	if (ctx.obj) {
		if (group_ != ctx.group) {
			group_ = ctx.group;
			InvalidateRect(hwnd_, nullptr, FALSE);
		}
		capture_->requestedFrame = ctx.frame;
		if (ctx.group) {
			g_edit->rendering_scene_video(ctx.frame, nullptr, [](void*, int, const void*, int, int, int) {});
		} else {
			g_edit->rendering_object_video(ctx.obj, ctx.frame, true, nullptr, [](void*, int, const void*, int, int, int) {});
		}
	}
}

void Editor::takeImage() {
	if (!capture_) return;
	std::vector<CapturedImage> list;
	{
		std::lock_guard<std::mutex> lk(capture_->m);
		if (capture_->serial == imageSerial_) return;
		imageSerial_ = capture_->serial;
		for (auto& kv : capture_->images) list.push_back(kv.second);
	}
	// レイヤーの小さい(奥の)ものから描く
	std::stable_sort(list.begin(), list.end(), [](const CapturedImage& a, const CapturedImage& b) { return a.layer < b.layer; });
	std::vector<ViewImage> imgs;
	double l = 1e300, t = 1e300, r = -1e300, btm = -1e300;
	for (auto& ci : list) {
		const int w = ci.w, h = ci.h;
		if (w <= 0 || h <= 0 || ci.pixels.size() < (size_t)w * h) continue;
		auto bmp = std::make_unique<G::Bitmap>(w, h, PixelFormat32bppPARGB);
		G::BitmapData bd;
		G::Rect rc(0, 0, w, h);
		if (bmp->LockBits(&rc, G::ImageLockModeWrite, PixelFormat32bppPARGB, &bd) != G::Ok) continue;
		for (int y = 0; y < h; y++) {
			auto* dst = (uint8_t*)bd.Scan0 + (size_t)y * bd.Stride;
			const PIXEL_RGBA* src = ci.pixels.data() + (size_t)y * w;
			for (int x = 0; x < w; x++) {
				const unsigned a = src[x].a;
				dst[x * 4 + 0] = (uint8_t)((src[x].b * a + 127) / 255);
				dst[x * 4 + 1] = (uint8_t)((src[x].g * a + 127) / 255);
				dst[x * 4 + 2] = (uint8_t)((src[x].r * a + 127) / 255);
				dst[x * 4 + 3] = (uint8_t)a;
			}
		}
		bmp->UnlockBits(&bd);
		ViewImage vi;
		vi.bmp = std::move(bmp);
		vi.w = w;
		vi.h = h;
		vi.xf = ci.xf;
		vi.layer = ci.layer;
		for (int k = 0; k < 4; k++) {
			const Vec2 c = ci.xf.apply({ (k & 1) ? w / 2.0 : -w / 2.0, (k & 2) ? h / 2.0 : -h / 2.0 });
			l = std::min(l, c.x);
			r = std::max(r, c.x);
			t = std::min(t, c.y);
			btm = std::max(btm, c.y);
		}
		imgs.push_back(std::move(vi));
	}
	if (imgs.empty()) return;
	// 輪郭 (切れ込みの端が輪郭の外か内かの表示に使う。描画と同じ作り方)
	contour_ = Contour{};
	if (!group_ && list.size() == 1 && list[0].xf.isIdentity()) {
		const CapturedImage& ci = list[0];
		std::vector<uint8_t> a((size_t)ci.w * ci.h);
		for (size_t i = 0; i < a.size(); i++) a[i] = ci.pixels[i].a;
		buildContour(ci.w, ci.h, a.data(), contour_);
	} else if (r > l && btm > t) {
		// グループ制御・複数の画像 : 重ねた画像の不透明な部分の輪郭 (目安。描画と同じく長い辺 1024px まで縮めて取る)
		const double W = r - l, H = btm - t;
		const double S = std::min(1.0, 1024.0 / std::max(W, H));
		const int mw = std::max(1, (int)std::ceil(W * S)), mh = std::max(1, (int)std::ceil(H * S));
		std::vector<uint8_t> a((size_t)mw * mh, 0);
		for (const CapturedImage& ci : list) {
			if (ci.w <= 0 || ci.h <= 0) continue;
			for (int y = 0; y < mh; y++) {
				for (int x = 0; x < mw; x++) {
					uint8_t& d = a[(size_t)y * mw + x];
					if (d) continue;
					const Vec2 g{ l + (x + 0.5) * W / mw, t + (y + 0.5) * H / mh };
					const Vec2 u = ci.xf.inverse(g);
					const int px = (int)std::floor(u.x + ci.w / 2.0), py = (int)std::floor(u.y + ci.h / 2.0);
					if (px < 0 || py < 0 || px >= ci.w || py >= ci.h) continue;
					if (ci.pixels[(size_t)py * ci.w + px].a) d = 255;
				}
			}
		}
		if (buildContour(mw, mh, a.data(), contour_, 1024)) {
			for (auto& loop : contour_.loops) {
				for (auto& q : loop) {
					q.x = l + (q.x + mw / 2.0) * W / mw;
					q.y = t + (q.y + mh / 2.0) * H / mh;
				}
			}
			contour_.area *= (W / mw) * (H / mh);
			contour_.src.reset();  // 座標を書き換えたので、粗い段階は作らない (表示と端の判定に使うだけ)
		}
	}
	const bool sizeChanged = std::fabs((r - l) - conW()) > 0.5 || std::fabs((btm - t) - conH()) > 0.5;
	images_ = std::move(imgs);
	conL_ = l;
	conR_ = r;
	conT_ = t;
	conB_ = btm;
	if (sizeChanged || needFit_) fitView();
	InvalidateRect(hwnd_, nullptr, FALSE);
}

Pose& Editor::pose(int side) {
	const int i = std::clamp(sec_.section + std::max(side, 0), 0, (int)doc_.poses.size() - 1);
	return doc_.poses[i];
}

double Editor::getField(Field f, int side) {
	const bool pinOk = selPin_ >= 0 && selPin_ < (int)doc_.pins.size();
	const bool boneOk = selBone_ >= 0 && selBone_ < (int)doc_.bones.size();
	switch (f) {
		case F_PinEnabled: return pinOk ? doc_.pins[selPin_].enabled : 0;
		case F_PinX: return pinOk ? doc_.pins[selPin_].x : 0;
		case F_PinY: return pinOk ? doc_.pins[selPin_].y : 0;
		case F_MoveX: return pinOk ? pose(side).pins[selPin_].mx : 0;
		case F_MoveY: return pinOk ? pose(side).pins[selPin_].my : 0;
		case F_BoneEnabled: return boneOk ? doc_.bones[selBone_].enabled : 0;
		case F_BoneX: return boneOk ? doc_.bones[selBone_].x : 0;
		case F_BoneY: return boneOk ? doc_.bones[selBone_].y : 0;
		case F_Angle: return boneOk ? pose(side).bones[selBone_].angle : 0;
		case F_Stretch: return boneOk ? pose(side).bones[selBone_].stretch : 0;
		case F_Swing: return boneOk ? pose(side).bones[selBone_].swing : 0;
		case F_Period: return boneOk ? pose(side).bones[selBone_].period : 0;
		case F_Phase: return boneOk ? doc_.bones[selBone_].phase : 0;
		case F_Prop: return boneOk ? pose(side).bones[selBone_].propagate : 0;
		case F_Shinari: return boneOk ? pose(side).bones[selBone_].shinari : 0;
		case F_CutEnabled: return cutSelected() ? doc_.cuts[selCut_].enabled : 0;
		case F_CutX: return cutSelected() ? doc_.cuts[selCut_].points[selCutPt_].x : 0;
		case F_CutY: return cutSelected() ? doc_.cuts[selCut_].points[selCutPt_].y : 0;
		default: return 0;
	}
}

void Editor::setField(Field f, int side, double v) {
	const FieldMeta& m = kFields[f];
	v = std::clamp(v, m.min, m.max);
	const double q = std::pow(10.0, m.decimals);
	v = std::round(v * q) / q;
	const bool pinOk = selPin_ >= 0 && selPin_ < (int)doc_.pins.size();
	const bool boneOk = selBone_ >= 0 && selBone_ < (int)doc_.bones.size();
	const float fv = (float)v;
	switch (f) {
		case F_PinEnabled: if (pinOk) doc_.pins[selPin_].enabled = v != 0; break;
		case F_PinX: if (pinOk) doc_.pins[selPin_].x = fv; break;
		case F_PinY: if (pinOk) doc_.pins[selPin_].y = fv; break;
		case F_MoveX: if (pinOk) pose(side).pins[selPin_].mx = fv; break;
		case F_MoveY: if (pinOk) pose(side).pins[selPin_].my = fv; break;
		case F_BoneEnabled: if (boneOk) doc_.bones[selBone_].enabled = v != 0; break;
		case F_BoneX: if (boneOk) doc_.bones[selBone_].x = fv; break;
		case F_BoneY: if (boneOk) doc_.bones[selBone_].y = fv; break;
		case F_Angle: if (boneOk) pose(side).bones[selBone_].angle = fv; break;
		case F_Stretch: if (boneOk) pose(side).bones[selBone_].stretch = fv; break;
		case F_Swing: if (boneOk) pose(side).bones[selBone_].swing = fv; break;
		case F_Period: if (boneOk) pose(side).bones[selBone_].period = fv; break;
		case F_Phase: if (boneOk) doc_.bones[selBone_].phase = fv; break;
		case F_Prop: if (boneOk) pose(side).bones[selBone_].propagate = fv; break;
		case F_Shinari: if (boneOk) pose(side).bones[selBone_].shinari = fv; break;
		case F_CutEnabled: if (cutSelected()) doc_.cuts[selCut_].enabled = v != 0; break;
		case F_CutX: if (cutSelected()) doc_.cuts[selCut_].points[selCutPt_].x = fv; break;
		case F_CutY: if (cutSelected()) doc_.cuts[selCut_].points[selCutPt_].y = fv; break;
		default: break;
	}
}

uint8_t* Editor::interpRef(Field f) {
	Pose& p = pose(0);  // 区間の左の点が、その区間の補間の選択を持つ
	const bool pinOk = selPin_ >= 0 && selPin_ < (int)p.pins.size();
	const bool boneOk = selBone_ >= 0 && selBone_ < (int)p.bones.size();
	switch (f) {
		case F_MoveX: return pinOk ? &p.pins[selPin_].interp[0] : nullptr;
		case F_MoveY: return pinOk ? &p.pins[selPin_].interp[1] : nullptr;
		case F_Angle: return boneOk ? &p.bones[selBone_].interp[0] : nullptr;
		case F_Stretch: return boneOk ? &p.bones[selBone_].interp[1] : nullptr;
		case F_Swing: return boneOk ? &p.bones[selBone_].interp[2] : nullptr;
		case F_Period: return boneOk ? &p.bones[selBone_].interp[3] : nullptr;
		case F_Prop: return boneOk ? &p.bones[selBone_].interp[4] : nullptr;
		case F_Shinari: return boneOk ? &p.bones[selBone_].interp[5] : nullptr;
		default: return nullptr;
	}
}

// 補間の選択メニュー (設定画面の「ポーズ補間の数」までを表示)
void Editor::chooseInterp(size_t wi) {
	if (wi >= widgets_.size()) return;
	const Widget w = widgets_[wi];
	uint8_t* r = interpRef(w.field);
	if (!r) return;
	HMENU menu = CreatePopupMenu();
	const int n = std::max(poseCount_, (int)*r + 1);
	for (int k = 0; k < n; k++) {
		wchar_t b[48];
		std::swprintf(b, 48, k < poseCount_ ? L"ポーズ補間%d" : L"ポーズ補間%d（非表示）", k + 1);
		AppendMenuW(menu, MF_STRING | (k == *r ? MF_CHECKED : 0), 1000 + k, b);
	}
	POINT pt{ w.rc.left, w.rc.bottom };
	ClientToScreen(hwnd_, &pt);
	const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
	DestroyMenu(menu);
	if (cmd >= 1000 && cmd < 1000 + n) {
		r = interpRef(w.field);  // メニュー表示中に読み直された場合に備えて取り直す
		if (r && (int)*r != cmd - 1000) {
			*r = (uint8_t)(cmd - 1000);
			commit();
		}
	}
	InvalidateRect(hwnd_, nullptr, FALSE);
}

void Editor::addPin(double x, double y) {
	if ((int)doc_.pins.size() >= kMaxPins) return;
	Pin p;
	p.x = (float)std::round(x * 10) / 10;
	p.y = (float)std::round(y * 10) / 10;
	doc_.pins.push_back(p);
	doc_.normalize();
	selPin_ = (int)doc_.pins.size() - 1;
}

void Editor::deletePin(int i) {
	if (i < 0 || i >= (int)doc_.pins.size()) return;
	doc_.pins.erase(doc_.pins.begin() + i);
	for (auto& p : doc_.poses) {
		if (i < (int)p.pins.size()) p.pins.erase(p.pins.begin() + i);
	}
	if (selPin_ >= (int)doc_.pins.size()) selPin_ = (int)doc_.pins.size() - 1;
}

void Editor::addBone(double x, double y, int parent) {
	if ((int)doc_.bones.size() >= kMaxBones) return;
	Bone b;
	b.x = (float)std::round(x * 10) / 10;
	b.y = (float)std::round(y * 10) / 10;
	b.parent = (int16_t)(parent >= 0 && parent < (int)doc_.bones.size() ? parent : -1);
	doc_.bones.push_back(b);
	doc_.normalize();
	selBone_ = (int)doc_.bones.size() - 1;
}

// 親から child への線上にボーンを追加し、親 → 新しいボーン → child の順につなぐ
void Editor::insertBone(int child, double x, double y) {
	if ((int)doc_.bones.size() >= kMaxBones || child < 0 || child >= (int)doc_.bones.size()) return;
	const int parent = doc_.bones[child].parent;
	addBone(x, y, parent);
	doc_.bones[child].parent = (int16_t)selBone_;
	doc_.sanitize();
}

void Editor::deleteBone(int b) {
	if (b < 0 || b >= (int)doc_.bones.size()) return;
	const int16_t parent = doc_.bones[b].parent;
	for (auto& bone : doc_.bones) {
		if (bone.parent == b) bone.parent = parent;  // A→B→C で B を消すと A→C
	}
	for (auto& pin : doc_.pins) {
		if (pin.bone == b) pin.bone = -1;  // 紐付いていたピンは紐付きなし
	}
	doc_.bones.erase(doc_.bones.begin() + b);
	for (auto& bone : doc_.bones) {
		if (bone.parent > b) bone.parent--;
	}
	for (auto& pin : doc_.pins) {
		if (pin.bone > b) pin.bone--;
	}
	for (auto& p : doc_.poses) {
		if (b < (int)p.bones.size()) p.bones.erase(p.bones.begin() + b);
	}
	selBone_ = -1;
	doc_.sanitize();
}

// ハサミのボタン : 画像の中央に、中心から左右に画像の幅の 1/4 の位置を端とする切れ込みを足し、始点を選ぶ
void Editor::addCut() {
	if ((int)doc_.cuts.size() >= kMaxCuts) return;
	const double cx = (conL_ + conR_) / 2, cy = (conT_ + conB_) / 2, q = conW() / 4;
	Cut c;
	c.points.push_back({ (float)(std::round((cx - q) * 10) / 10), (float)(std::round(cy * 10) / 10) });
	c.points.push_back({ (float)(std::round((cx + q) * 10) / 10), (float)(std::round(cy * 10) / 10) });
	doc_.cuts.push_back(c);
	selCut_ = (int)doc_.cuts.size() - 1;
	selCutPt_ = 0;
}

// 選択中の点を消す : 中間の点はその点だけ、始点・終点は切れ込みごと
void Editor::deleteCutPoint() {
	if (!cutSelected()) return;
	auto& pts = doc_.cuts[selCut_].points;
	if (selCutPt_ > 0 && selCutPt_ + 1 < (int)pts.size()) {
		pts.erase(pts.begin() + selCutPt_);
	} else {
		doc_.cuts.erase(doc_.cuts.begin() + selCut_);
	}
	selCut_ = selCutPt_ = -1;
}

bool Editor::cutEndOpen(const CutPoint& p) const {
	if (!contour_.empty()) return !contour_.inside({ p.x, p.y });
	return p.x <= conL_ || p.x >= conR_ || p.y <= conT_ || p.y >= conB_;
}

// ---------------------------------------------------------------------------
// 配置
void Editor::layout() {
	GetClientRect(hwnd_, &rcClient_);
	const int W = rcClient_.right, H = rcClient_.bottom;
	auto S = [this](double v) { return (int)std::lround(v * s_); };
	const int tabH = S(30);
	rcTabs_ = { 0, 0, W, tabH };
	rcTabPin_ = { 0, 0, W / 2, tabH };
	rcTabBone_ = { W / 2, 0, W, tabH };

	widgets_.clear();
	const int pad = S(8), rowH = S(28), gap = S(4), labelW = S(92);
	std::vector<Widget> rows;  // 位置は後で決める
	auto addRow = [&](WType t, Field f, const std::wstring& text = L"") {
		Widget w;
		w.type = t;
		w.field = f;
		w.text = text;
		rows.push_back(w);
	};
	if (cutSelected()) {
		const int n = (int)doc_.cuts[selCut_].points.size();
		wchar_t b[64];
		if (selCutPt_ == 0) {
			std::swprintf(b, 64, L"切れ込み%d ・ 始点", selCut_ + 1);
		} else if (selCutPt_ == n - 1) {
			std::swprintf(b, 64, L"切れ込み%d ・ 終点", selCut_ + 1);
		} else {
			std::swprintf(b, 64, L"切れ込み%d ・ 中間点 %d", selCut_ + 1, selCutPt_);
		}
		addRow(WType::Toggle, F_CutEnabled);
		addRow(WType::Label, F_Count, b);
		addRow(WType::Number, F_CutX);
		addRow(WType::Number, F_CutY);
	} else if (tab_ == 0) {
		if (selPin_ >= 0 && selPin_ < (int)doc_.pins.size()) {
			addRow(WType::Toggle, F_PinEnabled);
			addRow(WType::Number, F_PinX);
			addRow(WType::Number, F_PinY);
			addRow(WType::Header, F_Count);
			addRow(WType::Number, F_MoveX);
			addRow(WType::Number, F_MoveY);
		} else {
			addRow(WType::Label, F_Count, L"プレビュー上でピンを選択すると、ここで値を編集できます");
		}
	} else {
		if (selBone_ >= 0 && selBone_ < (int)doc_.bones.size()) {
			addRow(WType::Toggle, F_BoneEnabled);
			addRow(WType::Number, F_BoneX);
			addRow(WType::Number, F_BoneY);
			addRow(WType::Header, F_Count);
			addRow(WType::Number, F_Angle);
			addRow(WType::Number, F_Stretch);
			addRow(WType::Group, F_Count, L"揺れ");
			addRow(WType::Number, F_Swing);
			addRow(WType::Number, F_Period);
			addRow(WType::Number, F_Phase);
			addRow(WType::Number, F_Prop);
			addRow(WType::Number, F_Shinari);
		} else {
			addRow(WType::Label, F_Count, L"プレビュー上でボーンを選択すると、ここで値を編集できます");
		}
	}
	const int statusH = S(22);
	const int kReservedRows = 12;  // ボーンを選択したときの行数 (最も多い場合) を常に確保する
	const int panelH = pad + rowH + gap + std::max(kReservedRows, (int)rows.size()) * rowH + pad + statusH;
	int canvasBottom = std::max(tabH + S(120), H - panelH);
	rcCanvas_ = { 0, tabH, W, canvasBottom };
	rcPanel_ = { 0, canvasBottom, W, H };
	const int bs = S(26);
	rcRefresh_ = { rcCanvas_.right - S(6) - bs, rcCanvas_.top + S(6), rcCanvas_.right - S(6), rcCanvas_.top + S(6) + bs };
	rcFit_ = { rcRefresh_.left - S(4) - bs, rcRefresh_.top, rcRefresh_.left - S(4), rcRefresh_.bottom };
	rcCut_ = { rcFit_.left - S(4) - bs, rcFit_.top, rcFit_.left - S(4), rcFit_.bottom };

	// ボタンの行
	int y = rcPanel_.top + pad;
	const int x0 = pad, x1 = W - pad;
	auto button = [&](int id, int l, int r, const std::wstring& text, bool en) {
		Widget w;
		w.type = WType::Button;
		w.id = id;
		w.rc = { l, y, r, y + rowH };
		w.text = text;
		w.enabled = en;
		widgets_.push_back(w);
	};
	if (tab_ == 0) {
		const int small = S(32);
		button(B_AddPin, x0, x1 - 2 * (small + gap), L"ピンを追加", (int)doc_.pins.size() < kMaxPins && found_);
		button(B_DelPin, x1 - 2 * small - gap, x1 - small - gap, L"−", (selPin_ >= 0 || cutSelected()) && found_);
		button(B_ResetPins, x1 - small, x1, L"↺", !doc_.pins.empty() && found_);
	} else {
		const int small = S(32);
		button(B_AddBone, x0, x1 - small - gap, L"ボーンを追加", (int)doc_.bones.size() < kMaxBones && found_);
		button(B_DelBone, x1 - small, x1, L"−", (selBone_ >= 0 || cutSelected()) && found_);
	}
	y += rowH + gap;

	// 設定項目の行
	const int colGap = S(8);
	const int valL = x0 + labelW;
	const int interpW = S(64);
	const int colW = (x1 - valL - 2 * colGap - interpW) / 2;
	const int interpL = x1 - interpW;
	for (auto& w : rows) {
		w.rc = { x0, y, x1, y + rowH - S(4) };
		if (w.type == WType::Number) {
			const FieldMeta& m = kFields[w.field];
			auto makeBar = [&](int l, int r, int side) {
				Widget b = w;
				b.side = side;
				b.rc = { l, y + S(2), r, y + rowH - S(4) };
				const int sq = b.rc.bottom - b.rc.top;
				b.rcMinus = { b.rc.left, b.rc.top, b.rc.left + sq, b.rc.bottom };
				b.rcPlus = { b.rc.right - sq, b.rc.top, b.rc.right, b.rc.bottom };
				b.rcValue = { b.rcMinus.right, b.rc.top, b.rcPlus.left, b.rc.bottom };
				widgets_.push_back(b);
			};
			Widget lab;
			lab.type = WType::Label;
			lab.rc = { x0, y, valL, y + rowH - S(2) };
			lab.text = m.label;
			widgets_.push_back(lab);
			if (m.perPose) {
				makeBar(valL, valL + colW, 0);
				makeBar(valL + colW + colGap, valL + 2 * colW + colGap, 1);
				Widget sel;
				sel.type = WType::Interp;
				sel.field = w.field;
				sel.rc = { interpL, y + S(2), x1, y + rowH - S(4) };
				widgets_.push_back(sel);
			} else {
				makeBar(valL, x1, -1);
			}
		} else if (w.type == WType::Toggle) {
			Widget lab;
			lab.type = WType::Label;
			lab.rc = { x0, y, valL, y + rowH - S(2) };
			lab.text = kFields[w.field].label;
			widgets_.push_back(lab);
			Widget t = w;
			t.rc = { valL, y + S(4), valL + S(40), y + rowH - S(6) };
			widgets_.push_back(t);
		} else if (w.type == WType::Header) {
			const int k = sec_.section, n = (int)sec_.pointFrames.size();
			auto name = [&](int i) -> std::wstring {
				wchar_t b[64];
				if (i == 0) {
					std::swprintf(b, 64, L"左：開始点");
				} else if (i == n - 1) {
					std::swprintf(b, 64, L"右：終了点");
				} else {
					std::swprintf(b, 64, L"%ls：中間点%d", i == k ? L"左" : L"右", i);
				}
				return b;
			};
			Widget a;
			a.type = WType::Header;
			a.rc = { valL, y, valL + colW, y + rowH - S(4) };
			a.text = name(k);
			widgets_.push_back(a);
			Widget b = a;
			b.rc = { valL + colW + colGap, y, valL + 2 * colW + colGap, y + rowH - S(4) };
			b.text = name(std::min(k + 1, n - 1));
			widgets_.push_back(b);
			Widget c = a;
			c.rc = { interpL, y, x1, y + rowH - S(4) };
			c.text = L"補間";
			widgets_.push_back(c);
		} else {
			widgets_.push_back(w);
		}
		y += rowH;
	}
	if (!status_.empty()) {
		Widget st;
		st.type = WType::Label;
		st.rc = { x0, H - pad - statusH, x1, H - pad };
		st.text = status_;
		st.id = -1;  // 注意表示
		widgets_.push_back(st);
	}
	if (needFit_) fitView();
}

void Editor::fitView() {
	const double ch = std::max(1L, rcCanvas_.bottom - rcCanvas_.top);
	zoom_ = ch / conH();
	viewX_ = (conL_ + conR_) / 2;
	viewY_ = (conT_ + conB_) / 2;
	if (!images_.empty()) needFit_ = false;
}

G::PointF Editor::toScreen(double x, double y) const {
	const double cx = (rcCanvas_.left + rcCanvas_.right) * 0.5, cy = (rcCanvas_.top + rcCanvas_.bottom) * 0.5;
	return G::PointF((G::REAL)(cx + (x - viewX_) * zoom_), (G::REAL)(cy + (y - viewY_) * zoom_));
}

void Editor::toImage(int sx, int sy, double& x, double& y) const {
	const double cx = (rcCanvas_.left + rcCanvas_.right) * 0.5, cy = (rcCanvas_.top + rcCanvas_.bottom) * 0.5;
	x = (sx - cx) / zoom_ + viewX_;
	y = (sy - cy) / zoom_ + viewY_;
}

bool Editor::hScrollVisible() const { return conW() * zoom_ > (rcCanvas_.right - rcCanvas_.left) + 0.5; }
bool Editor::vScrollVisible() const { return conH() * zoom_ > (rcCanvas_.bottom - rcCanvas_.top) + 0.5; }

void Editor::scrollGeometry(bool horizontal, RECT& track, RECT& thumb, double& lo, double& hi) const {
	const int t = (int)std::lround(10 * s_);
	if (horizontal) {
		track = { rcCanvas_.left, rcCanvas_.bottom - t, rcCanvas_.right - (vScrollVisible() ? t : 0), rcCanvas_.bottom };
		const double vis = (rcCanvas_.right - rcCanvas_.left) / zoom_;
		lo = std::min(conL_, viewX_ - vis / 2);
		hi = std::max(conR_, viewX_ + vis / 2);
		const double len = track.right - track.left;
		thumb = track;
		thumb.left = track.left + (LONG)((viewX_ - vis / 2 - lo) / (hi - lo) * len);
		thumb.right = track.left + (LONG)((viewX_ + vis / 2 - lo) / (hi - lo) * len);
	} else {
		track = { rcCanvas_.right - t, rcCanvas_.top, rcCanvas_.right, rcCanvas_.bottom - (hScrollVisible() ? t : 0) };
		const double vis = (rcCanvas_.bottom - rcCanvas_.top) / zoom_;
		lo = std::min(conT_, viewY_ - vis / 2);
		hi = std::max(conB_, viewY_ + vis / 2);
		const double len = track.bottom - track.top;
		thumb = track;
		thumb.top = track.top + (LONG)((viewY_ - vis / 2 - lo) / (hi - lo) * len);
		thumb.bottom = track.top + (LONG)((viewY_ + vis / 2 - lo) / (hi - lo) * len);
	}
}

// ---------------------------------------------------------------------------
// 描画
void Editor::paint(HDC hdc) {
	const int W = rcClient_.right, H = rcClient_.bottom;
	if (W <= 0 || H <= 0) return;
	if (!memDC_ || memW_ != W || memH_ != H) {
		if (memDC_) {
			SelectObject(memDC_, memOld_);
			DeleteObject(memBmp_);
			DeleteDC(memDC_);
		}
		memDC_ = CreateCompatibleDC(hdc);
		memBmp_ = CreateCompatibleBitmap(hdc, W, H);
		memOld_ = SelectObject(memDC_, memBmp_);
		memW_ = W;
		memH_ = H;
	}
	{
		G::Graphics g(memDC_);
		g.SetSmoothingMode(G::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(G::TextRenderingHintClearTypeGridFit);
		g.Clear(kBg);

		// タブ
		G::Font font(family_.get(), (G::REAL)(12.5 * s_), G::FontStyleRegular, G::UnitPixel);
		G::StringFormat center;
		center.SetAlignment(G::StringAlignmentCenter);
		center.SetLineAlignment(G::StringAlignmentCenter);
		for (int i = 0; i < 2; i++) {
			const RECT& r = i == 0 ? rcTabPin_ : rcTabBone_;
			G::SolidBrush b(tab_ == i ? kTabSel : kTab);
			g.FillRectangle(&b, toRectF(r));
			if (tab_ == i) {
				G::Pen pen(kTabSelLine, (G::REAL)(1.0 * s_));
				g.DrawRectangle(&pen, (G::REAL)r.left + 0.5f, (G::REAL)r.top + 0.5f, (G::REAL)(r.right - r.left - 1), (G::REAL)(r.bottom - r.top - 1));
			}
			G::SolidBrush tb(kText);
			g.DrawString(i == 0 ? L"ピン" : L"ボーン", -1, &font, toRectF(r), &center, &tb);
		}

		g.SetClip(toRectF(rcCanvas_));
		paintCanvas(g);
		g.ResetClip();
		paintPanel(g);
	}
	BitBlt(hdc, 0, 0, W, H, memDC_, 0, 0, SRCCOPY);
}

void Editor::paintCanvas(G::Graphics& g) {
	const float S = (float)s_;
	// 背景のチェック柄
	{
		const int cs = (int)std::lround(12 * s_);
		G::SolidBrush b1(kChk1), b2(kChk2);
		g.FillRectangle(&b2, toRectF(rcCanvas_));
		for (int y = rcCanvas_.top, j = 0; y < rcCanvas_.bottom; y += cs, j++) {
			for (int x = rcCanvas_.left, i = 0; x < rcCanvas_.right; x += cs, i++) {
				if ((i + j) & 1) g.FillRectangle(&b1, x, y, cs, cs);
			}
		}
	}
	// 画像 (変形前)
	const G::PointF tl = toScreen(conL_, conT_);
	const G::RectF imgRect(tl.X, tl.Y, (G::REAL)(conW() * zoom_), (G::REAL)(conH() * zoom_));
	if (!images_.empty()) {
		g.SetInterpolationMode(drag_ == Drag::None ? G::InterpolationModeHighQualityBilinear : G::InterpolationModeBilinear);
		g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
		const double scx = (rcCanvas_.left + rcCanvas_.right) * 0.5, scy = (rcCanvas_.top + rcCanvas_.bottom) * 0.5;
		for (auto& vi : images_) {
			// 画面 = 表示の変換 × オブジェクトの位置・回転・拡大率 × 画像
			G::Matrix m;
			m.Translate((G::REAL)(scx - viewX_ * zoom_), (G::REAL)(scy - viewY_ * zoom_));
			m.Scale((G::REAL)zoom_, (G::REAL)zoom_);
			m.Translate((G::REAL)vi.xf.x, (G::REAL)vi.xf.y);
			m.Rotate((G::REAL)vi.xf.rz);
			m.Scale((G::REAL)vi.xf.sx, (G::REAL)vi.xf.sy);
			m.Translate((G::REAL)-vi.xf.cx, (G::REAL)-vi.xf.cy);
			g.SetTransform(&m);
			g.DrawImage(vi.bmp.get(), G::RectF((G::REAL)(-vi.w / 2.0), (G::REAL)(-vi.h / 2.0), (G::REAL)vi.w, (G::REAL)vi.h), 0, 0,
						(G::REAL)vi.w, (G::REAL)vi.h, G::UnitPixel);
		}
		g.ResetTransform();
		g.SetPixelOffsetMode(G::PixelOffsetModeDefault);
	} else {
		G::Pen pen(kBorder, 1);
		pen.SetDashStyle(G::DashStyleDash);
		g.DrawRectangle(&pen, imgRect);
	}

	G::Font font(family_.get(), (G::REAL)(12.5 * s_), G::FontStyleRegular, G::UnitPixel);
	G::StringFormat center;
	center.SetAlignment(G::StringAlignmentCenter);
	center.SetLineAlignment(G::StringAlignmentCenter);
	if (!found_ || !loaded_) {
		G::SolidBrush tb(kText);
		g.DrawString(found_ ? L"読み込み中…" : status_.c_str(), -1, &font, toRectF(rcCanvas_), &center, &tb);
	} else if (images_.empty()) {
		G::SolidBrush tb(kTextDim);
		RECT r = rcCanvas_;
		r.top = r.bottom - (LONG)(28 * s_);
		g.DrawString(L"画像を取得しています…（プレビューを更新すると表示されます）", -1, &font, toRectF(r), &center, &tb);
	}

	if (found_ && loaded_) {
		const float pinR = 6 * S;
		// 輪郭 (不透明な部分の輪郭。切れ込みの端が外か内かの境目)
		if (!contour_.empty()) {
			G::Pen pen(G::Color(200, kContour.GetR(), kContour.GetG(), kContour.GetB()), 1.2f * S);
			for (auto& lp : contour_.loops) {
				if (lp.size() < 2) continue;
				std::vector<G::PointF> pts;
				pts.reserve(lp.size());
				for (auto& p : lp) pts.push_back(toScreen(p.x, p.y));
				g.DrawPolygon(&pen, pts.data(), (INT)pts.size());
			}
		}
		const bool cutSel = cutSelected();
		// ボーン (両方のタブで表示。操作できるのはボーンタブのみ)
		{
			for (int b = 0; b < (int)doc_.bones.size(); b++) {
				const int p = doc_.bones[b].parent;
				if (p < 0) continue;
				const G::PointF a = toScreen(doc_.bones[p].x, doc_.bones[p].y);
				const G::PointF c = toScreen(doc_.bones[b].x, doc_.bones[b].y);
				const int alpha = doc_.bones[b].enabled ? 255 : 110;
				G::Pen pen(G::Color(alpha, kOrange.GetR(), kOrange.GetG(), kOrange.GetB()), 2 * S);
				g.DrawLine(&pen, a, c);
				// 中央の矢印 (親 → 子)
				const float dx = c.X - a.X, dy = c.Y - a.Y, len = std::sqrt(dx * dx + dy * dy);
				if (len > 18 * S) {
					const float ux = dx / len, uy = dy / len;
					const G::PointF m((a.X + c.X) / 2, (a.Y + c.Y) / 2);
					const float as = 5 * S;
					G::PointF tri[3] = { G::PointF(m.X + ux * as, m.Y + uy * as),
										 G::PointF(m.X - ux * as - uy * as * 0.8f, m.Y - uy * as + ux * as * 0.8f),
										 G::PointF(m.X - ux * as + uy * as * 0.8f, m.Y - uy * as - ux * as * 0.8f) };
					G::SolidBrush br(G::Color(alpha, kOrange.GetR(), kOrange.GetG(), kOrange.GetB()));
					g.FillPolygon(&br, tri, 3);
				}
			}
			// ピンとボーンの紐付け (点線)
			for (auto& pin : doc_.pins) {
				if (pin.bone < 0 || pin.bone >= (int)doc_.bones.size()) continue;
				G::Pen pen(G::Color(220, kOrange.GetR(), kOrange.GetG(), kOrange.GetB()), 1.5f * S);
				pen.SetDashStyle(G::DashStyleDash);
				g.DrawLine(&pen, toScreen(pin.x, pin.y), toScreen(doc_.bones[pin.bone].x, doc_.bones[pin.bone].y));
			}
		}
		// ボーンのひし形
		auto drawDiamonds = [&]() {
			const float d = 7 * S;
			for (int b = 0; b < (int)doc_.bones.size(); b++) {
				const G::PointF c = toScreen(doc_.bones[b].x, doc_.bones[b].y);
				const int alpha = doc_.bones[b].enabled ? 255 : 110;
				G::PointF dia[4] = { G::PointF(c.X, c.Y - d), G::PointF(c.X + d, c.Y), G::PointF(c.X, c.Y + d), G::PointF(c.X - d, c.Y) };
				G::SolidBrush fill(G::Color(alpha, kOrange.GetR(), kOrange.GetG(), kOrange.GetB()));
				G::Pen outline(G::Color(alpha, 255, 255, 255), 1.5f * S);
				g.FillPolygon(&fill, dia, 4);
				g.DrawPolygon(&outline, dia, 4);
				if (tab_ == 1 && b == selBone_ && !cutSel) {
					const float rr = d + 5 * S;
					G::Pen ring(G::Color(alpha, kOrange.GetR(), kOrange.GetG(), kOrange.GetB()), 2 * S);
					g.DrawEllipse(&ring, c.X - rr, c.Y - rr, 2 * rr, 2 * rr);
				}
			}
		};
		if (tab_ == 0) drawDiamonds();  // ピンタブではピンを上に描く
		// ピン
		for (int i = 0; i < (int)doc_.pins.size(); i++) {
			const Pin& pin = doc_.pins[i];
			const G::PointF c = toScreen(pin.x, pin.y);
			const int alpha = pin.enabled ? 255 : 110;
			G::SolidBrush fill(G::Color(alpha, kPin.GetR(), kPin.GetG(), kPin.GetB()));
			G::Pen outline(G::Color(alpha, 255, 255, 255), 1.5f * S);
			g.FillEllipse(&fill, c.X - pinR, c.Y - pinR, 2 * pinR, 2 * pinR);
			g.DrawEllipse(&outline, c.X - pinR, c.Y - pinR, 2 * pinR, 2 * pinR);
			const bool selected = tab_ == 0 && i == selPin_ && !cutSel;
			const bool linkedToSel = tab_ == 1 && selBone_ >= 0 && pin.bone == selBone_ && !cutSel;
			if (selected || linkedToSel) {
				const float rr = pinR + 4 * S;
				G::Pen ring(selected ? G::Color(alpha, kPin.GetR(), kPin.GetG(), kPin.GetB()) : G::Color(alpha, kOrange.GetR(), kOrange.GetG(), kOrange.GetB()), 2 * S);
				g.DrawEllipse(&ring, c.X - rr, c.Y - rr, 2 * rr, 2 * rr);
			}
		}
		if (tab_ == 1) drawDiamonds();

		// 切れ込み (両方のタブで操作できるので一番上に描く)
		for (int c = 0; c < (int)doc_.cuts.size(); c++) {
			const Cut& cut = doc_.cuts[c];
			if (cut.points.size() < 2) continue;
			const int alpha = cut.enabled ? 255 : 110;
			std::vector<G::PointF> pts;
			for (auto& p : cut.points) pts.push_back(toScreen(p.x, p.y));
			G::Pen edge(G::Color(alpha, 255, 255, 255), 4.5f * S);
			edge.SetLineJoin(G::LineJoinRound);
			G::Pen line(G::Color(alpha, kCut.GetR(), kCut.GetG(), kCut.GetB()), 2.2f * S);
			line.SetLineJoin(G::LineJoinRound);
			g.DrawLines(&edge, pts.data(), (INT)pts.size());
			g.DrawLines(&line, pts.data(), (INT)pts.size());
			const float pr = 5.5f * S;
			G::SolidBrush fill(G::Color(alpha, kCut.GetR(), kCut.GetG(), kCut.GetB()));
			G::Pen outline(G::Color(alpha, 255, 255, 255), 1.5f * S);
			G::Pen ring(G::Color(alpha, kCut.GetR(), kCut.GetG(), kCut.GetB()), 2 * S);
			for (int i = 0; i < (int)pts.size(); i++) {
				const bool end = i == 0 || i + 1 == (int)pts.size();
				const bool open = end && cutEndOpen(cut.points[i]);  // 開いた端は四角、閉じた端・中間の点は丸
				const G::PointF& q = pts[i];
				const bool sel = c == selCut_ && i == selCutPt_;
				if (open) {
					g.FillRectangle(&fill, q.X - pr, q.Y - pr, 2 * pr, 2 * pr);
					g.DrawRectangle(&outline, q.X - pr, q.Y - pr, 2 * pr, 2 * pr);
					if (sel) {
						const float rr = pr + 4 * S;
						g.DrawRectangle(&ring, q.X - rr, q.Y - rr, 2 * rr, 2 * rr);
					}
				} else {
					g.FillEllipse(&fill, q.X - pr, q.Y - pr, 2 * pr, 2 * pr);
					g.DrawEllipse(&outline, q.X - pr, q.Y - pr, 2 * pr, 2 * pr);
					if (sel) {
						const float rr = pr + 4 * S;
						g.DrawEllipse(&ring, q.X - rr, q.Y - rr, 2 * rr, 2 * rr);
					}
				}
			}
		}
		// 切れ込みの線にカーソルがあるとき : 点が増える位置
		if (hoverCut_ && drag_ == Drag::None) {
			const G::PointF q = toScreen(hoverX_, hoverY_);
			const float pr = 5.5f * S;
			G::SolidBrush faint(G::Color(110, kCut.GetR(), kCut.GetG(), kCut.GetB()));
			g.FillEllipse(&faint, q.X - pr, q.Y - pr, 2 * pr, 2 * pr);
		}
		if (group_) {
			G::SolidBrush tb(kTextDim);
			RECT r = rcCanvas_;
			r.left += (LONG)(8 * s_);
			r.top = r.bottom - (LONG)(26 * s_);
			G::StringFormat left;
			left.SetAlignment(G::StringAlignmentNear);
			left.SetLineAlignment(G::StringAlignmentCenter);
			g.DrawString(L"グループ制御では切れ込みは効きません", -1, &font, toRectF(r), &left, &tb);
		}
	}

	// 右上のボタン (切れ込みを追加・表示の初期化・画像の更新)
	for (int i = -1; i < 2; i++) {
		const RECT& r = i < 0 ? rcCut_ : i == 0 ? rcFit_ : rcRefresh_;
		const bool enabled = i >= 0 || (found_ && loaded_ && (int)doc_.cuts.size() < kMaxCuts);
		fillRound(g, r, 3 * S, enabled ? G::Color(230, 70, 70, 70) : G::Color(230, 52, 52, 52));
		G::Pen pen(enabled ? kText : kTextDim, 1.4f * S);
		const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f, a = 7 * S;
		if (i < 0) {
			// ハサミ : 2つの持ち手 (輪) と、交差する2枚の刃
			const float hr = 2.6f * S;
			const G::PointF h1(cx - 3.6f * S, cy + 4.2f * S), h2(cx + 3.6f * S, cy + 4.2f * S);
			g.DrawEllipse(&pen, h1.X - hr, h1.Y - hr, 2 * hr, 2 * hr);
			g.DrawEllipse(&pen, h2.X - hr, h2.Y - hr, 2 * hr, 2 * hr);
			g.DrawLine(&pen, h1.X + hr * 0.7f, h1.Y - hr * 0.7f, cx + 3.2f * S, cy - 7 * S);
			g.DrawLine(&pen, h2.X - hr * 0.7f, h2.Y - hr * 0.7f, cx - 3.2f * S, cy - 7 * S);
		} else if (i == 0) {
			// 十字の矢印
			g.DrawLine(&pen, cx - a, cy, cx + a, cy);
			g.DrawLine(&pen, cx, cy - a, cx, cy + a);
			const float h = 2.5f * S;
			G::PointF arrows[4][3] = { { G::PointF(cx - a, cy), G::PointF(cx - a + h, cy - h), G::PointF(cx - a + h, cy + h) },
									   { G::PointF(cx + a, cy), G::PointF(cx + a - h, cy - h), G::PointF(cx + a - h, cy + h) },
									   { G::PointF(cx, cy - a), G::PointF(cx - h, cy - a + h), G::PointF(cx + h, cy - a + h) },
									   { G::PointF(cx, cy + a), G::PointF(cx - h, cy + a - h), G::PointF(cx + h, cy + a - h) } };
			G::SolidBrush br(kText);
			for (auto& t : arrows) g.FillPolygon(&br, t, 3);
		} else {
			// 円形の矢印
			const float rr = 6 * S;
			g.DrawArc(&pen, cx - rr, cy - rr, 2 * rr, 2 * rr, 300, 280);
			const float ex = cx + rr * std::cos(300 * 3.14159265f / 180), ey = cy + rr * std::sin(300 * 3.14159265f / 180);
			const float h = 3 * S;
			G::PointF tri[3] = { G::PointF(ex + h, ey - h * 0.2f), G::PointF(ex - h * 0.6f, ey - h), G::PointF(ex - h * 0.2f, ey + h) };
			G::SolidBrush br(kText);
			g.FillPolygon(&br, tri, 3);
		}
	}

	// スクロールバー (拡大して画像がはみ出している時だけ)
	for (int i = 0; i < 2; i++) {
		const bool horiz = i == 0;
		if (horiz ? !hScrollVisible() : !vScrollVisible()) continue;
		RECT track, thumb;
		double lo, hi;
		scrollGeometry(horiz, track, thumb, lo, hi);
		G::SolidBrush tb(G::Color(120, 20, 20, 20));
		g.FillRectangle(&tb, toRectF(track));
		fillRound(g, thumb, 3 * S, kScroll);
	}
}

void Editor::paintPanel(G::Graphics& g) {
	const float S = (float)s_;
	G::SolidBrush pb(kPanel);
	g.FillRectangle(&pb, toRectF(rcPanel_));
	G::Pen line(kBorder, 1);
	g.DrawLine(&line, (G::REAL)rcPanel_.left, (G::REAL)rcPanel_.top + 0.5f, (G::REAL)rcPanel_.right, (G::REAL)rcPanel_.top + 0.5f);

	G::Font font(family_.get(), (G::REAL)(12.5 * s_), G::FontStyleRegular, G::UnitPixel);
	G::Font small(family_.get(), (G::REAL)(11.5 * s_), G::FontStyleRegular, G::UnitPixel);
	G::StringFormat center, left;
	center.SetAlignment(G::StringAlignmentCenter);
	center.SetLineAlignment(G::StringAlignmentCenter);
	left.SetAlignment(G::StringAlignmentNear);
	left.SetLineAlignment(G::StringAlignmentCenter);
	G::SolidBrush text(kText), dim(kTextDim);

	for (size_t i = 0; i < widgets_.size(); i++) {
		const Widget& w = widgets_[i];
		switch (w.type) {
			case WType::Button: {
				fillRound(g, w.rc, 3 * S, w.enabled ? kBtn : kBtnDisabled);
				g.DrawString(w.text.c_str(), -1, &font, toRectF(w.rc), &center, w.enabled ? &text : &dim);
				break;
			}
			case WType::Label: {
				g.DrawString(w.text.c_str(), -1, w.id == -1 ? &small : &font, toRectF(w.rc), &left, w.id == -1 ? &dim : &text);
				break;
			}
			case WType::Group: {
				g.DrawString((L"▾ " + w.text).c_str(), -1, &font, toRectF(w.rc), &left, &text);
				break;
			}
			case WType::Interp: {
				const uint8_t* r = interpRef(w.field);
				fillRound(g, w.rc, 2 * S, kBtn);
				wchar_t b[32];
				std::swprintf(b, 32, L"補間%d ▾", r ? *r + 1 : 1);
				const bool beyond = r && *r + 1 > poseCount_;  // 設定画面で非表示になっている補間
				g.DrawString(b, -1, &font, toRectF(w.rc), &center, beyond ? &dim : &text);
				break;
			}
			case WType::Header: {
				g.DrawString(w.text.c_str(), -1, &small, toRectF(w.rc), &center, &dim);
				break;
			}
			case WType::Toggle: {
				const bool on = getField(w.field, -1) != 0;
				const RECT& r = w.rc;
				const float hgt = (float)(r.bottom - r.top);
				fillRound(g, r, hgt / 2, on ? kToggleOn : kToggleOff);
				const float knob = hgt - 4 * S;
				const float kx = on ? r.right - 2 * S - knob : r.left + 2 * S;
				G::SolidBrush kb(G::Color(255, 40, 40, 40));
				g.FillEllipse(&kb, kx, r.top + 2 * S, knob, knob);
				break;
			}
			case WType::Number: {
				fillRound(g, w.rc, 2 * S, kBar);
				G::SolidBrush side(kBarSide);
				g.FillRectangle(&side, toRectF(w.rcMinus));
				g.FillRectangle(&side, toRectF(w.rcPlus));
				g.DrawString(L"−", -1, &font, toRectF(w.rcMinus), &center, &dim);
				g.DrawString(L"+", -1, &font, toRectF(w.rcPlus), &center, &dim);
				if (i != editingWidget_) {
					g.DrawString(formatValue(w.field, getField(w.field, w.side)).c_str(), -1, &font, toRectF(w.rcValue), &center, &text);
				}
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// 当たり判定
int Editor::hitPin(int sx, int sy) const {
	const double r = 10 * s_;
	int best = -1;
	double bd = r * r;
	for (int i = (int)doc_.pins.size() - 1; i >= 0; i--) {
		const G::PointF p = toScreen(doc_.pins[i].x, doc_.pins[i].y);
		const double d = (p.X - sx) * (p.X - sx) + (p.Y - sy) * (p.Y - sy);
		if (d <= bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

int Editor::hitBone(int sx, int sy) const {
	const double r = 10 * s_;
	int best = -1;
	double bd = r * r;
	for (int i = (int)doc_.bones.size() - 1; i >= 0; i--) {
		const G::PointF p = toScreen(doc_.bones[i].x, doc_.bones[i].y);
		const double d = (p.X - sx) * (p.X - sx) + (p.Y - sy) * (p.Y - sy);
		if (d <= bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

// ボーン間の線 (親 → 子) に当たったら、その子ボーンの番号と画像上の位置を返す
int Editor::hitSegment(int sx, int sy, double& ix, double& iy) const {
	const double tol = 6 * s_;
	int best = -1;
	double bd = tol;
	for (int b = 0; b < (int)doc_.bones.size(); b++) {
		const int p = doc_.bones[b].parent;
		if (p < 0) continue;
		const G::PointF a = toScreen(doc_.bones[p].x, doc_.bones[p].y), c = toScreen(doc_.bones[b].x, doc_.bones[b].y);
		const double vx = c.X - a.X, vy = c.Y - a.Y, l2 = vx * vx + vy * vy;
		if (l2 < 1) continue;
		const double t = ((sx - a.X) * vx + (sy - a.Y) * vy) / l2;
		if (t <= 0.05 || t >= 0.95) continue;
		const double px = a.X + vx * t, py = a.Y + vy * t;
		const double d = std::hypot(sx - px, sy - py);
		if (d <= bd) {
			bd = d;
			best = b;
			ix = doc_.bones[p].x + (doc_.bones[b].x - doc_.bones[p].x) * t;
			iy = doc_.bones[p].y + (doc_.bones[b].y - doc_.bones[p].y) * t;
		}
	}
	return best;
}

bool Editor::hitCutPoint(int sx, int sy, int& cut, int& pt) const {
	const double r = 10 * s_;
	double bd = r * r;
	cut = pt = -1;
	for (int c = (int)doc_.cuts.size() - 1; c >= 0; c--) {
		const auto& pts = doc_.cuts[c].points;
		for (int i = (int)pts.size() - 1; i >= 0; i--) {
			const G::PointF p = toScreen(pts[i].x, pts[i].y);
			const double d = (p.X - sx) * (p.X - sx) + (p.Y - sy) * (p.Y - sy);
			if (d <= bd) {
				bd = d;
				cut = c;
				pt = i;
			}
		}
	}
	return cut >= 0;
}

// 切れ込みの線から 6px 以内 : 切れ込みの番号、線分の番号 (点 seg と seg+1 の間)、画像上の位置
bool Editor::hitCutSegment(int sx, int sy, int& cut, int& seg, double& ix, double& iy) const {
	const double tol = 6 * s_;
	double bd = tol;
	cut = seg = -1;
	for (int c = 0; c < (int)doc_.cuts.size(); c++) {
		const auto& pts = doc_.cuts[c].points;
		for (int i = 0; i + 1 < (int)pts.size(); i++) {
			const G::PointF a = toScreen(pts[i].x, pts[i].y), b = toScreen(pts[i + 1].x, pts[i + 1].y);
			const double vx = b.X - a.X, vy = b.Y - a.Y, l2 = vx * vx + vy * vy;
			if (l2 < 1) continue;
			const double t = std::clamp(((sx - a.X) * vx + (sy - a.Y) * vy) / l2, 0.0, 1.0);
			const double d = std::hypot(sx - (a.X + vx * t), sy - (a.Y + vy * t));
			if (d <= bd) {
				bd = d;
				cut = c;
				seg = i;
				ix = pts[i].x + (pts[i + 1].x - pts[i].x) * t;
				iy = pts[i].y + (pts[i + 1].y - pts[i].y) * t;
			}
		}
	}
	return cut >= 0;
}

// ---------------------------------------------------------------------------
// マウス操作
void Editor::onLButtonDown(int x, int y, WPARAM) {
	if (edit_ && IsWindowVisible(edit_)) endTextEdit(true);
	SetFocus(hwnd_);
	if (inRect(rcTabPin_, x, y) || inRect(rcTabBone_, x, y)) {
		const int t = inRect(rcTabPin_, x, y) ? 0 : 1;
		if (t != tab_) {
			tab_ = t;
			layout();
			InvalidateRect(hwnd_, nullptr, FALSE);
		}
		return;
	}
	if (inRect(rcCanvas_, x, y)) {
		if (inRect(rcFit_, x, y)) {
			fitView();
			InvalidateRect(hwnd_, nullptr, FALSE);
			return;
		}
		if (inRect(rcRefresh_, x, y)) {
			requestImage();
			return;
		}
		for (int i = 0; i < 2; i++) {
			const bool horiz = i == 0;
			if (horiz ? !hScrollVisible() : !vScrollVisible()) continue;
			RECT track, thumb;
			double lo, hi;
			scrollGeometry(horiz, track, thumb, lo, hi);
			if (inRect(track, x, y)) {
				drag_ = horiz ? Drag::HScroll : Drag::VScroll;
				dragStartX_ = x;
				dragStartY_ = y;
				dragViewX_ = viewX_;
				dragViewY_ = viewY_;
				SetCapture(hwnd_);
				return;
			}
		}
		if (!found_ || !loaded_) return;
		if (inRect(rcCut_, x, y)) {
			// ハサミのボタン : 切れ込みを足す
			if ((int)doc_.cuts.size() >= kMaxCuts) {
				MessageBeep(MB_OK);
				return;
			}
			addCut();
			commit();
			layout();
			InvalidateRect(hwnd_, nullptr, FALSE);
			return;
		}
		double ix, iy;
		toImage(x, y, ix, iy);
		// 切れ込みの点・線は、ピン・ボーンの操作より優先する (両方のタブ)
		bool cutHandled = false;
		{
			int hc = -1, hp = -1, sg = -1;
			double px = 0, py = 0;
			if (hitCutPoint(x, y, hc, hp)) {
				cutHandled = true;
			} else if (hitCutSegment(x, y, hc, sg, px, py)) {
				cutHandled = true;
				auto& pts = doc_.cuts[hc].points;
				if ((int)pts.size() >= kMaxCutPoints) {
					MessageBeep(MB_OK);
					hc = -1;
				} else {
					pts.insert(pts.begin() + sg + 1, CutPoint{ (float)(std::round(px * 10) / 10), (float)(std::round(py * 10) / 10) });
					selCut_ = hc;
					selCutPt_ = sg + 1;
					commit();
					hp = selCutPt_;
					if (!cutSelected()) hc = -1;  // 保存できずに読み直した場合
				}
			}
			if (cutHandled && hc >= 0) {
				selCut_ = hc;
				selCutPt_ = hp;
				drag_ = Drag::CutPt;
				dragOffX_ = doc_.cuts[hc].points[hp].x - ix;
				dragOffY_ = doc_.cuts[hc].points[hp].y - iy;
			}
		}
		if (cutHandled) {
			hoverCut_ = false;
		} else if (tab_ == 0) {
			selCut_ = selCutPt_ = -1;
			int hit = hitPin(x, y);
			if (hit < 0) {
				if ((int)doc_.pins.size() >= kMaxPins) {
					MessageBeep(MB_OK);
					return;
				}
				addPin(ix, iy);
				commit();
				hit = selPin_;
				if (hit < 0 || hit >= (int)doc_.pins.size()) {  // 保存できずに読み直した場合
					layout();
					InvalidateRect(hwnd_, nullptr, FALSE);
					return;
				}
			}
			selPin_ = hit;
			drag_ = Drag::Pin;
			dragIndex_ = hit;
			dragOffX_ = doc_.pins[hit].x - ix;
			dragOffY_ = doc_.pins[hit].y - iy;
		} else {
			selCut_ = selCutPt_ = -1;
			const int hb = hitBone(x, y);
			if (hb >= 0) {
				selBone_ = hb;
				drag_ = Drag::Bone;
				dragIndex_ = hb;
				dragOffX_ = doc_.bones[hb].x - ix;
				dragOffY_ = doc_.bones[hb].y - iy;
			} else {
				const int hp = hitPin(x, y);
				if (hp >= 0) {
					// ボーン選択中にピンをクリック : 紐付け / 解除
					if (selBone_ >= 0) {
						doc_.pins[hp].bone = (int16_t)(doc_.pins[hp].bone == selBone_ ? -1 : selBone_);
						commit();
					}
				} else {
					double sx2 = 0, sy2 = 0;
					const int seg = hitSegment(x, y, sx2, sy2);
					if ((int)doc_.bones.size() >= kMaxBones) {
						MessageBeep(MB_OK);
					} else if (seg >= 0) {
						insertBone(seg, sx2, sy2);
						commit();
					} else {
						addBone(ix, iy, selBone_);
						commit();
					}
				}
			}
		}
		dragStartX_ = x;
		dragStartY_ = y;
		dragMoved_ = false;
		if (drag_ != Drag::None) SetCapture(hwnd_);
		layout();
		InvalidateRect(hwnd_, nullptr, FALSE);
		return;
	}
	// 下の設定項目
	for (size_t i = 0; i < widgets_.size(); i++) {
		Widget& w = widgets_[i];
		if (!inRect(w.rc, x, y)) continue;
		if (w.type == WType::Button) {
			if (w.enabled) onButton(w.id);
			return;
		}
		if (w.type == WType::Toggle) {
			setField(w.field, -1, getField(w.field, -1) != 0 ? 0 : 1);
			commit();
			InvalidateRect(hwnd_, nullptr, FALSE);
			return;
		}
		if (w.type == WType::Interp) {
			chooseInterp(i);
			return;
		}
		if (w.type == WType::Number) {
			const FieldMeta& m = kFields[w.field];
			if (inRect(w.rcMinus, x, y) || inRect(w.rcPlus, x, y)) {
				const double d = inRect(w.rcMinus, x, y) ? -m.step : m.step;
				setField(w.field, w.side, getField(w.field, w.side) + d);
				commit();
				InvalidateRect(hwnd_, nullptr, FALSE);
				return;
			}
			drag_ = Drag::Number;
			dragWidget_ = i;
			dragStartX_ = x;
			dragStartY_ = y;  // 前のドラッグの値が残っていると、動かさずに離してもドラッグ扱いになり、直接入力にならなかった
			dragStartValue_ = getField(w.field, w.side);
			dragMoved_ = false;
			SetCapture(hwnd_);
			return;
		}
	}
}

void Editor::onMouseMove(int x, int y, WPARAM keys) {
	if (drag_ == Drag::None) {
		// 切れ込みの線の上 (点の上を除く) : 点が増える位置を表示
		bool hv = false;
		double hx = 0, hy = 0;
		if (found_ && loaded_ && inRect(rcCanvas_, x, y) && !inRect(rcCut_, x, y) && !inRect(rcFit_, x, y) && !inRect(rcRefresh_, x, y)) {
			int c, p, sg;
			if (!hitCutPoint(x, y, c, p) && hitCutSegment(x, y, c, sg, hx, hy)) hv = (int)doc_.cuts[c].points.size() < kMaxCutPoints;
		}
		if (hv != hoverCut_ || (hv && (hx != hoverX_ || hy != hoverY_))) {
			hoverCut_ = hv;
			hoverX_ = hx;
			hoverY_ = hy;
			InvalidateRect(hwnd_, nullptr, FALSE);
		}
		return;
	}
	if (std::abs(x - dragStartX_) > 2 || std::abs(y - dragStartY_) > 2) dragMoved_ = true;
	double ix, iy;
	toImage(x, y, ix, iy);
	switch (drag_) {
		case Drag::Pin:
			if (dragMoved_ && dragIndex_ >= 0 && dragIndex_ < (int)doc_.pins.size()) {
				doc_.pins[dragIndex_].x = (float)(std::round((ix + dragOffX_) * 10) / 10);
				doc_.pins[dragIndex_].y = (float)(std::round((iy + dragOffY_) * 10) / 10);
			}
			break;
		case Drag::Bone:
			if (dragMoved_ && dragIndex_ >= 0 && dragIndex_ < (int)doc_.bones.size()) {
				doc_.bones[dragIndex_].x = (float)(std::round((ix + dragOffX_) * 10) / 10);
				doc_.bones[dragIndex_].y = (float)(std::round((iy + dragOffY_) * 10) / 10);
			}
			break;
		case Drag::CutPt:
			if (dragMoved_ && cutSelected()) {
				auto& p = doc_.cuts[selCut_].points[selCutPt_];
				p.x = (float)(std::round((ix + dragOffX_) * 10) / 10);
				p.y = (float)(std::round((iy + dragOffY_) * 10) / 10);
			}
			break;
		case Drag::Pan:
			viewX_ = dragViewX_ - (x - dragStartX_) / zoom_;
			viewY_ = dragViewY_ - (y - dragStartY_) / zoom_;
			break;
		case Drag::HScroll:
		case Drag::VScroll: {
			RECT track, thumb;
			double lo, hi;
			const bool horiz = drag_ == Drag::HScroll;
			scrollGeometry(horiz, track, thumb, lo, hi);
			const double len = horiz ? track.right - track.left : track.bottom - track.top;
			if (horiz) {
				viewX_ = dragViewX_ + (x - dragStartX_) * (hi - lo) / std::max(1.0, len);
			} else {
				viewY_ = dragViewY_ + (y - dragStartY_) * (hi - lo) / std::max(1.0, len);
			}
			break;
		}
		case Drag::Number: {
			if (dragWidget_ < widgets_.size() && dragMoved_) {
				const Widget& w = widgets_[dragWidget_];
				double rate = kFields[w.field].dragRate;
				if (keys & MK_SHIFT) rate *= 0.1;
				if (keys & MK_CONTROL) rate *= 10;
				setField(w.field, w.side, dragStartValue_ + (x - dragStartX_) * rate);
			}
			break;
		}
		default: break;
	}
	InvalidateRect(hwnd_, nullptr, FALSE);
}

void Editor::onLButtonUp(int x, int) {
	const Drag d = drag_;
	drag_ = Drag::None;
	ReleaseCapture();
	if ((d == Drag::Pin || d == Drag::Bone || d == Drag::CutPt) && dragMoved_) commit();
	if (d == Drag::CutPt) layout();
	if (d == Drag::Number) {
		if (dragMoved_) {
			commit();
		} else if (dragWidget_ < widgets_.size()) {
			(void)x;
			beginTextEdit(dragWidget_);
		}
	}
	if (pendingReload_) {
		pendingReload_ = false;
		reload();
	}
	InvalidateRect(hwnd_, nullptr, FALSE);
}

void Editor::onButton(int id) {
	switch (id) {
		case B_AddPin:
			selCut_ = selCutPt_ = -1;
			addPin(0, 0);
			commit();
			break;
		case B_DelPin:
			if (cutSelected()) {
				deleteCutPoint();
			} else {
				deletePin(selPin_);
			}
			commit();
			break;
		case B_ResetPins:
			if (MessageBoxW(hwnd_, L"全てのピンの移動X・移動Yを 0 に戻しますか？\n（全ての中間点が対象です）", kEffectName,
							MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
				for (auto& p : doc_.poses) {
					for (auto& pp : p.pins) pp = PinPose{};
				}
				commit();
			}
			break;
		case B_AddBone:
			selCut_ = selCutPt_ = -1;
			addBone(0, 0, selBone_);
			commit();
			break;
		case B_DelBone:
			if (cutSelected()) {
				deleteCutPoint();
			} else {
				deleteBone(selBone_);
			}
			commit();
			break;
	}
	layout();
	InvalidateRect(hwnd_, nullptr, FALSE);
}

void Editor::beginTextEdit(size_t wi) {
	if (wi >= widgets_.size() || !edit_) return;
	const Widget& w = widgets_[wi];
	editingWidget_ = wi;
	wchar_t buf[64];
	std::swprintf(buf, 64, L"%.*f", kFields[w.field].decimals, getField(w.field, w.side));
	SetWindowTextW(edit_, buf);
	const RECT& r = w.rcValue;
	const int eh = (int)(20 * s_);
	MoveWindow(edit_, r.left + 2, (r.top + r.bottom - eh) / 2, r.right - r.left - 4, eh, TRUE);
	ShowWindow(edit_, SW_SHOW);
	SetFocus(edit_);
	SendMessageW(edit_, EM_SETSEL, 0, -1);
	InvalidateRect(hwnd_, nullptr, FALSE);
}

void Editor::endTextEdit(bool apply) {
	if (editingWidget_ == (size_t)-1) return;
	const size_t wi = editingWidget_;
	editingWidget_ = (size_t)-1;
	if (apply && wi < widgets_.size()) {
		wchar_t buf[64] = {};
		GetWindowTextW(edit_, buf, 64);
		wchar_t* end = nullptr;
		const double v = std::wcstod(buf, &end);
		if (end != buf) {
			const Widget& w = widgets_[wi];
			setField(w.field, w.side, v);
			commit();
		}
	}
	ShowWindow(edit_, SW_HIDE);
	if (GetFocus() == edit_) SetFocus(hwnd_);
	InvalidateRect(hwnd_, nullptr, FALSE);
}

void Editor::zoomAt(int sx, int sy, double factor) {
	double ix, iy;
	toImage(sx, sy, ix, iy);
	zoom_ = std::clamp(zoom_ * factor, 0.02, 64.0);
	double nx, ny;
	toImage(sx, sy, nx, ny);
	viewX_ += ix - nx;
	viewY_ += iy - ny;
	InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
LRESULT CALLBACK Editor::editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	auto* self = (Editor*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (self) {
		if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
			self->endTextEdit(wp == VK_RETURN);
			return 0;
		}
		if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
		if (msg == WM_KILLFOCUS) {
			LRESULT r = CallWindowProcW(self->editOrigProc_, hwnd, msg, wp, lp);
			self->endTextEdit(true);
			return r;
		}
		return CallWindowProcW(self->editOrigProc_, hwnd, msg, wp, lp);
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Editor::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	Editor* self = nullptr;
	if (msg == WM_NCCREATE) {
		self = (Editor*)((CREATESTRUCTW*)lp)->lpCreateParams;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
		self->hwnd_ = hwnd;
	} else {
		self = (Editor*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	}
	if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
	return self->handle(msg, wp, lp);
}

LRESULT Editor::handle(UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_SIZE:
			layout();
			InvalidateRect(hwnd_, nullptr, FALSE);
			return 0;
		case WM_DPICHANGED: {
			s_ = HIWORD(wp) / 96.0;
			const RECT* r = (const RECT*)lp;
			SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}
		case WM_GETMINMAXINFO: {
			auto* mm = (MINMAXINFO*)lp;
			mm->ptMinTrackSize.x = (LONG)(380 * s_);
			mm->ptMinTrackSize.y = (LONG)(520 * s_);
			return 0;
		}
		case WM_ERASEBKGND: return 1;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd_, &ps);
			paint(hdc);
			EndPaint(hwnd_, &ps);
			return 0;
		}
		case WM_CTLCOLOREDIT: {
			static HBRUSH br = CreateSolidBrush(RGB(74, 74, 74));
			SetTextColor((HDC)wp, RGB(240, 240, 240));
			SetBkColor((HDC)wp, RGB(74, 74, 74));
			return (LRESULT)br;
		}
		case WM_LBUTTONDOWN: onLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
		case WM_LBUTTONDBLCLK: onLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
		case WM_MOUSEMOVE: onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
		case WM_LBUTTONUP:
			if (drag_ != Drag::None && drag_ != Drag::Pan) onLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
			return 0;
		case WM_MBUTTONDOWN: {
			const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
			if (inRect(rcCanvas_, x, y) && drag_ == Drag::None) {
				drag_ = Drag::Pan;
				dragStartX_ = x;
				dragStartY_ = y;
				dragViewX_ = viewX_;
				dragViewY_ = viewY_;
				SetCapture(hwnd_);
			}
			return 0;
		}
		case WM_MBUTTONUP:
			if (drag_ == Drag::Pan) {
				drag_ = Drag::None;
				ReleaseCapture();
				if (pendingReload_) {
					pendingReload_ = false;
					reload();
				}
				InvalidateRect(hwnd_, nullptr, FALSE);
			}
			return 0;
		case WM_RBUTTONDOWN: {
			const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
			if (drag_ != Drag::None || !inRect(rcCanvas_, x, y) || !found_ || !loaded_) return 0;
			// 切れ込みの中間の点を右クリック : その点を消す (始点・終点では何もしない)
			int hc, hp;
			if (hitCutPoint(x, y, hc, hp)) {
				const int n = (int)doc_.cuts[hc].points.size();
				if (hp > 0 && hp + 1 < n) {
					selCut_ = hc;
					selCutPt_ = hp;
					deleteCutPoint();
					commit();
					layout();
					InvalidateRect(hwnd_, nullptr, FALSE);
				}
				return 0;
			}
			// ボーンモードで何もない場所を右クリック : 選択解除
			if (tab_ == 1 && hitBone(x, y) < 0 && hitPin(x, y) < 0) {
				double a, b;
				int c2, s2;
				if (hitSegment(x, y, a, b) < 0 && !hitCutSegment(x, y, c2, s2, a, b)) {
					selBone_ = -1;
					selCut_ = selCutPt_ = -1;
					layout();
					InvalidateRect(hwnd_, nullptr, FALSE);
				}
			}
			return 0;
		}
		case WM_CAPTURECHANGED:
			if ((HWND)lp != hwnd_ && drag_ != Drag::None) {
				const Drag d = drag_;
				drag_ = Drag::None;
				if ((d == Drag::Pin || d == Drag::Bone || d == Drag::CutPt || d == Drag::Number) && dragMoved_) commit();
			}
			return 0;
		case WM_MOUSEWHEEL: {
			POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			ScreenToClient(hwnd_, &pt);
			if (!inRect(rcCanvas_, pt.x, pt.y)) return 0;
			const int delta = GET_WHEEL_DELTA_WPARAM(wp);
			const WORD keys = GET_KEYSTATE_WPARAM(wp);
			if (keys & MK_CONTROL) {
				zoomAt(pt.x, pt.y, std::pow(1.15, delta / 120.0));
			} else if (keys & MK_SHIFT) {
				if (hScrollVisible()) viewX_ -= delta / 120.0 * 60 / zoom_;
			} else {
				if (vScrollVisible()) viewY_ -= delta / 120.0 * 60 / zoom_;
			}
			InvalidateRect(hwnd_, nullptr, FALSE);
			return 0;
		}
		case WM_MOUSEHWHEEL: {
			if (hScrollVisible()) viewX_ += GET_WHEEL_DELTA_WPARAM(wp) / 120.0 * 60 / zoom_;
			InvalidateRect(hwnd_, nullptr, FALSE);
			return 0;
		}
		case WM_PD_RELOAD:
			if (drag_ != Drag::None || editingWidget_ != (size_t)-1) {
				pendingReload_ = true;
			} else {
				reload();
			}
			if (wp) requestImage();
			return 0;
		case WM_PD_IMAGE: takeImage(); return 0;
		case WM_PD_UNDO:
			if (wp) {
				redo();
			} else {
				undo();
			}
			return 0;
		case WM_KEYDOWN:
			if ((wp == 'Z' || wp == 'Y') && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
				if (wp == 'Z') {
					undo();
				} else {
					redo();
				}
				return 0;
			}
			break;
		case WM_CLOSE: DestroyWindow(hwnd_); return 0;
		case WM_DESTROY: {
			setCaptureSlot(target_.effectId, nullptr);
			g_editors.erase(target_.effectId);
			updateKeyboardHook();
			if (memDC_) {
				SelectObject(memDC_, memOld_);
				DeleteObject(memBmp_);
				DeleteDC(memDC_);
				memDC_ = nullptr;
			}
			return 0;
		}
		case WM_NCDESTROY: {
			SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
			delete this;
			return 0;
		}
	}
	return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace

// ===========================================================================
bool registerEditorClass() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = &Editor::wndProc;
	wc.hInstance = g_hinst;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = kClassName;
	return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void openEditor(const TargetRef& target) {
	auto it = g_editors.find(target.effectId);
	if (it != g_editors.end()) {
		HWND h = it->second->hwnd();
		if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
		SetForegroundWindow(h);
		return;
	}
	auto* ed = new Editor(target);
	g_editors[target.effectId] = ed;
	if (!ed->create()) {
		g_editors.erase(target.effectId);
		delete ed;
		logWarn(L"[パペット変形] 設定ウィンドウを作成できませんでした");
	}
	updateKeyboardHook();
}

// 本体のショートカット処理より先に Ctrl+Z / Ctrl+Y を受け取るため、メインスレッドにキーボードフックを置く
HHOOK g_kbHook = nullptr;

LRESULT CALLBACK keyboardHook(int code, WPARAM wp, LPARAM lp) {
	if (code == HC_ACTION && (wp == 'Z' || wp == 'Y') && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) &&
		!(GetKeyState(VK_MENU) & 0x8000)) {
		HWND fg = GetForegroundWindow();
		for (auto& kv : g_editors) {
			if (kv.second->hwnd() != fg) continue;
			if (GetFocus() == kv.second->editControl()) break;  // 数値の入力中は入力欄の取り消し
			const bool keyDown = !(lp & 0x80000000);
			const bool repeat = (lp & 0x40000000) != 0;
			if (keyDown && !repeat) PostMessageW(fg, WM_PD_UNDO, wp == 'Y' ? 1 : 0, 0);
			return 1;  // 本体の「元に戻す」「やり直し」には渡さない
		}
	}
	return CallNextHookEx(g_kbHook, code, wp, lp);
}

void updateKeyboardHook() {
	if (!g_editors.empty() && !g_kbHook) {
		g_kbHook = SetWindowsHookExW(WH_KEYBOARD, keyboardHook, nullptr, GetCurrentThreadId());
	} else if (g_editors.empty() && g_kbHook) {
		UnhookWindowsHookEx(g_kbHook);
		g_kbHook = nullptr;
	}
}

void reloadAllEditors(bool frameChanged) {
	for (auto& kv : g_editors) PostMessageW(kv.second->hwnd(), WM_PD_RELOAD, frameChanged ? 1 : 0, 0);
}

void closeAllEditors() {
	std::vector<HWND> hs;
	for (auto& kv : g_editors) hs.push_back(kv.second->hwnd());
	for (HWND h : hs) DestroyWindow(h);
}

}  // namespace pd
