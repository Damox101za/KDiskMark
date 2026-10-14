#ifndef REPORTDATA_H
#define REPORTDATA_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Benchmark reports are kept as JSON. The JSON history file is the source of truth,
// other formats (CSV) are generated from it.
//
// History file layout:
//   { "version": 1, "reports": [ <report>, ... ] }
//
// Report layout:
//   { "date": ISO 8601 UTC, "app_version", "fio_version", "os", "target", "device_model",
//     "profile", "mixed", "mix_read_percent", "file_size_mib", "loops",
//     "measuring_time_s", "interval_time_s", "test_data",
//     "system":  { "os", "kernel", "cpu", "cpu_threads", "ram_mib" },
//     "drive":   { "device", "model", "firmware", "protocol", "filesystem",
//                  "capacity_bytes", "target_total_bytes", "target_free_bytes" },
//     "health":  { "available", "message", "health_percent"|null, "smart_passed"|null,
//                  "percentage_used"|null, "temperature_c"|null, "power_on_hours"|null },
//     "filesystem_health": { "available", "message", "filesystem",
//                  "health_percent"|null, "error_count"|null },
//     "summary": { "score", "rating", "peak_seq_read_mb_s", "peak_seq_write_mb_s",
//                  "performance_percent"|null },
//     "results": [ { "section": "read|write|mix", "pattern": "sequential|random",
//                    "block_size_kib", "queue_depth", "threads",
//                    "bandwidth_mb_s", "bandwidth_min_mb_s", "bandwidth_avg_mb_s",
//                    "bandwidth_max_mb_s", "iops", "latency_us" }, ... ] }
//
// Version 1 files (no system/drive/health/summary, no min/avg/max) are still read.
namespace ReportData
{
    constexpr int FormatVersion = 2;

    // $XDG_DATA_HOME/kdiskmark/results.json
    QString historyPath();

    // Appends the report to the history file. Returns false and fills error on failure.
    bool appendToHistory(const QJsonObject &report, QString *error = nullptr);

    // Reads every report from the history file. A missing file is an empty history.
    QJsonArray loadHistory(QString *error = nullptr);

    // Host information that does not depend on the benchmark target.
    QJsonObject systemInfo();

    // Overall score (geometric mean of all bandwidths, MB/s) and a plain-language class,
    // derived from the "results" array of a report.
    QJsonObject summarize(const QJsonArray &results);

    // Extra sections appended to the plain text report: per-test min/avg/max, summary,
    // drive health, drive and system. Works for reports of any format version.
    QString toTextExtras(const QJsonObject &report);

    // Geometric mean of result / reference over the standard tests, in percent, where 100 is a
    // mainstream NVMe SSD (see the table in reportdata.cpp). Can exceed 100. Returns -1 when
    // none of the results is a standard test.
    int performancePercent(const QJsonArray &results);

    QString toCsv(const QJsonObject &report, bool withHeader = true);
    QString toCsv(const QJsonArray &reports);

    bool writeJson(const QString &fileName, const QJsonObject &report, QString *error = nullptr);
    bool writeText(const QString &fileName, const QString &text, QString *error = nullptr);
}

#endif // REPORTDATA_H
