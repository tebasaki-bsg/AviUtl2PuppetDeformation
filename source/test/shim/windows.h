// Linux上で host.cpp を試験するための最小限の windows.h 代替
#pragma once
#include <cstdint>
#include <cwchar>
typedef uint32_t DWORD; typedef int BOOL; typedef unsigned int UINT; typedef unsigned char BYTE;
typedef const wchar_t* LPCWSTR; typedef const char* LPCSTR; typedef void* HWND; typedef void* HINSTANCE; typedef void* LPCVOID;
typedef uintptr_t WPARAM; typedef intptr_t LPARAM; typedef intptr_t LRESULT; typedef intptr_t LONG_PTR; typedef intptr_t INT_PTR;
#define WM_APP 0x8000
#define TRUE 1
#define FALSE 0
inline BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM) { return TRUE; }
