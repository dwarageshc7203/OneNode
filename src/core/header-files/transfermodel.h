#ifndef TRANSFERMODEL_H
#define TRANSFERMODEL_H

#include "corebridge.h"

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// The "clerk" between CoreBridge and the screen.
// It listens to CoreBridge's four signals and keeps one row per transfer.
// A QML ListView can show it directly, and redraws by itself when a row changes.
class TransferModel : public QAbstractListModel {

    Q_OBJECT

public:
    // The columns ("roles") of the model. QML reads them by NAME (see roleNames()),
    // and the names must match the "required property" names in TransferRow.qml.
    enum Role {
        FileNameRole = Qt :: UserRole + 1, // Qt :: UserRole is where custom roles start
        PeerRole,
        DirectionRole,
        StatusRole,
        DoneRole,
        TotalRole,
        ErrorRole
    };

    explicit TransferModel (CoreBridge *bridge, QObject *parent = nullptr);

    // the three methods every list model must provide
    int rowCount (const QModelIndex &parent = QModelIndex()) const override;
    QVariant data (const QModelIndex &index, int role) const override;
    QHash <int, QByteArray> roleNames() const override;

private:
    // one row of the list
    struct Item {
        quint64 id = 0;
        QString fileName;
        QString peer;
        QString direction = "send"; // "send" or "receive"
        QString status = "active";  // "active", "done" or "failed"
        double done = 0;            // double, not qint64: QML numbers must not overflow
        double total = 0;
        QString error;
    };

    QVector <Item> items; // index 0 = newest transfer (shown at the top)

    int rowOfId (quint64 id) const; // the row that has this id, or -1
    void onStarted (quint64 id, const QString &fileName, CoreBridge :: Direction direction, qint64 total, const QString &peer);
    void onProgress (quint64 id, qint64 done, qint64 total);
    void onFinished (quint64 id);
    void onFailed (quint64 id, const QString &reason);
};

#endif // TRANSFERMODEL_H
