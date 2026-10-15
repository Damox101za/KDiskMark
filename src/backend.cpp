#include "backend.h"

#include <QCoreApplication>
#include <QFileInfo>

QString Backend::fioProgram()
{
#ifdef Q_OS_WIN
    // An installer bundles fio next to the application (in its own folder, with its files).
    const QString directory = QCoreApplication::applicationDirPath();
    for (const QString &candidate : { directory + QStringLiteral("/fio/fio.exe"),
                                      directory + QStringLiteral("/fio.exe") }) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
#endif

    return QStringLiteral("fio");
}

QByteArray Backend::jsonFromOutput(const QString &output)
{
    const QByteArray bytes = output.toUtf8();

    if (bytes.startsWith('{')) {
        return bytes;
    }

    const int start = bytes.indexOf("\n{");
    return start >= 0 ? bytes.mid(start + 1) : bytes;
}
