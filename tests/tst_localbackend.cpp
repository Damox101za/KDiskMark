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

        // Buffered I/O: a temporary directory may be on tmpfs, which has no O_DIRECT
        QVERIFY(backend.startBenchmarkTest(1, 16, 70, false, false, false, 4, 1, 1, "randread")
                .value("success").toBool());
        QVERIFY(finished.wait(20000));

        const QList<QVariant> result = finished.takeFirst();
        QVERIFY(result.at(0).toBool());

        const QJsonDocument json = QJsonDocument::fromJson(result.at(1).toString().toUtf8());
        QVERIFY(json.isObject());
        const QJsonObject job = json.object().value("jobs").toArray().first().toObject();
        QVERIFY(job.value("read").toObject().value("bw_bytes").toDouble() > 0);

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
