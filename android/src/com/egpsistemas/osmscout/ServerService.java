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
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Servicio en primer plano que mantiene vivo el proceso del servidor.
 *
 * No sirve HTTP ni toca Valhalla: eso vive en el proceso de Qt, donde lo arranca
 * main.cpp. Lo unico que hace este servicio es tener una notificacion
 * permanente, que es lo que le dice a Android que el proceso no se puede matar
 * aunque el usuario se vaya a otra app.
 *
 * Hace falta porque el caso de uso es precisamente ese: el usuario abre Navius,
 * y esta app se queda detras. Sin servicio en primer plano, Android la mata en
 * cuanto necesita memoria —y un movil navegando con mapas cargados la necesita—
 * asi que las rutas dejarian de calcularse a mitad de viaje.
 */
public class ServerService extends Service
{
    private static final String CHANNEL_ID = "osmscout_server";
    private static final int    NOTIF_ID   = 1;

    /** La llama Qt por JNI cuando el servidor ya esta escuchando. */
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

    @Override
    public IBinder onBind(Intent intent)
    {
        return null;   // no se une nadie: se arranca y se para, nada mas
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId)
    {
        createChannel();

        final Notification notification =
                new Notification.Builder(this, CHANNEL_ID)
                        .setContentTitle("OSM Scout Server")
                        .setContentText("Mapas sin conexión disponibles en el puerto 8553")
                        .setSmallIcon(android.R.drawable.ic_menu_mapmode)
                        .setOngoing(true)
                        .build();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
            startForeground(NOTIF_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        else
            startForeground(NOTIF_ID, notification);

        // START_STICKY: si aun asi Android lo mata, que lo vuelva a levantar.
        return START_STICKY;
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
