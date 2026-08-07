import QtQuick
import QtQuick.Window
import QtQuick.Controls

// Pantalla del servidor: estado de los motores y gestor de mapas.
Window {
    id: win
    visible: true
    title: "OSM Scout Server"
    color: "#0D1B2A"

    readonly property bool activo: serverOk && (engineOk || mapboxOk || geoOk)

    Column {
        id: cabecera
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 16 }
        spacing: 12

        Text {
            text: "OSM Scout Server"
            color: "white"; font.pixelSize: 26; font.bold: true
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Rectangle {
            width: parent.width
            height: estado.implicitHeight + 24
            radius: 8
            color: "#1C2D40"
            border.color: win.activo ? "#66BB6A" : "#EF5350"

            Column {
                id: estado
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                spacing: 4

                Text {
                    // El ✗ (U+2717) sale como cuadradito en este movil, aunque el
                    // ✓ si esta. Esta app no empaqueta fuente de simbolos como
                    // Navius, asi que se usa solo lo que el sistema garantiza.
                    text: win.activo ? "✓ Activo · 127.0.0.1:" + serverPort
                                     : "No disponible"
                    color: win.activo ? "#66BB6A" : "#EF5350"
                    font.pixelSize: 18; font.bold: true
                }
                Text {
                    text: "Rutas " + (engineOk ? "✓" : "—")
                          + "   Mapa " + (mapboxOk ? "✓ (" + sectionCount + ")" : "—")
                          + "   Búsqueda " + (geoOk ? "✓ " + territory : "—")
                    color: "#90A4AE"; font.pixelSize: 14
                    wrapMode: Text.Wrap; width: parent.width
                }
            }
        }

        Row {
            spacing: 10
            Button {
                text: "Actualizar catálogo"
                enabled: !mapManager.busy
                onClicked: mapManager.refreshCatalogue()
            }
            Text {
                text: mapManager.status
                color: "#90A4AE"; font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
                width: win.width - 200; elide: Text.ElideRight
            }
        }

        ProgressBar {
            width: parent.width
            visible: mapManager.busy
            from: 0; to: 100
            value: mapManager.progress
        }

        // Los motores se cargan al arrancar, asi que un mapa recien descargado
        // no se usa hasta reiniciar. Se avisa y se da el boton, en vez de
        // rearrancarlos en caliente: Valhalla y el geocoder mapean ficheros en
        // memoria y recargarlos con peticiones en vuelo es pedir problemas.
        Rectangle {
            width: parent.width
            height: aviso.implicitHeight + 20
            radius: 6
            color: "#2A1F0D"
            border.color: "#FFA726"
            visible: mapManager.installed.length > 0 && !win.activo

            Column {
                id: aviso
                anchors { left: parent.left; right: parent.right
                          verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 8

                Text {
                    text: "Hay mapas descargados que aún no se están usando.\n"
                          + "Reinicia el servidor para cargarlos."
                    color: "#FFCC80"; font.pixelSize: 14
                    wrapMode: Text.Wrap; width: parent.width
                }
                Button {
                    text: "Reiniciar servidor"
                    enabled: !mapManager.busy
                    onClicked: Qt.quit()
                }
            }
        }

        // ── Instalados ──────────────────────────────────────────────────────
        // Aparte de la lista de disponibles: son pocos y lo que se quiere hacer
        // con ellos —ver que hay y quitar lo que sobra— no tiene nada que ver
        // con buscar entre 444.
        Column {
            width: parent.width
            spacing: 4
            visible: mapManager.installed.length > 0

            Text {
                text: "Instalados"
                color: "#90A4AE"; font.pixelSize: 14; font.bold: true
            }

            Repeater {
                model: mapManager.installed
                delegate: Item {
                    width: parent.width
                    height: 40

                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        width: parent.width - 130
                        text: modelData
                        color: "#66BB6A"; font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                    Button {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: "Desinstalar"
                        enabled: !mapManager.busy
                        onClicked: mapManager.uninstall(modelData)
                    }
                }
            }
        }
    }

    // Lista de territorios agrupada por continente. Son 444 en el catálogo, y
    // sin agrupar hay que recorrer la pantalla entera para llegar a Europa.
    // El filtro, cuando se usa, salta la agrupación y enseña las coincidencias
    // directamente: buscar «spain» no debería obligar a abrir un desplegable.
    Column {
        anchors { top: cabecera.bottom; left: parent.left; right: parent.right
                  bottom: parent.bottom; margins: 16; topMargin: 12 }
        spacing: 8

        TextField {
            id: filtro
            width: parent.width
            placeholderText: "Filtrar territorio (p. ej. spain)"
            // El estilo por defecto pinta el texto oscuro y sobre este fondo no
            // se lee. Se cambian SOLO los colores: al sustituir tambien el
            // background, el control pierde la geometria que le da el estilo y
            // el recuadro acaba tapando el texto.
            color: "#ECEFF1"
            placeholderTextColor: "#607D8B"
        }

        ListView {
            id: lista
            width: parent.width
            height: parent.height - filtro.height - 8
            clip: true
            spacing: 2

            property var abiertos: ({})

            // Con filtro: lista plana de coincidencias. Sin filtro: un elemento
            // por continente, y debajo los suyos si está desplegado.
            model: {
                var t = mapManager.territories
                var f = filtro.text.toLowerCase()
                if (f !== "")
                    return t.filter(function(x) { return x.toLowerCase().indexOf(f) >= 0 })
                                .map(function(x) { return { tipo: "hoja", id: x } })

                var grupos = []
                var vistos = {}
                for (var i = 0; i < t.length; i++) {
                    var cont = t[i].indexOf("/") > 0 ? t[i].split("/")[0] : t[i]
                    if (!vistos[cont]) {
                        vistos[cont] = []
                        grupos.push(cont)
                    }
                    vistos[cont].push(t[i])
                }

                var out = []
                for (var g = 0; g < grupos.length; g++) {
                    var c = grupos[g]
                    out.push({ tipo: "grupo", id: c, cuantos: vistos[c].length })
                    if (lista.abiertos[c])
                        for (var j = 0; j < vistos[c].length; j++)
                            out.push({ tipo: "hoja", id: vistos[c][j] })
                }
                return out
            }

            delegate: Item {
                width: lista.width
                height: 46

                // ── Continente ──────────────────────────────────────────────
                Rectangle {
                    anchors.fill: parent
                    visible: modelData.tipo === "grupo"
                    color: "#16243440"

                    Text {
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        // "+"/"−" y no triangulos: a la fuente del sistema le
                        // faltan los glifos ▸▾ y salen como cuadraditos. Es el
                        // mismo problema que Navius resolvio empaquetando un
                        // subconjunto de FreeSerif; aqui no compensa por dos
                        // caracteres.
                        text: (lista.abiertos[modelData.id] ? "−  " : "+  ")
                              + modelData.id + "   (" + modelData.cuantos + ")"
                        color: "#90CAF9"; font.pixelSize: 16; font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            var a = lista.abiertos
                            a[modelData.id] = !a[modelData.id]
                            lista.abiertos = a   // reasignar para que el modelo se reevalúe
                        }
                    }
                }

                // ── Territorio ──────────────────────────────────────────────
                Item {
                    anchors.fill: parent
                    visible: modelData.tipo === "hoja"
                    property bool yaEsta: mapManager.installed.indexOf(modelData.id) >= 0

                    Text {
                        anchors { left: parent.left; leftMargin: filtro.text === "" ? 28 : 4
                                  verticalCenter: parent.verticalCenter }
                        width: parent.width - 130
                        text: modelData.id
                        color: parent.yaEsta ? "#66BB6A" : "#CFD8DC"
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                    Button {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: parent.yaEsta ? "Reinstalar" : "Instalar"
                        enabled: !mapManager.busy
                        onClicked: mapManager.install(modelData.id)
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 1; color: "#1C2D40"
                    }
                }
            }
        }
    }

    Text {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; margins: 4 }
        text: mapManager.territories.length === 0
              ? "Pulsa «Actualizar catálogo» para ver los mapas disponibles"
              : ""
        color: "#607D8B"; font.pixelSize: 13
    }
}
