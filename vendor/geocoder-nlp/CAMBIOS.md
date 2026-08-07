# Cambios sobre el geocoder-nlp original

Copia de `geocoder-nlp` de rinigus. Se anota aquí todo lo que se desvía del
original, para que se pueda rehacer si algún día se actualiza.

## `geocoder.cpp` — `search_rank_location_bias()`

```c
- zoom = std::max(zoom, 18);
+ zoom = std::min(std::max(zoom, 1), 18);
```

El original acota el zoom **por abajo** a 18, y con eso `(18 - zoom)` nunca es
positivo: el radio del sesgo se queda clavado en 250 m pase lo que pase, así que
más allá de un par de kilómetros `exp(-distancia/250)` es cero y el punto de
referencia deja de influir. Se ve enseguida: buscar «Barcelona» estando en
Barcelona devolvía calles y parajes de Salamanca, León, Jaén y Almería, idéntico
con referencia y sin ella.

De paso arregla un comportamiento indefinido: con `zoom > 18` el desplazamiento
`1 << (18 - zoom)` es negativo.

La fórmula viene de Photon, que acota el zoom **por arriba** a 18 —que es lo que
tiene sentido: menos zoom, radio más grande—. Con el arreglo, `zoom=12` da un
radio de 16 km, que es la escala a la que se busca un destino conduciendo.
