#!/usr/bin/env bash
# 在 Linux 上交叉编译 Windows exe（llvm-mingw 工具链）
# 说明：沙箱工作目录对 LLVM 工具的写入不兼容（会得到全零文件），
#       因此本脚本统一在 /tmp 下编译，再把成品复制回项目目录。
set -e
cd "$(dirname "$0")"
LLVM=./toolchain/llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64/bin
CC="$LLVM/x86_64-w64-mingw32-clang"
RC="$LLVM/x86_64-w64-mingw32-windres"
if [ ! -x "$CC" ]; then
  echo "错误：未找到 llvm-mingw 工具链" >&2
  exit 1
fi
BUILD=/tmp/ipbuild
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp main.c app.rc app.manifest icon.ico "$BUILD/"
echo "== 编译资源 (icon/manifest/version) =="
"$RC" --codepage=65001 "$BUILD/app.rc" -o "$BUILD/app_res.o"
echo "== 编译主程序 =="
"$CC" -O2 -Wall -Wextra \
  "$BUILD/main.c" "$BUILD/app_res.o" \
  -mwindows \
  -o "$BUILD/上网环境设置工具 v5.2.exe" \
  -lgdi32 -ladvapi32 -liphlpapi -lwininet -lurlmon -lshell32
echo "== 复制成品 =="
cp "$BUILD/上网环境设置工具 v5.2.exe" "./上网环境设置工具 v5.2.exe"
ls -la "./上网环境设置工具 v5.2.exe"
xxd "./上网环境设置工具 v5.2.exe" | head -1
