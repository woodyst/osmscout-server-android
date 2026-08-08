# OSM Scout Server para Android

Mapas, rutas, búsqueda y puntos de interés **sin conexión**, servidos por HTTP
desde el propio móvil. Es el port a Android de
[osmscout-server](https://github.com/rinigus/osmscout-server) de Rinigus, que en
Ubuntu Touch y Sailfish lleva años haciendo esto mismo.

No tiene mapa ni navegación: es un **servidor**. Escucha en
`127.0.0.1:8553` y responde a cualquier app del dispositivo que hable su API.
Se escribió para [Navius](https://github.com/woodyst/navius), pero la API es la
del original, así que sirve para cualquier cliente que ya la use.

> **Por qué va aparte y con otra licencia.** OSM Scout Server es
> GPL-3.0-or-later. Navius Android es una app cerrada, así que este código no
> puede vivir dentro de ella: se hablan por HTTP, sin enlazar nada. Es la misma
> separación que ya existe en Ubuntu Touch.

## Qué hace

| Servicio | Endpoint | Motor |
|---|---|---|
| Rutas | `/v2/route` y el resto de `/v2/*` | Valhalla 3.4.0 |
| Tiles vectoriales | `/v1/mbgl/*` | SQLite (MBTiles) |
| Buscar destinos | `/v1/search`, `/v2/search` | geocoder-nlp + libpostal |
| Puntos de interés | `/v1/guide`, `/v1/poi_types` | geocoder-nlp |
| Estado | `/v1/status` | — (añadido aquí, no está en el original) |
| Descarga de mapas | interfaz propia | mismo catálogo y mismo servidor que el original |

La búsqueda y los POIs recorren **todos los territorios instalados**, abriendo
sus bases por turnos, porque geocoder-nlp solo admite una abierta a la vez.

## Cómo funciona en Android

El servidor corre **dentro de un servicio de Android, en su propio proceso**, y
lo despierta el cliente con un Intent explícito cuando lo necesita — el
equivalente de la activación por D-Bus de Ubuntu Touch:

```java
Intent i = new Intent();
i.setComponent(new ComponentName("com.egpsistemas.osmscout",
                                 "com.egpsistemas.osmscout.ServerService"));
context.startForegroundService(i);   // con la app en primer plano
```

Quien lo llame necesita declararlo en su manifiesto, o desde Android 11 el
Intent se bloquea sin decir por qué:

```xml
<queries><package android:name="com.egpsistemas.osmscout" /></queries>
```

Cargar los motores de un territorio grande lleva sus veinte segundos, así que
conviene sondear `/v1/activate` con paciencia antes de darlo por no disponible.

## Los mapas

Se usan **tal cual** los ficheros que prepara el servidor de Rinigus: no se
genera ni se importa nada en el dispositivo. La app trae su propio gestor de
descargas, con el mismo catálogo que el original.

```
/sdcard/Android/data/com.egpsistemas.osmscout/files/Maps.OSM/
├── valhalla/tiles/                 rutas
├── mapboxgl/
│   ├── packages/                   tiles-world.sqlite, tiles-section-7-X-Y.sqlite
│   └── glyphs/glyphs.sqlite        fuentes del rotulado
├── geocoder-nlp/<territorio>/      búsqueda y POIs
└── postal/                         normalización de direcciones
```

Tiene que ser una ruta real del sistema de ficheros: Valhalla mapea los tiles en
memoria y un `content://` del SAF no serviría.

**El formato importa.** Cada motor declara la versión que sabe leer y se
comprueba contra el catálogo antes de descargar nada. Cambiar la versión de
Valhalla sin más deja los tiles ilegibles.

## Compilar

Qt 6.8.3 (`android_arm64_v8a`) y NDK 26.1.10909125. Primero las dependencias
cruzadas, que van a un prefijo común, y después el APK:

```sh
scripts/build-valhalla-android.sh all     # protobuf + Valhalla 3.4.0
scripts/build-microhttpd-android.sh       # servidor HTTP
scripts/build-sqlite-android.sh           # amalgamación, con RTREE
scripts/build-geocoder-deps-android.sh    # libpostal, marisa, kyotocabinet
scripts/build-bzip2-android.sh            # descompresión de las descargas
scripts/build-android.sh                  # el APK
```

Los scripts esperan Qt, el SDK y el NDK en el `$HOME`; se ajustan en las
primeras líneas de cada uno. Hace falta una máquina con holgura: Valhalla y
libpostal no se compilan en un rato.

## Crédito y licencia

Todo el mérito del diseño es de **Rinigus**, autor de
[osmscout-server](https://github.com/rinigus/osmscout-server). Aquí se ha
portado a Android conservando sus algoritmos y su API. La capa HTTP
(`vendor/uhttp/`) se copia prácticamente tal cual; las desviaciones sobre el
original van anotadas en un `CAMBIOS.md` junto al código afectado, para que se
puedan rehacer si el upstream se actualiza.

**GPL-3.0-or-later**, como el original. Ver [LICENSE](LICENSE).

Copyright (C) 2016-2018 Rinigus · Copyright (C) 2026 EGP Sistemas

Los mapas son de [OpenStreetMap](https://www.openstreetmap.org/copyright), bajo
ODbL, y los preparan y sirven los contribuidores de OSM Scout Server.
