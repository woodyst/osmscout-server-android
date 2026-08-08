#!/bin/bash
# Cross-compila libmicrohttpd para Android arm64. Necesita una maquina con holgura: no es un build de un rato.
#
# Es el servidor HTTP que usa OSM Scout Server (server/src/uhttp/), no Qt. Se
# compila con autotools, asi que aqui no vale el patron de CMake del resto: se
# le pasa --host y las herramientas del NDK a mano.
#
# Va estatica: es una libreria pequena y asi no hay que empaquetar otra .so ni
# depender del orden de carga en el APK.
set -euo pipefail

NDK=$HOME/android-sdk/ndk/26.1.10909125
API=28
HOST=aarch64-linux-android

SRC=$HOME/src_valhalla
PREFIX=$SRC/install-android-arm64
VERSION=${MHD_VERSION:-0.9.77}
JOBS=${JOBS:-8}

TOOLS=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
dir=$SRC/libmicrohttpd-$VERSION

mkdir -p "$SRC" "$PREFIX"

if [ ! -d "$dir" ]; then
    echo "== descargando libmicrohttpd $VERSION =="
    curl -sSL --fail -o "$SRC/libmicrohttpd-$VERSION.tar.gz" \
        "https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-$VERSION.tar.gz"
    tar xf "$SRC/libmicrohttpd-$VERSION.tar.gz" -C "$SRC"
fi

cd "$dir"

# --disable-https: TLS traeria gnutls, y aqui todo el trafico es loopback entre
#                  dos apps del mismo dispositivo; no hay nada que cifrar.
# --disable-doc/examples/curl: no aportan nada y curl no existe en Android.
echo "== configurando =="
./configure \
    --host="$HOST" \
    --prefix="$PREFIX" \
    --enable-static --disable-shared \
    --disable-https --disable-doc --disable-examples --disable-curl \
    CC="$TOOLS/${HOST}${API}-clang" \
    AR="$TOOLS/llvm-ar" \
    RANLIB="$TOOLS/llvm-ranlib" \
    STRIP="$TOOLS/llvm-strip" \
    >/dev/null

echo "== compilando =="
make -j"$JOBS" >/dev/null
make install >/dev/null

ls -la "$PREFIX/lib/libmicrohttpd.a"
