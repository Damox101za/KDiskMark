#include "benchmark.h"

#include "global.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStorageInfo>
#include <QTimer>

#include "backend.h"

Benchmark::Benchmark()
{
    m_running = false;
    m_helperAuthorized = false;

    m_backend = Backend::create(this);

    QProcess process;
    process.start(Backend::fioProgram(), {"--version"});
    process.waitForFinished();

    m_FIOVersion = process.readAllStandardOutput().simplified();

    process.close();
}

Benchmark::~Benchmark() {}

QString Benchmark::getFIOVersion()
{
    return m_FIOVersion;
}

bool Benchmark::isFIODetected()
{
    return m_FIOVersion.indexOf("fio-") == 0;
}

void Benchmark::setDir(const QString &dir)
{
    m_dir = dir;
}

QString Benchmark::getBenchmarkFile()
{
    return m_dir;
}

void Benchmark::startTest(int blockSize, int queueDepth, int threads, const QString &rw, const QString &statusMessage)
{
    const AppSettings settings;

    PerformanceResult totalRead { 0, 0, 0 }, totalWrite { 0, 0, 0 };
    QVector<float> samples;

    unsigned int index = 0;

    for (int i = 0; i < settings.getLoopsCount(); i++) {
        if (!m_running) break;

        emit benchmarkStatusUpdate(statusMessage.arg(index + 1).arg(settings.getLoopsCount()));

        if (settings.getFlusingCacheState()) {
            handleReply(m_backend->flushPageCache());
        }

        if (!isRunning()) return;

        bool success = false;
        QString output, errorOutput;

        const bool started = runTask([&]() {
            return m_backend->startBenchmarkTest(settings.getMeasuringTime(),
                                                 settings.getFileSize(),
                                                 settings.getRandomReadPercentage(),
                                                 settings.getBenchmarkTestData() == Global::BenchmarkTestData::Zeros,
                                                 settings.getCacheBypassState(),
                                                 settings.getContinuousGenerationState(),
                                                 blockSize, queueDepth, threads, rw);
        }, &success, &output, &errorOutput);

        if (!started || !isRunning()) return;

        if (!success) {
            setRunning(false);
        }

        if (m_running) {
            index++;

            auto result = parseResult(output, errorOutput);

            if (rw.contains("read")) {
                samples << result.read.Bandwidth;
            }
            else if (rw.contains("write")) {
                samples << result.write.Bandwidth;
            }
            else {
                const float p = settings.getRandomReadPercentage();
                samples << (result.read.Bandwidth * p + result.write.Bandwidth * (100.f - p)) / 100.f;
            }

            switch (settings.getPerformanceProfile())
            {
                case Global::PerformanceProfile::Default:
                    totalRead  += result.read;
                    totalWrite += result.write;
                break;
                case Global::PerformanceProfile::Peak:
                case Global::PerformanceProfile::RealWorld:
                case Global::PerformanceProfile::Demo:
                    totalRead.updateWithBetterValues(result.read);
                    totalWrite.updateWithBetterValues(result.write);
                break;
            }
        }

        if (rw.contains("read")) {
            sendResult(totalRead, index);
        }
        else if (rw.contains("write")) {
            sendResult(totalWrite, index);
        }
        else if (rw.contains("rw")) {
            float p = settings.getRandomReadPercentage();
            sendResult((totalRead * p + totalWrite * (100.f - p)) / 100.f, index);
        }

        sendStats(samples);
    }
}

void Benchmark::sendResult(const Benchmark::PerformanceResult &result, const int index)
{
    const AppSettings settings;
    if (settings.getPerformanceProfile() == Global::PerformanceProfile::Default) {
        for (auto target : m_targets) {
            emit resultReady(target, result / index);
        }
    }
    else {
        for (auto target : m_targets) {
            emit resultReady(target, result);
        }
    }
}

void Benchmark::sendStats(const QVector<float> &samples)
{
    if (samples.isEmpty()) return;

    float minimum = samples.first(), maximum = samples.first(), sum = 0;
    for (float sample : samples) {
        minimum = qMin(minimum, sample);
        maximum = qMax(maximum, sample);
        sum += sample;
    }

    for (auto target : m_targets) {
        emit statsReady(target, minimum, sum / samples.size(), maximum, samples.size());
    }
}

void Benchmark::fetchDriveHealth()
{
    const QString volumeDevice = QStorageInfo(getBenchmarkFile()).device();
    const QString device = DriveHealth::physicalDevice(volumeDevice);

    DriveHealth health = DriveHealth::unavailable(tr("Drive health is not available."), device);

    // Health is extra information, so any failure here must never stop the benchmark.
    const QVariantMap content = m_backend->getDriveHealth(device);

    if (content.value(QStringLiteral("success")).toBool()) {
        if (content.value(QStringLiteral("available")).toBool()) {
            health = DriveHealth::fromSmartctlJson(content.value(QStringLiteral("json")).toByteArray(), device);
        }
        else {
            const QString error = content.value(QStringLiteral("error")).toString();
            if (!error.isEmpty()) health = DriveHealth::unavailable(error, device);
        }
    }

    emit driveHealthReady(health);
}

Benchmark::ParsedJob Benchmark::parseResult(const QString &output, const QString &errorOutput)
{
    QJsonDocument jsonResponse = QJsonDocument::fromJson(output.toUtf8());
    QJsonObject jsonObject = jsonResponse.object();
    QJsonArray jobs = jsonObject["jobs"].toArray();

    ParsedJob parsedJob {{0, 0, 0}, {0, 0, 0}};

    if (jobs.isEmpty() && !errorOutput.isEmpty()) {
        setRunning(false);
        emit failed(errorOutput);
    }
    else if (jobs.isEmpty()) {
        setRunning(false);
        emit failed("Bad FIO output.");
    }
    else {
        QJsonObject job = jobs.first().toObject();

        if (job["error"].toInt() == 0) {
            QJsonObject jobRead = job["read"].toObject();
            parsedJob.read.Bandwidth = jobRead.value("bw").toInt() / 1000.0; // to mb
            parsedJob.read.IOPS = jobRead.value("iops").toDouble();
            parsedJob.read.Latency = jobRead["clat_ns"].toObject().value("mean").toDouble() / 1000.0; // to usec

            QJsonObject jobWrite = job["write"].toObject();
            parsedJob.write.Bandwidth = jobWrite.value("bw").toInt() / 1000.0; // to mb
            parsedJob.write.IOPS = jobWrite.value("iops").toDouble();
            parsedJob.write.Latency = jobWrite["clat_ns"].toObject().value("mean").toDouble() / 1000.0; // to usec
        }
        else {
            setRunning(false);
            emit failed(errorOutput/*.mid(errorOutput.simplified().lastIndexOf("=") + 1)*/);
        }
    }

    return parsedJob;
}

void Benchmark::setRunning(bool state)
{
    if (m_running == state)
        return;

    m_running = state;

    if (!m_running && m_helperAuthorized) {
        m_backend->stopCurrentTask();
    }

    emit runningStateChanged(state);
}

bool Benchmark::isRunning()
{
    return m_running;
}

void Benchmark::runBenchmark(QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> tests)
{
    setRunning(true);

    const AppSettings settings;

    QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>> item;

    QMutableListIterator<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> iter(tests);
    // Set to 0 all the targets for current tests
    while (iter.hasNext()) {
        item = iter.next();
        if (item.first.second == Global::BenchmarkIOReadWrite::Read && settings.getBenchmarkMode() == Global::BenchmarkMode::WriteMix) { iter.remove(); continue; }
        if (item.first.second == Global::BenchmarkIOReadWrite::Write && settings.getBenchmarkMode() == Global::BenchmarkMode::ReadMix) { iter.remove(); continue; }
        auto targets = item.second;
        for (auto obj : targets) {
            emit resultReady(obj, PerformanceResult());
        }
    }

    iter.toFront();

    // If there are no tests in the queue
    if (!iter.hasNext()) {
        setRunning(false);
        return;
    }

    emit benchmarkStatusUpdate(tr("Preparing..."));

    initSession(); if (!isRunning()) return;

    if (AppSettings().getCoWDetectionState()) {
        prepareDirectory(getBenchmarkFile()); if (!isRunning()) return;
    }

    prepareFile(getBenchmarkFile(), settings.getFileSize());

    if (isRunning()) fetchDriveHealth();

    while (iter.hasNext() && isRunning()) {
        item = iter.next();

        m_targets = item.second;

        Global::BenchmarkParams params = settings.getBenchmarkParams(item.first.first, settings.getPerformanceProfile());

        switch (item.first.second)
        {
        case Global::BenchmarkIOReadWrite::Read:
            if (params.Pattern == Global::BenchmarkIOPattern::SEQ) {
                startTest(params.BlockSize, params.Queues, params.Threads,
                          Global::getRWSequentialRead(), tr("Sequential Read %1/%2"));
            }
            else {
                startTest(params.BlockSize, params.Queues, params.Threads,
                          Global::getRWRandomRead(), tr("Random Read %1/%2"));
            }
            break;
        case Global::BenchmarkIOReadWrite::Write:
            if (params.Pattern == Global::BenchmarkIOPattern::SEQ) {
                startTest(params.BlockSize, params.Queues, params.Threads,
                          Global::getRWSequentialWrite(), tr("Sequential Write %1/%2"));
            }
            else {
                startTest(params.BlockSize, params.Queues, params.Threads,
                          Global::getRWRandomWrite(), tr("Random Write %1/%2"));
            }
            break;
        case Global::BenchmarkIOReadWrite::Mix:
            if (params.Pattern == Global::BenchmarkIOPattern::SEQ) {
                startTest(params.BlockSize, params.Queues, params.Threads,
                          Global::getRWSequentialMix(), tr("Sequential Mix %1/%2"));
            }
            else {
                startTest(params.BlockSize, params.Queues, params.Threads,
                          Global::getRWRandomMix(), tr("Random Mix %1/%2"));
            }
            break;
        }

        if (iter.hasNext()) {
            for (int i = 0, intervalTime = settings.getIntervalTime(); i < intervalTime && m_running; i++) {
                emit benchmarkStatusUpdate(tr("Interval Time %1/%2 sec").arg(i).arg(intervalTime));
                QEventLoop loop;
                QTimer::singleShot(1000, &loop, SLOT(quit()));
                loop.exec();
            }
        }
    }

    handleReply(m_backend->removeBenchmarkFile());

    setRunning(false);

    // Turn off the helper, otherwise it will not be able to perform another test
    m_backend->endSession();

    emit finished(); // Only needed when closing the app during a running benchmarking
}

void Benchmark::initSession()
{
    m_helperAuthorized = false;

    handleReply(m_backend->initSession());

    // Process was not stopped by handleReply, consider that the authorization was successful
    if (isRunning()) m_helperAuthorized = true;
}

void Benchmark::prepareDirectory(const QString &benchmarkFile)
{
    QVariantMap reply = m_backend->checkCowStatus(benchmarkFile);
    if (!handleReply(reply)) return;

    if (reply.value(QStringLiteral("hasCow")).toBool()) {
        bool createNoCowDir = false;
        auto callback = [&createNoCowDir](bool create) {
            createNoCowDir = create;
        };

        auto conn = QObject::connect(this, &Benchmark::createNoCowDirectoryResponse, callback);
        emit cowCheckRequired();
        QObject::disconnect(conn);

        if (createNoCowDir) {
            reply = m_backend->createNoCowDirectory(benchmarkFile);
            if (!handleReply(reply)) return;

            this->setDir(reply.value(QStringLiteral("path")).toString());
            emit directoryChanged(this->getBenchmarkFile());
        }
    }
}

void Benchmark::prepareFile(const QString &benchmarkFile, int fileSize)
{
    bool success = false;
    QString output, errorOutput;

    const bool started = runTask([&]() {
        return m_backend->prepareBenchmarkFile(benchmarkFile, fileSize,
                                               AppSettings().getBenchmarkTestData() == Global::BenchmarkTestData::Zeros);
    }, &success, &output, &errorOutput);

    if (!started || !isRunning()) return;

    if (!success) {
        setRunning(false);
        emit failed(!errorOutput.isEmpty() ? errorOutput : "The benchmark file could not be prepared.");
    }

    parseResult(output, errorOutput);
}

bool Benchmark::handleReply(const QVariantMap &reply)
{
    if (!isRunning()) return false;

    if (!reply.value(QStringLiteral("success")).toBool()) {
        setRunning(false);

        if (reply.value(QStringLiteral("accessDenied")).toBool()) {
            emit failed(tr("Could not obtain administrator privileges."));
        }
        else {
            const QString error = reply.value(QStringLiteral("error")).toString();
            emit failed(!error.isEmpty() ? error : "An error has occurred when executing a task.");
        }

        return false;
    }

    return true;
}

bool Benchmark::runTask(const std::function<QVariantMap()> &start, bool *success, QString *output, QString *errorOutput)
{
    bool finished = false;
    QEventLoop loop;

    // Connected before the task starts so a task that finishes at once is not missed
    auto conn = connect(m_backend, &Backend::taskFinished,
                        [&](bool ok, const QString &out, const QString &err) {
        *success = ok;
        *output = out;
        *errorOutput = err;
        finished = true;
        loop.quit();
    });

    const bool started = handleReply(start());

    if (started && !finished) {
        loop.exec();
    }

    QObject::disconnect(conn);

    return started;
}
