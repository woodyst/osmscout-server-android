#!/bin/bash
# Cross-compila Valhalla 3.4.0 para Android arm64. Se ejecuta EN erebos3.
#
# Version fijada por pkg-valhalla-lite de rinigus, que es lo que usa OSM Scout
# Server. NO subirla sin comprobar que los tiles ya descargados siguen cargando:
# el formato cambia entre versiones y los paquetes declaran "version: 2".
#
# Lo que hay que cruzar es sorprendentemente poco (verificado en la fase 0):
#   Boost      -> SOLO CABECERAS. Valhalla enlaza Boost::boost, el objetivo
#                 header-only, asi que se reutilizan las de Debian tal cual.
#                 Hace falta libboost-all-dev, no libboost-dev: Debian reparte
#                 las cabeceras por componentes y midgard/util.cc incluye
#                 boost/archive/iterators, que es de Serialization.
#   libcurl    -> no hace falta: solo se busca si ENABLE_HTTP o ENABLE_DATA_TOOLS.
#   zlib       -> la trae el NDK.
#   protobuf   -> lo unico que hay que compilar de verdad, y en dos mitades:
#                 el protoc de ANFITRION genera los .pb.cc, y la libreria de
#                 DESTINO se enlaza en el binario ARM. Ambos han de ser la
#                 misma version o el codigo generado no casa con el runtime.
set -euo pipefail

NDK=$HOME/android-sdk/ndk/26.1.10909125
API=28
ABI=arm64-v8a

SRC=$HOME/src_valhalla
PREFIX=$SRC/install-android-arm64
VALHALLA=$HOME/pkg-valhalla-lite/valhalla
JOBS=${JOBS:-8}

TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake

# La misma que trae Debian, para que protoc y libreria vayan a la par.
PROTOBUF_VERSION=$(protoc --version | awk '{print $2}')

mkdir -p "$SRC" "$PREFIX"

cmake_android() {
    cmake "$@" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$API" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
}

# ── protobuf ────────────────────────────────────────────────────────────────
# La 3.21 es la ultima antes de que protobuf pasara a depender de abseil, que
# arrastraria otra cross-compilacion entera. Coincide con la de Debian.
build_protobuf() {
    local tarball=protobuf-cpp-$PROTOBUF_VERSION.tar.gz
    local dir=$SRC/protobuf-$PROTOBUF_VERSION

    # OJO con el tag: protobuf cambio de versionado y la 3.21.12 se publica bajo
    # "v21.12", sin el 3. Con v3.21.12 GitHub devuelve un 404 en HTML que tar no
    # sabe abrir, y el error que sale es "esto no parece un archivo tar".
    local tag=v${PROTOBUF_VERSION#3.}

    if [ ! -d "$dir" ]; then
        echo "== descargando protobuf $PROTOBUF_VERSION (tag $tag) =="
        curl -sSL --fail -o "$SRC/$tarball" \
            "https://github.com/protocolbuffers/protobuf/releases/download/$tag/$tarball"
        tar xf "$SRC/$tarball" -C "$SRC"
    fi

    echo "== compilando protobuf para $ABI =="
    # Estatica: una .so mas que empaquetar y cargar en el APK no aporta nada, y
    # asi el enlazado no depende del orden de carga en Android.
    cmake_android -S "$dir" -B "$dir/build-android" \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
        -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -Dprotobuf_WITH_ZLIB=OFF
    cmake --build "$dir/build-android" -j"$JOBS"
    cmake --install "$dir/build-android"
}

# ── boost ───────────────────────────────────────────────────────────────────
# Un directorio con SOLO las cabeceras de Boost, para poder pasarlo por -isystem
# sin arrastrar el resto de /usr/include del anfitrion.
BOOST_HEADERS=$SRC/boost-headers
BOOST_VERSION=$(sed -n 's/.*BOOST_LIB_VERSION "\([0-9_]*\)".*/\1/p' /usr/include/boost/version.hpp | tr _ .).0

prepare_boost() {
    if [ ! -d "$BOOST_HEADERS/boost" ]; then
        echo "== preparando cabeceras de Boost =="
        mkdir -p "$BOOST_HEADERS"
        cp -a /usr/include/boost "$BOOST_HEADERS/"
    fi

    # Ademas hay que darle un BoostConfig.cmake propio. CMake 3.30 elimino el
    # modulo FindBoost, asi que find_package(Boost) SOLO funciona en modo CONFIG;
    # sin fichero de config, Valhalla se va por la rama de conan y aborta con
    # "conan needs to be installed for boost".
    #
    # No se reutiliza el de Debian a proposito: ese declara /usr/include como
    # directorio de inclusion, y metersela a un compilador cruzado por -isystem
    # pondria las cabeceras de glibc del anfitrion por delante del sysroot del
    # NDK. Este apunta solo al arbol de Boost copiado arriba.
    cat > "$BOOST_HEADERS/BoostConfig.cmake" <<'EOF'
foreach(_t Boost::boost Boost::headers)
  if(NOT TARGET ${_t})
    add_library(${_t} INTERFACE IMPORTED)
    set_target_properties(${_t} PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}")
  endif()
endforeach()
set(Boost_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
set(Boost_FOUND TRUE)
EOF

    cat > "$BOOST_HEADERS/BoostConfigVersion.cmake" <<EOF
set(PACKAGE_VERSION $BOOST_VERSION)
if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
EOF
}

# ── valhalla ────────────────────────────────────────────────────────────────
patch_valhalla() {
    cd "$VALHALLA"
    # Falta en gcc reciente y en libc++ del NDK. El patch 0005 del spec arregla
    # otros ficheros pero no este.
    grep -q "#include <algorithm>" src/baldr/admin.cc \
        || sed -i "1i #include <algorithm>" src/baldr/admin.cc
}

build_valhalla() {
    echo "== compilando valhalla para $ABI =="
    rm -rf "$VALHALLA/build-android"

    # Notas de los flags:
    #   ENABLE_HTTP=OFF        Android no trae libcurl.
    #   ENABLE_DATA_TOOLS=OFF  los tiles vienen hechos; ademas quita sqlite,
    #                          spatialite, luajit y geos de un plumazo.
    #   ENABLE_SERVICES=OFF    el HTTP lo pone el servidor, no Valhalla.
    #   SHARED_LINKER_FLAGS    -llog: el logger de Valhalla YA tiene rama de
    #                          Android y llama a __android_log_print, pero su
    #                          CMake no enlaza liblog. Sin esto compila entero y
    #                          revienta al final, al enlazar la .so.
    #   CMAKE_CXX_FLAGS        por donde entran las cabeceras de Boost, ver abajo.
    #   Protobuf_*             biblioteca de destino, protoc de anfitrion.
    #
    # Lo de Boost tiene truco. CMake 3.30 ELIMINO el modulo FindBoost, asi que
    # -DBoost_INCLUDE_DIR ya no hace nada: find_package(Boost) solo funciona en
    # modo CONFIG, y el fichero de config de Debian no lo encuentra la toolchain
    # de Android porque restringe la busqueda al sysroot del NDK. El resultado es
    # que Boost::boost queda VACIO y la compilacion falla mucho despues, en el
    # primer fichero que incluya boost/, con un "file not found" que no parece
    # tener nada que ver.
    #
    # Por eso se inyecta a mano con -isystem. Apunta a un directorio que contiene
    # SOLO boost/, copiado aparte: pasarle /usr/include entero a un compilador
    # cruzado seria meterle las cabeceras de glibc del anfitrion por delante del
    # sysroot del NDK.
    cmake_android -S "$VALHALLA" -B "$VALHALLA/build-android" -Wno-dev \
        -DBUILD_SHARED_LIBS=ON \
        -DENABLE_DATA_TOOLS=OFF -DENABLE_PYTHON_BINDINGS=OFF \
        -DENABLE_SERVICES=OFF -DENABLE_HTTP=OFF \
        -DENABLE_TOOLS=OFF -DENABLE_TESTS=OFF -DENABLE_BENCHMARKS=OFF \
        -DENABLE_WERROR=OFF -DENABLE_SINGLE_FILES_WERROR=OFF -DENABLE_CCACHE=OFF \
        -DBoost_DIR="$BOOST_HEADERS" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
        -DCMAKE_CXX_FLAGS="-isystem $BOOST_HEADERS" \
        -DCMAKE_SHARED_LINKER_FLAGS="-llog" \
        -DProtobuf_INCLUDE_DIR="$PREFIX/include" \
        -DProtobuf_LIBRARY="$PREFIX/lib/libprotobuf.a" \
        -DProtobuf_LITE_LIBRARY="$PREFIX/lib/libprotobuf-lite.a" \
        -DProtobuf_PROTOC_EXECUTABLE=/usr/bin/protoc

    # Con Ninja falla: "multiple rules generate src/libvalhalla.so". Por eso
    # cmake --build sin generador, que aqui cae en Makefiles.
    cmake --build "$VALHALLA/build-android" -j"$JOBS"
}

# ── test de ruta ────────────────────────────────────────────────────────────
build_test() {
    echo "== compilando el test de ruta =="
    local cc=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$API-clang++
    local out=$SRC/valhalla_route_test

    "$cc" -std=c++17 -O2 \
        "$HOME/valhalla-test/valhalla_route_test.cpp" \
        -I "$VALHALLA" \
        -I "$VALHALLA/build-android/src" \
        -I "$VALHALLA/third_party/date/include" \
        -I "$VALHALLA/third_party/rapidjson/include" \
        -I "$PREFIX/include" \
        -isystem "$BOOST_HEADERS" \
        -L "$VALHALLA/build-android/src" -lvalhalla \
        -L "$PREFIX/lib" -lprotobuf-lite \
        -lz -llog \
        -o "$out"
    echo "test: $out"
}

case "${1:-all}" in
    protobuf) build_protobuf ;;
    valhalla) prepare_boost; patch_valhalla; build_valhalla ;;
    test)     build_test ;;
    all)      build_protobuf; prepare_boost; patch_valhalla; build_valhalla; build_test ;;
    *) echo "uso: $0 [protobuf|valhalla|test|all]" >&2; exit 2 ;;
esac
