#include "backend.h"

#include "localbackend.h"
#ifndef Q_OS_WIN
#include "dbusbackend.h"
#endif

Backend *Backend::create(QObject *parent)
{
#ifdef Q_OS_WIN
    return new LocalBackend(parent);
#else
    // Development and testing switch: run fio as the current user, without the helper or polkit.
    if (qgetenv("KDISKMARK_BACKEND") == "local") {
        return new LocalBackend(parent);
    }

    return new DBusBackend(parent);
#endif
}
