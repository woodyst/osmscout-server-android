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
#include <QQueue>
#include <QString>
#include <QStringList>

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

public:
    explicit MapManager(const QString &mapsDir, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int progress() const { return m_progress; }
    QStringList territories() const { return m_territories; }
    QStringList installed() const;

    /// Baja el catalogo del servidor. Sin el no se puede instalar nada.
    Q_INVOKABLE void refreshCatalogue();

    /// Descarga e instala un territorio entero: rutas, tiles, busqueda y los
    /// datos de libpostal de su pais.
    Q_INVOKABLE void install(const QString &id);

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
    void enqueueFeature(const QJsonObject &territory, const QString &feature,
                        const QStringList &files);
    void enqueuePackages(const QJsonObject &territory, const QString &feature,
                         const QString &subdir);
    void next();
    bool writeDecompressed(const QByteArray &data, const QString &dest);
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
    int  m_total{0};
    int  m_progress{0};
    bool m_busy{false};
};

#endif // MAPMANAGER_H
