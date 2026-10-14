#include "helper.h"

#include <QCoreApplication>
#include <QtDBus>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <PolkitQt1/Authority>
#include <PolkitQt1/Subject>

#include <signal.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <unistd.h>

namespace
{
    // Bounds of what the settings dialog can produce, with a little headroom. The helper runs
    // fio as root, so it must not trust whatever a D-Bus client sends.
    constexpr int MaxFileSizeMiB = 64 * 1024;
    constexpr int MaxMeasuringTimeSec = 3600;
    constexpr int MaxBlockSizeKiB = 8192;
    constexpr int MaxQueueDepth = 512;
    constexpr int MaxThreads = 64;

    bool inRange(int value, int minimum, int maximum)
    {
        return value >= minimum && value <= maximum;
    }

    // Returns an empty string when the parameters are acceptable, otherwise the reason.
    QString validateFileSize(int fileSize)
    {
        if (!inRange(fileSize, 1, MaxFileSizeMiB)) {
            return QStringLiteral("The file size must be between 1 and %1 MiB.").arg(MaxFileSizeMiB);
        }

        return QString();
    }

    QString validateTestParameters(int measuringTime, int fileSize, int randomReadPercentage,
                                   int blockSize, int queueDepth, int threads, const QString &rw)
    {
        static const QStringList allowedRw = {
            QStringLiteral("read"), QStringLiteral("write"), QStringLiteral("rw"),
            QStringLiteral("randread"), QStringLiteral("randwrite"), QStringLiteral("randrw")
        };

        if (!allowedRw.contains(rw)) {
            return QStringLiteral("The I/O pattern is not supported.");
        }

        if (!inRange(measuringTime, 1, MaxMeasuringTimeSec)) {
            return QStringLiteral("The measuring time must be between 1 and %1 seconds.").arg(MaxMeasuringTimeSec);
        }

        if (!inRange(randomReadPercentage, 0, 100)) {
            return QStringLiteral("The read percentage must be between 0 and 100.");
        }

        if (!inRange(blockSize, 1, MaxBlockSizeKiB)) {
            return QStringLiteral("The block size must be between 1 and %1 KiB.").arg(MaxBlockSizeKiB);
        }

        if (!inRange(queueDepth, 1, MaxQueueDepth)) {
            return QStringLiteral("The queue depth must be between 1 and %1.").arg(MaxQueueDepth);
        }

        if (!inRange(threads, 1, MaxThreads)) {
            return QStringLiteral("The number of threads must be between 1 and %1.").arg(MaxThreads);
        }

        return validateFileSize(fileSize);
    }
}

HelperAdaptor::HelperAdaptor(Helper *parent) :
    QDBusAbstractAdaptor(parent)
{
    m_parentHelper = parent;
}

QVariantMap HelperAdaptor::initSession()
{
    return m_parentHelper->initSession();
}

QVariantMap HelperAdaptor::endSession()
{
    return m_parentHelper->endSession();
}

QVariantMap HelperAdaptor::prepareBenchmarkFile(const QString &benchmarkFile, int fileSize, bool fillZeros)
{
    return m_parentHelper->prepareBenchmarkFile(benchmarkFile, fileSize, fillZeros);
}

QVariantMap HelperAdaptor::startBenchmarkTest(int measuringTime, int fileSize, int randomReadPercentage, bool fillZeros, bool cacheBypass, bool continuousGeneration,
                                              int blockSize, int queueDepth, int threads, const QString &rw)
{
    return m_parentHelper->startBenchmarkTest(
      measuringTime, fileSize, randomReadPercentage, fillZeros, cacheBypass,
      continuousGeneration, blockSize, queueDepth, threads, rw);
}

QVariantMap HelperAdaptor::flushPageCache()
{
    return m_parentHelper->flushPageCache();
}

QVariantMap HelperAdaptor::removeBenchmarkFile()
{
    return m_parentHelper->removeBenchmarkFile();
}

QVariantMap HelperAdaptor::stopCurrentTask()
{
    return m_parentHelper->stopCurrentTask();
}

QVariantMap HelperAdaptor::checkCowStatus(const QString &path)
{
    return m_parentHelper->checkCowStatus(path);
}

QVariantMap HelperAdaptor::createNoCowDirectory(const QString &path)
{
    return m_parentHelper->createNoCowDirectory(path);
}

Helper::Helper() : m_helperAdaptor(new HelperAdaptor(this))
{
    if (!QDBusConnection::systemBus().isConnected() || !QDBusConnection::systemBus().registerService(QStringLiteral("dev.jonmagon.kdiskmark.helperinterface")) ||
        !QDBusConnection::systemBus().registerObject(QStringLiteral("/Helper"), this)) {
        qWarning() << QDBusConnection::systemBus().lastError().message();
        qApp->quit();
    }

    m_serviceWatcher = new QDBusServiceWatcher(this);
    m_serviceWatcher->setConnection(QDBusConnection::systemBus());
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, qApp, [this](const QString &service) {
        m_serviceWatcher->removeWatchedService(service);
        if (m_serviceWatcher->watchedServices().isEmpty()) {
            qApp->quit();
        }
    });

    QObject::connect(this, &Helper::taskFinished, m_helperAdaptor, &HelperAdaptor::taskFinished);

    // A single process object, owned by the helper, so a fio that is still running is
    // killed when the helper exits and finished() always refers to the current task.
    m_process = new QProcess(this);

    connect(m_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus exitStatus) {
        emit taskFinished(exitStatus == QProcess::NormalExit,
                          QString(m_process->readAllStandardOutput()),
                          QString(m_process->readAllStandardError()));
    });

    // finished() is not emitted when fio cannot be started at all, report that as a failed task
    // instead of leaving the client waiting forever.
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit taskFinished(false, QString(), m_process->errorString());
        }
    });
}

bool Helper::startFio(const QStringList &arguments)
{
    if (m_process->state() != QProcess::NotRunning) {
        return false;
    }

    m_process->start(QStringLiteral("fio"), arguments);
    return true;
}

QVariantMap Helper::initSession()
{
    if (!calledFromDBus()) {
        return {};
    }

    if (m_serviceWatcher->watchedServices().contains(message().service())) {
        return {{"success", true}};
    }

    if (!m_serviceWatcher->watchedServices().isEmpty()) {
        return {{"success", false}, {"error", "There are already registered DBus connection."}};
    }

    // The authorization below spins a nested event loop, so another initSession() can be
    // dispatched while it is pending. Without this guard two callers could both be authorised
    // for the single session, and a caller denied in the meantime would quit the helper
    // under the one whose prompt is still open.
    if (m_authInProgress) {
        return {{"success", false}, {"error", "Another authorization is already in progress."}};
    }

    PolkitQt1::SystemBusNameSubject subject(message().service());
    PolkitQt1::Authority *authority = PolkitQt1::Authority::instance();

    PolkitQt1::Authority::Result result = PolkitQt1::Authority::No;
    QEventLoop e;
    connect(authority, &PolkitQt1::Authority::checkAuthorizationFinished, &e, [&e, &result](PolkitQt1::Authority::Result _result) {
        result = _result;
        e.quit();
    });

    m_authInProgress = true;
    authority->checkAuthorization(QStringLiteral("dev.jonmagon.kdiskmark.helper.init"), subject, PolkitQt1::Authority::AllowUserInteraction);
    e.exec();
    m_authInProgress = false;

    if (authority->hasError()) {
        qDebug() << "Encountered error while checking authorization, error code: " << authority->lastError() << authority->errorDetails();
        authority->clearError();
    }

    switch (result) {
    case PolkitQt1::Authority::Yes:
        // track who called into us so we can close when all callers have gone away
        m_serviceWatcher->addWatchedService(message().service());
        return {{"success", true}};
    default:
        sendErrorReply(QDBusError::AccessDenied);
        if (m_serviceWatcher->watchedServices().isEmpty())
            qApp->quit();
        return {};
    }
}

QVariantMap Helper::endSession()
{
    if (!isCallerAuthorized()) {
        return {};
    }

    qApp->exit();

    return {};
}

// Turns a client supplied directory into the canonical path that is then used for every operation.
// Symlinks anywhere in the path are resolved once, here, so the later calls never traverse a link,
// and the pseudo filesystems and device nodes are refused after resolution, which makes spellings
// such as //dev, /./dev or a link into /proc ineffective.
bool Helper::resolveDirectory(const QString &path, QString *resolved, QString *error)
{
    if (!path.startsWith(QLatin1Char('/'))) {
        *error = QStringLiteral("The path must be absolute.");
        return false;
    }

    const QFileInfo info(path);

    // A link as the last component is refused outright, as before
    if (info.isSymLink()) {
        *error = QStringLiteral("The path should not be symbolic link.");
        return false;
    }

    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !QFileInfo(canonical).isDir()) {
        *error = QStringLiteral("The path is not an existing directory.");
        return false;
    }

    for (const char *forbidden : { "/dev", "/proc", "/sys" }) {
        const QString root = QLatin1String(forbidden);
        if (canonical == root || canonical.startsWith(root + QLatin1Char('/'))) {
            *error = QStringLiteral("The path cannot be inside %1.").arg(root);
            return false;
        }
    }

    *resolved = canonical;
    return true;
}

QVariantMap Helper::prepareBenchmarkFile(const QString &benchmarkPath, int fileSize, bool fillZeros)
{
    if (!isCallerAuthorized()) {
        return {};
    }

    if (m_process->state() != QProcess::NotRunning) {
        return {{"success", false}, {"error", "Another task is still running."}};
    }

    // If benchmarking has been done, but removeBenchmarkFile has not been called,
    // and benchmarking on a new file is called, then reject the request. The *previous* file must be removed first.
    if (!m_benchmarkFile.fileName().isNull()) {
        return {{"success", false}, {"error", "A new benchmark session should be started."}};
    }

    QString directory, pathError;
    if (!resolveDirectory(benchmarkPath, &directory, &pathError)) {
        return {{"success", false}, {"error", pathError}};
    }

    // Checked before the file is created so a rejected request leaves nothing behind
    if (const QString error = validateFileSize(fileSize); !error.isEmpty()) {
        return {{"success", false}, {"error", error}};
    }

    m_benchmarkFile.setFileTemplate(QStringLiteral("%1/%2").arg(directory).arg("kdiskmark-XXXXXX.tmp"));

    if (!m_benchmarkFile.open()) {
        return {{"success", false}, {"error", QStringLiteral("An error occurred while creating the benchmark file: %1").arg(m_benchmarkFile.errorString())}};
    }

    startFio(QStringList()
             << QStringLiteral("--output-format=json")
             << QStringLiteral("--create_only=1")
             << QStringLiteral("--filename=%1").arg(m_benchmarkFile.fileName())
             << QStringLiteral("--size=%1m").arg(fileSize)
             << QStringLiteral("--zero_buffers=%1").arg(fillZeros)
             << QStringLiteral("--name=prepare"));

    return {{"success", true}};
}

QVariantMap Helper::startBenchmarkTest(int measuringTime, int fileSize, int randomReadPercentage, bool fillZeros, bool cacheBypass, bool continuousGeneration,
                                       int blockSize, int queueDepth, int threads, const QString &rw)
{
    if (!isCallerAuthorized()) {
        return {};
    }

    if (m_benchmarkFile.fileName().isNull() || !QFile(m_benchmarkFile.fileName()).exists()) {
        return {{"success", false}, {"error", "The benchmark file was not pre-created."}};
    }

    if (const QString error = validateTestParameters(measuringTime, fileSize, randomReadPercentage,
                                                     blockSize, queueDepth, threads, rw);
        !error.isEmpty()) {
        return {{"success", false}, {"error", error}};
    }

    const bool started = startFio(QStringList()
                     << QStringLiteral("--output-format=json")
                     << QStringLiteral("--ioengine=libaio")
                     << QStringLiteral("--randrepeat=0")
                     << QStringLiteral("--refill_buffers=%1").arg(continuousGeneration)
                     << QStringLiteral("--end_fsync=1")
                     << QStringLiteral("--direct=%1").arg(cacheBypass)
                     << QStringLiteral("--rwmixread=%1").arg(randomReadPercentage)
                     << QStringLiteral("--filename=%1").arg(m_benchmarkFile.fileName())
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
        return {{"success", false}, {"error", "Another task is still running."}};
    }

    return {{"success", true}};
}

QVariantMap Helper::flushPageCache()
{
    if (!isCallerAuthorized()) {
        return {};
    }

    if (m_benchmarkFile.fileName().isNull() || !QFile(m_benchmarkFile.fileName()).exists()) {
        return {{"success", false}, {"error", "A benchmark file must first be created."}};
    }

    QFile file("/proc/sys/vm/drop_caches");

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write("1");
        file.close();
    }
    else {
        return {{"success", false}, {"error", file.errorString()}};
    }

    return {{"success", true}};
}

QVariantMap Helper::removeBenchmarkFile()
{
    if (!isCallerAuthorized()) {
        return {};
    }

    if (m_benchmarkFile.fileName().isNull() || !QFile(m_benchmarkFile.fileName()).exists()) {
        return {{"success", false}, {"error", "Cannot remove the benchmark file, because it doesn't exist."}};
    }

    m_benchmarkFile.close();

    return {{"success", m_benchmarkFile.remove()}};
}

QVariantMap Helper::stopCurrentTask()
{
    if (!isCallerAuthorized()) {
        return {};
    }

    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();

        // fio normally exits on SIGTERM; do not let a stuck process block the helper forever.
        if (!m_process->waitForFinished(5000)) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }

    return {{"success", true}};
}

QVariantMap Helper::checkCowStatus(const QString &path)
{
    if (!isCallerAuthorized()) {
        return {};
    }

    QString directory, pathError;
    if (!resolveDirectory(path, &directory, &pathError)) {
        return {{"success", false}, {"error", pathError}};
    }

    struct statfs fs_info;
    if (statfs(directory.toUtf8().constData(), &fs_info) != 0) {
        return {{"success", false}, {"error", QStringLiteral("Cannot get filesystem info: %1").arg(strerror(errno))}};
    }

    switch (fs_info.f_type) {
        case 0x9123683E: // BTRFS_SUPER_MAGIC
        case 0xca451a4e: // BCACHEFS_SUPER_MAGIC
            break;
        default:
            return {{"success", true}, {"hasCow", false}};
    }

    int fd = open(directory.toUtf8().constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return {{"success", false}, {"error", QStringLiteral("Cannot open directory: %1").arg(strerror(errno))}};
    }

    unsigned long flags = 0;
    bool hasCow = false;
    if (ioctl(fd, FS_IOC_GETFLAGS, &flags) >= 0) {
        hasCow = !(flags & FS_NOCOW_FL);
    } else {
        qWarning() << QStringLiteral("Failed to get FS flags for") << directory << QStringLiteral(":") << strerror(errno);
    }

    close(fd);
    return {{"success", true}, {"hasCow", hasCow}};
}

QVariantMap Helper::createNoCowDirectory(const QString &path)
{
    if (!isCallerAuthorized()) {
        return {};
    }

    QString directory, pathError;
    if (!resolveDirectory(path, &directory, &pathError)) {
        return {{"success", false}, {"error", pathError}};
    }

    const QString newDir = QDir(directory).absoluteFilePath(QStringLiteral("kdiskmark_no_cow"));
    const QByteArray newDirPath = newDir.toUtf8();

    // Create the directory ourselves instead of mkpath(): if the name already exists it must be
    // a real directory that this helper (root) owns. A link, or a directory somebody else planted
    // in a shared parent such as /tmp, is refused before anything is changed on it.
    if (mkdir(newDirPath.constData(), 0755) != 0) {
        if (errno != EEXIST) {
            return {{"success", false}, {"error", QStringLiteral("Failed to create directory: %1").arg(newDir)}};
        }

        struct stat st;
        if (lstat(newDirPath.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
            return {{"success", false}, {"error", QStringLiteral("%1 already exists and is not a directory owned by the helper.").arg(newDir)}};
        }
    }

    int fd = open(newDirPath.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return {{"success", false}, {"error", QStringLiteral("Cannot open new directory: %1").arg(strerror(errno))}};
    }

    unsigned long flags = 0;
    if (ioctl(fd, FS_IOC_GETFLAGS, &flags) < 0) {
        close(fd);
        return {{"success", false}, {"error", QStringLiteral("Cannot get flags: %1").arg(strerror(errno))}};
    }

    flags |= FS_NOCOW_FL;
    bool success = (ioctl(fd, FS_IOC_SETFLAGS, &flags) >= 0);
    close(fd);

    if (!success) {
        return {{"success", false}, {"error", QStringLiteral("Cannot set NoCow flag: %1").arg(strerror(errno))}};
    }

    return {{"success", true}, {"path", newDir}};
}

bool Helper::isCallerAuthorized()
{
    if (!calledFromDBus()) {
        return false;
    }

    return m_serviceWatcher->watchedServices().contains(message().service());
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    Helper helper;
    a.exec();
}
