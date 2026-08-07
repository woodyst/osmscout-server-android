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

package com.egpsistemas.osmscout;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.util.Log;

import org.qtproject.qt.android.bindings.QtService;

/**
 * El servidor de mapas, como servicio de Android.
 *
 * Antes esto era un servicio vacio: el servidor HTTP y Valhalla vivian en el
 * proceso de la Activity, y el servicio solo existia para que Android no lo
 * matara al pasar el usuario a Navius. Eso obligaba a tenerlo encendido siempre.
 *
 * Ahora el servidor corre AQUI. Al extender QtService, Android arranca el Qt de
 * este proceso y ejecuta main(), al que el manifiesto le pasa «-service» para
 * que levante los motores y el HTTP sin interfaz ninguna. Con eso lo puede
 * despertar Navius con un Intent explicito cuando lo necesita, que es el
 * equivalente de lo que hace D-Bus en Ubuntu Touch, y el resto del tiempo no
 * hay nada corriendo.
 *
 * Sigue siendo un servicio en PRIMER PLANO mientras vive, y eso no sobra: el
 * caso de uso es justo que el usuario este mirando Navius, y sin la notificacion
 * permanente Android mata el proceso en cuanto necesita memoria —un movil
 * navegando con mapas cargados la necesita— y las rutas dejarian de calcularse
 * a mitad de viaje.
 */
public class ServerService extends QtService
{
    private static final String TAG        = "OSMSCOUT";
    private static final String CHANNEL_ID = "osmscout_server";
    private static final int    NOTIF_ID   = 1;

    /** La llama la interfaz por JNI. Navius manda el Intent por su cuenta. */
    public static void start(Context context)
    {
        final Intent intent = new Intent(context, ServerService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            context.startForegroundService(intent);
        else
            context.startService(intent);
    }

    public static void stop(Context context)
    {
        context.stopService(new Intent(context, ServerService.class));
    }

    /**
     * Lo PRIMERO, y antes de que Qt haga nada.
     *
     * Quien arranca con startForegroundService() tiene un plazo corto para
     * llamar a startForeground(), y si no lo cumple Android mata el proceso con
     * un ANR —«did not then call Service.startForeground()»—. Cargar el Qt de
     * este proceso, con libvalhalla y todo lo demas, se come ese plazo: puesto
     * en onStartCommand no llegaba a tiempo. Aqui si.
     */
    @Override
    public void onCreate()
    {
        irAPrimerPlano();

        // Y Qt, en OTRO hilo. QtServiceBase.onCreate() acaba llamando a
        // QtNative.startApplication(), que ejecuta main() en el hilo desde el
        // que se le llama y no vuelve hasta que la aplicacion termina. Puesto
        // en el hilo principal del servicio, ese onCreate no retorna nunca y
        // Android lo da por colgado: ANR «executing service, waited 20001ms»,
        // con su dialogo en pantalla, aunque el servidor este funcionando.
        //
        // En el caso de la Activity, Qt ya hace esto mismo: main() corre en un
        // hilo aparte. Aqui solo se replica.
        new Thread(new Runnable() {
            @Override
            public void run()
            {
                ServerService.super.onCreate();
            }
        }, "osmscout-qt").start();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId)
    {
        // Repetirlo no molesta —solo actualiza la notificacion— y cubre el caso
        // de que el servicio ya estuviera creado y solo llegue el arranque.
        irAPrimerPlano();
        super.onStartCommand(intent, flags, startId);

        // START_STICKY: si aun asi Android lo mata, que lo vuelva a levantar.
        return START_STICKY;
    }

    private void irAPrimerPlano()
    {
        createChannel();

        final Notification notification =
                new Notification.Builder(this, CHANNEL_ID)
                        .setContentTitle("OSM Scout Server")
                        .setContentText("Mapas sin conexión disponibles en el puerto 8553")
                        .setSmallIcon(android.R.drawable.ic_menu_mapmode)
                        .setOngoing(true)
                        .build();

        // specialUse y no dataSync: desde Android 15, dataSync tiene un tope de
        // 6 h al dia y al agotarlo el sistema para el servicio. Para algo que
        // tiene que responder conduciendo eso no sirve, y un viaje largo las
        // gasta. specialUse no tiene tope; a cambio hay que declarar para que es,
        // y esta en el <property> del manifiesto.
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
                startForeground(NOTIF_ID, notification,
                                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            else
                startForeground(NOTIF_ID, notification);
            Log.i(TAG, "en primer plano");
        } catch (Exception e) {
            // Si falla, el servidor sigue arrancando: lo que se pierde es la
            // proteccion contra que Android mate el proceso. Verlo importa.
            Log.e(TAG, "no se pudo pasar a primer plano: " + e);
        }
    }

    private void createChannel()
    {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O)
            return;

        // Importancia baja: es un aviso de estado permanente, no algo que deba
        // sonar ni asomarse por encima de lo que el usuario este haciendo.
        final NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Servidor de mapas", NotificationManager.IMPORTANCE_LOW);
        channel.setShowBadge(false);
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }
}
