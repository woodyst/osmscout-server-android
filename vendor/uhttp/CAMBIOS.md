# uhttp — cambios respecto a OSM Scout Server

Estos ficheros vienen de `server/src/uhttp/` de
[osmscout-server](https://github.com/rinigus/osmscout-server) y son
**GPL-3.0-or-later**, de Rinigus. Se copian lo más literalmente posible para
poder seguir sus cambios; lo que se toque va anotado aquí y marcado en el código
con un comentario que empieza por `EGP:`.

Origen: rama `master`, commit `910ea5a`.

## Cambios

### `microhttpserver.cpp` — `std::min` no deduce el tipo (Qt 6)

```cpp
- size_t tosend = std::min(max, data.size() - p);
+ size_t tosend = std::min<size_t>(max, (size_t)data.size() - p);
```

En Qt 6 `QByteArray::size()` devuelve `qsizetype`, que en arm64 es `long long`,
mientras que el `max` que pasa libmicrohttpd es `size_t`, o sea `unsigned long`.
Son dos tipos distintos del mismo tamaño, y `std::min` es una plantilla con un
único parámetro de tipo, así que no compila. Upstream va con Qt 5, donde
`size()` es `int` y la expresión acaba promocionando a algo que sí deduce.

No es específico de Android: le pasaría a cualquiera que compilase este fichero
con Qt 6.

## Lo que NO se ha tocado

- El `HAS_MICRO_HTTP_CLEANUP_TIMER`, que está comentado en upstream y trae la
  única dependencia real de `QObject`. Se deja igual.
- La API de `ServiceBase`, que es por donde engancha nuestro `RouteService`.
