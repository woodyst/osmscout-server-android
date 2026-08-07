#!/bin/bash
# Cross-compila libbz2 para Android arm64. Se ejecuta EN erebos3.
#
# Los mapas del servidor de rinigus vienen comprimidos con bzip2 —de ahi que su
# catalogo distinga "size" de "size-compressed"—. El Map Manager original los
# descomprime al vuelo lanzando el binario "bunzip2"
# (server/src/filedownloader.cpp:72), y Android no lo trae.
#
# Asi que se enlaza la libreria y se descomprime en el proceso. Mismo patron que
# con libcurl: lo que en el escritorio es un binario, aqui es una libreria.
#
# bzip2 no usa autotools ni CMake: es un Makefile a mano, asi que se compilan
# los objetos directamente, que ademas evita pelearse con su install.
set -euo pipefail

NDK=$HOME/android-sdk/ndk/26.1.10909125
API=28
HOST=aarch64-linux-android

SRC=$HOME/src_valhalla
PREFIX=$SRC/install-android-arm64
VERSION=${BZIP2_VERSION:-1.0.8}

TOOLS=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
CC="$TOOLS/${HOST}${API}-clang"
dir=$SRC/bzip2-$VERSION

mkdir -p "$SRC" "$PREFIX/lib" "$PREFIX/include"

if [ ! -d "$dir" ]; then
    echo "== descargando bzip2 $VERSION =="
    curl -sSL --fail -o "$SRC/bzip2.tar.gz" \
        "https://sourceware.org/pub/bzip2/bzip2-$VERSION.tar.gz"
    tar xf "$SRC/bzip2.tar.gz" -C "$SRC"
fi

cd "$dir"
echo "== compilando bzip2 para arm64 =="

# -fPIC obligatorio: la app es una libreria compartida y esto se enlaza dentro.
for f in blocksort huffman crctable randtable compress decompress bzlib; do
    "$CC" -c -O2 -fPIC -D_FILE_OFFSET_BITS=64 "$f.c" -o "$SRC/bz_$f.o"
done

"$TOOLS/llvm-ar" rcs "$PREFIX/lib/libbz2.a" "$SRC"/bz_*.o
cp bzlib.h "$PREFIX/include/"

ls -la "$PREFIX/lib/libbz2.a"
