/* Comprueba que microtar deja leer una pieza en varias tandas, que es en lo que
   se apoya la extraccion por trozos de MapManager::extractTar (mapmanager.cpp).
   Antes se leia la pieza entera de una vez y en RAM, y un .tar de mapas trae
   dentro ficheros de cientos de megas: en un movil eso mata la app.

   Se compila y se ejecuta en el anfitrion, sin Android de por medio:

       gcc -I vendor/microtar tests/extraccion_por_trozos.c \
           vendor/microtar/microtar.c -o /tmp/prueba && /tmp/prueba
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "microtar.h"

int main(void) {
    const unsigned GRANDE = 9u << 20;   /* 9 MB, mas de dos trozos de 4 */
    char *datos = malloc(GRANDE);
    for (unsigned i = 0; i < GRANDE; i++) datos[i] = (char)(i * 31 + 7);

    mtar_t tar;
    mtar_open(&tar, "/tmp/prueba.tar", "w");
    mtar_write_file_header(&tar, "grande.bin", GRANDE);
    mtar_write_data(&tar, datos, GRANDE);
    mtar_write_file_header(&tar, "chico.txt", 5);
    mtar_write_data(&tar, "hola\n", 5);
    mtar_finalize(&tar);
    mtar_close(&tar);

    mtar_open(&tar, "/tmp/prueba.tar", "r");
    mtar_header_t h;
    int piezas = 0, fallos = 0;
    while (mtar_read_header(&tar, &h) == MTAR_ESUCCESS) {
        if (h.type == MTAR_TREG) {
            const unsigned CHUNK = 4u << 20;
            char *buf = malloc(CHUNK);
            unsigned restante = h.size, leidos = 0;
            int tandas = 0;
            while (restante > 0) {
                unsigned n = restante < CHUNK ? restante : CHUNK;
                if (mtar_read_data(&tar, buf, n) != MTAR_ESUCCESS) { fallos++; break; }
                /* el contenido tiene que salir en orden */
                for (unsigned i = 0; i < n; i++)
                    if (!strcmp(h.name, "grande.bin") && buf[i] != (char)((leidos + i) * 31 + 7))
                        { fallos++; restante = n; break; }
                leidos += n; restante -= n; tandas++;
            }
            printf("%-11s %8u bytes en %d tandas%s\n", h.name, leidos, tandas,
                   leidos == h.size ? "" : "  <-- TAMANO MAL");
            if (leidos != h.size) fallos++;
            free(buf);
            piezas++;
        }
        if (mtar_next(&tar) != MTAR_ESUCCESS) break;
    }
    mtar_close(&tar);
    printf("%d piezas, %d fallos\n", piezas, fallos);
    return fallos ? 1 : 0;
}
