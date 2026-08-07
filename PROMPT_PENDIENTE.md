# Pendiente en OSM Scout Server para Android

Escrito el 2026-08-07 para arrancar la siguiente sesión sin contexto previo.
El plan por fases está en `~/prog_ia/navius/docs/PLAN-mapas-locales-android.md`.

Estado: **los cinco servicios funcionan y están probados en el móvil real** —
rutas, tiles, búsqueda, POIs y descarga de mapas.

---

## 1. La búsqueda solo mira UN territorio  ← lo primero

**Síntoma:** con España y Argelia instalados, buscar un destino o un POI solo
encuentra cosas de uno de los dos. Las rutas y el mapa sí funcionan en todos.

**Causa:** `geocoder-nlp` carga **una base a la vez**, y como todavía no hay
selección de mapa, `GeoEngine::start()` (`src/geoengine.cpp`) coge **la más
grande** de las instaladas. Lo peor no es que elija: es que si mañana se instala
un territorio mayor, cambia sola y en silencio.

**Cómo lo hace el original**, que es lo que hay que replicar: en
`geomaster.cpp`, `GeoMaster::search()` recorre `m_countries` y llama a
`m_geocoder.load()` de cada base **dentro del bucle de búsqueda**, acumulando
resultados y quedándose con los que resuelven más niveles de jerarquía
(`levels_resolved`). No mantiene varias abiertas: las va cargando por turnos en
cada consulta.

**A tener en cuenta:** cargar una base cuesta, así que el tiempo de respuesta se
multiplica por el número de territorios. Conviene ordenar por cercanía al punto
de referencia que ya llega en la petición (`lat`/`lng`) y cortar, en vez de
recorrerlos todos siempre.

Aplica igual a `GeoEngine::guide()`, que tiene el mismo problema.

## 2. Comprobar que la descarga grande aguanta

Se arregló el consumo de memoria (commit `4ecb224`): antes se juntaban dos
copias del fichero en RAM y Android mataba el proceso al instalar
`europe/spain`. Ahora se escribe según llega y se descomprime por bloques.

Quedó **descargando España al cerrar la sesión**, por los paquetes de Valhalla.
Lo que de verdad rompía son las secciones de `mapboxgl` (150–222 MB cada una),
que van **al final de la cola**. Si llegó ahí sin morir, el arreglo está
confirmado; si no, mirar `logcat | grep 'OSMSCOUT\[maps\]'`.

Ojo con el espacio: España completa son unos 3,5 GB y en el teléfono ya está
Argelia de una prueba anterior.

## 3. Cosas menores

- **`⛽` (U+26FD) sale como cuadradito en Navius.** Los otros siete símbolos que
  faltaban ya están arreglados (commit `d05fbb7` de `navius_android`), pero
  FreeSerif no tiene el surtidor y la única fuente del sistema con ese glifo es
  `NotoColorEmoji`, que es de mapas de bits a color y este port deja fuera a
  propósito. Hay que elegir otro carácter o empaquetar un emoji monocromo.
- **El Map Manager no reanuda ni borra.** Si se corta una descarga, al reintentar
  se baja todo otra vez. Tampoco hay forma de desinstalar un territorio.
- **No comprueba versiones.** El catálogo trae `version` por motor y aquí se
  ignora; si el servidor sube el formato, los datos viejos dejarán de cargar sin
  aviso claro.
- **El servicio en primer plano es de tipo `dataSync`**, que desde Android 15
  tiene límite de 6 h diarias. Ver el riesgo 2 del plan.

## 4. Del lado de Navius

- **Los POIs del cliente siguen yendo a Overpass**: `/v1/guide` está servido pero
  Navius no lo llama. La búsqueda sí se conectó, en los tres ports.
- **El puente de depuración por ficheros no escribe `navius_ack`** en Android: el
  `PUT` a `file://` no funciona. Por eso el comando `geocode<texto>` saca el
  resultado por `console.warn`.

---

## Trampas ya pagadas — no volver a tropezar

Todas son del mismo tipo: **el mensaje existe pero nadie lo ve**.

| Dónde | Qué pasa |
|---|---|
| `eprintln!` de Rust | No llega a logcat |
| `std::cerr` de C++ | Tampoco. De ahí `src/cerrtolog.h` |
| `console.log` de QML | Tampoco; `console.warn` sí |
| `PUT` a `file://` | No escribe, y no da error |

Y de compilación:

- **`-fPIC` en todo lo estático.** La app es una librería compartida. Sin eso
  compila todo y solo revienta al enlazar, con un `relocation R_AARCH64_…
  cannot be used against symbol 'vtable for marisa::Exception'`.
- **`SQLITE_ENABLE_RTREE`**, o `/v1/guide` falla con `no such module: rtree`.
- **OpenSSL empaquetado**, o cualquier HTTPS falla con `TLS initialization
  failed`: Android no expone libcrypto/libssl a las apps.
- **Las URL del servidor de mapas** no son `<servidor>/<path>/<fichero>`: el
  catálogo trae una entrada `url` con el directorio **versionado** de cada motor
  (`geocoder-nlp-39`, `valhalla-34`…) y todo lleva `.bz2` al final.
- **Compilar en Debug**, que en Release el APK sale sin firmar y no se instala.
