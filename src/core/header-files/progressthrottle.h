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
    static constexpr int intervalMs = 100; // 10 updates per second is plenty for a progress bar
};

#endif // PROGRESSTHROTTLE_H
