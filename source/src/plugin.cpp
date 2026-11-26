// パペット変形 for AviUtl2 - プラグインの入口 (汎用プラグイン .aux2)
#include <objidl.h>
#include <gdiplus.h>

#include "editor.h"
#include "host.h"

namespace pd {
FILTER_PLUGIN_TABLE* filterTable();
}

using namespace pd;

namespace {

COMMON_PLUGIN_TABLE g_commonTable = {
	L"パペット変形",
	L"パペット変形 for AviUtl2 v1.0.1 - ピンとボーンで画像を変形",
};

ULONG_PTR g_gdiplusToken = 0;
DWORD g_hostVersion = 0;

// 本体のイベントは別スレッドから届くので、メインスレッドへ回して処理する
void onEvent(void* param) {
	if (g_msgWnd) PostMessageW(g_msgWnd, WM_PD_EVENT, (WPARAM)(INT_PTR)param, 0);
}

LRESULT CALLBACK msgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_PD_OPEN: {
			auto* ref = (TargetRef*)lp;
			if (ref) {
				openEditor(*ref);
				delete ref;
			}
			return 0;
		}
		case WM_PD_EVENT: {
			const auto type = (EVENT_TYPE)(int)wp;
			bool frameChanged = false;
			if (type == EVENT_TYPE::CHANGE_EDIT_FRAME || type == EVENT_TYPE::CHANGE_EDIT_SCENE) {
				if (g_edit) {
					EDIT_INFO info{};
					g_edit->get_edit_info(&info, sizeof(info));
					g_editFrame = info.frame;
				}
				frameChanged = true;
			}
			reloadAllEditors(frameChanged);
			return 0;
		}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_hinst = hinst;
		DisableThreadLibraryCalls(hinst);
	}
	return TRUE;
}

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) { return &g_commonTable; }

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) { g_logger = logger; }

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	g_hostVersion = version;
	return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
	closeAllEditors();
	if (g_msgWnd) {
		DestroyWindow(g_msgWnd);
		g_msgWnd = nullptr;
	}
	if (g_gdiplusToken) {
		Gdiplus::GdiplusShutdown(g_gdiplusToken);
		g_gdiplusToken = 0;
	}
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	Gdiplus::GdiplusStartupInput gsi;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);

	host->register_filter_plugin(filterTable());
	g_edit = host->create_edit_handle();

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = msgWndProc;
	wc.hInstance = g_hinst;
	wc.lpszClassName = L"PuppetDeformationMessageWindow";
	RegisterClassExW(&wc);
	g_msgWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hinst, nullptr);
	registerEditorClass();

	host->register_event_listener(EVENT_TYPE::UPDATE_OBJECT, (void*)(INT_PTR)EVENT_TYPE::UPDATE_OBJECT, onEvent);
	host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_FRAME, (void*)(INT_PTR)EVENT_TYPE::CHANGE_EDIT_FRAME, onEvent);
	host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_SCENE, (void*)(INT_PTR)EVENT_TYPE::CHANGE_EDIT_SCENE, onEvent);

	wchar_t buf[128];
	swprintf(buf, 128, L"[パペット変形] 読み込みました (本体バージョン番号 %lu)", (unsigned long)g_hostVersion);
	logInfo(buf);
}
