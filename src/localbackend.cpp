#include "localbackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

LocalBackend::LocalBackend(QObject *parent) : Backend(parent), m_process(new QProcess(this))
{
    connect(m_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        // fio reports its own errors in the JSON and through a non-zero exit code
        emit taskFinished(exitStatus == QProcess::NormalExit && exitCode == 0,
                          QString::fromUtf8(m_process->readAllStandardOutput()),
                          QString::fromUtf8(m_process->readAllStandardError()));
    });

    // finished() is not emitted when fio cannot be started at all; report that as a failed task.
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit taskFinished(false, QString(),
                              tr("fio could not be started (%1). Install fio, or place it next to the application.")
                              .arg(Backend::fioProgram()));
        }
    });
}

LocalBackend::~LocalBackend()
{
    stopProcess();

    if (!m_benchmarkPath.isEmpty()) {
        QFile::remove(m_benchmarkPath);
    }
}

QVariantMap LocalBackend::initSession()
{
    // A backend lives as long as the application, unlike the helper process, so every run starts
    // from a clean state. Without this a file that could not be removed (locked, drive pulled)
    // would make every later run fail with "a new session should be started".
    stopProcess();

    if (!m_benchmarkPath.isEmpty()) {
        QFile::remove(m_benchmarkPath);
        m_benchmarkPath.clear();
    }

    return {{"success", true}};
}

QVariantMap LocalBackend::endSession()
{
    stopProcess();
    removeBenchmarkFile();
    return {{"success", true}};
}

QVariantMap LocalBackend::checkCowStatus(const QString &)
{
    // Needs the helper's privileges to inspect and change; treated as "no Copy-on-Write".
    return {{"success", true}, {"hasCow", false}};
}

QVariantMap LocalBackend::createNoCowDirectory(const QString &)
{
    return {{"success", false}, {"error", tr("Creating a Copy-on-Write free directory is not supported here.")}};
}

QString LocalBackend::fioFilename() const
{
    QString name = QDir::toNativeSeparators(m_benchmarkPath);

#ifdef Q_OS_WIN
    // fio treats ':' as a separator in file names; the one of a drive letter must be escaped.
    name.replace(QLatin1Char(':'), QStringLiteral("\\:"));
#endif

    return name;
}

QStringList LocalBackend::platformOptions() const
{
#ifdef Q_OS_WIN
    // Jobs are threads on Windows; without this fio prints a notice about it before the results
    return { QStringLiteral("--thread") };
#else
    return {};
#endif
}

bool LocalBackend::startFio(const QStringList &arguments)
{
    if (m_process->state() != QProcess::NotRunning) {
        return false;
    }

    m_process->start(Backend::fioProgram(), arguments);
    return true;
}

QVariantMap LocalBackend::prepareBenchmarkFile(const QString &path, int fileSize, bool fillZeros)
{
    if (m_process->state() != QProcess::NotRunning) {
        return {{"success", false}, {"error", tr("Another task is still running.")}};
    }

    if (!m_benchmarkPath.isEmpty()) {
        return {{"success", false}, {"error", tr("A new benchmark session should be started.")}};
    }

    const QFileInfo directory(path);
    if (!directory.isDir()) {
        return {{"success", false}, {"error", tr("The path is not an existing directory.")}};
    }

    // Reserve a unique name by creating the file exclusively, then close it for real.
    static const QString characters = QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QString errorString;

    for (int attempt = 0; attempt < 100 && m_benchmarkPath.isEmpty(); attempt++) {
        QString suffix;
        for (int i = 0; i < 6; i++) {
            suffix += characters.at(static_cast<int>(QRandomGenerator::global()->bounded(characters.size())));
        }

        const QString candidate = QDir(path).absoluteFilePath(QStringLiteral("kdiskmark-%1.tmp").arg(suffix));

        QFile file(candidate);
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            file.close();
            m_benchmarkPath = candidate;
        }
        else {
            errorString = file.errorString();
        }
    }

    if (m_benchmarkPath.isEmpty()) {
        return {{"success", false}, {"error", tr("An error occurred while creating the benchmark file: %1")
                                              .arg(errorString)}};
    }

    startFio(QStringList()
             << QStringLiteral("--output-format=json")
             << platformOptions()
             << QStringLiteral("--create_only=1")
             << QStringLiteral("--filename=%1").arg(fioFilename())
             << QStringLiteral("--size=%1m").arg(fileSize)
             << QStringLiteral("--zero_buffers=%1").arg(fillZeros)
             << QStringLiteral("--name=prepare"));

    return {{"success", true}};
}

QVariantMap LocalBackend::startBenchmarkTest(int measuringTime, int fileSize, int randomReadPercentage,
                                             bool fillZeros, bool cacheBypass, bool continuousGeneration,
                                             int blockSize, int queueDepth, int threads, const QString &rw)
{
    if (m_benchmarkPath.isEmpty() || !QFile::exists(m_benchmarkPath)) {
        return {{"success", false}, {"error", tr("The benchmark file was not pre-created.")}};
    }

#ifdef Q_OS_WIN
    const QString ioEngine = QStringLiteral("windowsaio");
#else
    const QString ioEngine = QStringLiteral("libaio");
#endif

    const bool started = startFio(QStringList()
             << QStringLiteral("--output-format=json")
             << platformOptions()
             << QStringLiteral("--ioengine=%1").arg(ioEngine)
             << QStringLiteral("--randrepeat=0")
             << QStringLiteral("--refill_buffers=%1").arg(continuousGeneration)
             << QStringLiteral("--end_fsync=1")
             << QStringLiteral("--direct=%1").arg(cacheBypass)
             << QStringLiteral("--rwmixread=%1").arg(randomReadPercentage)
             << QStringLiteral("--filename=%1").arg(fioFilename())
             << QStringLiteral("--name=%1").arg(rw)
             << QStringLiteral("--size=%1m").arg(fileSize)
             << QStringLiteral("--zero_buffers=%1").arg(fillZeros)
             << QStringLiteral("--bs=%1k").arg(blockSize)
             << QStringLiteral("--runtime=%1").arg(measuringTime)
             << QStringLiteral("--rw=%1").arg(rw)
             << QStringLiteral("--iodepth=%1").arg(queueDepth)
             << QStringLiteral("--numjobs=%1").arg(threads)
             << QStringLiteral("--group_reporting"));

    if (!started) {
        return {{"success", false}, {"error", tr("Another task is still running.")}};
    }

    return {{"success", true}};
}

QVariantMap LocalBackend::flushPageCache()
{
    // Dropping the page cache needs root on Linux and does not exist on Windows. Direct I/O,
    // which is what the test uses by default, does not depend on it.
    return {{"success", true}};
}

QVariantMap LocalBackend::removeBenchmarkFile()
{
    if (m_benchmarkPath.isEmpty() || !QFile::exists(m_benchmarkPath)) {
        m_benchmarkPath.clear();
        return {{"success", false}, {"error", tr("Cannot remove the benchmark file, because it doesn't exist.")}};
    }

    const bool removed = QFile::remove(m_benchmarkPath);
    if (removed) {
        m_benchmarkPath.clear();
    }

    return {{"success", removed}};
}

void LocalBackend::stopProcess()
{
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }

#ifdef Q_OS_WIN
    // terminate() only posts WM_CLOSE, which a console program such as fio never handles
    m_process->kill();
#else
    m_process->terminate();
    if (!m_process->waitForFinished(5000)) {
        m_process->kill();
    }
#endif

    m_process->waitForFinished(2000);
}

QVariantMap LocalBackend::stopCurrentTask()
{
    stopProcess();
    return {{"success", true}};
}

QVariantMap LocalBackend::getDriveHealth(const QString &)
{
    return {{"success", true}, {"available", false},
            {"error", tr("Drive health is not available on this platform.")}};
}
