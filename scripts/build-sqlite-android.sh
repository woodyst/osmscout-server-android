#!/bin/bash
# Cross-compila SQLite para Android arm64. Necesita una maquina con holgura: no es un build de un rato.
#
# Android trae libsqlite en /system, pero el NDK NO la expone a las apps: no hay
# cabeceras ni .so contra la que enlazar, y usar la del sistema por dlopen seria
# depender de una version que cambia con cada fabricante. Se compila la
# amalgamacion, que es un unico .c y no tiene dependencias.
#
# La usan mapboxglengine (tiles vectoriales, ficheros .sqlite del Map Manager) y
# mas adelante el geocoder.
set -euo pipefail

NDK=$HOME/android-sdk/ndk/26.1.10909125
API=28
HOST=aarch64-linux-android

SRC=$HOME/src_valhalla
PREFIX=$SRC/install-android-arm64
# Version de la amalgamacion: 3.46.1 (sqlite-amalgamation-3460100).
VERSION=${SQLITE_VERSION:-3460100}
YEAR=${SQLITE_YEAR:-2024}

TOOLS=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
dir=$SRC/sqlite-amalgamation-$VERSION

mkdir -p "$SRC" "$PREFIX/lib" "$PREFIX/include"

if [ ! -d "$dir" ]; then
    echo "== descargando sqlite $VERSION =="
    curl -sSL --fail -o "$SRC/sqlite.zip" \
        "https://www.sqlite.org/$YEAR/sqlite-amalgamation-$VERSION.zip"
    unzip -q -o "$SRC/sqlite.zip" -d "$SRC"
fi

echo "== compilando sqlite para arm64 =="
# Los defines son los que recomienda upstream para uso empotrado, mas dos que
# aqui NO son opcionales:
#
#   SQLITE_THREADSAFE=1   libmicrohttpd atiende cada peticion en su hilo y
#                         mapboxglengine comparte las conexiones entre ellos.
#   SQLITE_ENABLE_RTREE   la busqueda de POIs por cercania consulta la tabla
#                         object_primary_rtree del geocoder. Sin esto la
#                         amalgamacion no trae el modulo y /v1/guide falla con
#                         "no such module: rtree" — y el error va a std::cerr,
#                         que en Android no se ve (de ahi src/cerrtolog.h).
"$TOOLS/${HOST}${API}-clang" -c -O2 -fPIC \
    -DSQLITE_THREADSAFE=1 \
    -DSQLITE_ENABLE_RTREE \
    -DSQLITE_ENABLE_COLUMN_METADATA \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    -DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
    "$dir/sqlite3.c" -o "$SRC/sqlite3.o"

"$TOOLS/llvm-ar" rcs "$PREFIX/lib/libsqlite3.a" "$SRC/sqlite3.o"
cp "$dir/sqlite3.h" "$dir/sqlite3ext.h" "$PREFIX/include/"

ls -la "$PREFIX/lib/libsqlite3.a"
