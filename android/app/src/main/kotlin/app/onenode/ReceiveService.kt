package app.onenode

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder

// "Receive" = this foreground service. Android only lets an app keep a network server alive in the background when
// the user can see a notification, so the notification IS the visible sign that this phone is open for files.
class ReceiveService : Service() {
    private val engine get() = (application as OneNodeApp).engine

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            engine.stopReceiving()
            stopSelf()
            return START_NOT_STICKY
        }
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(
            this, 1, Intent(this, ReceiveService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE,
        )
        val notification = Notification.Builder(this, OneNodeApp.CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_onenode)
            .setContentTitle("OneNode is ready to receive")
            .setContentText("Visible as \"${engine.name.value}\" on this Wi-Fi")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .setOngoing(true)
            .build()
        startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        return START_NOT_STICKY // after a crash or a reboot nothing listens until the user turns "Receive" on again
    }

    // Android 15 limits "data sync" services to about 6 hours a day: stop cleanly instead of being killed.
    override fun onTimeout(startId: Int, fgsType: Int) {
        engine.stopReceiving()
        stopSelf()
    }

    override fun onDestroy() {
        engine.onServiceStopped()
        super.onDestroy()
    }

    companion object {
        const val ACTION_STOP = "app.onenode.STOP"
        const val NOTIFICATION_ID = 1
    }
}
