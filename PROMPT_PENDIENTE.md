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

## 2e. Ofrecer los arreglos a upstream — CONVERSACIÓN ABIERTA el 26/09/2026

Rinigus contestó al contacto de Edi. Dos cosas que cambian el panorama:

- **Autoriza la distribución** del port: «sure, as long as you comply with
  GPL». Ya no hay duda sobre publicarlo.
- **Abre la puerta a fusionar**: «maybe we one day can merge it to have a single
  code base that spans from linux to android».

Si eso llega a pasar, lo que hay que ofrecerle está ya aislado y documentado. Son
**dos cambios, y ninguno es específico de Android**:

1. **El sesgo por cercanía del geocoder** (ver 2d y
   `vendor/geocoder-nlp/CAMBIOS.md`). Es el importante: **le afecta hoy en
   Sailfish y en Ubuntu Touch**, no solo aquí. Una línea:
   `zoom = std::min(std::max(zoom, 1), 18)` en vez de `std::max(zoom, 18)`.
   Arregla además un desplazamiento negativo, que es comportamiento indefinido.
2. **El `std::min` de `uhttp/microhttpserver.cpp`** (ver
   `vendor/uhttp/CAMBIOS.md`). Solo se manifiesta compilando con Qt 6, porque
   `QByteArray::size()` pasó de `int` a `qsizetype`. Upstream sigue en Qt 5, así
   que hoy no le duele, pero le dolerá el día que migre.

**Lo que hay que decirle sin que lo pregunte**, porque es lo único que se desvía
de su contrato y no un simple port: **`/v1/status` es una extensión nuestra**. Se
añadió porque con el servidor en su propio proceso, su propia interfaz ya no
tiene los motores a mano. Si se fusionan los códigos, esa es una decisión de
diseño suya, no nuestra.

Lo demás específico de Android es la **activación**, que sustituye a D-Bus:
servicio en primer plano en su propio proceso, Intent explícito desde el cliente,
`<queries>` en el manifiesto del cliente —sin ella Android 11+ bloquea el Intent
en silencio— y `/v1/activate` conservado por compatibilidad pero respondiendo al
instante. Ver 3b.

## 2f. Publicar en Google Play — PENDIENTE, decidido el 26/09/2026

Sin este servidor en la tienda, la navegación sin conexión que **ya se anuncia en
la ficha de Navius** no existe para nadie que no compile. Así que va.

**Hacerlo después de que Navius esté publicada.** Abrir un segundo frente con una
revisión en curso y los 12 testers a medio reunir complica el seguimiento sin
ganar nada.

### Lo que hace falta, por orden de esfuerzo

1. **El mismo salto de toolchain que Navius, y peor.** `targetSdk` está en 35 y
   Play exige 36; y el alineado a páginas de 16 KB aplica igual. La diferencia:
   Navius solo tenía que alinear **una** biblioteca propia, y aquí se compilan
   **siete** —Valhalla, microhttpd, libpostal, marisa, kyotocabinet, SQLite y
   bzip2—. A cada guion de `scripts/` hay que añadirle:

       -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384

   Y subir a Qt 6.9.3 y NDK 27.3, que es lo que ya está instalado en `ia`. Ver
   la sección equivalente en el `pte.txt` de `navius_android`, que tiene el
   procedimiento y el comando de verificación con `readelf`.

2. **Clave de firma.** Hoy se firma con la de depuración. Lo sensato es
   **reutilizar `~/navius-keys/navius-upload.jks` con otro alias** en vez de
   crear una segunda clave irreemplazable que custodiar. Play lo permite: la
   clave de subida puede ser la misma para varias apps.

3. **Ficha propia entera**: icono 512, gráfico 1024×500, capturas, descripciones
   y las once declaraciones de contenido. Lo único rápido de verdad es
   **Seguridad de los datos**, que es casi vacía: el servidor no recoge nada del
   usuario, solo descarga mapas del catálogo de Rinigus.

4. **GPL en Play** no es problema —a diferencia de Apple— y el repositorio
   público ya cumple la obligación de ofrecer el código.

### Lo que hay que averiguar antes, sin suponerlo

**Si el requisito de 12 testers durante 14 días se aplica por cuenta o por
aplicación.** La impresión es que el acceso a producción se concede a la cuenta y
que una vez aprobada no se repite, pero **no está confirmado**. Se verá en la
consola cuando Navius supere la prueba. De eso depende que esto sea «subir y ya»
o volver a empezar con dos semanas de calendario.

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
