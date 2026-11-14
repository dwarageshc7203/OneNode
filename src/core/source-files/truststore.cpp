#include "truststore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

TrustStore :: TrustStore (const QString &folder) : filePath(QDir(folder).filePath("trusted_devices.json"))
{
}

bool TrustStore :: isValidFingerprint (const QString &text)
{
    static const QRegularExpression pattern("^[0-9a-f]{64}$");
    return pattern.match(text).hasMatch();
}

QString TrustStore :: cleanName (const QString &name)
{
    QString cleaned;
    for (const QChar c : name) {
        if (c.isSpace()) {
            cleaned.append(' '); // tabs and line breaks become a plain space (simplified() below tidies the runs)
        } else if (!c.isNonCharacter() && c.category() != QChar :: Other_Control && c.category() != QChar :: Other_Format
            && c.category() != QChar :: Separator_Line && c.category() != QChar :: Separator_Paragraph) {
            cleaned.append(c);
        }
    }
    cleaned = cleaned.simplified(); // trims and turns any run of spaces/tabs into one space
    cleaned = cleaned.left(64);
    return cleaned.isEmpty() ? QStringLiteral("Unknown device") : cleaned;
}

QString TrustStore :: load()
{
    list.clear();
    readOnly = false;

    QFile file(filePath);
    if (!file.exists()) {
        return QString(); // first start: nobody is trusted yet
    }

    // from here on, any problem means: keep the list empty and refuse to write
    readOnly = true;

    if (!file.open(QIODevice :: ReadOnly)) {
        return "Could not read the list of paired devices (" + filePath + ").";
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument :: fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError :: NoError || !document.isObject()) {
        return "The list of paired devices is damaged (" + filePath + "). It was not changed.";
    }

    const QJsonObject root = document.object();
    if (root.value("schemaVersion").toInt(-1) != 1) {
        return "The list of paired devices was made by a different version of OneNode (" + filePath + "). It was not changed.";
    }

    QList<Device> loaded;
    const QJsonArray entries = root.value("devices").toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString fingerprint = entry.value("fingerprint").toString();
        if (!isValidFingerprint(fingerprint)) {
            return "The list of paired devices is damaged (" + filePath + "). It was not changed.";
        }
        loaded.append({fingerprint, cleanName(entry.value("name").toString()), entry.value("pairedAt").toString()});
    }

    list = loaded;
    readOnly = false;
    return QString();
}

bool TrustStore :: isTrusted (const QString &fingerprint) const
{
    const QString wanted = fingerprint.toLower();
    for (const Device &device : list) {
        if (device.fingerprint == wanted) {
            return true;
        }
    }
    return false;
}

QString TrustStore :: nameOf (const QString &fingerprint) const
{
    const QString wanted = fingerprint.toLower();
    for (const Device &device : list) {
        if (device.fingerprint == wanted) {
            return device.name;
        }
    }
    return QString();
}

QList<TrustStore :: Device> TrustStore :: devices() const
{
    return list;
}

QString TrustStore :: add (const QString &fingerprint, const QString &name)
{
    if (readOnly) {
        return "The list of paired devices could not be read, so nothing can be added to it.";
    }
    const QString wanted = fingerprint.toLower();
    if (!isValidFingerprint(wanted)) {
        return "That is not a valid device fingerprint.";
    }

    const QList<Device> before = list;

    bool found = false;
    for (Device &device : list) {
        if (device.fingerprint == wanted) {
            device.name = cleanName(name); // same device, maybe renamed: keep ONE entry and its original pairing time
            found = true;
        }
    }
    if (!found) {
        list.append({wanted, cleanName(name), QDateTime :: currentDateTimeUtc().toString(Qt :: ISODate)});
    }

    const QString problem = save();
    if (!problem.isEmpty()) {
        list = before; // not saved, so not trusted either
    }
    return problem;
}

QString TrustStore :: remove (const QString &fingerprint)
{
    if (readOnly) {
        return "The list of paired devices could not be read, so nothing can be removed from it.";
    }

    const QString wanted = fingerprint.toLower();
    const QList<Device> before = list;

    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).fingerprint == wanted) {
            list.removeAt(i);
            break;
        }
    }

    const QString problem = save();
    if (!problem.isEmpty()) {
        list = before;
    }
    return problem;
}

QString TrustStore :: save() const
{
    QJsonArray entries;
    for (const Device &device : list) {
        QJsonObject entry;
        entry["fingerprint"] = device.fingerprint;
        entry["name"] = device.name;
        entry["pairedAt"] = device.pairedAt;
        entries.append(entry);
    }
    QJsonObject root;
    root["schemaVersion"] = 1;
    root["devices"] = entries;

    QDir().mkpath(QFileInfo(filePath).absolutePath());

    // QSaveFile writes to a temporary file and renames it over the real one on commit(),
    // so a crash or a full disk never leaves a half-written list
    QSaveFile file(filePath);
    if (!file.open(QIODevice :: WriteOnly)) {
        return "Could not save the list of paired devices (" + filePath + ").";
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument :: Indented));
    if (!file.commit()) {
        return "Could not save the list of paired devices (" + filePath + ").";
    }
    return QString();
}
