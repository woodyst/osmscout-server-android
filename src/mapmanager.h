/*
 * Copyright (C) 2026 EGP Sistemas
 *
 * This file is part of OSM Scout Server for Android.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MAPMANAGER_H
#define MAPMANAGER_H

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QFile>
#include <QQueue>
#include <QString>
#include <QStringList>

#include <bzlib.h>

/// Descarga de mapas. Equivalente reducido del MapManager de OSM Scout Server:
/// mismo servidor, mismo catalogo y mismas rutas, pero sin reanudacion ni
/// comprobacion de versiones, que son mejoras posteriores.
///
/// Dos diferencias obligadas respecto al original, las dos por lo mismo —el
/// original lanza procesos externos y en Android no hay donde:
///
///   - descomprime con libbz2 enlazada en vez de lanzar "bunzip2"
///   - extrae los .tar con microtar en vez de con el tar del sistema
class MapManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QStringList territories READ territories NOTIFY changed)
    Q_PROPERTY(QStringList installed READ installed NOTIFY changed)
    Q_PROPERTY(QString pending READ pending NOTIFY changed)

public:
    explicit MapManager(const QString &mapsDir, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int progress() const { return m_progress; }
    QStringList territories() const { return m_territories; }
    QStringList installed() const;

    /// Territorio que se quedo a medias, si lo hay. Vacio cuando no hay nada
    /// pendiente. Es lo que permite ofrecer «Reanudar» al volver a abrir.
    QString pending() const { return m_pending; }

    /// Baja el catalogo del servidor. Sin el no se puede instalar nada.
    Q_INVOKABLE void refreshCatalogue();

    /// Descarga e instala un territorio entero: rutas, tiles, busqueda y los
    /// datos de libpostal de su pais.
    Q_INVOKABLE void install(const QString &id);

    /// Borra un territorio, respetando lo que compartan los demas.
    Q_INVOKABLE void uninstall(const QString &id);

    /// Sigue la descarga que se quedo a medias, por donde iba.
    Q_INVOKABLE void resume();

    /// Tira lo que se llevaba descargado del territorio a medias y se olvida de
    /// el. Sin esto, cortar una instalacion dejaba los ficheros en disco sin
    /// constar como instalados: ocupando sitio, sin salir en la lista y sin
    /// forma de quitarlos desde la interfaz. Paso de verdad con Argelia, 5 GB.
    Q_INVOKABLE void discard();

signals:
    void changed();
    void finished(bool ok, const QString &message);

private:
    struct Job {
        QString url;       ///< de donde se baja
        QString dest;      ///< donde acaba, ya descomprimido
        bool    isTar{false};  ///< si hay que extraerlo despues
    };

    void reloadTerritories();
    void setStatus(const QString &s, int progress = -1);

    /// El fichero de progreso. Se escribe al empezar y despues de CADA fichero,
    /// no al final: si se escribiera al final, un corte a mitad no dejaria
    /// constancia de nada, que es justo lo que pasaba antes.
    void saveProgress();
    void clearProgress();
    bool loadProgress();

    /// Si el fichero ya esta en disco no se vuelve a bajar. Vale porque se
    /// escribe a <fichero>.part y solo se renombra al terminar: lo que tiene el
    /// nombre bueno esta completo. De los .tar queda su .list, que es la senal
    /// de que se extrajeron.
    bool yaEsta(const Job &job) const;

    /// Borra los datos de un territorio respetando lo que compartan los otros.
    /// Lo usan uninstall() y discard(): el primero con lo instalado, el segundo
    /// con lo que se quedo a medias.
    int borrarDatos(const QJsonObject &mio, const QJsonObject &otros);

    void enqueueFeature(const QJsonObject &territory, const QString &feature,
                        const QStringList &files);
    void enqueueGlobalIfMissing(const QJsonObject &cat, const QString &id,
                                const QString &feature, const QStringList &files);
    void enqueuePackages(const QJsonObject &territory, const QString &feature,
                         const QString &subdir);
    void next();

    // Descompresion por bloques. No se puede hacer de una vez: las secciones de
    // tiles pasan de 200 MB y, entre el buffer de descarga y el de salida, el
    // proceso se iba de medio giga y Android lo mataba.
    bool streamStart(const QString &dest);
    bool streamFeed(const QByteArray &chunk);
    bool streamFinish(const QString &dest);
    void streamAbort();
    bool extractTar(const QString &tarPath, const QString &destDir);

    QString featureUrl(const QString &feature, const QString &rel) const;
    QString fullPath(const QString &rel) const;
    QJsonObject catalogue() const;

    QNetworkAccessManager m_net;
    QString m_mapsDir;
    QString m_serverUrl;
    QString m_status;
    QStringList m_territories;
    QQueue<Job> m_queue;
    QString m_installing;
    QString m_pending;   ///< territorio a medias segun el fichero de progreso
    QFile m_out;         ///< se escribe a <dest>.part y se renombra al acabar
    bz_stream m_bz;
    bool m_bzActive{false};   ///< hay un flujo bzip2 abierto
    bool m_bzDecided{false};  ///< ya se sabe si venia comprimido o no
    QByteArray m_head;        ///< primeros bytes, para mirar la firma "BZh"
    int  m_total{0};
    int  m_progress{0};
    bool m_busy{false};
};

#endif // MAPMANAGER_H
