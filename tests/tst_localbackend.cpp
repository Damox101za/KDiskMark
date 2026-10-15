#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "localbackend.h"

// Runs the real fio, so these tests are skipped when it is not installed.
class tst_LocalBackend : public QObject
{
    Q_OBJECT

private:
    static bool hasFio()
    {
        QProcess process;
        process.start(Backend::fioProgram(), { QStringLiteral("--version") });
        return process.waitForFinished(5000) && process.exitCode() == 0;
    }

    static QStringList benchmarkFiles(const QString &directory)
    {
        return QDir(directory).entryList({ QStringLiteral("kdiskmark-*.tmp") }, QDir::Files);
    }

private slots:
    void initTestCase()
    {
        if (!hasFio()) QSKIP("fio is not installed");
    }

    void runsATestAndCleansUp()
    {
        QTemporaryDir dir;
        LocalBackend backend;
        QSignalSpy finished(&backend, &Backend::taskFinished);

        QVERIFY(backend.initSession().value("success").toBool());

        QVERIFY(backend.prepareBenchmarkFile(dir.path(), 16, false).value("success").toBool());
        QVERIFY(finished.wait(20000));
        QVERIFY(finished.takeFirst().at(0).toBool());
        QCOMPARE(benchmarkFiles(dir.path()).size(), 1);

        // Buffered I/O: a temporary directory may be on tmpfs, which has no O_DIRECT.
        // A just written file can be briefly locked on Windows (virus scanner), so the run is
        // retried and every attempt is recorded, to tell a transient lock from a real error.
        QStringList attempts;
        double bandwidth = 0;

        for (int attempt = 1; attempt <= 3 && bandwidth <= 0; attempt++) {
            finished.clear();
            QVERIFY(backend.startBenchmarkTest(1, 16, 70, false, false, false, 4, 1, 1, "randread")
                    .value("success").toBool());
            QVERIFY(finished.wait(20000));

            const QList<QVariant> result = finished.takeFirst();
            QVERIFY(result.at(0).toBool());

            const QString output = result.at(1).toString();
            const QJsonDocument json = QJsonDocument::fromJson(Backend::jsonFromOutput(output));
            QVERIFY2(json.isObject(), qPrintable("fio printed: " + output.left(400) + " / stderr: " + result.at(2).toString().left(200)));

            const QJsonObject job = json.object().value("jobs").toArray().first().toObject();
            bandwidth = job.value("read").toObject().value("bw_bytes").toDouble();

            attempts << QStringLiteral("attempt %1: error=%2 bw_bytes=%3 stderr='%4' size=%5")
                        .arg(attempt).arg(job.value("error").toInt()).arg(bandwidth)
                        .arg(result.at(2).toString().left(160))
                        .arg(QDir(dir.path()).entryInfoList({"kdiskmark-*.tmp"}, QDir::Files).value(0).size());

            if (bandwidth <= 0) QTest::qWait(500);
        }

        QVERIFY2(bandwidth > 0, qPrintable(QStringLiteral("dir=%1 | ").arg(dir.path()) + attempts.join(" | ")));
        if (attempts.size() > 1) qWarning("needed %d attempts: %s", int(attempts.size()), qPrintable(attempts.join(" | ")));

        QVERIFY(backend.removeBenchmarkFile().value("success").toBool());
        QCOMPARE(benchmarkFiles(dir.path()).size(), 0);
    }

    void refusesASecondTaskWhileOneRuns()
    {
        QTemporaryDir dir;
        LocalBackend backend;
        QSignalSpy finished(&backend, &Backend::taskFinished);

        QVERIFY(backend.prepareBenchmarkFile(dir.path(), 64, false).value("success").toBool());
        QVERIFY(finished.wait(20000));
        finished.clear();

        QVERIFY(backend.startBenchmarkTest(20, 64, 70, false, false, false, 4, 1, 1, "randwrite")
                .value("success").toBool());

        const QVariantMap second = backend.startBenchmarkTest(20, 64, 70, false, false, false, 4, 1, 1, "randwrite");
        QVERIFY(!second.value("success").toBool());
        QVERIFY(!second.value("error").toString().isEmpty());

        // stopping ends the running task promptly and reports it as finished
        QElapsedTimer timer;
        timer.start();
        QVERIFY(backend.stopCurrentTask().value("success").toBool());
        QVERIFY(timer.elapsed() < 10000);
        QVERIFY(finished.count() >= 1 || finished.wait(5000));

        backend.removeBenchmarkFile();
    }

    void endSessionRemovesTheFile()
    {
        QTemporaryDir dir;
        LocalBackend backend;
        QSignalSpy finished(&backend, &Backend::taskFinished);

        QVERIFY(backend.prepareBenchmarkFile(dir.path(), 16, false).value("success").toBool());
        QVERIFY(finished.wait(20000));
        QCOMPARE(benchmarkFiles(dir.path()).size(), 1);

        backend.endSession();
        QCOMPARE(benchmarkFiles(dir.path()).size(), 0);
    }

    void aMissingDirectoryIsAnError()
    {
        LocalBackend backend;
        const QVariantMap reply = backend.prepareBenchmarkFile("/this/does/not/exist", 16, false);

        QVERIFY(!reply.value("success").toBool());
        QVERIFY(!reply.value("error").toString().isEmpty());
    }

    void findsTheJsonAfterNotices()
    {
        const QString notice = QStringLiteral("fio: this platform does not support process shared mutexes\n");
        QCOMPARE(Backend::jsonFromOutput(notice + "{\"jobs\": []}"), QByteArray("{\"jobs\": []}"));
        QCOMPARE(Backend::jsonFromOutput("{\"jobs\": []}"), QByteArray("{\"jobs\": []}"));
        QCOMPARE(Backend::jsonFromOutput("no json here"), QByteArray("no json here"));
    }

    void startingBeforePreparingIsAnError()
    {
        LocalBackend backend;
        QVERIFY(!backend.startBenchmarkTest(1, 16, 70, false, false, false, 4, 1, 1, "read")
                .value("success").toBool());
    }

    void unsupportedFeaturesAreReportedHonestly()
    {
        LocalBackend backend;

        QVERIFY(!backend.checkCowStatus("/tmp").value("hasCow").toBool());

        const QVariantMap health = backend.getDriveHealth("/dev/sda");
        QVERIFY(health.value("success").toBool());
        QVERIFY(!health.value("available").toBool());
    }
};

QTEST_GUILESS_MAIN(tst_LocalBackend)
#include "tst_localbackend.moc"
