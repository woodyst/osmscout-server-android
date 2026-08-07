#!/bin/bash
# Cross-compila las dependencias del geocoder para Android arm64.
# Se ejecuta EN erebos3.
#
#   marisa-trie    el trie de los nombres normalizados (geonlp-normalized.trie)
#   kyotocabinet   la base de identificadores (geonlp-normalized-id.kch)
#   libpostal      normalizacion y troceado de direcciones
#
# OJO CON LAS VERSIONES. Los datos que descarga el Map Manager vienen generados
# con unas versiones concretas y declaran "version 6"; el trie de marisa y la
# base de kyotocabinet son formatos binarios, no texto. Si algo deja de cargar
# tras cambiar una version, ese es el primer sitio donde mirar.
#
# Los tres van con autotools, asi que no vale el patron de CMake del resto: se
# les pasa --host y las herramientas del NDK a mano.
set -euo pipefail

NDK=$HOME/android-sdk/ndk/26.1.10909125
API=28
HOST=aarch64-linux-android

SRC=$HOME/src_valhalla
PREFIX=$SRC/install-android-arm64
JOBS=${JOBS:-8}

TOOLS=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
export CC="$TOOLS/${HOST}${API}-clang"
export CXX="$TOOLS/${HOST}${API}-clang++"
export AR="$TOOLS/llvm-ar"
export RANLIB="$TOOLS/llvm-ranlib"
export STRIP="$TOOLS/llvm-strip"
export CPPFLAGS="-I$PREFIX/include"
export LDFLAGS="-L$PREFIX/lib"
# -fPIC en TODO: la app de Android es una libreria compartida, asi que cualquier
# .a que se enlace dentro tiene que ser codigo independiente de posicion. Sin
# esto el enlazado falla al final con "relocation R_AARCH64_... cannot be used
# against symbol 'vtable for marisa::Exception'", que no dice de donde viene.
export CFLAGS="-O2 -fPIC"
export CXXFLAGS="-O2 -fPIC"

mkdir -p "$SRC" "$PREFIX"

fetch_git() {
    local url=$1 dir=$2 ref=${3:-}
    [ -d "$dir" ] && return 0
    git clone -q "$url" "$dir"
    [ -n "$ref" ] && (cd "$dir" && git checkout -q "$ref")
    return 0
}

# ── marisa-trie ─────────────────────────────────────────────────────────────
build_marisa() {
    local dir=$SRC/marisa-trie
    fetch_git https://github.com/s-yata/marisa-trie "$dir" v0.2.6
    cd "$dir"
    [ -f configure ] && : || autoreconf -fi >/dev/null 2>&1
    echo "== marisa =="
    ./configure --host="$HOST" --prefix="$PREFIX" \
        --enable-static --disable-shared >/dev/null
    make -j"$JOBS" >/dev/null
    make install >/dev/null
    ls -la "$PREFIX/lib/libmarisa.a"
}

# ── kyotocabinet ────────────────────────────────────────────────────────────
build_kyotocabinet() {
    local ver=1.2.80
    local dir=$SRC/kyotocabinet-$ver
    if [ ! -d "$dir" ]; then
        curl -sSL --fail -o "$SRC/kc.tar.gz" \
            "https://dbmx.net/kyotocabinet/pkg/kyotocabinet-$ver.tar.gz"
        tar xf "$SRC/kc.tar.gz" -C "$SRC"
    fi
    cd "$dir"
    echo "== kyotocabinet =="
    # Su configure detecta la arquitectura del ANFITRION y mete -mtune y
    # atomicos de x86; con --host no siempre lo evita, asi que se le fuerzan
    # los CXXFLAGS. -Wno-* porque es C++ de 2012 y clang moderno protesta por
    # cosas que no son errores reales aqui.
    CXXFLAGS="$CXXFLAGS -Wno-deprecated-declarations -Wno-unused-but-set-variable" \
    ./configure --host="$HOST" --prefix="$PREFIX" \
        --enable-static --disable-shared \
        --disable-lzo --disable-lzma >/dev/null
    make -j"$JOBS" >/dev/null
    make install >/dev/null
    ls -la "$PREFIX/lib/libkyotocabinet.a"
}

# ── libpostal ───────────────────────────────────────────────────────────────
build_libpostal() {
    local dir=$SRC/libpostal
    fetch_git https://github.com/openvenues/libpostal "$dir"
    cd "$dir"
    [ -f configure ] || ./bootstrap.sh >/dev/null
    echo "== libpostal =="
    # --disable-data-download: los modelos ya estan descargados por el Map
    #   Manager; bajarlos aqui serian cientos de MB y ademas no cabrian en el
    #   directorio de compilacion cruzada.
    # --disable-sse2: es x86; en ARM hay que quitarlo o ni configura.
    ./configure --host="$HOST" --prefix="$PREFIX" \
        --enable-static --disable-shared \
        --disable-data-download --disable-sse2 \
        --datadir="$PREFIX/share" >/dev/null
    make -j"$JOBS" >/dev/null
    make install >/dev/null
    ls -la "$PREFIX/lib/libpostal.a"
}

case "${1:-all}" in
    marisa)       build_marisa ;;
    kyoto)        build_kyotocabinet ;;
    postal)       build_libpostal ;;
    all)          build_marisa; build_kyotocabinet; build_libpostal ;;
    *) echo "uso: $0 [marisa|kyoto|postal|all]" >&2; exit 2 ;;
esac
