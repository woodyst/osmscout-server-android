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

#ifndef CERRTOLOG_H
#define CERRTOLOG_H

#include <QDebug>

#include <iostream>
#include <streambuf>
#include <string>

/// Manda std::cerr a logcat.
///
/// Hace falta porque en Android NADIE ve la salida estandar de error: Qt no la
/// vuelca a logcat, asi que todo lo que escriba a cerr desaparece. Y el codigo
/// vendorizado —geocoder-nlp sobre todo— informa de sus errores exactamente asi:
///
///     std::cerr << "Geocoder exception: " << e.what() << std::endl;
///
/// El resultado era que una consulta fallaba y lo unico que se sabia es que
/// habia devuelto false. Se redirige en vez de parchear los ficheros, para no
/// separarlos de upstream y porque vale para todos a la vez.
class CerrToLog : public std::streambuf
{
public:
    CerrToLog() { m_saved = std::cerr.rdbuf(this); }
    ~CerrToLog() override { std::cerr.rdbuf(m_saved); }

protected:
    // Se acumula hasta el salto de linea: logcat trabaja por mensajes, y sacar
    // un registro por caracter seria ilegible.
    int overflow(int c) override
    {
        if (c == traits_type::eof())
            return c;
        if (c == '\n')
            flushLine();
        else
            m_line += char(c);
        return c;
    }

    int sync() override
    {
        flushLine();
        return 0;
    }

private:
    void flushLine()
    {
        if (m_line.empty())
            return;
        qWarning().noquote() << "OSMSCOUT[cerr]:" << QString::fromStdString(m_line);
        m_line.clear();
    }

    std::streambuf *m_saved{nullptr};
    std::string m_line;
};

#endif // CERRTOLOG_H
