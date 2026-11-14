#pragma once

#include <QList>
#include <QString>

// The list of devices this device has paired with (PROTOCOL.md section 9).
//
// Analogy: the guest list at a door. Each line is a card number (fingerprint) and the name
// the guest told us. "Paired" simply means "is on the list". The list lives in a file so it
// survives a restart. This class knows files and text only: no network, no QML.
class TrustStore {

public:

    struct Device {
        QString fingerprint; // 64 lowercase hex characters
        QString name;        // what the device called itself, already cleaned
        QString pairedAt;    // ISO time (UTC) of the first pairing
    };

    explicit TrustStore (const QString &folder); // the list is the file trusted_devices.json in this folder

    // Reads the file. A missing file is fine (nobody is trusted yet).
    // A damaged or unknown-version file is an error: the list then stays empty AND read-only,
    // so we never overwrite a list we could not understand. Returns "" or a message for the user.
    QString load();

    bool isTrusted (const QString &fingerprint) const;
    QString nameOf (const QString &fingerprint) const; // "" when not trusted
    QList<Device> devices() const;

    // Adds a device, or refreshes the name of one that is already on the list. Saves at once.
    // Returns "" or a message (and then the list is unchanged).
    QString add (const QString &fingerprint, const QString &name);
    QString remove (const QString &fingerprint); // removing someone who is not on the list is not an error

    // Names come from the network, so they are untrusted text: no control characters,
    // at most 64 characters, and "Unknown device" if nothing is left (PROTOCOL.md section 3).
    static QString cleanName (const QString &name);
    static bool isValidFingerprint (const QString &text); // exactly 64 lowercase hex characters

private:

    QString filePath;
    QList<Device> list;
    bool readOnly = false; // true after a failed load()

    QString save() const; // atomic: writes a temporary file, then renames it over the real one
};
