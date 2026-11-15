package app.onenode

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager

class OneNodeApp : Application() {
    // One Engine for the whole process: the screen, the receive service and the share sheet all talk to it.
    lateinit var engine: Engine
        private set

    override fun onCreate() {
        super.onCreate()
        val channel = NotificationChannel(CHANNEL, "Receiving", NotificationManager.IMPORTANCE_LOW).apply {
            description = "Shown while OneNode is ready to receive files"
            setShowBadge(false)
        }
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        engine = Engine(this)
    }

    companion object {
        const val CHANNEL = "receiving"
    }
}
