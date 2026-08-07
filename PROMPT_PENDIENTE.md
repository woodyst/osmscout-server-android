# Pendiente en OSM Scout Server para Android

Escrito el 2026-08-07 para arrancar la siguiente sesión sin contexto previo.
El plan por fases está en `~/prog_ia/navius/docs/PLAN-mapas-locales-android.md`.

Estado: **los cinco servicios del servidor funcionan y están probados en el móvil
real** — rutas, tiles, búsqueda, POIs y descarga de mapas.

Del lado de Navius, sin cobertura funcionan **ruta, mapa, búsqueda de destino y
POIs** en los tres ports, y la búsqueda mira ya todos los territorios
instalados. Lo que queda son mejoras de robustez, no funcionalidad: reanudar
descargas, comprobar versiones de formato y el Intent de arranque.

## El objetivo, fijado por Edi el 2026-08-07

**Cobertura total en local, en los tres ports, con internet caído.** No es «que
funcione algo»: es que Navius sea usable entero sin red, en Android, Ubuntu Touch
y postmarketOS.

| Servicio | Servidor | Cliente | Sin red |
|---|---|---|---|
| Ruta | ✅ | ✅ los 3 ports | **sí** |
| Mapa | ✅ | ✅ los 3 ports | **sí** |
| Buscar destino | ✅ | ✅ los 3 ports | **sí** |
| POIs | ✅ | ✅ los 3 ports | **sí** |
| Radares | — | caché propia | **sí**, si ya se barrió la zona |
| Alertas y mensajes de usuarios | — | — | no, y a propósito |

**El objetivo está cumplido**, con la salvedad de los radares en zona nunca
barrida (tarea 1b). La API comunitaria —alertas de otros conductores, mensajes,
compartir viaje— seguirá siendo online porque no es un servicio de mapas: sin
red no hay nada que sincronizar.

Criterio de aceptación: **con el móvil en modo avión, buscar un destino, calcular
la ruta, navegarla con el mapa dibujándose y que aparezcan los POIs y los avisos
de radar del trayecto.** En los tres ports.

Estado del criterio a 2026-08-08: cumplido en Android salvo los radares en zona
nunca barrida (ver tarea 1b). En UT y pmOS el código es el mismo pero **falta
probarlo en sus dispositivos**.

---

## 1. Búsqueda multi-territorio — HECHO el 2026-08-08

Commit `db9aa7e`. Se replica lo del original: las bases se abren **por turnos
dentro de la propia consulta**, porque geocoder-nlp solo admite una abierta.
`search()` conserva el resultado que resuelve más niveles de jerarquía y junta
los que empatan; `guide()` acumula entre territorios y ordena por distancia al
final, así que las fronteras dejan de cortar.

Probado en el móvil con España y Andorra: «Andorra la Vella» ya sale (antes,
nada), búsquedas de 100 a 630 ms y 27 gasolineras de los dos países en una
consulta a caballo de la frontera, en 116 ms.

Dos cosas que el original no necesita y aquí sí:

- **Quitar repetidos.** Los extractos se solapan: Andorra la Vella sale igual en
  `europe-spain` que en `europe-andorra`, con las mismas coordenadas. Con un
  solo territorio eso no pasaba nunca; con todos es lo normal.
- **Recorrerlos todos, sin cortar al primer acierto.** El original tiene ajuste
  para eso porque admite decenas de mapas; aquí se instalan pocos y abrir una
  base sale a ~150 ms. **Si alguien instala veinte territorios esto se nota**:
  el tiempo por consulta queda en el registro (`OSMSCOUT: búsqueda en N
  territorio(s) … ms`), que es por donde se vería.

De paso desapareció la tabla de cuatro países que adivinaba el directorio de
libpostal: ahora sale de `countries_requested.json`, donde el Map Manager guarda
la entrada entera del catálogo. Si el fichero no está —instalaciones viejas— se
recurre a mirar qué directorios hay, sin datos de país.

## 1b. POIs locales — HECHO el 2026-08-08

Cerrado en los tres ports: `navius_android` `fd3bfd2`, UT `e878022` (subido a
`origin/main` de `github.com/woodyst/navius.git`) y pmOS `e02fefc` (subido a
`main` de `github.com/woodyst/navius-postmarketos.git`).

Probado en el móvil real **en modo avión**: 34 gasolineras servidas por
`/v1/guide`, con precios del MINETUR de la caché local, cuatro segundos desde el
toque. En modo avión las peticiones fallan al instante, así que la cadena de
Overpass se agota enseguida y no hay que esperar los timeouts.

Cómo quedó, por si hay que tocarlo:

- El tipo del geocoder es la etiqueta OSM con guión bajo (`amenity=fuel` →
  `amenity_fuel`), que es justo `tag + "_" + val` de `_poiDefs`. Los ocho tipos
  que usa Navius responden, **incluido `amenity_parking`**, que no está en la
  tabla de alias: esa tabla es solo para buscar por nombre, no para filtrar.
- La respuesta se convierte a la forma de Overpass (`{elements:[…]}`) para que
  `_poiProcess` no se entere de por dónde vinieron. El `title` viene como
  «Nombre, número, calle» y se parte por la primera coma.
- En modo «en ruta» hay que repetir la consulta por cada muestra, porque el
  geocoder pide punto y radio. Van en serie y con tope de 40 puntos.

**Los radares NO salen del geocoder.** Comprobado: su índice tiene 302 tipos y
`highway=speed_camera` no es uno de ellos. Sin cobertura siguen saliendo de la
caché local de radares (tabla `radares` en LocalStorage), que ya existía y es
otra vía: funciona si la zona se barrió alguna vez con red. Para tenerlos de
verdad offline en zona nueva haría falta otra fuente; no la hay hoy.

**Trampa que costó una sesión y ya está pagada en los tres ports:**
`NavSearch.js` NO es `.pragma library`, así que **cada componente que lo importa
tiene SU copia de las variables**. `SearchPanel` es quien pide búsquedas y POIs,
y no recibía el interruptor: se quedaba sin respaldo local aunque el servidor
estuviera detectado. Todo interruptor que ponga `Main.qml` hay que reenviarlo al
panel, igual que `setNavUrl`.

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

## 2c. El índice no devuelve los nombres de ciudad muy comunes

Lo levantó el agente del port de UT y está **verificado, pero sin arreglar**.

Buscar **«Barcelona» a secas no devuelve la ciudad de Barcelona**. No es que no
esté: «Barcelona Barcelonès» la encuentra al instante, como
`boundary_administrative`. Lo que pasa es que hay tantos objetos llamados
Barcelona —plazas, calles, parajes, aldeas— que el motor corta el conjunto de
candidatos antes de llegar a ella. Comprobado pidiendo **200 resultados: ni uno
a menos de 50 km** del centro de Barcelona.

Con nombres menos repetidos funciona bien: «Mataró», «Terrassa» y
«Andorra la Vella» devuelven la ciudad, y «Sabadell Vallès Occidental» también.

**Dónde está el corte**, para quien lo retome: en `geocoder.cpp`,
`m_max_inter_results = m_max_results + m_max_inter_offset` (100), y el bucle
sobre `search_result` rompe al llegar a ese tope. Los candidatos se recorren en
el orden del trie, no por relevancia, así que subir el tope solo mueve el
problema. El arreglo de verdad sería puntuar antes de truncar, que es meterse a
fondo en la librería.

Mitigado por el lado del cliente hasta donde se puede: se piden 25 resultados y
se enseñan los 6 más cercanos (ver 2d).

## 2d. El sesgo por cercanía — ARREGLADO el 2026-08-08

Commit `b5f87a7`, y en los tres ports el orden se arregla además en el cliente
(`navius_android` `48b1dc6`, UT `7c68c4f`, pmOS `77ced48`, los dos últimos ya
subidos).

`search_rank_location_bias()` de geocoder-nlp acotaba el zoom **por abajo** a 18,
con lo que el radio del sesgo se quedaba clavado en 250 m y el punto de
referencia no influía a más de dos kilómetros. Detalle en
`vendor/geocoder-nlp/CAMBIOS.md`.

**Ojo si algún día se actualiza el vendor**: el parche se pierde y vuelve el
síntoma —resultados de la otra punta del país—. El orden del lado del cliente lo
tapa, pero el servidor volvería a elegir mal qué 25 manda.

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
