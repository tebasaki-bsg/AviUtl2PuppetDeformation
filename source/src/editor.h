// パペット変形 for AviUtl2 - 専用設定ウィンドウ
#pragma once
#include "host.h"

namespace pd {

// 指定のエフェクトの設定ウィンドウを開く (既に開いていれば前面に出す)
void openEditor(const TargetRef& target);
// 開いている全ウィンドウにデータの読み直しを依頼する (frameChanged なら画像も取り直す)
void reloadAllEditors(bool frameChanged);
// 全ウィンドウを閉じる (プラグイン終了時)
void closeAllEditors();
// ウィンドウクラスの登録
bool registerEditorClass();

}  // namespace pd
