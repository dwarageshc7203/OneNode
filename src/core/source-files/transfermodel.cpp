#include "transfermodel.h"

TransferModel :: TransferModel (CoreBridge *bridge, QObject *parent) : QAbstractListModel (parent)
{
    // Each lambda takes only the arguments it needs: Qt matches slot arguments from the left,
    // so a slot may take fewer arguments than the signal sends (here we skip the fileName).
    connect(bridge, &CoreBridge :: transferStarted, this, &TransferModel :: onStarted);
    connect(bridge, &CoreBridge :: transferProgress, this, [this](quint64 id, const QString &, qint64 done, qint64 total) {
        onProgress(id, done, total);
    });
    connect(bridge, &CoreBridge :: transferFinished, this, [this](quint64 id, const QString &) {
        onFinished(id);
    });
    connect(bridge, &CoreBridge :: transferFailed, this, [this](quint64 id, const QString &, const QString &reason) {
        onFailed(id, reason);
    });
}

int TransferModel :: rowCount (const QModelIndex &parent) const
{
    if (parent.isValid()) { // a flat list has no children
        return 0;
    }
    return items.size();
}

QVariant TransferModel :: data (const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= items.size()) {
        return QVariant();
    }

    const Item &item = items.at(index.row());
    switch (role) {
        case FileNameRole:  return item.fileName;
        case PeerRole:      return item.peer;
        case DirectionRole: return item.direction;
        case StatusRole:    return item.status;
        case DoneRole:      return item.done;
        case TotalRole:     return item.total;
        case ErrorRole:     return item.error;
        default:            return QVariant();
    }
}

QHash <int, QByteArray> TransferModel :: roleNames() const
{
    return {
        { FileNameRole,  "fileName" },
        { PeerRole,      "peer" },
        { DirectionRole, "direction" },
        { StatusRole,    "status" },
        { DoneRole,      "done" },
        { TotalRole,     "total" },
        { ErrorRole,     "error" }
    };
}

int TransferModel :: rowOfId (quint64 id) const
{
    for (int row = 0; row < items.size(); ++row) { // the list is short, so a plain search is fine
        if (items.at(row).id == id) {
            return row;
        }
    }
    return -1;
}

void TransferModel :: onStarted (quint64 id, const QString &fileName, CoreBridge :: Direction direction, qint64 total, const QString &peer)
{
    if (rowOfId(id) != -1) { // already known: ignore a repeated announcement
        return;
    }

    Item item;
    item.id = id;
    item.fileName = fileName;
    item.peer = peer;
    item.direction = (direction == CoreBridge :: Direction :: Send) ? "send" : "receive";
    item.total = static_cast <double> (total);

    // tell the views "a row is about to appear at position 0", change the data, then say "done".
    beginInsertRows(QModelIndex(), 0, 0);
    items.prepend(item);
    endInsertRows();
}

void TransferModel :: onProgress (quint64 id, qint64 done, qint64 total)
{
    const int row = rowOfId(id);
    if (row == -1) { // a transfer we never saw start: ignore
        return;
    }

    items[row].done = static_cast <double> (done);
    items[row].total = static_cast <double> (total);
    emit dataChanged(index(row), index(row), { DoneRole, TotalRole }); // only these two columns changed
}

void TransferModel :: onFinished (quint64 id)
{
    const int row = rowOfId(id);
    if (row == -1) {
        return;
    }

    items[row].status = "done";
    items[row].done = items[row].total; // a finished file is 100 %
    emit dataChanged(index(row), index(row), { StatusRole, DoneRole });
}

void TransferModel :: onFailed (quint64 id, const QString &reason)
{
    const int row = rowOfId(id);
    if (row == -1) {
        return;
    }

    items[row].status = "failed";
    items[row].error = reason;
    emit dataChanged(index(row), index(row), { StatusRole, ErrorRole });
}
