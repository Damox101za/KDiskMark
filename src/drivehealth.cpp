#include "drivehealth.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace
{
    int clampPercent(int value)
    {
        return std::max(0, std::min(100, value));
    }

    // SATA SSD attributes whose normalized value is the remaining life, in order of preference.
    // 231 SSD_Life_Left, 233 Media_Wearout_Indicator, 202 Percent_Lifetime_Remain,
    // 169 Remaining_Lifetime_Perc, 177 Wear_Leveling_Count
    const int lifeLeftAttributes[] = { 231, 233, 202, 169, 177 };
}

DriveHealth DriveHealth::unavailable(const QString &message, const QString &device)
{
    DriveHealth health;
    health.message = message;
    health.device = device;
    return health;
}

DriveHealth DriveHealth::fromSmartctlJson(const QByteArray &json, const QString &device)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return unavailable(QStringLiteral("Unreadable smartctl output."), device);

    const QJsonObject root = doc.object();

    // smartctl reports its own failures (no permission, unsupported device) as messages.
    QString smartctlMessage;
    const QJsonArray messages = root.value(QStringLiteral("smartctl")).toObject()
                                    .value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &message : messages) {
        if (message.toObject().value(QStringLiteral("severity")).toString() == QLatin1String("error")) {
            smartctlMessage = message.toObject().value(QStringLiteral("string")).toString();
            break;
        }
    }

    const bool hasStatus = root.contains(QStringLiteral("smart_status"));
    const QJsonObject nvmeLog = root.value(QStringLiteral("nvme_smart_health_information_log")).toObject();
    const QJsonArray ataTable = root.value(QStringLiteral("ata_smart_attributes")).toObject()
                                    .value(QStringLiteral("table")).toArray();

    if (!hasStatus && nvmeLog.isEmpty() && ataTable.isEmpty()) {
        return unavailable(smartctlMessage.isEmpty()
                           ? QStringLiteral("The drive does not report SMART data (virtual or unsupported disk).")
                           : smartctlMessage, device);
    }

    DriveHealth health;
    health.available = true;
    health.device = device.isEmpty() ? root.value(QStringLiteral("device")).toObject()
                                          .value(QStringLiteral("name")).toString() : device;
    health.model = root.value(QStringLiteral("model_name")).toString();
    health.firmware = root.value(QStringLiteral("firmware_version")).toString();
    health.protocol = root.value(QStringLiteral("device")).toObject().value(QStringLiteral("protocol")).toString();
    health.capacityBytes = static_cast<qint64>(root.value(QStringLiteral("user_capacity")).toObject()
                                                   .value(QStringLiteral("bytes")).toDouble());

    if (hasStatus)
        health.smartPassed = root.value(QStringLiteral("smart_status")).toObject()
                                 .value(QStringLiteral("passed")).toBool() ? 1 : 0;

    const QJsonValue temperature = root.value(QStringLiteral("temperature")).toObject()
                                       .value(QStringLiteral("current"));
    if (temperature.isDouble())
        health.temperatureC = temperature.toInt();

    const QJsonValue hours = root.value(QStringLiteral("power_on_time")).toObject().value(QStringLiteral("hours"));
    if (hours.isDouble())
        health.powerOnHours = static_cast<qint64>(hours.toDouble());

    if (nvmeLog.contains(QStringLiteral("percentage_used"))) {
        health.percentageUsed = nvmeLog.value(QStringLiteral("percentage_used")).toInt();
        // The NVMe estimate may exceed 100 when the rated endurance is passed.
        health.healthPercent = clampPercent(100 - health.percentageUsed);

        if (nvmeLog.value(QStringLiteral("critical_warning")).toInt() != 0)
            health.smartPassed = 0;
    }
    else {
        for (int id : lifeLeftAttributes) {
            for (const QJsonValue &item : ataTable) {
                const QJsonObject attribute = item.toObject();
                if (attribute.value(QStringLiteral("id")).toInt() == id) {
                    health.healthPercent = clampPercent(attribute.value(QStringLiteral("value")).toInt());
                    break;
                }
            }
            if (health.hasHealthPercent())
                break;
        }
    }

    return health;
}

QJsonObject DriveHealth::toJson() const
{
    QJsonObject json {
        { QStringLiteral("available"), available },
        { QStringLiteral("message"), message },
        { QStringLiteral("device"), device },
        { QStringLiteral("model"), model },
        { QStringLiteral("firmware"), firmware },
        { QStringLiteral("protocol"), protocol },
        { QStringLiteral("capacity_bytes"), static_cast<double>(capacityBytes) },
    };

    // Unknown values are null, never a made up number.
    json[QStringLiteral("health_percent")] = hasHealthPercent() ? QJsonValue(healthPercent) : QJsonValue();
    json[QStringLiteral("smart_passed")] = smartPassed >= 0 ? QJsonValue(smartPassed == 1) : QJsonValue();
    json[QStringLiteral("percentage_used")] = percentageUsed >= 0 ? QJsonValue(percentageUsed) : QJsonValue();
    json[QStringLiteral("temperature_c")] = hasTemperature() ? QJsonValue(temperatureC) : QJsonValue();
    json[QStringLiteral("power_on_hours")] = powerOnHours >= 0 ? QJsonValue(static_cast<double>(powerOnHours)) : QJsonValue();

    return json;
}

QString DriveHealth::physicalDevice(const QString &blockDevice)
{
    QString name = QFileInfo(blockDevice).canonicalFilePath().section(QLatin1Char('/'), -1);
    if (name.isEmpty())
        name = blockDevice.section(QLatin1Char('/'), -1);

    // Follow device-mapper stacks down to their first backing device, a few levels at most.
    for (int depth = 0; depth < 4 && !name.isEmpty(); depth++) {
        const QDir slaves(QStringLiteral("/sys/class/block/%1/slaves").arg(name));
        const QStringList backing = slaves.entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);

        if (backing.isEmpty())
            break;

        name = backing.first();
    }

    // A partition lives inside its disk's sysfs directory.
    if (QFileInfo::exists(QStringLiteral("/sys/class/block/%1/partition").arg(name))) {
        const QString parent = QFileInfo(QFileInfo(QStringLiteral("/sys/class/block/%1/..").arg(name))
                                         .canonicalFilePath()).fileName();
        if (!parent.isEmpty())
            name = parent;
    }

    return name.isEmpty() ? blockDevice : QStringLiteral("/dev/%1").arg(name);
}

FilesystemHealth FilesystemHealth::fromErrorCount(const QString &filesystem, qint64 errors)
{
    FilesystemHealth health;
    health.available = true;
    health.filesystem = filesystem;
    health.errorCount = errors;
    // 100 % with no recorded errors, 5 points off for each one.
    health.healthPercent = clampPercent(100 - static_cast<int>(qMin<qint64>(errors, 100) * 5));
    return health;
}

FilesystemHealth FilesystemHealth::read(const QString &volumeDevice, const QString &fileSystemType)
{
    FilesystemHealth unavailable;
    unavailable.filesystem = fileSystemType;

    const QString name = volumeDevice.section(QLatin1Char('/'), -1);

    if (fileSystemType == QLatin1String("btrfs")) {
        // /sys/fs/btrfs/<fsid>/devices/<name> exists for every member device,
        // and devinfo/<devid>/error_stats holds that device's counters.
        const QDir root(QStringLiteral("/sys/fs/btrfs"));
        for (const QString &fsid : root.entryList(QDir::NoDotAndDotDot | QDir::Dirs)) {
            if (!QFileInfo::exists(root.filePath(fsid + QStringLiteral("/devices/") + name)))
                continue;

            qint64 errors = 0;
            bool found = false;

            const QDir devinfo(root.filePath(fsid + QStringLiteral("/devinfo")));
            for (const QString &devid : devinfo.entryList(QDir::NoDotAndDotDot | QDir::Dirs)) {
                QFile stats(devinfo.filePath(devid + QStringLiteral("/error_stats")));
                if (!stats.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;

                found = true;

                // sysfs files report a size of 4096, so atEnd()/readLine() loops never finish.
                const QList<QByteArray> lines = stats.readAll().split('\n');
                for (const QByteArray &line : lines) {
                    const QList<QByteArray> parts = line.simplified().split(' ');
                    if (parts.size() == 2)
                        errors += parts.at(1).toLongLong();
                }
            }

            if (found)
                return fromErrorCount(fileSystemType, errors);

            unavailable.message = QStringLiteral("Btrfs error statistics are not exposed by this kernel.");
            return unavailable;
        }

        unavailable.message = QStringLiteral("The Btrfs filesystem of %1 was not found.").arg(volumeDevice);
        return unavailable;
    }

    if (fileSystemType.startsWith(QLatin1String("ext"))) {
        QFile count(QStringLiteral("/sys/fs/ext4/%1/errors_count").arg(name));
        if (count.open(QIODevice::ReadOnly | QIODevice::Text))
            return fromErrorCount(fileSystemType, count.readAll().simplified().toLongLong());

        unavailable.message = QStringLiteral("The kernel does not expose ext4 error counters for %1.").arg(volumeDevice);
        return unavailable;
    }

    unavailable.message = fileSystemType.isEmpty()
                          ? QStringLiteral("Unknown filesystem.")
                          : QStringLiteral("Filesystem health is not supported for %1.").arg(fileSystemType);
    return unavailable;
}

QJsonObject FilesystemHealth::toJson() const
{
    QJsonObject json {
        { QStringLiteral("available"), available },
        { QStringLiteral("message"), message },
        { QStringLiteral("filesystem"), filesystem },
    };

    json[QStringLiteral("health_percent")] = available ? QJsonValue(healthPercent) : QJsonValue();
    json[QStringLiteral("error_count")] = available ? QJsonValue(static_cast<double>(errorCount)) : QJsonValue();

    return json;
}
