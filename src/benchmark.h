#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <QProcess>
#include <QStringList>
#include <QString>
#include <QObject>

#include <functional>
#include <memory>

#include "appsettings.h"
#include "drivehealth.h"

class Backend;

class Benchmark : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(Benchmark)

public:
    Benchmark();
    ~Benchmark();

    QString getFIOVersion();
    bool isFIODetected();

    void setDir(const QString &dir);
    QString getBenchmarkFile();

    void setRunning(bool state);
    bool isRunning();

    // Targets are opaque receivers (e.g. QProgressBar in the GUI, result cells in the TUI)
    // reported back through the resultReady signal.
    void runBenchmark(QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> tests);

    struct PerformanceResult
    {
        float Bandwidth;
        float IOPS;
        float Latency;

        PerformanceResult operator+ (const PerformanceResult& rhs)
        {
            return *this += rhs;
        }

        PerformanceResult operator+= (const PerformanceResult& rhs)
        {
            Bandwidth += rhs.Bandwidth;
            IOPS += rhs.IOPS;
            Latency += rhs.Latency;
            return *this;
        }

        PerformanceResult operator/ (const unsigned int rhs) const
        {
            if (rhs == 0) return *this;

            return PerformanceResult { Bandwidth / rhs, IOPS / rhs, Latency / rhs };
        }

        PerformanceResult operator* (const unsigned int rhs) const
        {
            return PerformanceResult { Bandwidth * rhs, IOPS * rhs, Latency * rhs };
        }

        void updateWithBetterValues(const PerformanceResult& result) {
            Bandwidth = Bandwidth < result.Bandwidth ? result.Bandwidth : Bandwidth;
            IOPS = IOPS < result.IOPS ? result.IOPS : IOPS;
            if (Latency == 0) Latency = result.Latency;
            Latency = Latency > result.Latency ? result.Latency : Latency;
        }
    };

    struct ParsedJob
    {
        PerformanceResult read, write;
    };

private:
    bool m_running;
    bool m_helperAuthorized;
    QString m_FIOVersion;
    QVector<QObject*> m_targets;
    QString m_dir;

    Backend *m_backend = nullptr;

private:
    void startTest(int blockSize, int queueDepth, int threads, const QString &rw, const QString &statusMessage);
    Benchmark::ParsedJob parseResult(const QString &output, const QString &errorOutput);
    void sendResult(const Benchmark::PerformanceResult &result, const int index);
    void sendStats(const QVector<float> &samples);
    void fetchDriveHealth();

    void initSession();
    void prepareDirectory(const QString &benchmarkFile);
    void prepareFile(const QString &benchmarkFile, int fileSize);

    // Reports a failed reply of the backend. True when the call succeeded and the run goes on.
    bool handleReply(const QVariantMap &reply);
    // Starts a task, waits for its taskFinished and returns whether it could be started.
    bool runTask(const std::function<QVariantMap()> &start, bool *success, QString *output, QString *errorOutput);

signals:
    void benchmarkStatusUpdate(const QString &name);
    void resultReady(QObject *target, const Benchmark::PerformanceResult &result);
    // Bandwidth (MB/s) of the individual loops of a test: minimum, average, maximum and loops done.
    void statsReady(QObject *target, float minimum, float average, float maximum, int samples);
    void driveHealthReady(const DriveHealth &health);
    void failed(const QString &error);
    void finished();
    void runningStateChanged(bool state);
    void cowCheckRequired();
    void directoryChanged(const QString &newDir);
    void createNoCowDirectoryResponse(bool create);
};

#endif // BENCHMARK_H
