// パペット変形 for AviUtl2 - 本体(AviUtl2)とのやり取り
#pragma once
#include <windows.h>

#include <atomic>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deform.h"
#include "model.h"
#include "sdk.h"

namespace pd {

// エフェクト名・設定項目名 (エイリアスファイルのキー名にもなります)
extern const wchar_t* const kEffectName;     // L"パペット変形"
extern const wchar_t* const kItemMethod;     // L"変形方式"
extern const wchar_t* const kItemStiffness;  // L"剛性"
extern const wchar_t* const kItemPreviewPrecision;  // L"プレビュー精度"
extern const wchar_t* const kItemExportPrecision;   // L"書き出し精度"
extern const wchar_t* const kItemPoseCount;  // L"ポーズ補間の数"
extern const wchar_t* const kItemPoseTracks[kPoseTracks];  // L"ポーズ補間1" 〜 L"ポーズ補間16"
extern const wchar_t* const kItemButton;     // L"設定ウィンドウを開く"
extern const wchar_t* const kDataNames[kChunkCount];

// 本体から受け取ったハンドル
extern EDIT_HANDLE* g_edit;
extern LOG_HANDLE* g_logger;
extern HINSTANCE g_hinst;
extern std::atomic<int> g_editFrame;  // 現在の編集フレーム (シーン基準)

void logInfo(const std::wstring& s);
void logWarn(const std::wstring& s);

// 編集対象の識別 (アプリ起動中は固有のID)
struct TargetRef {
	int64_t objectId = 0;
	int64_t effectId = 0;
};

// 編集セクション内で、IDからオブジェクトとエフェクトのハンドルを探す
// layerMax : 探すレイヤーの上限 (EDIT_INFO::layer_max)
bool findTarget(EDIT_SECTION* e, const TargetRef& t, int layerMax, OBJECT_HANDLE& obj, EFFECT_HANDLE& eff);

// エフェクトの汎用データ項目から読み込む / 書き込む
bool readDoc(EDIT_SECTION* e, EFFECT_HANDLE eff, Doc& doc);
bool writeDoc(EDIT_SECTION* e, EFFECT_HANDLE eff, const Doc& doc);
// 保存用データの連結・分割
bool docFromChunks(const std::vector<std::pair<const void*, int>>& chunks, Doc& doc);

// 区間の情報
struct SectionInfo {
	std::vector<int> pointFrames;  // 開始点・中間点・終了点 (オブジェクト先頭からの相対フレーム)
	int section = 0;               // 現在の区間 (左の点 = section, 右の点 = section+1)
	int relFrame = 0;              // オブジェクト先頭からの現在フレーム
	double fracs[kPoseTracks] = {};  // ポーズ補間ごとの、左の点から右の点への補間割合
	int layer = 0;                 // オブジェクトのレイヤー (0から)
	int frameStart = 0;            // オブジェクトの開始フレーム (シーン基準)
};

// sceneFrame : 現在のフレーム (シーン基準)
// trackNow : 現在の各「ポーズ補間」の値 (kPoseTracks 個。フィルタ処理中のみ。不明なら nullptr)
bool getSectionInfo(EDIT_SECTION* e, OBJECT_HANDLE obj, EFFECT_HANDLE eff, int sceneFrame, const double* trackNow, SectionInfo& out);
// 「ポーズ補間」ごとの補間割合 (sec.fracs) を求める。trackMask のビットが立っている補間だけを本体に問い合わせる
void fillTrackFracs(EDIT_SECTION* e, EFFECT_HANDLE eff, const double* trackNow, uint32_t trackMask, SectionInfo& sec);

// 「ポーズ補間の数」の設定値 (1〜kPoseTracks)
int getPoseTrackCount(EDIT_SECTION* e, EFFECT_HANDLE eff);

// 各「ポーズ補間」で、点の値が同じになっていて移動方法が設定されている場合に、0,1,2,… を振り直す
void renumberPoseTrack(EDIT_SECTION* e, EFFECT_HANDLE eff, int pointCount);

// ---- 専用ウィンドウへの入力画像の受け渡し ----
// フィルタ処理で、エフェクトに入ってきた画像(自身の変形前)を写し取ってウィンドウへ渡す
// グループ制御の場合は、配下のオブジェクトごとに画像と位置を受け取り、ウィンドウで重ねて表示する
struct CapturedImage {
	std::vector<PIXEL_RGBA> pixels;
	int w = 0, h = 0;
	Xform2D xf;      // 画像座標 → ピン・ボーンの座標 (自身のオブジェクトなら変換なし)
	int frame = 0;   // 写し取ったフレーム (シーン基準)
	int layer = 0;   // 描画対象のレイヤー (重ねる順)
};
struct CaptureSlot {
	std::mutex m;
	std::map<int64_t, CapturedImage> images;  // 描画対象のオブジェクトID → 画像
	int latestFrame = -1;
	uint64_t serial = 0;
	HWND notify = nullptr;                     // 画像が届いたら知らせるウィンドウ
	int64_t objectId = 0;                      // 編集対象のオブジェクト (グループ制御の場合はグループ制御のオブジェクト)
	std::atomic<int> requestedFrame{ -1 };     // このフレームの描画では必ず写し取る
};
void setCaptureSlot(int64_t effectId, std::shared_ptr<CaptureSlot> slot);  // nullptr で解除
std::shared_ptr<CaptureSlot> getCaptureSlot(int64_t effectId);
// 編集対象のオブジェクトIDから探す (グループ制御に付けたパペット変形が、配下のオブジェクトの処理として呼ばれた場合用)
std::shared_ptr<CaptureSlot> getCaptureSlotByObject(int64_t objectId);

// ウィンドウメッセージ
constexpr UINT WM_PD_OPEN = WM_APP + 0x101;     // lParam = TargetRef* (メッセージ用ウィンドウ宛て)
constexpr UINT WM_PD_EVENT = WM_APP + 0x102;    // wParam = EVENT_TYPE (メッセージ用ウィンドウ宛て)
constexpr UINT WM_PD_RELOAD = WM_APP + 0x103;   // 編集ウィンドウ宛て : データを読み直す
constexpr UINT WM_PD_IMAGE = WM_APP + 0x104;    // 編集ウィンドウ宛て : 画像が届いた
constexpr UINT WM_PD_UNDO = WM_APP + 0x105;     // 編集ウィンドウ宛て : 直前の操作を取り消す
extern HWND g_msgWnd;

// エフェクトが同じオブジェクト内で何番目のパペット変形か (1から)
int effectOrdinal(EDIT_SECTION* e, OBJECT_HANDLE obj, EFFECT_HANDLE eff);

}  // namespace pd
