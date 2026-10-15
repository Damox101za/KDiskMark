#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QJsonObject>

#include "benchmark.h"
#include "appsettings.h"
#include "drivehealth.h"

class QComboBox;
class QProgressBar;
class QLabel;
class HealthGauge;
class QStorageInfo;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_Test_1_clicked();

    void on_pushButton_Test_2_clicked();

    void on_pushButton_Test_3_clicked();

    void on_pushButton_Test_4_clicked();

    void on_pushButton_All_clicked();

    void on_actionAbout_triggered();

    void on_actionQueues_Threads_triggered();

    void on_comboBox_Storages_currentIndexChanged(int index);

    void on_loopsCount_valueChanged(int arg1);

    void on_comboBox_ComparisonUnit_currentIndexChanged(int index);

    void on_actionCopy_triggered();

    void on_actionSave_triggered();

    void on_comboBox_MixRatio_currentIndexChanged(int index);

    void on_refreshStoragesButton_clicked();

    void on_comboBox_fileSize_currentIndexChanged(int index);

    void on_actionFlush_Pagecache_triggered(bool checked);

    void on_actionUse_O_DIRECT_triggered(bool checked);

    void on_actionTestData_Continuous_triggered(bool checked);

    void on_actionCoW_detection_triggered(bool checked);

private:
    Ui::MainWindow *ui;
    Benchmark *m_benchmark;
    QVector<QProgressBar*> m_progressBars;
    QString m_windowTitle;
    DriveHealth m_driveHealth;
    HealthGauge *m_healthGauge = nullptr;
    HealthGauge *m_fsGauge = nullptr;
    HealthGauge *m_perfGauge = nullptr;
    QLabel *m_healthDetails = nullptr;
    QLabel *m_scoreValue = nullptr;
    QLabel *m_scoreRating = nullptr;

    void setupInsightsPanel();
    void updateInsightsPanel();
    FilesystemHealth currentFilesystemHealth() const;
    void applyThemeStyleSheet();

    void updateFileSizeList();
    void updateStoragesList();
    void addItemToStoragesList(const Global::Storage &storage);
    void defineBenchmark(std::function<void()> bodyFunc);
    void closeEvent(QCloseEvent *event);
    QString formatSize(quint64 available, quint64 total);
    QString getTextBenchmarkResult();
    QJsonObject getBenchmarkReport();
    void exportHistory();
    void updateBenchmarkButtonsContent();
    void updatePresetsSelection();
    void refreshProgressBars();
    void updateProgressBar(QProgressBar *progressBar);
    void updateLabels();
    bool runCombinedRandomTest();
    QString combineOutputTestResult(const QProgressBar *progressBar, const Global::BenchmarkParams &params);
    void resizeComboBoxItemsPopup(QComboBox *combobox);
    void updateProgressBarsStyle();
    void handleDirectoryChanged(const QString &newDir);

public slots:
    void benchmarkStatusUpdate(const QString &name);
    void benchmarkFailed(const QString &error);
    void handleResults(QObject *target, const Benchmark::PerformanceResult &result);
    void handleStats(QObject *target, float minimum, float average, float maximum, int samples);
    void handleDriveHealth(const DriveHealth &health);
    void localeSelected(QAction* act);
    void profileSelected(QAction* act);
    void modeSelected(QAction* act);
    void testDataSelected(QAction* act);
    void presetSelected(QAction* act);
    void themeSelected(QAction* act);
    void benchmarkStateChanged(bool state);
    void handleCowCheck();

protected slots:
    virtual void changeEvent(QEvent * event);
};
#endif // MAINWINDOW_H
