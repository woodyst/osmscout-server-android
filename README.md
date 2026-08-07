# OSM Scout Server para Android

Mapas, rutas, búsqueda y POIs **sin conexión** para Navius Android.

Es un proyecto **separado de Navius y con otra licencia**, y no por comodidad:
OSM Scout Server es GPL-3.0-or-later y Navius Android es cerrado, así que su
código no puede vivir dentro de Navius. Los dos se hablan por HTTP en
`localhost:8553`, sin enlazar nada — igual que ya ocurre en Ubuntu Touch.

Basado en [osmscout-server](https://github.com/rinigus/osmscout-server) de
Rinigus. La capa HTTP (`vendor/uhttp/`) se copia tal cual; los motores se portan
conservando sus algoritmos. Las desviaciones van anotadas con un comentario
`EGP:` y en un `CAMBIOS.md` junto al código afectado.

## Estado

| Servicio | Endpoint | Estado |
|---|---|---|
| Rutas | `/v2/route` | funcionando |
| Tiles vectoriales | `/v1/mbgl/*` | funcionando |
| Búsqueda | `/v1/search`, `/v2/search` | funcionando |
| POIs | `/v1/guide`, `/v1/poi_types` | funcionando |
| Descarga de mapas | Map Manager | pendiente — mecanismo ya documentado en el plan |

Plan por fases en `~/prog_ia/navius/docs/PLAN-mapas-locales-android.md`.

## Compilar

Se compila **en erebos3**, con Qt 6.8.3 android_arm64_v8a y NDK 26.1.10909125.
Primero las dependencias cruzadas, que van a un prefijo común:

```
scripts/build-valhalla-android.sh all     # protobuf + Valhalla 3.4.0
scripts/build-microhttpd-android.sh       # servidor HTTP
scripts/build-sqlite-android.sh           # amalgamación
scripts/build-android.sh                  # el APK
```

## Mapas

Se reutilizan **tal cual** los que descarga OSM Scout Server en Ubuntu Touch: son
ficheros ya preparados, no se genera nada en el dispositivo. Van en el
almacenamiento externo propio de la app, que es una ruta real del sistema de
ficheros — hace falta que lo sea porque Valhalla mapea los tiles en memoria y un
`content://` del SAF no serviría:

```
/sdcard/Android/data/com.egpsistemas.osmscout/files/Maps.OSM/
├── valhalla/tiles/          rutas
└── mapboxgl/
    ├── packages/            tiles-world.sqlite, tiles-section-7-X-Y.sqlite
    └── glyphs/glyphs.sqlite fuentes
```

**No cambiar la versión de Valhalla** sin comprobar que los tiles siguen
cargando: el formato cambia entre versiones y los paquetes declaran `version 2`.
