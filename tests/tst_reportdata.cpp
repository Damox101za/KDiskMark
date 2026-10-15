#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "reportdata.h"

namespace
{
    QJsonObject row(const QString &section, const QString &pattern, int blockSizeKiB,
                    int queues, int threads, double bandwidth)
    {
        return {
            { QStringLiteral("section"), section },
            { QStringLiteral("pattern"), pattern },
            { QStringLiteral("block_size_kib"), blockSizeKiB },
            { QStringLiteral("queue_depth"), queues },
            { QStringLiteral("threads"), threads },
            { QStringLiteral("bandwidth_mb_s"), bandwidth },
            { QStringLiteral("iops"), 1.0 },
            { QStringLiteral("latency_us"), 2.0 }
        };
    }

    QJsonObject report(const QString &target, const QString &model, const QJsonArray &results)
    {
        return {
            { QStringLiteral("date"), QStringLiteral("2026-10-14T16:30:00Z") },
            { QStringLiteral("target"), target },
            { QStringLiteral("device_model"), model },
            { QStringLiteral("profile"), QStringLiteral("Default") },
            { QStringLiteral("file_size_mib"), 1024 },
            { QStringLiteral("loops"), 5 },
            { QStringLiteral("results"), results }
        };
    }
}

class tst_ReportData : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keeps the history file out of the real data directory
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("kdiskmark-tst"));
    }

    void init() { QFile::remove(ReportData::historyPath()); }
    void cleanup() { QFile::remove(ReportData::historyPath()); }

    void csvHasOneRowPerTestAndAHeader()
    {
        const QJsonObject r = report("/mnt", "Model", QJsonArray {
            row("read", "sequential", 1024, 8, 1, 3000), row("write", "random", 4, 1, 1, 100) });

        const QStringList lines = ReportData::toCsv(r).split("\r\n", Qt::SkipEmptyParts);

        QCOMPARE(lines.size(), 3);
        QVERIFY(lines.at(0).startsWith(QLatin1String("date,target,device_model,")));
        QVERIFY(lines.at(1).contains(QLatin1String(",read,sequential,1024,8,1,3000,")));
    }

    void csvQuotesCommasAndQuotes()
    {
        const QString csv = ReportData::toCsv(report("/mnt/a,\"b\"", "M", QJsonArray { row("read", "random", 4, 1, 1, 1) }));
        QVERIFY(csv.contains(QLatin1String("\"/mnt/a,\"\"b\"\"\"")));
    }

    void csvNeutralisesSpreadsheetFormulas()
    {
        for (const QString &text : { QStringLiteral("=EVIL()"), QStringLiteral("+1"), QStringLiteral("-1"), QStringLiteral("@SUM(A1)") }) {
            const QString csv = ReportData::toCsv(report("/mnt", text, QJsonArray { row("read", "random", 4, 1, 1, 1) }));
            QVERIFY2(csv.contains(QLatin1String(",'") + text + QLatin1Char(',')), qPrintable(text));
        }
    }

    void historyAppendsAndLoads()
    {
        QString error;
        const QJsonObject r = report("/mnt", "M", QJsonArray { row("read", "random", 4, 1, 1, 1) });

        QVERIFY(ReportData::loadHistory(&error).isEmpty());
        QVERIFY(ReportData::appendToHistory(r, &error));
        QVERIFY(ReportData::appendToHistory(r, &error));

        QCOMPARE(ReportData::loadHistory(&error).size(), 2);
        QVERIFY(error.isEmpty());
    }

    void historyIsNotOverwrittenWhenCorrupt()
    {
        QVERIFY(QDir().mkpath(QFileInfo(ReportData::historyPath()).absolutePath()));
        QFile file(ReportData::historyPath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");
        file.close();

        QString error;
        QVERIFY(!ReportData::appendToHistory(report("/mnt", "M", {}), &error));
        QVERIFY(!error.isEmpty());

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{broken"));
    }

    void summaryScoreAndClass()
    {
        const QJsonObject s = ReportData::summarize(QJsonArray {
            row("read", "sequential", 1024, 8, 1, 4000), row("write", "sequential", 1024, 8, 1, 1000) });

        QCOMPARE(s.value("score").toInt(), 2000);   // geometric mean of 4000 and 1000
        QCOMPARE(s.value("rating").toString(), QStringLiteral("High-end NVMe class"));
        QCOMPARE(s.value("peak_seq_read_mb_s").toDouble(), 4000.0);
    }

    void summaryOfNothing()
    {
        const QJsonObject s = ReportData::summarize({});
        QCOMPARE(s.value("score").toInt(), 0);
        QCOMPARE(s.value("rating").toString(), QStringLiteral("No results"));
        QVERIFY(s.value("performance_percent").isNull());
    }

    void performanceIsHundredPercentAtTheReference()
    {
        const QJsonArray reference {
            row("read", "sequential", 1024, 8, 1, 3000), row("read", "random", 4, 1, 1, 60),
            row("write", "sequential", 1024, 1, 1, 2000), row("write", "random", 4, 32, 1, 300) };

        QCOMPARE(ReportData::performancePercent(reference), 100);

        QJsonArray doubled;
        for (const QJsonValue &item : reference) {
            QJsonObject r = item.toObject();
            r["bandwidth_mb_s"] = r.value("bandwidth_mb_s").toDouble() * 2;
            doubled.append(r);
        }
        QCOMPARE(ReportData::performancePercent(doubled), 200);   // may exceed 100
    }

    void performanceIgnoresNonStandardTests()
    {
        QCOMPARE(ReportData::performancePercent(QJsonArray { row("read", "random", 16, 4, 2, 500) }), -1);
    }

    void textExtrasToleratesAnOldReport()
    {
        // A version 1 report has none of the newer sections
        const QString text = ReportData::toTextExtras(report("/mnt", "M", QJsonArray { row("read", "random", 4, 1, 1, 1) }));
        QVERIFY(!text.contains(QLatin1String("[Summary]")));
    }
};

QTEST_APPLESS_MAIN(tst_ReportData)
#include "tst_reportdata.moc"
