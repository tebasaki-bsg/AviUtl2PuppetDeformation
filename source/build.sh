#!/bin/sh
# Windows (x64) 向けに PuppetDeformation.aux2 をビルドする (Zig の clang を使用)
set -e
cd "$(dirname "$0")"
mkdir -p build
python3 -m ziglang c++ -target x86_64-windows-gnu -shared -O2 -std=c++20 \
  -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN_DISABLED \
  -Wall -Wno-unused-parameter -Wno-missing-field-initializers \
  src/model.cpp src/deform.cpp src/arap.cpp src/mesh.cpp src/host.cpp src/filter.cpp src/editor.cpp src/plugin.cpp \
  -o build/PuppetDeformation.dll \
  -lgdiplus -luser32 -lgdi32 -lmsimg32 -s
rm -f build/PuppetDeformation.lib build/PuppetDeformation.pdb
mv -f build/PuppetDeformation.dll build/PuppetDeformation.aux2
echo "built: build/PuppetDeformation.aux2"
