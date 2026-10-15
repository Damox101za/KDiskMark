#include "reportdata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QtMath>

#include <cmath>
#include <QStringList>

namespace
{
    const QStringList csvColumns = {
        QStringLiteral("date"), QStringLiteral("target"), QStringLiteral("device_model"),
        QStringLiteral("profile"), QStringLiteral("file_size_mib"), QStringLiteral("loops"),
        QStringLiteral("section"), QStringLiteral("pattern"), QStringLiteral("block_size_kib"),
        QStringLiteral("queue_depth"), QStringLiteral("threads"),
        QStringLiteral("bandwidth_mb_s"), QStringLiteral("bandwidth_min_mb_s"),
        QStringLiteral("bandwidth_avg_mb_s"), QStringLiteral("bandwidth_max_mb_s"),
        QStringLiteral("iops"), QStringLiteral("latency_us"),
        QStringLiteral("score"), QStringLiteral("rating"), QStringLiteral("performance_percent")
    };

    QString csvField(const QString &value)
    {
        // Guard against spreadsheet formula injection from user controlled text (paths, model names)
        QString text = value;
        if (!text.isEmpty() && QStringLiteral("=+-@\t\r").contains(text.at(0)))
            text.prepend(QLatin1Char('\''));

        if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')) ||
            text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))) {
            text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            return QLatin1Char('"') + text + QLatin1Char('"');
        }

        return text;
    }

    // Missing or null values (older reports, unknown health) are left empty.
    QString numberField(const QJsonValue &value)
    {
        return value.isDouble() ? QString::number(value.toDouble(), 'g', 12) : QString();
    }

    QString csvRows(const QJsonObject &report)
    {
        QString out;

        const QJsonObject summary = report.value(QStringLiteral("summary")).toObject();
        const QJsonObject drive = report.value(QStringLiteral("drive")).toObject();

        // Older reports only know the device model by its top level key.
        const QString model = drive.value(QStringLiteral("model")).toString().isEmpty()
                              ? report.value(QStringLiteral("device_model")).toString()
                              : drive.value(QStringLiteral("model")).toString();

        const QJsonArray results = report.value(QStringLiteral("results")).toArray();
        for (const QJsonValue &item : results) {
            const QJsonObject r = item.toObject();

            const QStringList fields = {
                csvField(report.value(QStringLiteral("date")).toString()),
                csvField(report.value(QStringLiteral("target")).toString()),
                csvField(model),
                csvField(report.value(QStringLiteral("profile")).toString()),
                numberField(report.value(QStringLiteral("file_size_mib"))),
                numberField(report.value(QStringLiteral("loops"))),
                csvField(r.value(QStringLiteral("section")).toString()),
                csvField(r.value(QStringLiteral("pattern")).toString()),
                numberField(r.value(QStringLiteral("block_size_kib"))),
                numberField(r.value(QStringLiteral("queue_depth"))),
                numberField(r.value(QStringLiteral("threads"))),
                numberField(r.value(QStringLiteral("bandwidth_mb_s"))),
                numberField(r.value(QStringLiteral("bandwidth_min_mb_s"))),
                numberField(r.value(QStringLiteral("bandwidth_avg_mb_s"))),
                numberField(r.value(QStringLiteral("bandwidth_max_mb_s"))),
                numberField(r.value(QStringLiteral("iops"))),
                numberField(r.value(QStringLiteral("latency_us"))),
                numberField(summary.value(QStringLiteral("score"))),
                csvField(summary.value(QStringLiteral("rating")).toString()),
                numberField(summary.value(QStringLiteral("performance_percent")))
            };

            out += fields.join(QLatin1Char(',')) + QLatin1String("\r\n");
        }

        return out;
    }

    QString csvHeader()
    {
        return csvColumns.join(QLatin1Char(',')) + QLatin1String("\r\n");
    }

    bool writeAtomically(const QString &fileName, const QByteArray &data, QString *error)
    {
        QSaveFile file(fileName);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString();
            return false;
        }

        file.write(data);

        if (!file.commit()) {
            if (error) *error = file.errorString();
            return false;
        }

        return true;
    }
}

namespace ReportData
{
    QString historyPath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/results.json");
    }

    QJsonArray loadHistory(QString *error)
    {
        QFile file(historyPath());
        if (!file.exists())
            return {};

        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = file.errorString();
            return {};
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error) *error = parseError.error != QJsonParseError::NoError
                                ? parseError.errorString() : QStringLiteral("Unexpected history file format.");
            return {};
        }

        return doc.object().value(QStringLiteral("reports")).toArray();
    }

    bool appendToHistory(const QJsonObject &report, QString *error)
    {
        QString loadError;
        QJsonArray reports = loadHistory(&loadError);

        // Never overwrite a history file we could not read, it would destroy the user's data.
        if (!loadError.isEmpty()) {
            if (error) *error = loadError;
            return false;
        }

        reports.append(report);

        const QString path = historyPath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            if (error) *error = QStringLiteral("Cannot create %1").arg(QFileInfo(path).absolutePath());
            return false;
        }

        const QJsonObject root {
            { QStringLiteral("version"), FormatVersion },
            { QStringLiteral("reports"), reports }
        };

        return writeAtomically(path, QJsonDocument(root).toJson(QJsonDocument::Indented), error);
    }

    QJsonObject systemInfo()
    {
        QString cpu;
        QFile cpuInfo(QStringLiteral("/proc/cpuinfo"));
        if (cpuInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // /proc files have no reliable size, read them in one go instead of line by line
            const QStringList lines = QString::fromUtf8(cpuInfo.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.startsWith(QLatin1String("model name"))) {
                    cpu = line.section(QLatin1Char(':'), 1).simplified();
                    break;
                }
            }
        }

        qint64 ramMiB = 0;
        QFile memInfo(QStringLiteral("/proc/meminfo"));
        if (memInfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines = QString::fromUtf8(memInfo.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.startsWith(QLatin1String("MemTotal:"))) {
                    ramMiB = line.section(QLatin1Char(':'), 1).simplified().section(QLatin1Char(' '), 0, 0).toLongLong() / 1024;
                    break;
                }
            }
        }

        return {
            { QStringLiteral("os"), QStringLiteral("%1 %2").arg(QSysInfo::productType(), QSysInfo::productVersion()) },
            { QStringLiteral("kernel"), QStringLiteral("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()) },
            { QStringLiteral("cpu"), cpu },
            { QStringLiteral("cpu_threads"), QThread::idealThreadCount() },
            { QStringLiteral("ram_mib"), static_cast<double>(ramMiB) }
        };
    }

    int performancePercent(const QJsonArray &results)
    {
        // Reference MB/s of a mainstream PCIe 3.0 NVMe SSD for the standard Default profile tests.
        struct Reference { const char *section; const char *pattern; int blockSizeKiB, queues, threads; double mbPerSec; };
        static const Reference references[] = {
            { "read",  "sequential", 1024,  8, 1, 3000 }, { "read",  "sequential", 1024, 1, 1, 1500 },
            { "read",  "random",        4, 32, 1,  350 }, { "read",  "random",        4, 1, 1,   60 },
            { "write", "sequential", 1024,  8, 1, 2500 }, { "write", "sequential", 1024, 1, 1, 2000 },
            { "write", "random",        4, 32, 1,  300 }, { "write", "random",        4, 1, 1,  150 },
        };

        double logSum = 0;
        int counted = 0;

        for (const QJsonValue &item : results) {
            const QJsonObject r = item.toObject();
            const double bandwidth = r.value(QStringLiteral("bandwidth_mb_s")).toDouble();
            if (bandwidth <= 0)
                continue;

            for (const Reference &ref : references) {
                if (r.value(QStringLiteral("section")).toString() == QLatin1String(ref.section)
                    && r.value(QStringLiteral("pattern")).toString() == QLatin1String(ref.pattern)
                    && r.value(QStringLiteral("block_size_kib")).toInt() == ref.blockSizeKiB
                    && r.value(QStringLiteral("queue_depth")).toInt() == ref.queues
                    && r.value(QStringLiteral("threads")).toInt() == ref.threads) {
                    logSum += std::log(bandwidth / ref.mbPerSec);
                    counted++;
                    break;
                }
            }
        }

        return counted > 0 ? qRound(std::exp(logSum / counted) * 100.0) : -1;
    }

    QJsonObject summarize(const QJsonArray &results)
    {
        double logSum = 0, peakSeqRead = 0, peakSeqWrite = 0;
        int counted = 0;

        for (const QJsonValue &item : results) {
            const QJsonObject r = item.toObject();
            const double bandwidth = r.value(QStringLiteral("bandwidth_mb_s")).toDouble();
            if (bandwidth <= 0)
                continue;

            logSum += std::log(bandwidth);
            counted++;

            if (r.value(QStringLiteral("pattern")).toString() == QLatin1String("sequential")) {
                const QString section = r.value(QStringLiteral("section")).toString();
                if (section == QLatin1String("read")) peakSeqRead = qMax(peakSeqRead, bandwidth);
                else if (section == QLatin1String("write")) peakSeqWrite = qMax(peakSeqWrite, bandwidth);
            }
        }

        const double score = counted > 0 ? std::exp(logSum / counted) : 0;

        // The class is a rough guide from the best sequential figure, not a certification.
        const double peak = qMax(peakSeqRead, peakSeqWrite);
        QString rating;
        if (counted == 0)        rating = QStringLiteral("No results");
        else if (peak < 200)     rating = QStringLiteral("Hard drive class");
        else if (peak < 700)     rating = QStringLiteral("SATA SSD class");
        else if (peak < 3500)    rating = QStringLiteral("NVMe class");
        else                     rating = QStringLiteral("High-end NVMe class");

        return {
            { QStringLiteral("score"), qRound(score) },
            { QStringLiteral("rating"), rating },
            { QStringLiteral("peak_seq_read_mb_s"), peakSeqRead },
            { QStringLiteral("peak_seq_write_mb_s"), peakSeqWrite },
            { QStringLiteral("performance_percent"), performancePercent(results) >= 0
                                                     ? QJsonValue(performancePercent(results)) : QJsonValue() }
        };
    }

    QString toTextExtras(const QJsonObject &report)
    {
        QStringList out;

        const QJsonArray results = report.value(QStringLiteral("results")).toArray();

        auto sizeText = [](double bytes) {
            return bytes >= 1024.0 * 1024 * 1024 ? QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2)
                                                 : QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024), 0, 'f', 0);
        };

        auto statText = [](const QJsonValue &value) {
            return (value.isDouble() ? QString::number(value.toDouble(), 'f', 3) : QStringLiteral("n/a")).rightJustified(9);
        };

        bool hasStats = false;
        for (const QJsonValue &item : results)
            hasStats = hasStats || item.toObject().contains(QStringLiteral("bandwidth_avg_mb_s"));

        if (hasStats) {
            out << QString()
                << QStringLiteral("[Bandwidth over %1 loops, MB/s: min / avg / max]")
                   .arg(report.value(QStringLiteral("loops")).toInt());

            for (const QJsonValue &item : results) {
                const QJsonObject r = item.toObject();
                const int blockSize = r.value(QStringLiteral("block_size_kib")).toInt();

                out << QStringLiteral("%1 %2 %3 %4 (Q=%5, T=%6): %7 / %8 / %9")
                       .arg(r.value(QStringLiteral("section")).toString(), -5)
                       .arg(r.value(QStringLiteral("pattern")).toString() == QLatin1String("sequential")
                            ? QStringLiteral("Sequential") : QStringLiteral("Random"), -10)
                       .arg(blockSize >= 1024 ? blockSize / 1024 : blockSize, 3)
                       .arg(blockSize >= 1024 ? QStringLiteral("MiB") : QStringLiteral("KiB"))
                       .arg(r.value(QStringLiteral("queue_depth")).toInt(), 3)
                       .arg(r.value(QStringLiteral("threads")).toInt(), 2)
                       .arg(statText(r.value(QStringLiteral("bandwidth_min_mb_s"))))
                       .arg(statText(r.value(QStringLiteral("bandwidth_avg_mb_s"))))
                       .arg(statText(r.value(QStringLiteral("bandwidth_max_mb_s"))));
            }
        }

        const QJsonObject summary = report.value(QStringLiteral("summary")).toObject();
        if (!summary.isEmpty()) {
            out << QString()
                << QStringLiteral("[Summary]")
                << QStringLiteral("  Score: %1 (geometric mean of all results, MB/s)").arg(summary.value(QStringLiteral("score")).toInt())
                << QStringLiteral("  Class: %1").arg(summary.value(QStringLiteral("rating")).toString())
                << QStringLiteral("  Peak sequential: %1 MB/s read, %2 MB/s write")
                   .arg(summary.value(QStringLiteral("peak_seq_read_mb_s")).toDouble(), 0, 'f', 1)
                   .arg(summary.value(QStringLiteral("peak_seq_write_mb_s")).toDouble(), 0, 'f', 1);

            if (summary.value(QStringLiteral("performance_percent")).isDouble())
                out << QStringLiteral("  Performance: %1 % of a mainstream NVMe SSD (100 % = reference)")
                       .arg(summary.value(QStringLiteral("performance_percent")).toInt());
        }

        const QJsonObject drive = report.value(QStringLiteral("drive")).toObject();
        if (!drive.isEmpty()) {
            out << QString() << QStringLiteral("[Drive]");
            if (!drive.value(QStringLiteral("device")).toString().isEmpty())
                out << QStringLiteral("  Device: %1").arg(drive.value(QStringLiteral("device")).toString());
            if (!drive.value(QStringLiteral("model")).toString().isEmpty())
                out << QStringLiteral("  Model: %1").arg(drive.value(QStringLiteral("model")).toString());
            if (!drive.value(QStringLiteral("firmware")).toString().isEmpty())
                out << QStringLiteral("  Firmware: %1").arg(drive.value(QStringLiteral("firmware")).toString());
            if (!drive.value(QStringLiteral("filesystem")).toString().isEmpty())
                out << QStringLiteral("  Filesystem: %1").arg(drive.value(QStringLiteral("filesystem")).toString());
            if (drive.value(QStringLiteral("target_total_bytes")).toDouble() > 0)
                out << QStringLiteral("  Target volume: %1 free of %2")
                       .arg(sizeText(drive.value(QStringLiteral("target_free_bytes")).toDouble()),
                            sizeText(drive.value(QStringLiteral("target_total_bytes")).toDouble()));
        }

        const QJsonObject system = report.value(QStringLiteral("system")).toObject();
        if (!system.isEmpty()) {
            out << QString() << QStringLiteral("[System]")
                << QStringLiteral("  CPU: %1 (%2 threads)").arg(system.value(QStringLiteral("cpu")).toString())
                                                           .arg(system.value(QStringLiteral("cpu_threads")).toInt())
                << QStringLiteral("  RAM: %1 MiB").arg(static_cast<qint64>(system.value(QStringLiteral("ram_mib")).toDouble()));
        }

        return out.join(QLatin1Char('\n'));
    }

    QString toCsv(const QJsonObject &report, bool withHeader)
    {
        return (withHeader ? csvHeader() : QString()) + csvRows(report);
    }

    QString toCsv(const QJsonArray &reports)
    {
        QString out = csvHeader();
        for (const QJsonValue &report : reports)
            out += csvRows(report.toObject());
        return out;
    }

    bool writeJson(const QString &fileName, const QJsonObject &report, QString *error)
    {
        return writeAtomically(fileName, QJsonDocument(report).toJson(QJsonDocument::Indented), error);
    }

    bool writeText(const QString &fileName, const QString &text, QString *error)
    {
        return writeAtomically(fileName, text.toUtf8(), error);
    }
}
