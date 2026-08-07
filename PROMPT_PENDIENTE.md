# Pendiente en OSM Scout Server para Android

Escrito el 2026-08-07 para arrancar la siguiente sesión sin contexto previo.
El plan por fases está en `~/prog_ia/navius/docs/PLAN-mapas-locales-android.md`.

Estado: **los cinco servicios del servidor funcionan y están probados en el móvil
real** — rutas, tiles, búsqueda, POIs y descarga de mapas.

Del lado de Navius, sin cobertura funcionan **ruta, mapa y búsqueda de destino**,
que es la operativa principal. Faltan los POIs, que Navius sigue pidiendo a
Overpass. Las dos primeras tareas de esta lista son las que cierran el círculo, y
las dos las pidió Edi expresamente.

## El objetivo, fijado por Edi el 2026-08-07

**Cobertura total en local, en los tres ports, con internet caído.** No es «que
funcione algo»: es que Navius sea usable entero sin red, en Android, Ubuntu Touch
y postmarketOS.

| Servicio | Servidor | Cliente | Sin red |
|---|---|---|---|
| Ruta | ✅ | ✅ los 3 ports | **sí** |
| Mapa | ✅ | ✅ los 3 ports | **sí** |
| Buscar destino | ✅ | ✅ los 3 ports | **sí** ⚠️ un territorio |
| POIs y radares | ✅ | ❌ **ninguno de los 3** | **no** |
| Alertas y mensajes de usuarios | — | — | no, y a propósito |

Lo único que queda para el objetivo son las **tareas 1 y 1b**. La API comunitaria
—alertas de otros conductores, mensajes, compartir viaje— seguirá siendo online
porque no es un servicio de mapas: sin red no hay nada que sincronizar.

Criterio de aceptación: **con el móvil en modo avión, buscar un destino, calcular
la ruta, navegarla con el mapa dibujándose y que aparezcan los POIs y los avisos
de radar del trayecto.** En los tres ports.

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

## 1b. Los POIs locales no se usan  ← lo segundo

**Decidido por Edi el 2026-08-07**, junto con lo de arriba: hay que servir los
POIs del dispositivo cuando falte cobertura.

**Estado:** el servidor ya sirve `/v1/guide` y `/v1/poi_types`, probado en el
móvil (gasolineras a 2 km de Plaça Catalunya, con distancias). Pero **Navius
sigue pidiéndolos a su Overpass** en los tres ports, así que sin cobertura no
hay gasolineras, ni cafeterías, ni **radares** — que salen por esa misma vía y
son de lo más visible cuando faltan.

**Es el mismo trabajo que ya se hizo con la búsqueda** (commit `7d8d305` de
`navius_android`), y conviene copiar ese patrón entero:

1. En `NavSearch.js`, junto a `OSMSCOUT_SEARCH`, un `OSMSCOUT_GUIDE` y el mismo
   interruptor `_osmScoutSearchOk`, que ya lo pone `Main.qml` con el resultado
   de `detectOsmScout()`.
2. Online primero (Overpass), local de respaldo si falla — igual que la
   búsqueda, y por la misma razón: el índice de Overpass está más al día.
3. Convertir la respuesta del servidor a la forma que ya consume la interfaz.
   El servidor devuelve `{origin, results:[{title, admin_region, lat, lng,
   type, distance}]}` y Overpass devuelve `{elements:[...]}`; hay que mapear,
   como se hizo con los Feature de Photon.
4. Portarlo a mano a UT y pmOS: los QML de los tres son independientes.
5. **Commit por port**, y en UT y pmOS **push al git publico** —lo pidio Edi
   expresamente—. Ojo: esos dos hay que publicarlos desde el repositorio
   publico, no desde otro remoto. Comprobar `git remote -v` antes de subir.

**Ojo con los radares**, que no son un POI cualquiera: hoy salen de una consulta
Overpass propia con `highway=speed_camera`. Comprobar si el geocoder los tiene
con ese tipo antes de dar por hecho que `/v1/guide` los cubre.

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

## 2b. Una descarga interrumpida deja datos huerfanos

**Sintoma real:** se instalo `africa/algeria`, se paro la app a mitad, y los
ficheros se quedaron en disco **sin constar como instalados**. Ocupaban espacio,
no salian en «Instalados» y no habia forma de quitarlos desde la interfaz. Hubo
que borrar los 5 GB a mano.

**Causa:** `countries_requested.json` solo se escribe **al terminar** el
territorio entero (`MapManager::next()`, cuando la cola se vacia). Si se corta
antes, no queda constancia de nada.

**Lo que hay que hacer, y el usuario lo pidio explicitamente:**

1. **Anotar el progreso segun avanza**, no al final. Basta con guardar el estado
   del territorio en curso —los trabajos pendientes— en un fichero aparte, y
   marcarlo como completo al vaciar la cola.
2. **Reanudar**: al arrancar, si hay un territorio a medias, ofrecer «Reanudar» o
   «Descartar». Reanudar es barato porque ya se sabe que ficheros faltan.
3. **Saltarse lo que ya esta**. Hoy solo se comprueba para los globales
   (`enqueueGlobalIfMissing`). Instalar Andorra teniendo Espana volvio a bajar
   238 MB de una seccion de tiles que ya estaba en disco, porque los paquetes de
   zonas fronterizas se comparten. Misma comprobacion que para los globales.
4. **Poder borrar lo huerfano**: si hay ficheros de un territorio que no consta
   instalado, ofrecer limpiarlo.

Detalle a favor: la descarga ya escribe a `<fichero>.part` y solo renombra al
terminar, asi que **un fichero presente esta completo** y un `.part` señala
justo por donde se corto. La mitad del trabajo esta hecha.

## 3. Cosas menores

- **`⛽` (U+26FD) sale como cuadradito en Navius.** Los otros siete símbolos que
  faltaban ya están arreglados (commit `d05fbb7` de `navius_android`), pero
  FreeSerif no tiene el surtidor y la única fuente del sistema con ese glifo es
  `NotoColorEmoji`, que es de mapas de bits a color y este port deja fuera a
  propósito. Hay que elegir otro carácter o empaquetar un emoji monocromo.
- **Desinstalar ya esta** (commit `8d83f61`), y respeta los paquetes que
  comparten los territorios vecinos. Lo instalado antes de ese commit no tiene
  `.tar.list` y de eso solo se borra el geocoder.
- **No comprueba versiones.** El catálogo trae `version` por motor y aquí se
  ignora; si el servidor sube el formato, los datos viejos dejarán de cargar sin
  aviso claro.
- **Que Navius despierte al servidor, en vez de tenerlo siempre vivo.**
  Decidido por Edi el 2026-08-07.

  Hoy el servidor levanta un servicio en primer plano de tipo `dataSync` y se
  queda corriendo. Desde Android 15 ese tipo tiene **límite de 6 h diarias**, y
  al agotarse el sistema lo para: para algo que debe responder conduciendo, no
  sirve a largo plazo.

  La alternativa elegida —y es la que Edi ya aprobó en su día para el arranque,
  §2.1 del plan— es el **equivalente del D-Bus de Ubuntu Touch**: Navius lanza un
  Intent explícito al servicio del servidor justo antes de detectarlo. Navius
  está en primer plano en ese momento, así que `startForegroundService()` está
  permitido, y los 30 s de espera que `detectOsmScout()` ya tiene escritos
  sirven exactamente para eso.

  Con eso el servicio deja de tener que vivir siempre: se levanta cuando hace
  falta y el límite diario deja de ser un problema.

  Nunca se implementó el Intent porque el servicio en primer plano bastaba para
  probar. Falta: el lado de Navius (los tres ports) y decidir cuándo se para el
  servidor —al cerrar Navius, o por inactividad—.

  Recordatorio legal, ya razonado: **lanzar un Intent por nombre NO es enlazar**.
  No entra código GPL en Navius, no comparten proceso ni compilación.

## 4. Del lado de Navius

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
