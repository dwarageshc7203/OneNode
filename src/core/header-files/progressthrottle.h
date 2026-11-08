#ifndef PROGRESSTHROTTLE_H
#define PROGRESSTHROTTLE_H

#include <QElapsedTimer>
#include <QtGlobal>

// Decides whether a progress update is worth emitting: at most one per interval,
// but always the first one and the last one (done >= total).
// just a gatekeeper
class ProgressThrottle
{
public:
    bool shouldEmit(qint64 done, qint64 total)
    {
        // what actually happens here is that signals should be sent only on 3 condition
        // 1) if the transfer processes is starting up (when started flag is false - default value)
        // 2) if the time taken is greater than interval time
        // 3) if the transfer process is done (done equals or greater than total bytes)
        if (!started || done >= total) {
            started = true;
            timer.start();
            return true;
        }
        if (timer.elapsed() >= intervalMs) {
            timer.restart();
            return true;
        }
        return false;
    }

private:
    QElapsedTimer timer; // measures time since the last emitted update
    bool started = false;
    static constexpr int intervalMs = 100; // 10 updates per second is plenty for a progress bar (1 sec = 1000 millisecond)
};

#endif // PROGRESSTHROTTLE_H
