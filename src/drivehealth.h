#ifndef DRIVEHEALTH_H
#define DRIVEHEALTH_H

#include <QJsonObject>
#include <QString>

// Drive health as reported by SMART (through smartmontools, run by the privileged helper).
//
// healthPercent is 0..100 where 100 is a drive with no wear. It is only known for drives
// that report a wear/life indicator (NVMe "percentage used", SATA SSD life-left attributes).
// A spinning disk that merely passes its SMART self-assessment has no percentage, only smartPassed.
struct DriveHealth
{
    bool available = false;      // true when SMART data could be read
    QString message;             // why the data is not available

    QString device;              // physical disk, e.g. /dev/nvme0n1
    QString model;
    QString firmware;
    QString protocol;            // ATA, NVMe, SCSI...
    qint64 capacityBytes = 0;

    int healthPercent = -1;      // -1 unknown
    int smartPassed = -1;        // 1 passed, 0 failed, -1 unknown
    int percentageUsed = -1;     // NVMe wear estimate, -1 unknown
    int temperatureC = -1000;    // -1000 unknown
    qint64 powerOnHours = -1;    // -1 unknown

    bool hasHealthPercent() const { return healthPercent >= 0; }
    bool hasTemperature() const { return temperatureC > -1000; }

    QJsonObject toJson() const;

    // Parses the output of `smartctl -j -a`.
    static DriveHealth fromSmartctlJson(const QByteArray &json, const QString &device = QString());

    static DriveHealth unavailable(const QString &message, const QString &device = QString());

    // Maps a partition, LVM or dm-crypt block device (/dev/nvme0n1p2, /dev/mapper/x) to the
    // physical disk SMART can talk to. Returns the input when it cannot be resolved.
    static QString physicalDevice(const QString &blockDevice);
};

// Health of the filesystem the benchmark runs on, from the kernel's own error counters.
// This is not the health of the drive: a failing disk can have a clean filesystem and the
// other way around. Supported: Btrfs (per device error stats) and ext4 (errors_count).
struct FilesystemHealth
{
    bool available = false;
    QString message;
    QString filesystem;
    int healthPercent = -1;   // 100 = no errors recorded
    qint64 errorCount = 0;

    QJsonObject toJson() const;

    // volumeDevice is the block device of the mounted volume, e.g. /dev/nvme0n1p2.
    static FilesystemHealth read(const QString &volumeDevice, const QString &fileSystemType);

    static FilesystemHealth fromErrorCount(const QString &filesystem, qint64 errors);
};

#endif // DRIVEHEALTH_H
