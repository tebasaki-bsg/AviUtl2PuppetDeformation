# パペット変形 for AviUtl2（試作版 v0.4.0）

ピンとボーンで画像を変形させる、AviUtl ExEdit2 用のプラグインです。切れ込みを入れて、その両側を別々に動かすこともできます。
YMM4 用プラグイン「[パペット変形](https://github.com/routersys/YMM4-PuppetDeformation)」の動作を参考に、AviUtl2 向けに新しく作りました。

## 注意事項

パペット変形 for AviUtl2 は無保証で提供されます。  
パペット変形 for AviUtl2 を使用したこと及び使用しなかったことによるいかなる損害について、開発者は責任を負いません。

これに同意できない場合、あなたはパペット変形 for AviUtl2 を使用することができません。

## 動作環境

- Windows 10 / 11（64bit）
- AviUtl ExEdit2 **v2.1.11 以降**（v2.1.13 時点の SDK で作成）

## 導入

1. `PuppetDeformation.aux2` を `C:\ProgramData\aviutl2\Plugin\` に置きます（一つ下のフォルダでも可）。
2. 以前お渡ししたスクリプト版 `パペット変形.anm2` を使っていた場合は、名前が重なるので
   `C:\ProgramData\aviutl2\Script\` から削除してください。
3. AviUtl2 を起動し直します。
4. オブジェクトに「フィルタ効果を追加」→「変形」→「パペット変形」を追加します。

## 設定画面の項目

| 項目 | 内容 |
|---|---|
| 変形方式 | 剛体（ARAP）／なめらか（MLS）。初期値は剛体（ARAP） |
| 剛性 | なめらか（MLS）のときだけ表示。初期値 2.00 |
| プレビュー精度 | 剛体（ARAP）のときだけ表示。プレビュー再生中の計算の細かさ（0.02／0.05／0.1／0.25／0.5px、初期値 0.25px）。大きいほど軽く、粗くなります |
| 書き出し精度 | 剛体（ARAP）のときだけ表示。停止中（編集中）とファイル出力のときの計算の細かさ（同じ選択肢、初期値 0.02px） |
| ポーズ補間の数 | 表示する「ポーズ補間」の数（1〜16、初期値1）。後から増やせます |
| ポーズ補間1〜16 | 中間点ごとのポーズの間をどう補間するかを、このトラックバーの移動方法で決めます（下記） |
| 設定ウィンドウを開く | ピン・ボーンを編集する専用ウィンドウを開きます。エフェクトごとに別のウィンドウが開きます |

## 専用ウィンドウ

- 画像は、そのパペット変形に入ってくる画像（上のエフェクト適用後、自身の変形前）です。
  ピンは基準位置、ボーンはジョイント位置で表示され、再生位置を動かしても変わりません。
- ピンタブでもボーンを表示します（操作できるのはボーンタブのみ）。
- 下の設定欄は、ピン・ボーンが未選択のときやタブを切り替えたときも同じ高さを確保するので、プレビューの位置は動きません。
- **Ctrl＋Z**（ウィンドウが前面のとき）：直前の操作を1つだけ取り消します（ドラッグはドラッグを始める前の状態へ）。
  AviUtl2 本体の「元に戻す」には渡しません。本体側で変更があった場合、取り消しの対象は破棄されます。
- 右上の十字ボタン：表示を初期化（画像の上下を表示エリアに合わせる）
- 右上の円形の矢印：画像を更新
- 右上のハサミのボタン（十字の左）：切れ込みを1本追加（下記）
- Ctrl＋ホイール：拡大縮小（上で拡大）／中ボタンドラッグ・スクロールバー：表示位置の移動
- 画像の不透明な部分の輪郭を緑の細い線で表示します。グループ制御では、重ねた画像の輪郭を目安として表示します。
- 追加した直後のエフェクトには、ピン・ボーン・切れ込みがありません（v0.3.2 までの「最初から4本のピン」は無くなりました）。

| 操作 | ピンタブ | ボーンタブ |
|---|---|---|
| 何もない所を左クリック | ピンを追加 | ボーンを追加（選択中のボーンがあればその子） |
| 左クリック／ドラッグ | ピンを選択／基準位置を変更 | ボーンを選択／ジョイント位置を変更（子は動かない） |
| ボーン間の線を左クリック | — | その位置にボーンを追加（親→新しいボーン→子 の順につながる） |
| ボーン選択中にピンを左クリック | — | 紐付け／もう一度で解除 |
| 何もない所を右クリック | — | 選択解除 |

### 切れ込み

紙に切れ込みを入れるように折れ線を引くと、その線の両側が分かれて別々に動きます。剛体（ARAP）・なめらか（MLS）のどちらでも効きます。

- ハサミのボタンで、画像の中央に切れ込みを1本追加します（端は中心から左右に画像の幅の 1/4 の位置）。追加した切れ込みの始点が選ばれます。
- 切れ込みは、ピンタブ・ボーンタブのどちらでも操作できます。切れ込みの点と線は、ピン・ボーンの操作より優先されます。
- 端の表示：輪郭の外にある端（開いた端）は四角、輪郭の内側にある端（閉じた端）は丸です。切れ込みは閉じた端で止まり、その先はつながったままです。
  両端とも輪郭の内側なら、穴のように開きます。
- 線にカーソルを合わせると、点が増える位置に薄い赤の丸が出ます。
- 選んだ点には赤い輪が付き、下の設定欄が「有効」と、その点の X・Y（見出しは「始点」「中間点 n」「終点」）に替わります。ピン・ボーンを選ぶと元に戻ります。
- 切れ込みの値（有効・X・Y）は中間点ごとには変わりません。無効にした切れ込みは半透明で表示され、変形には効きません。
- 切れ込みの線上にあるピン（ボーンの線上の制御点も含む）は、始点から終点を見て右側に付きます。
- ピンもボーンも無い、完全に切り離された部分は、近くのピンに合わせて形を保ったまま動きます。

| 操作 | 内容 |
|---|---|
| 点を左クリック／ドラッグ | 選択／位置を変更（0.1px 単位） |
| 線（6px 以内）を左クリック | その位置に点を増やして選択（そのままドラッグ可）。1本あたり 32 点まで（端を含む） |
| 中間点を右クリック | その点を消す |
| 中間点を選んで「−」 | その点を消す |
| 始点・終点を選んで「−」 | 切れ込みごと消す（始点・終点の右クリックでは何もしません） |

数値の欄は、横にドラッグで変更（Shift で細かく、Ctrl で大きく）、クリックで直接入力、−／＋ で1段階ずつ変更できます。
「移動X/Y」「角度」「伸縮」「揺れ」の各項目（揺れ位相を除く）は、再生位置を含む区間の **左の点の値** と **右の点の値** を並べて表示します。
「揺れ位相」は揺れの初期位相なので、基準X/Y・ジョイントX/Y と同じく全区間で共通の値（1つだけ）です。
その右の「補間」で、その区間（左の点〜右の点）にどの「ポーズ補間」を使うかを、ピン・ボーンごと、項目ごとに選べます（区間ごとに別々に選べます）。

## 中間点とポーズ補間

- 開始点・中間点・終了点ごとに、移動X/Y、角度、伸縮、揺れの値（ポーズ）を持ちます（揺れ位相は除く）。
- 区間の中では、左の点のポーズから右の点のポーズへ、各項目で選んだ「ポーズ補間」に従って補間されます（初期値はポーズ補間1）。
- 「ポーズ補間」に移動方法を設定しない場合は、時間に比例して直線的に補間されます。
- 「ポーズ補間」に移動方法（加減速など）を設定すると、そのイージングで補間されます。
  このとき各点の値は 0, 1, 2, … のように**異なる値**である必要があります。
  値が同じになっている場合は、専用ウィンドウで何か編集したときに自動で 0, 1, 2, … に振り直します。
- 「ポーズ補間の数」を減らして非表示になった補間を選んでいる項目は、そのまま非表示の補間で動きます（ウィンドウでは灰色表示）。
- v0.1.x の「ポーズ補間」は「ポーズ補間1」に名前が変わったため、以前の移動方法の設定は引き継がれません（ピン・ボーンのデータは引き継がれます）。

## グループ制御で使う

グループ制御に「パペット変形」を追加すると、配下のオブジェクト全体を1枚の絵として変形します。

- ピン・ボーンの座標は、グループ制御の座標（グループ制御の位置が原点）になります。
- 配下の各オブジェクトの X・Y・中心X/Y・Z軸回転・拡大率から、グループの中での位置を求め、同じ変形を当てはめます。
- 専用ウィンドウには、配下のオブジェクトをすべて重ねて表示します。
- 配下の各オブジェクトの不透明な部分を記録し、それらを合わせた輪郭に沿って、グループ全体の三角形の網目を作ります
  （輪郭は長い辺 1024px まで縮めて取ります）。1枚の画像のときと同じく、透明な隙間で隔てられた部分は別々に動きます。
- 配下の各オブジェクトは、それぞれの格子の網目で描き、グループ全体の網目の変形を読み取って当てはめます。
- 配下のオブジェクトが動いたり形が変わったりするフレームでは、網目の作り直しのため処理が重くなります
  （パペット変形自体の動きでは作り直しません）。
- 新しく配下に入ったオブジェクトは、最初に描かれたフレームでは網目に含まれないことがあります（次の描画で正しくなります）。
- X軸・Y軸回転（3D の傾き）は考慮しません。
- **グループ制御では切れ込みは効きません**（専用ウィンドウにその旨を表示します）。

## 揺れの計算

- 揺れているボーン：揺れ角度 × sin(2π × 時間 ÷ 揺れ周期 ＋ 揺れ位相)
- 揺れ位相は初期位相で、中間点ごとには変わりません（v0.3.1 以前のデータは開始点の値が使われます）
- k 世代下の子孫：揺れ角度 × 揺れ伝播^k × sin(… − k × しなり)

## v0.4.0 の変更点

- 切れ込み（折れ線）を追加しました。操作と表示は YMM4 用「パペット変形改」v0.2.0 と同じです。
- 1枚の画像のときの網目を、格子から「輪郭と切れ込みに沿った三角形の網目」に変えました。
- グループ制御の変形の網目も、配下を合わせた輪郭に沿った三角形の網目に変えました（切れ込みは入れません）。
  網目の作り直し（配下が動いたり形が変わったりしたフレーム）は格子より重くなります。
  シーン全体ほどの範囲（2100×1200、手3つと楕円）で約 38ms（格子では約 4ms）です。
- なめらか（MLS）の重みを、網目に沿った距離で計算するようにしました。切れ込みや透明な隙間をまたいで引っ張られません。
- 剛体（ARAP）の計算の細かさを「プレビュー精度」「書き出し精度」で選べるようにしました。
- 連立方程式の解き方を速くしました（頂点の並べ替え（RCM）＋スカイライン形式のコレスキー分解）。
- 追加した直後のエフェクトに、最初から4本のピンを置かないようにしました。
- 保存の形式：切れ込みがあるときだけ、データの末尾に切れ込みを足します（版の番号は 3 のまま）。
  v0.3.2 のデータはそのまま読めます。切れ込みの無いデータは v0.3.2 と同じ形式なので、v0.3.2 でも読めます。
  切れ込みのあるデータを v0.3.2 で開くと、切れ込みを無視してピン・ボーンだけを読みます。
- 手の画像（560×640）での1フレームの計算時間（この環境での計測、描画は含みません）：
  剛体 0.02px 約 3.3ms、0.25px 約 2.2ms、なめらか 約 0.4ms。網目を作る最初の1回は約 40ms です。

## v0.3.2 の変更点

- 揺れ位相を、中間点ごとの値から全区間で共通の値（初期位相）に変更し、補間の選択を削除しました。
- 処理を軽くしました（変形の結果は変わりません）。
  - 剛体（ARAP）の計算を、画像の不透明な部分だけで行うようにしました。
  - 反復計算を加速し（Anderson 加速）、反復の回数を平均54回から15回程度に減らしました。精度は以前より上がっています。
  - 同じフレームを描き直すときなど、入力が前回と同じ場合は計算を省きます。
  - 「ポーズ補間」の値は、現在の区間で使っているものだけを本体に問い合わせます。
  - グループ制御で、配下が動いていなければフレームが変わっても変形の場を作り直さないようにしました。
  - 手の画像（560×640）での1フレームの計算時間：約40ms → 約7ms

## 試作版の確認項目（お手数ですが実機でご確認ください）

v0.3.2 までの確認項目 1〜13 は確認済みです。v0.4.0 で変わった点を確認してください。
うまくいかなかった項目の番号と、ログの「[パペット変形]」を含む行を教えてください。

1. 起動後のログに「[パペット変形] 読み込みました」が出て、設定画面に「プレビュー精度」「書き出し精度」が出るか。
   変形方式を なめらか（MLS） にすると2つとも消え、剛性 が出るか。
2. 新しく追加したエフェクトで、ピンが1本も無い状態から始まるか。v0.3.2 で作ったプロジェクトを開くと、ピン・ボーンが元のまま読めるか。
3. 専用ウィンドウに、画像の輪郭が緑の線で出るか。右上の十字の左にハサミのボタンが出るか。
4. ハサミのボタンで切れ込みが中央に1本出て、始点が選ばれ、下の欄が「有効」「X」「Y」に替わるか。
5. 切れ込みの端を画像の外までドラッグしたとき、丸から四角に変わるか。端を外に出してピンを動かすと、切れ込みが開くか（剛体・なめらかの両方）。
6. 線をクリックして点が増えるか。中間点の右クリック・「−」で点が消えるか。始点・終点を選んで「−」で切れ込みごと消えるか。
7. Ctrl+Z で、切れ込みの追加・移動・削除が元に戻るか。
8. 切れ込みの「有効」を外すと、半透明になり変形に効かなくなるか。
9. 切れ込みの切り口に、反対側の色のにじみや、細い隙間・線が出ないか。
10. 再生中（プレビュー精度）と停止中（書き出し精度）で、見た目が大きく変わらないか。重さがどうか。
11. グループ制御に付けたとき、ウィンドウに「グループ制御では切れ込みは効きません」と、重ねた画像の輪郭（緑の線）が出るか。
    配下全体が1枚の絵のようにつながって変形するか（オブジェクトの境目でずれないか）。配下を動かしたときの重さがどうか。
12. 切れ込みを入れたまま保存し、開き直しても切れ込みが残っているか。

## 試作版での制限

- ウィンドウでのドラッグや数値の変更は、マウスを離したときに本体へ保存され、プレビューに反映されます（1回の操作が1回の「元に戻す」になります）。
- ARAP は反復計算（最大60回、通常は15回前後）で近似しているため、YMM4 版と細部の変形が異なる場合があります。
- 変形の網目（メッシュ）は画像の不透明な部分だけに作ります。透明な隙間で隔てられた部分は別々に動きます。
- 網目の三角形は最大 5440 個（目安 3000 個）です。大きい画像では輪郭を長い辺 2048px まで縮めて取ります。
- 切れ込みの近くで、ピンの無い「ひらひらした部分」は剛体（ARAP）の反復が収束しにくく、
  プレビュー精度 0.25px では書き出し精度 0.02px と比べて最大 15px 程度ずれることがあります（全体の平均のずれは小さい）。
- 上限：ピン 256本、ボーン 256本、切れ込み 64本（1本あたり 32 点）、ポーズ補間 16個。保存データの上限は 128KB（ピン・ボーンが最大のとき、中間点はおよそ 8 個まで）。

## 技術情報

- 変形：MLS 剛体変形（Schaefer et al. 2006、網目に沿った距離（Dijkstra）で重み付け）、ARAP（Igarashi et al. 2005 の2段階法 ＋ 局所・大域交互法（cot の重み）、Anderson 加速（Peng et al. 2018））
- 網目：α から輪郭を取り（Douglas–Peucker で単純化）、輪郭と切れ込みを辺に持つ Delaunay 三角形分割（Bowyer–Watson）。切れ込みの両側は頂点を分けます
- 連立方程式：RCM（Reverse Cuthill–McKee）で頂点を並べ替え、スカイライン形式のコレスキー分解で解きます
- 描画：変形した網目を AviUtl2 の `draw_poly_to_resource()` で描画（GPU）
- データ：設定画面に表示されない汎用データ項目（PuppetData0〜7）に保存
- AviUtl2 SDK：ＫＥＮくん作（`source/sdk/` に同梱。ライセンスは下記 Credits を参照）
- ビルド：`build.sh`（Zig の clang で Windows x64 向けにクロスコンパイル）

## ライセンス

パペット変形 for AviUtl2 は MIT License で公開しています。詳しくは同梱の `LICENSE` を参照してください。  
同梱・使用している他のソフトウェアのライセンスは、下記 Credits を参照してください。

## Credits

パペット変形 for AviUtl2 は、以下のソフトウェアを使用・参考にして作られています。

### [AviUtl ExEdit2 Plugin SDK](https://spring-fragrance.mints.ne.jp/aviutl/)

> [!NOTE]
> SDK のヘッダーファイルを未改変のまま `source/sdk/` に同梱し、ビルドに使用しています。

<details>
<summary>The MIT License</summary>

```
The MIT License

Copyright (c) 2025 Kenkun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
</details>

### [YMM4-PuppetDeformation](https://github.com/routersys/YMM4-PuppetDeformation)

> [!NOTE]
> 本プラグインは、YMM4 用プラグイン「パペット変形」の仕様と動作（ピンによる MLS 剛体変形、初期のピンの配置、剛性の範囲と初期値、ピンの上限など）を参考に、AviUtl2 向けに C++ で新しく実装したものです。

<details>
<summary>The MIT License</summary>

```
MIT License

Copyright (c) 2026 routersys

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
</details>

### [Mingw-w64](https://github.com/mingw-w64/mingw-w64)

> [!NOTE]
> プラグイン本体（`PuppetDeformation.aux2`）は、ビルド時に MinGW-w64 のランタイムの一部が静的にリンクされています。

<details>
<summary>MinGW-w64 runtime licensing</summary>

```
MinGW-w64 runtime licensing
***************************

This program or library was built using MinGW-w64 and statically
linked against the MinGW-w64 runtime. Some parts of the runtime
are under licenses which require that the copyright and license
notices are included when distributing the code in binary form.
These notices are listed below.


========================
Overall copyright notice
========================

Copyright (c) 2009, 2010, 2011, 2012, 2013 by the mingw-w64 project

This license has been certified as open source. It has also been designated
as GPL compatible by the Free Software Foundation (FSF).

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

   1. Redistributions in source code must retain the accompanying copyright
      notice, this list of conditions, and the following disclaimer.
   2. Redistributions in binary form must reproduce the accompanying
      copyright notice, this list of conditions, and the following disclaimer
      in the documentation and/or other materials provided with the
      distribution.
   3. Names of the copyright holders must not be used to endorse or promote
      products derived from this software without prior written permission
      from the copyright holders.
   4. The right to distribute this software or to use it for any purpose does
      not give you the right to use Servicemarks (sm) or Trademarks (tm) of
      the copyright holders.  Use of them is covered by separate agreement
      with the copyright holders.
   5. If any files are modified, you must cause the modified files to carry
      prominent notices stating that you changed the files and the date of
      any change.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESSED
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

======================================== 
getopt, getopt_long, and getop_long_only
======================================== 

Copyright (c) 2002 Todd C. Miller <Todd.Miller@courtesan.com> 
 
Permission to use, copy, modify, and distribute this software for any 
purpose with or without fee is hereby granted, provided that the above 
copyright notice and this permission notice appear in all copies. 
 	 
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

Sponsored in part by the Defense Advanced Research Projects
Agency (DARPA) and Air Force Research Laboratory, Air Force
Materiel Command, USAF, under agreement number F39502-99-1-0512.

        *       *       *       *       *       *       * 

Copyright (c) 2000 The NetBSD Foundation, Inc.
All rights reserved.

This code is derived from software contributed to The NetBSD Foundation
by Dieter Baron and Thomas Klausner.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


===============================================================
gdtoa: Converting between IEEE floating point numbers and ASCII
===============================================================

The author of this software is David M. Gay.

Copyright (C) 1997, 1998, 1999, 2000, 2001 by Lucent Technologies
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.

        *       *       *       *       *       *       *

The author of this software is David M. Gay.

Copyright (C) 2005 by David M. Gay
All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that the copyright notice and this permission notice and warranty
disclaimer appear in supporting documentation, and that the name of
the author or any of his current or former employers not be used in
advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN
NO EVENT SHALL THE AUTHOR OR ANY OF HIS CURRENT OR FORMER EMPLOYERS BE
LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

        *       *       *       *       *       *       *

The author of this software is David M. Gay.

Copyright (C) 2004 by David M. Gay.
All Rights Reserved
Based on material in the rest of /netlib/fp/gdota.tar.gz,
which is copyright (C) 1998, 2000 by Lucent Technologies.

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.


=========================
Parts of the math library
=========================

Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.

Developed at SunSoft, a Sun Microsystems, Inc. business.
Permission to use, copy, modify, and distribute this
software is freely granted, provided that this notice
is preserved.

        *       *       *       *       *       *       *

Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.

Developed at SunPro, a Sun Microsystems, Inc. business.
Permission to use, copy, modify, and distribute this
software is freely granted, provided that this notice
is preserved.

        *       *       *       *       *       *       *

FIXME: Cephes math lib
Copyright (C) 1984-1998 Stephen L. Moshier

It sounds vague, but as to be found at
<http://lists.debian.org/debian-legal/2004/12/msg00295.html>, it gives an
impression that the author could be willing to give an explicit
permission to distribute those files e.g. under a BSD style license. So
probably there is no problem here, although it could be good to get a
permission from the author and then add a license into the Cephes files
in MinGW runtime. At least on follow-up it is marked that debian sees the
version a-like BSD one. As MinGW.org (where those cephes parts are coming
from) distributes them now over 6 years, it should be fine.

===================================
Headers and IDLs imported from Wine
===================================

Some header and IDL files were imported from the Wine project. These files
are prominent maked in source. Their copyright belongs to contributors and
they are distributed under LGPL license.

Disclaimer

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
```
```
</details>
