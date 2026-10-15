#include <QtTest>

#include "drivehealth.h"

class tst_DriveHealth : public QObject
{
    Q_OBJECT

private slots:
    void nvmeHealthIsHundredMinusPercentageUsed()
    {
        const DriveHealth h = DriveHealth::fromSmartctlJson(R"({
            "device": {"protocol": "NVMe"}, "model_name": "Test NVMe", "firmware_version": "1.0",
            "user_capacity": {"bytes": 1000204886016},
            "smart_status": {"passed": true}, "temperature": {"current": 38},
            "power_on_time": {"hours": 4321},
            "nvme_smart_health_information_log": {"critical_warning": 0, "percentage_used": 3}
        })", "/dev/nvme0n1");

        QVERIFY(h.available);
        QCOMPARE(h.healthPercent, 97);
        QCOMPARE(h.percentageUsed, 3);
        QCOMPARE(h.smartPassed, 1);
        QCOMPARE(h.temperatureC, 38);
        QCOMPARE(h.powerOnHours, qint64(4321));
        QCOMPARE(h.model, QStringLiteral("Test NVMe"));
        QCOMPARE(h.device, QStringLiteral("/dev/nvme0n1"));
    }

    void wornOutNvmeIsClampedAndFailing()
    {
        // percentage_used may exceed 100 once the rated endurance is passed
        const DriveHealth h = DriveHealth::fromSmartctlJson(R"({
            "smart_status": {"passed": false},
            "nvme_smart_health_information_log": {"critical_warning": 4, "percentage_used": 130}
        })");

        QVERIFY(h.available);
        QCOMPARE(h.healthPercent, 0);
        QCOMPARE(h.smartPassed, 0);
    }

    void sataSsdUsesTheLifeLeftAttribute()
    {
        const DriveHealth h = DriveHealth::fromSmartctlJson(R"({
            "device": {"protocol": "ATA"}, "smart_status": {"passed": true},
            "ata_smart_attributes": {"table": [
                {"id": 5, "value": 100}, {"id": 177, "value": 96}, {"id": 231, "value": 92}]}
        })");

        QCOMPARE(h.healthPercent, 92);   // 231 is preferred over 177
    }

    void hardDiskHasNoPercentage()
    {
        const DriveHealth h = DriveHealth::fromSmartctlJson(R"({
            "smart_status": {"passed": true},
            "ata_smart_attributes": {"table": [{"id": 5, "value": 100}, {"id": 9, "value": 90}]}
        })");

        QVERIFY(h.available);
        QVERIFY(!h.hasHealthPercent());
        QCOMPARE(h.smartPassed, 1);
        QVERIFY(h.toJson().value("health_percent").isNull());
    }

    void unsupportedDeviceReportsSmartctlsReason()
    {
        const DriveHealth h = DriveHealth::fromSmartctlJson(R"({
            "smartctl": {"messages": [{"severity": "error", "string": "/dev/vdb: Unable to detect device type"}]}
        })", "/dev/vdb");

        QVERIFY(!h.available);
        QVERIFY(!h.hasHealthPercent());
        QCOMPARE(h.message, QStringLiteral("/dev/vdb: Unable to detect device type"));
    }

    void unreadableOutputIsNotAHealthValue()
    {
        const DriveHealth h = DriveHealth::fromSmartctlJson("not json");

        QVERIFY(!h.available);
        QVERIFY(!h.message.isEmpty());
    }

    void filesystemHealthDropsFivePointsPerError()
    {
        QCOMPARE(FilesystemHealth::fromErrorCount("btrfs", 0).healthPercent, 100);
        QCOMPARE(FilesystemHealth::fromErrorCount("btrfs", 3).healthPercent, 85);
        QCOMPARE(FilesystemHealth::fromErrorCount("btrfs", 500).healthPercent, 0);
        QCOMPARE(FilesystemHealth::fromErrorCount("ext4", 2).errorCount, qint64(2));
    }

    void filesystemHealthOfAnUnsupportedFilesystem()
    {
        const FilesystemHealth h = FilesystemHealth::read("/dev/null", "xfs");

        QVERIFY(!h.available);
        QVERIFY(!h.message.isEmpty());
        QVERIFY(h.toJson().value("health_percent").isNull());
    }
};

QTEST_APPLESS_MAIN(tst_DriveHealth)
#include "tst_drivehealth.moc"
