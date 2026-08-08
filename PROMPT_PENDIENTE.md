# Pendiente en OSM Scout Server para Android

Escrito el 2026-08-07 para arrancar la siguiente sesión sin contexto previo.
El plan por fases está en `~/prog_ia/navius/docs/PLAN-mapas-locales-android.md`.

Estado: **los cinco servicios del servidor funcionan y están probados en el móvil
real** — rutas, tiles, búsqueda, POIs y descarga de mapas.

Del lado de Navius, sin cobertura funcionan **ruta, mapa, búsqueda de destino y
POIs** en los tres ports, y la búsqueda mira ya todos los territorios
instalados. **La lista de robustez está cerrada**: descargas reanudables,
arranque por Intent y comprobación de versiones de formato.

Lo que queda son los dos límites conocidos —radares en zona nunca barrida (§1b)
y nombres de ciudad muy comunes (§2c)— y las cosas menores de §3.

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

Estado del criterio a 2026-08-08: **cumplido y probado en los tres ports** —Edi
confirma UT y postmarketOS—, salvo los radares en zona nunca barrida (tarea 1b)
y los nombres de ciudad muy comunes (tarea 2c).

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

## 2. Descargas grandes e interrumpidas — HECHO

El consumo de memoria se arregló en `4ecb224`: antes se juntaban dos copias del
fichero en RAM y Android mataba el proceso al instalar `europe/spain`. Ahora se
escribe según llega y se descomprime por bloques. Confirmado: España entera está
instalada en el móvil.

**Reanudar, descartar y no repetir lo ya bajado**: commit `3d1e321`, el
2026-08-08. Cortar una instalación dejaba los ficheros en disco sin constar como
instalados —pasó con Argelia y hubo que borrar 5 GB a mano—.

- Un `downloads_pending.json` con el territorio en curso y lo que falta, escrito
  antes del primer byte y **después de cada fichero**.
- Al abrir, si hay algo a medias, la interfaz ofrece **Reanudar** o
  **Descartar**. Descartar borra lo bajado de ese territorio respetando lo que
  compartan los instalados, más los `.part` sueltos.
- **No se vuelve a bajar lo que ya está**, porque se escribe a `<fichero>.part` y
  solo se renombra al terminar: lo que tiene el nombre bueno está completo. De
  los `.tar` se mira su `.list`. Con eso, instalar un vecino deja de repetir los
  paquetes compartidos, y el botón de un territorio instalado es «Completar».

Probado entero en el móvil: Malta cortada y reanudada, «Completar» saltándose 11
de 11, y Maldivas descartada borrando 178 elementos sin tocar España ni Andorra.

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
- **Versiones de formato — HECHO** (`1054de1`). Se comprueban las del catálogo
  contra las que sabe leer cada motor —las mismas del original; la del geocoder
  sale de `GeoNLP::Geocoder::version`—, al instalar (antes de bajar nada) y al
  arrancar (contra `countries_requested.json`). Probado falseando el fichero:
  bloquea la instalación y avisa en rojo de lo ya instalado.
## 3b. Navius despierta al servidor — HECHO el 2026-08-08

Commits `a7e0355` (servidor) y `e4d3f34` (`navius_android`). UT y postmarketOS no
necesitan nada: allí D-Bus ya hace de despertador.

El servidor ya no vive siempre. Corre **dentro del servicio de Android, en su
propio proceso**, y lo levanta Navius con un Intent explícito —el equivalente de
la activación por D-Bus— y lo para al cerrarse. Un solo binario con dos papeles,
que distingue el `-service` que el manifiesto le pasa al servicio:

- **servicio**: motores + HTTP, sin interfaz.
- **Activity**: gestor de mapas y estado, sin servidor. Solo puede haber uno
  escuchando en el 8553, así que pregunta por HTTP igual que Navius, contra un
  `/v1/status` nuevo que **no es del contrato del original**: es nuestro.

Probado en el móvil en modo avión con el servidor apagado: se abre Navius, sale
el Intent, arranca el proceso del servidor y Navius ya pinta el mapa con sus
tiles. Al cerrar Navius, se para.

**Tres trampas que costaron, y ninguna es evidente:**

1. **ANR «executing service, waited 20001ms».** `QtServiceBase.onCreate()` acaba
   llamando a `QtNative.startApplication()`, que ejecuta `main()` **en el hilo
   desde el que se le llama y no vuelve hasta que la aplicación termina**. En el
   hilo principal del servicio, ese `onCreate` no retorna nunca y Android lo da
   por colgado —con su diálogo en pantalla— aunque el servidor funcione. Se
   arranca Qt en otro hilo, que es lo que Qt ya hace con la Activity.
2. **ANR «did not then call Service.startForeground()».** Cargar Qt se come el
   plazo que hay para llamarlo. Va en `onCreate` y **antes** de Qt.
3. **Los motores se cargan con `QTimer::singleShot(0)`**, ya dentro del bucle de
   eventos. Cargar Valhalla y el geocoder de un territorio grande pasa de veinte
   segundos, y Android da por colgado lo que tarde más de eso en arrancar.

El tipo del servicio pasó de `dataSync` a **`specialUse`**: `dataSync` tiene tope
de 6 h diarias desde Android 15 y un viaje largo las gasta. Comprobado en el
móvil con `dumpsys activity services`: `types=40000000
fgsHasTimeLimitedType=false`. A cambio hay que declarar para qué es, y está en
el `<property>` del manifiesto.

Del lado de Navius hace falta el bloque **`<queries>`**: desde Android 11 una app
no ve a las demás si no las declara, y sin eso el Intent se bloquea sin decir por
qué —en el log solo sale `AppsFilter: … BLOCKED`—.

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
