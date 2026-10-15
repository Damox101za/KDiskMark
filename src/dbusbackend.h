#ifndef DBUSBACKEND_H
#define DBUSBACKEND_H

#include "backend.h"

#include <QDBusPendingCall>

class DevJonmagonKdiskmarkHelperInterface;

// The privileged helper (kdiskmark_helper) over the system D-Bus, authorised through polkit.
class DBusBackend : public Backend
{
    Q_OBJECT

public:
    explicit DBusBackend(QObject *parent = nullptr);

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
    DevJonmagonKdiskmarkHelperInterface *helperInterface(QString *error);
    // Waits for the call with the event loop running, so the interface stays responsive.
    QVariantMap wait(QDBusPendingCall call);
    // Runs one method of the helper's interface (call is a callable taking the interface).
    template <typename Call> QVariantMap invoke(Call call);

    DevJonmagonKdiskmarkHelperInterface *m_interface = nullptr;
};

#endif // DBUSBACKEND_H
