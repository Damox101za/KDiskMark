#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QString>
#include <QVariantMap>

// What the benchmark engine needs from whatever runs fio and owns the benchmark file.
//
// - DBusBackend: the privileged helper over the system D-Bus (Linux, the default).
// - LocalBackend: fio is started by the application itself with the user's own rights. Used on
//   Windows, which has no helper, and on Linux when KDISKMARK_BACKEND=local is set.
//
// Every call returns the map the helper's D-Bus methods return: "success", "error" and entries
// specific to the method. A caller that was refused is {success: false, accessDenied: true}.
// startBenchmarkTest() and prepareBenchmarkFile() return as soon as fio is started; the result
// arrives through taskFinished().
class Backend : public QObject
{
    Q_OBJECT

public:
    explicit Backend(QObject *parent = nullptr) : QObject(parent) {}
    ~Backend() override = default;

    // The backend to use on this platform.
    static Backend *create(QObject *parent = nullptr);

    // The fio executable: next to the application on Windows when bundled, otherwise "fio" from PATH.
    static QString fioProgram();

    virtual QVariantMap initSession() = 0;
    virtual QVariantMap endSession() = 0;
    virtual QVariantMap checkCowStatus(const QString &path) = 0;
    virtual QVariantMap createNoCowDirectory(const QString &path) = 0;
    virtual QVariantMap prepareBenchmarkFile(const QString &path, int fileSize, bool fillZeros) = 0;
    virtual QVariantMap startBenchmarkTest(int measuringTime, int fileSize, int randomReadPercentage,
                                           bool fillZeros, bool cacheBypass, bool continuousGeneration,
                                           int blockSize, int queueDepth, int threads, const QString &rw) = 0;
    virtual QVariantMap flushPageCache() = 0;
    virtual QVariantMap removeBenchmarkFile() = 0;
    virtual QVariantMap stopCurrentTask() = 0;
    virtual QVariantMap getDriveHealth(const QString &device) = 0;

signals:
    void taskFinished(bool success, const QString &output, const QString &errorOutput);
};

#endif // BACKEND_H
