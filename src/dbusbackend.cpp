#include "dbusbackend.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>

#include "helper_interface.h"

DBusBackend::DBusBackend(QObject *parent) : Backend(parent) {}

DevJonmagonKdiskmarkHelperInterface *DBusBackend::helperInterface(QString *error)
{
    if (!QDBusConnection::systemBus().isConnected()) {
        *error = QDBusConnection::systemBus().lastError().message();
        if (error->isEmpty()) *error = tr("Cannot connect to the system D-Bus.");
        return nullptr;
    }

    if (!m_interface) {
        m_interface = new dev::jonmagon::kdiskmark::helper(QStringLiteral("dev.jonmagon.kdiskmark.helperinterface"),
                                                           QStringLiteral("/Helper"), QDBusConnection::systemBus(), this);
        m_interface->setTimeout(10 * 24 * 3600 * 1000); // 10 days, the user may take a while to authenticate

        connect(m_interface, &DevJonmagonKdiskmarkHelperInterface::taskFinished,
                this, [this](bool success, const QString &output, const QString &errorOutput) {
            emit taskFinished(success, output, errorOutput);
        });
    }

    return m_interface;
}

QVariantMap DBusBackend::wait(QDBusPendingCall call)
{
    QDBusPendingCallWatcher watcher(call);
    QEventLoop loop;
    connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);

    if (!watcher.isFinished()) {
        loop.exec();
    }

    QDBusPendingReply<QVariantMap> reply = call;

    if (reply.isError()) {
        const QDBusError error = reply.error();
        return {{"success", false},
                {"accessDenied", error.type() == QDBusError::AccessDenied},
                {"error", !error.message().isEmpty() ? error.message() : error.name()}};
    }

    return reply.value();
}

template <typename Call>
QVariantMap DBusBackend::invoke(Call call)
{
    QString busError;
    DevJonmagonKdiskmarkHelperInterface *helper = helperInterface(&busError);

    if (!helper) {
        return {{"success", false}, {"error", busError}};
    }

    return wait(call(helper));
}

QVariantMap DBusBackend::initSession()
{
    return invoke([](auto *h) { return h->initSession(); });
}

QVariantMap DBusBackend::checkCowStatus(const QString &path)
{
    return invoke([&](auto *h) { return h->checkCowStatus(path); });
}

QVariantMap DBusBackend::createNoCowDirectory(const QString &path)
{
    return invoke([&](auto *h) { return h->createNoCowDirectory(path); });
}

QVariantMap DBusBackend::flushPageCache()
{
    return invoke([](auto *h) { return h->flushPageCache(); });
}

QVariantMap DBusBackend::removeBenchmarkFile()
{
    return invoke([](auto *h) { return h->removeBenchmarkFile(); });
}

QVariantMap DBusBackend::stopCurrentTask()
{
    return invoke([](auto *h) { return h->stopCurrentTask(); });
}

QVariantMap DBusBackend::getDriveHealth(const QString &device)
{
    return invoke([&](auto *h) { return h->getDriveHealth(device); });
}

QVariantMap DBusBackend::prepareBenchmarkFile(const QString &path, int fileSize, bool fillZeros)
{
    return invoke([&](auto *h) { return h->prepareBenchmarkFile(path, fileSize, fillZeros); });
}

QVariantMap DBusBackend::startBenchmarkTest(int measuringTime, int fileSize, int randomReadPercentage,
                                            bool fillZeros, bool cacheBypass, bool continuousGeneration,
                                            int blockSize, int queueDepth, int threads, const QString &rw)
{
    return invoke([&](auto *h) {
        return h->startBenchmarkTest(measuringTime, fileSize, randomReadPercentage, fillZeros,
                                     cacheBypass, continuousGeneration, blockSize, queueDepth, threads, rw);
    });
}

QVariantMap DBusBackend::endSession()
{
    QString busError;
    if (DevJonmagonKdiskmarkHelperInterface *helper = helperInterface(&busError)) {
        // The helper quits on this call, so there is no reply worth waiting for.
        helper->endSession();
    }

    return {{"success", true}};
}
