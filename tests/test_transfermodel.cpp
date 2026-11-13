#include "transfermodel.h"

#include <QtTest>
#include <QSignalSpy>

// These tests never touch the network: they fire CoreBridge's signals by hand
// and check what the model does with them.
class TestTransferModel : public QObject {

    Q_OBJECT

private:
    // read one column of one row by its role name, for readable tests
    static QVariant cell (const TransferModel &model, int row, int role)
    {
        return model.data(model.index(row), role);
    }

private slots:

    void startsEmpty()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);
        QCOMPARE(model.rowCount(), 0);
    }

    void startedAddsARowAtTheTop()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);

        emit bridge.transferStarted(1, "a.txt", CoreBridge :: Direction :: Send, 100, "10.0.0.5");
        emit bridge.transferStarted(2, "b.txt", CoreBridge :: Direction :: Receive, 200, "10.0.0.5");

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(cell(model, 0, TransferModel :: FileNameRole).toString(), QString("b.txt")); // newest first
        QCOMPARE(cell(model, 0, TransferModel :: DirectionRole).toString(), QString("receive"));
        QCOMPARE(cell(model, 1, TransferModel :: DirectionRole).toString(), QString("send"));
        QCOMPARE(cell(model, 0, TransferModel :: StatusRole).toString(), QString("active"));
        QCOMPARE(cell(model, 0, TransferModel :: TotalRole).toDouble(), 200.0);
        QCOMPARE(cell(model, 0, TransferModel :: PeerRole).toString(), QString("10.0.0.5"));
    }

    void progressUpdatesTheRightRowOnly()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);
        emit bridge.transferStarted(1, "a.txt", CoreBridge :: Direction :: Send, 100, "10.0.0.5");
        emit bridge.transferStarted(2, "b.txt", CoreBridge :: Direction :: Send, 100, "10.0.0.5");

        QSignalSpy spy(&model, &QAbstractItemModel :: dataChanged);
        emit bridge.transferProgress(1, "a.txt", 40, 100); // id 1 is now row 1 (pushed down by id 2)

        QCOMPARE(spy.count(), 1);
        QCOMPARE(cell(model, 1, TransferModel :: DoneRole).toDouble(), 40.0);
        QCOMPARE(cell(model, 0, TransferModel :: DoneRole).toDouble(), 0.0);
    }

    void finishedMarksDoneAndFull()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);
        emit bridge.transferStarted(1, "a.txt", CoreBridge :: Direction :: Receive, 100, "10.0.0.5");
        emit bridge.transferProgress(1, "a.txt", 90, 100);
        emit bridge.transferFinished(1, "a.txt");

        QCOMPARE(cell(model, 0, TransferModel :: StatusRole).toString(), QString("done"));
        QCOMPARE(cell(model, 0, TransferModel :: DoneRole).toDouble(), 100.0);
    }

    void failedKeepsTheReason()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);
        emit bridge.transferStarted(1, "a.txt", CoreBridge :: Direction :: Send, 100, "10.0.0.5");
        emit bridge.transferFailed(1, "a.txt", "Connection refused");

        QCOMPARE(cell(model, 0, TransferModel :: StatusRole).toString(), QString("failed"));
        QCOMPARE(cell(model, 0, TransferModel :: ErrorRole).toString(), QString("Connection refused"));
    }

    void unknownIdsAreIgnored()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);

        emit bridge.transferProgress(99, "x", 1, 2);
        emit bridge.transferFinished(99, "x");
        emit bridge.transferFailed(99, "x", "boom");

        QCOMPARE(model.rowCount(), 0); // nothing appeared, nothing crashed
    }

    void repeatedStartIsIgnored()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);
        emit bridge.transferStarted(1, "a.txt", CoreBridge :: Direction :: Send, 100, "10.0.0.5");
        emit bridge.transferStarted(1, "a.txt", CoreBridge :: Direction :: Send, 100, "10.0.0.5");

        QCOMPARE(model.rowCount(), 1);
    }

    void roleNamesMatchTransferRow()
    {
        CoreBridge bridge;
        TransferModel model(&bridge);
        const QList <QByteArray> names = model.roleNames().values();

        for (const char *name : { "fileName", "peer", "direction", "status", "done", "total", "error" }) {
            QVERIFY2(names.contains(name), name);
        }
    }
};

QTEST_MAIN(TestTransferModel)
#include "test_transfermodel.moc"
