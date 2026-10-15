#ifndef LOCALBACKEND_H
#define LOCALBACKEND_H

#include "backend.h"

#include <QProcess>
#include <QTemporaryFile>

// Runs fio directly, as the user who started the application. There is no privileged helper:
// benchmarking a file in a folder the user can write to does not need one.
//
// What needs privileges is not available here: dropping the page cache, Copy-on-Write
// handling and SMART data are reported as unsupported, and the test relies on direct I/O.
class LocalBackend : public Backend
{
    Q_OBJECT

public:
    explicit LocalBackend(QObject *parent = nullptr);

    QVariantMap initSession() override;
    QVariantMap endSession() override;
    QVariantMap checkCowStatus(const QString &path) override;
    QVariantMap createNoCowDirectory(const QString &path) override;
    QVariantMap prepareBenchmarkFile(const QString &path, int fileSize, bool fillZeros) override;
    QVariantMap startBenchmarkTest(int measuringTime, int fileSize, int randomReadPercentage,
                                   bool fillZeros, bool cacheBypass, bool continuousGeneration,
                                   int blockSize, int queueDepth, int threads, const QString &rw) override;
    QVariantMap flushPageCache() override;
    QVariantMap removeBenchmarkFile() override;
    QVariantMap stopCurrentTask() override;
    QVariantMap getDriveHealth(const QString &device) override;

private:
    bool startFio(const QStringList &arguments);
    void stopProcess();
    QString fioFilename() const;
    QStringList platformOptions() const;

    QProcess *m_process;
    QTemporaryFile m_benchmarkFile;
};

#endif // LOCALBACKEND_H
