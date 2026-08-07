import QtQuick
import QtQuick.Window

// Pantalla minima: estado del servidor y poco mas. La interfaz de verdad —mapas
// instalados, descargas, espacio ocupado— es de la fase 6.
Window {
    visible: true
    title: "OSM Scout Server"
    color: "#0D1B2A"

    Column {
        anchors.centerIn: parent
        spacing: 18
        width: parent.width * 0.85

        Text {
            text: "OSM Scout Server"
            color: "white"
            font.pixelSize: 28
            font.bold: true
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Rectangle {
            width: parent.width
            height: estado.implicitHeight + 32
            radius: 8
            color: "#1C2D40"
            border.color: (serverOk && (engineOk || mapboxOk)) ? "#66BB6A" : "#EF5350"

            Column {
                id: estado
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 8

                Text {
                    text: (serverOk && (engineOk || mapboxOk)) ? "✓ Activo" : "✗ No disponible"
                    color: (serverOk && (engineOk || mapboxOk)) ? "#66BB6A" : "#EF5350"
                    font.pixelSize: 22
                    font.bold: true
                }
                Text {
                    text: "Rutas (Valhalla): " + (engineOk ? "listo" : "sin mapas")
                    color: "#90A4AE"; font.pixelSize: 16
                }
                Text {
                    text: "Mapa (tiles): " + (mapboxOk ? sectionCount + " secciones" : "sin mapas")
                    color: "#90A4AE"; font.pixelSize: 16
                }
                Text {
                    text: "HTTP: " + (serverOk ? "escuchando en 127.0.0.1:" + serverPort
                                               : "no se pudo abrir el puerto")
                    color: "#90A4AE"; font.pixelSize: 16
                    wrapMode: Text.Wrap; width: parent.width
                }
                Text {
                    text: "Mapas: " + tileDir
                    color: "#607D8B"; font.pixelSize: 13
                    wrapMode: Text.WrapAnywhere; width: parent.width
                }
            }
        }

        Text {
            text: "Deja esta app abierta mientras navegas con Navius."
            color: "#607D8B"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
