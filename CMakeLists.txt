# OSM Scout Server para Android.
#
# Proyecto SEPARADO de Navius a proposito: OSM Scout Server es GPL-3.0-or-later
# y Navius Android es cerrado. Los dos se hablan por HTTP en localhost:8553, sin
# enlazar nada. Ver docs/PLAN-mapas-locales-android.md.
#
# Se compila con CMake y no con el qmake del proyecto original: sus .pro tienen
# "sabores" de interfaz (Sailfish, Kirigami, Lomiri...) y ninguno es Android,
# ademas de que en Qt 6 el despliegue de Android esta pensado para CMake.
cmake_minimum_required(VERSION 3.21)

project(osmscout-server-android VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

# ── Dependencias cruzadas a mano ────────────────────────────────────────────
# Las construyen scripts/build-valhalla-android.sh y build-microhttpd-android.sh.
# No se usa find_package: son instalaciones nuestras en un prefijo conocido, y
# buscarlas complicaria mas que ayudar.
set(ANDROID_DEPS_PREFIX "$ENV{HOME}/src_valhalla/install-android-arm64"
    CACHE PATH "Prefijo con protobuf y libmicrohttpd cruzados")
set(VALHALLA_SOURCE_DIR "$ENV{HOME}/pkg-valhalla-lite/valhalla"
    CACHE PATH "Arbol de Valhalla")
set(BOOST_HEADERS_DIR "$ENV{HOME}/src_valhalla/boost-headers"
    CACHE PATH "Directorio que contiene solo boost/")

set(VALHALLA_BUILD_DIR "${VALHALLA_SOURCE_DIR}/build-android")

qt_add_executable(osmscout-server
    src/main.cpp
    src/cerrtolog.h
    src/geoengine.cpp
    src/geoengine.h
    src/mapboxglengine.cpp
    src/mapboxglengine.h
    src/routeservice.cpp
    src/routeservice.h
    src/valhallaengine.cpp
    src/valhallaengine.h
    # Envoltorio C++ de sqlite que usa el servidor original para leer los tiles.
    # Vendorizado (MIT, de Wongoo Lee); en osmscout-server es un submodulo de
    # geocoder-nlp, y depender de ese arbol obligaria a clonarlo en la maquina
    # de compilacion solo por dos ficheros.
    vendor/sqlite3pp/sqlite3pp.cpp
    # geocoder-nlp, tambien de osmscout-server (GPL). Es su motor de busqueda:
    # libpostal trocea la consulta y esto la resuelve sobre el trie y la base.
    vendor/geocoder-nlp/geocoder.cpp
    vendor/geocoder-nlp/postal.cpp
    # Capa HTTP levantada tal cual de OSM Scout Server (GPL). Se copia sin
    # tocar para poder seguir sus cambios; solo depende de QByteArray.
    vendor/uhttp/microhttpconnection.cpp
    vendor/uhttp/microhttpconnectionstore.cpp
    vendor/uhttp/microhttpserver.cpp
    vendor/uhttp/microhttpservicebase.cpp
)

target_include_directories(osmscout-server PRIVATE
    src
    vendor/uhttp
    "${VALHALLA_SOURCE_DIR}"
    "${VALHALLA_BUILD_DIR}/src"
    "${VALHALLA_SOURCE_DIR}/third_party/date/include"
    "${VALHALLA_SOURCE_DIR}/third_party/rapidjson/include"
    "${ANDROID_DEPS_PREFIX}/include"
    vendor/sqlite3pp
    vendor/geocoder-nlp
)
# Boost va por -isystem y apuntando a un arbol que contiene SOLO boost/: pasarle
# /usr/include a un compilador cruzado meteria las cabeceras del anfitrion por
# delante del sysroot del NDK.
target_compile_options(osmscout-server PRIVATE -isystem "${BOOST_HEADERS_DIR}")

target_link_libraries(osmscout-server PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    "${VALHALLA_BUILD_DIR}/src/libvalhalla.so"
    "${ANDROID_DEPS_PREFIX}/lib/libmicrohttpd.a"
    "${ANDROID_DEPS_PREFIX}/lib/libprotobuf-lite.a"
    # Android trae sqlite en /system pero el NDK no la expone; se compila la
    # amalgamacion (scripts/build-sqlite-android.sh).
    "${ANDROID_DEPS_PREFIX}/lib/libsqlite3.a"
    # Busqueda: libpostal normaliza, marisa es el trie y kyotocabinet la base
    # de identificadores (scripts/build-geocoder-deps-android.sh).
    "${ANDROID_DEPS_PREFIX}/lib/libpostal.a"
    "${ANDROID_DEPS_PREFIX}/lib/libmarisa.a"
    "${ANDROID_DEPS_PREFIX}/lib/libkyotocabinet.a"
    z
    log   # el logger de Valhalla llama a __android_log_print
)

qt_add_resources(osmscout-server "recursos"
    PREFIX "/"
    FILES
        qml/Main.qml
        data/valhalla.json-3.4.0
        data/geocoder-npl-tag-aliases.json
        styles/mapboxgl/sprites/sprite.json
        styles/mapboxgl/sprites/sprite.png
        styles/mapboxgl/sprites/sprite_mc.json
        styles/mapboxgl/sprites/sprite_mc.png
        styles/mapboxgl/styles/mc-car-en.json
        styles/mapboxgl/styles/mc-car.json
        styles/mapboxgl/styles/mc-en.json
        styles/mapboxgl/styles/mc.json
        styles/mapboxgl/styles/osmbright-car-en.json
        styles/mapboxgl/styles/osmbright-car.json
        styles/mapboxgl/styles/osmbright-en.json
        styles/mapboxgl/styles/osmbright.json
)

if(ANDROID)
    set_target_properties(osmscout-server PROPERTIES
        QT_ANDROID_PACKAGE_NAME       com.egpsistemas.osmscout
        QT_ANDROID_APP_NAME           "OSM Scout Server"
        QT_ANDROID_VERSION_NAME       ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE       1
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_MIN_SDK_VERSION    28
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        # libvalhalla.so hay que meterla en el APK a mano: no es una dependencia
        # que androiddeployqt sepa descubrir.
        QT_ANDROID_EXTRA_LIBS "${VALHALLA_BUILD_DIR}/src/libvalhalla.so"
    )
endif()
