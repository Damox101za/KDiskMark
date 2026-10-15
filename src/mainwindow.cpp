#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QStorageInfo>
#include <QMetaEnum>
#include <QClipboard>
#include <QDate>
#include <QFileDialog>
#include <QTextStream>
#include <QAbstractItemView>
#include <QStyleFactory>
#include <QTimer>
#include <QActionGroup>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSysInfo>

#include "math.h"
#include "about.h"
#include "settings.h"
#include "diskdriveinfo.h"
#include "storageitemdelegate.h"
#include "global.h"
#include "reportdata.h"
#include "healthgauge.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_benchmark(new Benchmark)
{
    ui->setupUi(this);

    QActionGroup *localesGroup = new QActionGroup(this);

    const QVector<QLocale> locales = Global::getSupportedLocales();

    for (const QLocale &locale : locales) {
        QString langName = locale.nativeLanguageName();
        QAction *lang = new QAction(QStringLiteral("%1%2").arg(langName[0].toUpper(), langName.mid(1)), this);
        lang->setCheckable(true);
        lang->setData(locale);
        lang->setActionGroup(localesGroup);
        ui->menuLanguage->addAction(lang);

        if (QLocale().name() == locale.name()) lang->setChecked(true);
    }

    connect(localesGroup, &QActionGroup::triggered, this, &MainWindow::localeSelected);

    ui->extraIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(QSize(16, 16)));
    ui->extraIcon->setToolTip(tr("The device is encrypted. Performance may drop."));
    ui->extraIcon->setVisible(false);

    statusBar()->hide();

    ui->loopsCount->findChild<QLineEdit*>()->setReadOnly(true);

    ui->comboBox_Storages->setItemDelegate(new StorageItemDelegate());

    ui->actionCopy->setIcon(QIcon::fromTheme("edit-copy"));
    ui->actionSave->setIcon(QIcon::fromTheme("document-save"));

    QAction *exportHistoryAction = new QAction(tr("Export History as CSV..."), this);
    ui->menuFile->insertAction(ui->actionExit, exportHistoryAction);
    connect(exportHistoryAction, &QAction::triggered, this, &MainWindow::exportHistory);
    ui->actionExit->setIcon(QIcon::fromTheme("application-exit"));
    ui->menuTest_Data->setIcon(QIcon::fromTheme("distribute-randomize"));
    ui->actionQueues_Threads->setIcon(QIcon::fromTheme("configure"));
    ui->actionTheme_Use_Fusion->setIcon(QIcon::fromTheme("preferences-desktop-color"));
    ui->actionTheme_Stylesheet_Light->setIcon(QIcon::fromTheme("color-picker-white"));
    ui->actionTheme_Stylesheet_Dark->setIcon(QIcon::fromTheme("color-picker-black"));
    ui->actionTheme_Do_not_apply->setIcon(QIcon::fromTheme("edit-undo"));
    ui->actionAbout->setIcon(QIcon::fromTheme("kdiskmark"));
    ui->menuLanguage->setIcon(QIcon::fromTheme("language-chooser"));

    ui->actionDefault->setProperty("profile", Global::PerformanceProfile::Default);
    ui->actionDefault->setProperty("mixed", false);
    ui->actionPeak_Performance->setProperty("profile", Global::PerformanceProfile::Peak);
    ui->actionPeak_Performance->setProperty("mixed", false);
    ui->actionReal_World_Performance->setProperty("profile", Global::PerformanceProfile::RealWorld);
    ui->actionReal_World_Performance->setProperty("mixed", false);
    ui->actionDemo->setProperty("profile", Global::PerformanceProfile::Demo);
    ui->actionDemo->setProperty("mixed", false);
    ui->actionDefault_Mix->setProperty("profile", Global::PerformanceProfile::Default);
    ui->actionDefault_Mix->setProperty("mixed", true);
    ui->actionPeak_Performance_Mix->setProperty("profile", Global::PerformanceProfile::Peak);
    ui->actionPeak_Performance_Mix->setProperty("mixed", true);
    ui->actionReal_World_Performance_Mix->setProperty("profile", Global::PerformanceProfile::RealWorld);
    ui->actionReal_World_Performance_Mix->setProperty("mixed", true);

    QActionGroup *profilesGroup = new QActionGroup(this);
    ui->actionDefault->setActionGroup(profilesGroup);
    ui->actionPeak_Performance->setActionGroup(profilesGroup);
    ui->actionReal_World_Performance->setActionGroup(profilesGroup);
    ui->actionDemo->setActionGroup(profilesGroup);
    ui->actionDefault_Mix->setActionGroup(profilesGroup);
    ui->actionPeak_Performance_Mix->setActionGroup(profilesGroup);
    ui->actionReal_World_Performance_Mix->setActionGroup(profilesGroup);
    connect(profilesGroup, SIGNAL(triggered(QAction*)), this, SLOT(profileSelected(QAction*)));

    ui->actionRead_Write_Mix->setProperty("mode", Global::BenchmarkMode::ReadWriteMix);
    ui->actionRead_Mix->setProperty("mode", Global::BenchmarkMode::ReadMix);
    ui->actionWrite_Mix->setProperty("mode", Global::BenchmarkMode::WriteMix);

    QActionGroup *modesGroup = new QActionGroup(this);
    ui->actionRead_Write_Mix->setActionGroup(modesGroup);
    ui->actionRead_Mix->setActionGroup(modesGroup);
    ui->actionWrite_Mix->setActionGroup(modesGroup);
    connect(modesGroup, SIGNAL(triggered(QAction*)), this, SLOT(modeSelected(QAction*)));

    ui->actionTestData_Random->setProperty("data", Global::BenchmarkTestData::Random);
    ui->actionTestData_Zeros->setProperty("data", Global::BenchmarkTestData::Zeros);

    QActionGroup *testDataGroup = new QActionGroup(this);
    ui->actionTestData_Random->setActionGroup(testDataGroup);
    ui->actionTestData_Zeros->setActionGroup(testDataGroup);
    connect(testDataGroup, SIGNAL(triggered(QAction*)), this, SLOT(testDataSelected(QAction*)));

    ui->actionPreset_Standard->setProperty("preset", Global::BenchmarkPreset::Standard);
    ui->actionPreset_NVMe_SSD->setProperty("preset", Global::BenchmarkPreset::NVMe_SSD);

    QActionGroup *presetsGroup = new QActionGroup(this);
    ui->actionPreset_Standard->setActionGroup(presetsGroup);
    ui->actionPreset_NVMe_SSD->setActionGroup(presetsGroup);
    connect(presetsGroup, SIGNAL(triggered(QAction*)), this, SLOT(presetSelected(QAction*)));

    ui->actionTheme_Use_Fusion->setProperty("theme", Global::Theme::UseFusion);
    ui->actionTheme_Stylesheet_Light->setProperty("theme", Global::Theme::StyleSheetLight);
    ui->actionTheme_Stylesheet_Dark->setProperty("theme", Global::Theme::StyleSheetDark);
    ui->actionTheme_Do_not_apply->setProperty("theme", Global::Theme::DoNotApply);

    QActionGroup *themeGroup = new QActionGroup(this);
    ui->actionTheme_Use_Fusion->setActionGroup(themeGroup);
    ui->actionTheme_Stylesheet_Light->setActionGroup(themeGroup);
    ui->actionTheme_Stylesheet_Dark->setActionGroup(themeGroup);
    ui->actionTheme_Do_not_apply->setActionGroup(themeGroup);

    QAction *neonThemeAction = new QAction(tr("Neon Dark"), this);
    neonThemeAction->setCheckable(true);
    neonThemeAction->setProperty("theme", Global::Theme::StyleSheetNeon);
    neonThemeAction->setActionGroup(themeGroup);
    ui->menuTheme->insertAction(ui->actionTheme_Use_Fusion, neonThemeAction);
    connect(themeGroup, SIGNAL(triggered(QAction*)), this, SLOT(themeSelected(QAction*)));

    m_progressBars << ui->readBar_1 << ui->writeBar_1 << ui->mixBar_1
                   << ui->readBar_2 << ui->writeBar_2 << ui->mixBar_2
                   << ui->readBar_3 << ui->writeBar_3 << ui->mixBar_3
                   << ui->readBar_4 << ui->writeBar_4 << ui->mixBar_4
                   << ui->readBar_Demo << ui->writeBar_Demo;

    refreshProgressBars();

    // Load settings
    const AppSettings settings;

    for (QAction *action : { ui->actionDefault, ui->actionPeak_Performance, ui->actionReal_World_Performance, ui->actionDemo,
                             ui->actionDefault_Mix, ui->actionPeak_Performance_Mix, ui->actionReal_World_Performance_Mix }) {
        if (action->property("profile").toInt() == settings.getPerformanceProfile() && action->property("mixed").toBool() == settings.getMixedState()) {
            action->setChecked(true);
            profileSelected(action);
            break;
        }
    }

    int indexMixRatio = settings.getRandomReadPercentage() / 10 - 1;

    for (int i = 1; i <= 9; i++) {
        ui->comboBox_MixRatio->addItem(QStringLiteral("R%1%/W%2%").arg(i * 10).arg((10 - i) * 10));
    }

    ui->comboBox_ComparisonUnit->setCurrentIndex(settings.getComparisonUnit());
    ui->comboBox_MixRatio->setCurrentIndex(indexMixRatio);

    ui->actionTestData_Zeros->setChecked(settings.getBenchmarkTestData() == Global::BenchmarkTestData::Zeros);
    ui->actionTestData_Continuous->setChecked(settings.getContinuousGenerationState());
    ui->actionRead_Mix->setChecked(settings.getBenchmarkMode() == Global::BenchmarkMode::ReadMix);
    ui->actionWrite_Mix->setChecked(settings.getBenchmarkMode() == Global::BenchmarkMode::WriteMix);

    ui->actionUse_O_DIRECT->setChecked(settings.getCacheBypassState());
    ui->actionFlush_Pagecache->setChecked(settings.getFlusingCacheState());
    ui->actionCoW_detection->setChecked(settings.getCoWDetectionState());
    ui->loopsCount->setValue(settings.getLoopsCount());

    ui->actionTheme_Stylesheet_Light->setChecked(settings.getTheme() == Global::Theme::StyleSheetLight);
    ui->actionTheme_Stylesheet_Dark->setChecked(settings.getTheme() ==Global::Theme::StyleSheetDark);
    ui->actionTheme_Do_not_apply->setChecked(settings.getTheme() == Global::Theme::DoNotApply);
    ui->actionTheme_Use_Fusion->setChecked(settings.getTheme() == Global::Theme::UseFusion);
    neonThemeAction->setChecked(settings.getTheme() == Global::Theme::StyleSheetNeon);

    // Lets the Neon style colour read, write and mix bars differently.
    for (QProgressBar *progressBar : m_progressBars) {
        const QString name = progressBar->objectName();
        progressBar->setProperty("kdmKind", name.startsWith(QLatin1String("read")) ? "read"
                                          : name.startsWith(QLatin1String("write")) ? "write" : "mix");
    }

    setupInsightsPanel();

#ifdef Q_OS_WIN
    // Dropping the page cache and Copy-on-Write handling need the Linux helper
    for (QAction *action : { ui->actionFlush_Pagecache, ui->actionCoW_detection }) {
        action->setChecked(false);
        action->setEnabled(false);
        action->setToolTip(tr("Not available on Windows"));
    }
#endif

    updateProgressBarsStyle();

    updatePresetsSelection();
    updateBenchmarkButtonsContent();

    updateFileSizeList();

    updateStoragesList();

    // Set callbacks
    connect(m_benchmark, &Benchmark::runningStateChanged, this, &MainWindow::benchmarkStateChanged);
    connect(m_benchmark, &Benchmark::benchmarkStatusUpdate, this, &MainWindow::benchmarkStatusUpdate);
    connect(m_benchmark, &Benchmark::resultReady, this, &MainWindow::handleResults);
    connect(m_benchmark, &Benchmark::statsReady, this, &MainWindow::handleStats);
    connect(m_benchmark, &Benchmark::driveHealthReady, this, &MainWindow::handleDriveHealth);
    connect(m_benchmark, &Benchmark::failed, this, &MainWindow::benchmarkFailed);
    connect(m_benchmark, &Benchmark::cowCheckRequired, this, &MainWindow::handleCowCheck);
    connect(m_benchmark, &Benchmark::directoryChanged, this, &MainWindow::handleDirectoryChanged);

    QTimer::singleShot(0, [&] {
        if (!m_benchmark->isFIODetected()) {
            QMessageBox::critical(this, "KDiskMark",
                                  QObject::tr("No FIO was found. Please install FIO before using KDiskMark."));
            qApp->quit();
        }
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *)
{
    if (m_benchmark->isRunning()) {
        connect(m_benchmark, &Benchmark::finished, this, [this] () { qApp->exit(); });
        m_benchmark->setRunning(false);
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::LocaleChange: {
        if (const QLocale locale = AppSettings().locale(); locale == AppSettings::defaultLocale())
            AppSettings::applyLocale(locale);
        break;
    }
    case QEvent::LanguageChange: {
        ui->retranslateUi(this);
        updateFileSizeList();
        updateBenchmarkButtonsContent();
        updateLabels();

        QMetaEnum metaEnum = QMetaEnum::fromType<Global::ComparisonUnit>();

        QLocale locale = QLocale();

        for (auto const& progressBar: m_progressBars) {
            if (!progressBar->property("Demo").toBool())
                progressBar->setToolTip(
                    Global::getToolTipTemplate().arg(
                        locale.toString(progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::MBPerSec)).toFloat(), 'f', 3),
                        locale.toString(progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::GBPerSec)).toFloat(), 'f', 3),
                        locale.toString(progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::IOPS)).toFloat(), 'f', 3),
                        locale.toString(progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::Latency)).toFloat(), 'f', 3)));
            updateProgressBar(progressBar);
        }

        for (int i = 0; i < ui->comboBox_Storages->count(); i++) {
            QVariant variant = ui->comboBox_Storages->itemData(i);
            if (variant.canConvert<Global::Storage>()) {
                Global::Storage storage = variant.value<Global::Storage>();

                storage.formatedSize = formatSize(storage.bytesOccupied, storage.bytesTotal);

                ui->comboBox_Storages->setItemText(i, QStringLiteral("%1 %2% (%3)").arg(storage.path)
                                                   .arg(storage.bytesOccupied * 100 / storage.bytesTotal)
                                                   .arg(storage.formatedSize));

                ui->comboBox_Storages->setItemData(i, QVariant::fromValue(storage));
            }
        }

        ui->comboBox_Storages->setItemText(0, tr("Add a directory"));
        resizeComboBoxItemsPopup(ui->comboBox_Storages);

        break;
    }
    default:
        QMainWindow::changeEvent(event);
    }
}

void MainWindow::on_refreshStoragesButton_clicked()
{
    updateStoragesList();
}

void MainWindow::updateStoragesList()
{
    QString temp;

    QVariant variant = ui->comboBox_Storages->currentData();
    if (variant.canConvert<Global::Storage>())
        temp = variant.value<Global::Storage>().path;

    QVector<Global::Storage> permanentStoragesInList;
    for (int i = 0; i < ui->comboBox_Storages->count(); i++) {
        QVariant variant = ui->comboBox_Storages->itemData(i);
        if (variant.canConvert<Global::Storage>()) {
            Global::Storage storage = variant.value<Global::Storage>();
            if (storage.permanentInList)
                permanentStoragesInList.append(storage);
        }
    }

    ui->comboBox_Storages->clear();

    foreach (const QStorageInfo &volume, QStorageInfo::mountedVolumes()) {
        if (volume.isValid() && volume.isReady() && !volume.isReadOnly()) {
#ifdef Q_OS_WIN
            // Windows volumes are drive letters, there is no /dev path to look for
            const bool isStorage = true;
#else
            const bool isStorage = volume.device().indexOf("/dev") != -1;
#endif
            if (isStorage) {
                Global::Storage storage {
                    .path = volume.rootPath(),
                    .bytesTotal = volume.bytesTotal(),
                    .bytesOccupied = volume.bytesTotal() - volume.bytesFree(),
                    .formatedSize = formatSize(storage.bytesOccupied, storage.bytesTotal),
                };
                addItemToStoragesList(storage);
            }
        }
    }

    for (Global::Storage storage : permanentStoragesInList) {
        storage.formatedSize = formatSize(storage.bytesOccupied, storage.bytesTotal);
        addItemToStoragesList(storage);
    }

    if (!temp.isEmpty()) {
        int foundIndex = ui->comboBox_Storages->findText(temp, Qt::MatchContains);
        if (foundIndex != -1) ui->comboBox_Storages->setCurrentIndex(foundIndex);
    }

    ui->comboBox_Storages->insertItem(0, tr("Add a directory"));
    ui->comboBox_Storages->setItemData(0, Qt::AlignCenter, Qt::TextAlignmentRole);
    ui->comboBox_Storages->setItemIcon(0, style()->standardIcon(QStyle::SP_FileDialogNewFolder));

    // Resize items popup
    resizeComboBoxItemsPopup(ui->comboBox_Storages);
}

void MainWindow::addItemToStoragesList(const Global::Storage &storage)
{
    if (ui->comboBox_Storages->findText(storage.path, Qt::MatchContains) != -1)
         return;

    ui->comboBox_Storages->addItem(
                QStringLiteral("%1 %2% (%3)").arg(storage.path)
                .arg(storage.bytesOccupied * 100 / storage.bytesTotal)
                .arg(storage.formatedSize),
                QVariant::fromValue(storage)
                );
}

void MainWindow::updateFileSizeList()
{
    const AppSettings settings;

    int fileSize = settings.getFileSize();

    ui->comboBox_fileSize->clear();

    for (int i = 16; i <= 512; i *= 2) {
        ui->comboBox_fileSize->addItem(QStringLiteral("%1 %2").arg(i).arg(tr("MiB")), i);
    }

    for (int i = 1; i <= 64; i *= 2) {
        ui->comboBox_fileSize->addItem(QStringLiteral("%1 %2").arg(i).arg(tr("GiB")), i * 1024);
    }

    ui->comboBox_fileSize->setCurrentIndex(ui->comboBox_fileSize->findData(fileSize));

    resizeComboBoxItemsPopup(ui->comboBox_fileSize);
}

void MainWindow::on_comboBox_fileSize_currentIndexChanged(int index)
{
    if (index != -1) {
        AppSettings settings;
        settings.setFileSize(ui->comboBox_fileSize->currentData().toInt());
    }
}

void MainWindow::resizeComboBoxItemsPopup(QComboBox *combobox)
{
    int maxWidth = 0;
    QFontMetrics fontMetrics(combobox->font());
    for (int i = 0; i < combobox->count(); i++)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        int width = fontMetrics.horizontalAdvance(combobox->itemText(i));
#else
        int width = fontMetrics.width(combobox->itemText(i));
#endif

        if (width > maxWidth)
            maxWidth = width;
    }

    QAbstractItemView *view = combobox->view();
    if (view->minimumWidth() < maxWidth)
        view->setMinimumWidth(maxWidth + view->autoScrollMargin() + combobox->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
}

void MainWindow::on_actionUse_O_DIRECT_triggered(bool checked)
{
    AppSettings().setCacheBypassState(checked);
}

void MainWindow::on_actionTestData_Continuous_triggered(bool checked) {
    AppSettings().setContinuousGenerationState(checked);
}

void MainWindow::on_actionFlush_Pagecache_triggered(bool checked)
{
    AppSettings().setFlushingCacheState(checked);
}

void MainWindow::on_actionCoW_detection_triggered(bool checked)
{
    AppSettings().setCoWDetectionState(checked);
}

void MainWindow::updateBenchmarkButtonsContent()
{
    const AppSettings settings;

    Global::BenchmarkParams params;

    params = settings.getBenchmarkParams(Global::BenchmarkTest::Test_1, settings.getPerformanceProfile());
    ui->pushButton_Test_1->setText(Global::getBenchmarkButtonText(params));

    switch (settings.getPerformanceProfile())
    {
    case Global::PerformanceProfile::Default:
        ui->pushButton_Test_1->setToolTip(Global::getBenchmarkButtonToolTip(params));

        params = settings.getBenchmarkParams(Global::BenchmarkTest::Test_2);
        ui->pushButton_Test_2->setText(Global::getBenchmarkButtonText(params));
        ui->pushButton_Test_2->setToolTip(Global::getBenchmarkButtonToolTip(params));

        params = settings.getBenchmarkParams(Global::BenchmarkTest::Test_3);
        ui->pushButton_Test_3->setText(Global::getBenchmarkButtonText(params));
        ui->pushButton_Test_3->setToolTip(Global::getBenchmarkButtonToolTip(params));

        params = settings.getBenchmarkParams(Global::BenchmarkTest::Test_4);
        ui->pushButton_Test_4->setText(Global::getBenchmarkButtonText(params));
        ui->pushButton_Test_4->setToolTip(Global::getBenchmarkButtonToolTip(params));
        break;
    case Global::PerformanceProfile::Peak:
    case Global::PerformanceProfile::RealWorld:
        ui->pushButton_Test_1->setToolTip(Global::getBenchmarkButtonToolTip(params, true).arg(tr("MB/s")));

        params = settings.getBenchmarkParams(Global::BenchmarkTest::Test_2, settings.getPerformanceProfile());
        ui->pushButton_Test_2->setText(Global::getBenchmarkButtonText(params));
        ui->pushButton_Test_2->setToolTip(Global::getBenchmarkButtonToolTip(params, true).arg(tr("MB/s")));

        ui->pushButton_Test_3->setText(Global::getBenchmarkButtonText(params, tr("IOPS")));
        ui->pushButton_Test_3->setToolTip(Global::getBenchmarkButtonToolTip(params, true).arg(tr("IOPS")));

        ui->pushButton_Test_4->setText(Global::getBenchmarkButtonText(params, tr("μs")));
        ui->pushButton_Test_4->setToolTip(Global::getBenchmarkButtonToolTip(params, true).arg(tr("μs")));
        break;
    case Global::PerformanceProfile::Demo:
        ui->label_Demo->setText(QStringLiteral("%1 %2 %3, Q=%4, T=%5")
                                .arg(params.Pattern == Global::BenchmarkIOPattern::SEQ ? QStringLiteral("SEQ") : QStringLiteral("RND"))
                                .arg(params.BlockSize >= 1024 ? params.BlockSize / 1024 : params.BlockSize)
                                .arg(params.BlockSize >= 1024 ? tr("MiB") : tr("KiB"))
                                .arg(params.Queues).arg(params.Threads));
        break;
    }
}

void MainWindow::updatePresetsSelection()
{
    const AppSettings settings;

    auto testFunc = [&] (Global::BenchmarkTest test, Global::PerformanceProfile profile, Global::BenchmarkPreset preset) {
        return settings.getBenchmarkParams(test, profile) == settings.defaultBenchmarkParams(test, profile, preset);
    };

    Global::BenchmarkPreset preset = Global::BenchmarkPreset::Standard;
    bool testStandardPreset =
            testFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Default, preset) &&
            testFunc(Global::BenchmarkTest::Test_2, Global::PerformanceProfile::Default, preset) &&
            testFunc(Global::BenchmarkTest::Test_3, Global::PerformanceProfile::Default, preset) &&
            testFunc(Global::BenchmarkTest::Test_4, Global::PerformanceProfile::Default, preset) &&
            testFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Peak, preset) &&
            testFunc(Global::BenchmarkTest::Test_2, Global::PerformanceProfile::Peak, preset) &&
            testFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Demo, preset);

    ui->actionPreset_Standard->setChecked(testStandardPreset);

    if (!testStandardPreset) {
        Global::BenchmarkPreset preset = Global::BenchmarkPreset::NVMe_SSD;
        bool testNVMeSSDPreset =
                testFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Default, preset) &&
                testFunc(Global::BenchmarkTest::Test_2, Global::PerformanceProfile::Default, preset) &&
                testFunc(Global::BenchmarkTest::Test_3, Global::PerformanceProfile::Default, preset) &&
                testFunc(Global::BenchmarkTest::Test_4, Global::PerformanceProfile::Default, preset) &&
                testFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Peak, preset) &&
                testFunc(Global::BenchmarkTest::Test_2, Global::PerformanceProfile::Peak, preset) &&
                testFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Demo, preset);

        ui->actionPreset_NVMe_SSD->setChecked(testNVMeSSDPreset);
    }
}

void MainWindow::refreshProgressBars()
{
    QMetaEnum metaEnum = QMetaEnum::fromType<Global::ComparisonUnit>();

    QLocale locale = QLocale();

    for (auto const& progressBar: m_progressBars) {
        progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::MBPerSec), 0);
        progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::GBPerSec), 0);
        progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::IOPS), 0);
        progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::Latency), 0);
        progressBar->setValue(0);
        progressBar->setFormat(locale.toString(0., 'f', progressBar->property("Demo").toBool() ? 1 : 2));
        if (!progressBar->property("Demo").toBool())
        progressBar->setToolTip(
                    Global::getToolTipTemplate().arg(
                        locale.toString(0., 'f', 3),
                        locale.toString(0., 'f', 3),
                        locale.toString(0., 'f', 3),
                        locale.toString(0., 'f', 3)
                        )
                    );
    }
}

QString MainWindow::formatSize(quint64 available, quint64 total)
{
    QStringList units = { tr("Bytes"), tr("KiB"), tr("MiB"), tr("GiB"), tr("TiB"), tr("PiB") };
    int i;
    double outputAvailable = available;
    double outputTotal = total;
    for (i = 0; i < units.size() - 1; i++) {
        if (outputTotal < 1024) break;
        outputAvailable = outputAvailable / 1024;
        outputTotal = outputTotal / 1024;
    }
    QLocale locale = QLocale();
    return QStringLiteral("%1/%2 %3").arg(locale.toString(outputAvailable, 'f', 2))
            .arg(locale.toString(outputTotal, 'f', 2)).arg(units[i]);
}

void MainWindow::on_comboBox_ComparisonUnit_currentIndexChanged(int index)
{
    AppSettings().setComparisonUnit(Global::ComparisonUnit(index));
    updateLabels();

    for (auto const& progressBar: m_progressBars) {
        updateProgressBar(progressBar);
    }
}

void MainWindow::updateLabels()
{
    ui->label_Read->setText(Global::getComparisonLabelTemplate()
                            .arg(tr("Read"), ui->comboBox_ComparisonUnit->currentText()));

    ui->label_Write->setText(Global::getComparisonLabelTemplate()
                             .arg(tr("Write"), ui->comboBox_ComparisonUnit->currentText()));

    ui->label_Mix->setText(Global::getComparisonLabelTemplate()
                             .arg(tr("Mix"), ui->comboBox_ComparisonUnit->currentText()));

    ui->label_Unit_Read_Demo->setText(ui->comboBox_ComparisonUnit->currentText());
    ui->label_Unit_Write_Demo->setText(ui->comboBox_ComparisonUnit->currentText());
}

QString MainWindow::combineOutputTestResult(const QProgressBar *progressBar, const Global::BenchmarkParams &params)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<Global::ComparisonUnit>();

    return QStringLiteral("%1 %2 %3 (Q=%4, T=%5): %6 MB/s [ %7 IOPS] < %8 us>")
           .arg(params.Pattern == Global::BenchmarkIOPattern::SEQ ? "Sequential" : "Random")
           .arg(QString::number(params.BlockSize >= 1024 ? params.BlockSize / 1024 : params.BlockSize).rightJustified(3, ' '))
           .arg(params.BlockSize >= 1024 ? "MiB" : "KiB")
           .arg(QString::number(params.Queues).rightJustified(3, ' '))
           .arg(QString::number(params.Threads).rightJustified(2, ' '))
           .arg(QString::number(
                    progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::MBPerSec)).toFloat(), 'f', 3)
                .rightJustified(9, ' '))
           .arg(QString::number(
                    progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::IOPS)).toFloat(), 'f', 1)
                .rightJustified(8, ' '))
           .arg(QString::number(
                    progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::Latency)).toFloat(), 'f', 2)
                .rightJustified(8, ' '))
           .rightJustified(Global::getOutputColumnsCount(), ' ');
}

QString MainWindow::getTextBenchmarkResult()
{
    const AppSettings settings;

    QStringList output;

    output << QStringLiteral("KDiskMark (%1): https://github.com/JonMagon/KDiskMark")
              .arg(qApp->applicationVersion())
              .rightJustified(Global::getOutputColumnsCount(), ' ')
           << QStringLiteral("Flexible I/O Tester (%1): https://github.com/axboe/fio")
              .arg(m_benchmark->getFIOVersion())
              .rightJustified(Global::getOutputColumnsCount(), ' ')
           << QStringLiteral("-").repeated(Global::getOutputColumnsCount())
           << "* MB/s = 1,000,000 bytes/s [SATA/600 = 600,000,000 bytes/s]"
           << "* KB = 1000 bytes, KiB = 1024 bytes";

    output << QString()
           << "[Read]";
    if (settings.getPerformanceProfile() == Global::PerformanceProfile::Demo) {
        output << combineOutputTestResult(ui->readBar_Demo, settings.getBenchmarkParams(Global::BenchmarkTest::Test_1, settings.getPerformanceProfile()));
    }
    else {
        output << combineOutputTestResult(ui->readBar_1, settings.getBenchmarkParams(Global::BenchmarkTest::Test_1, settings.getPerformanceProfile()));
        output << combineOutputTestResult(ui->readBar_2, settings.getBenchmarkParams(Global::BenchmarkTest::Test_2, settings.getPerformanceProfile()));
        if (settings.getPerformanceProfile() == Global::PerformanceProfile::Default) {
            output << combineOutputTestResult(ui->readBar_3, settings.getBenchmarkParams(Global::BenchmarkTest::Test_3, settings.getPerformanceProfile()));
            output << combineOutputTestResult(ui->readBar_4, settings.getBenchmarkParams(Global::BenchmarkTest::Test_4, settings.getPerformanceProfile()));
        }
    }

    output << QString()
           << "[Write]";
    if (settings.getPerformanceProfile() == Global::PerformanceProfile::Demo) {
        output << combineOutputTestResult(ui->writeBar_Demo, settings.getBenchmarkParams(Global::BenchmarkTest::Test_1, settings.getPerformanceProfile()));
    }
    else {
        output << combineOutputTestResult(ui->writeBar_1, settings.getBenchmarkParams(Global::BenchmarkTest::Test_1, settings.getPerformanceProfile()));
        output << combineOutputTestResult(ui->writeBar_2, settings.getBenchmarkParams(Global::BenchmarkTest::Test_2, settings.getPerformanceProfile()));
        if (settings.getPerformanceProfile() == Global::PerformanceProfile::Default) {
            output << combineOutputTestResult(ui->writeBar_3, settings.getBenchmarkParams(Global::BenchmarkTest::Test_3, settings.getPerformanceProfile()));
            output << combineOutputTestResult(ui->writeBar_4, settings.getBenchmarkParams(Global::BenchmarkTest::Test_4, settings.getPerformanceProfile()));
        }
    }

    if (settings.getMixedState() && settings.getPerformanceProfile() != Global::PerformanceProfile::Demo) {
         output << QString()
                << QStringLiteral("[Mix] Read %1%/Write %2%")
                   .arg(settings.getRandomReadPercentage())
                   .arg(100 - settings.getRandomReadPercentage())
                << combineOutputTestResult(ui->mixBar_1, settings.getBenchmarkParams(Global::BenchmarkTest::Test_1, settings.getPerformanceProfile()))
                << combineOutputTestResult(ui->mixBar_2, settings.getBenchmarkParams(Global::BenchmarkTest::Test_2, settings.getPerformanceProfile()));
             if (settings.getPerformanceProfile() == Global::PerformanceProfile::Default) {
                 output << combineOutputTestResult(ui->mixBar_3, settings.getBenchmarkParams(Global::BenchmarkTest::Test_3, settings.getPerformanceProfile()));
                 output << combineOutputTestResult(ui->mixBar_4, settings.getBenchmarkParams(Global::BenchmarkTest::Test_4, settings.getPerformanceProfile()));
             }
    }

    QString profiles[] = { "Default", "Peak Performance", "Real World Performance", "Demo" };

    output << QString()
           << QStringLiteral("Profile: %1%2")
              .arg(profiles[(int)settings.getPerformanceProfile()]).arg(settings.getMixedState() ? " [+Mix]" : QString())
           << QStringLiteral("   Test: %1")
              .arg("%1 %2 (x%3) [Measure: %4 %5 / Interval: %6 %7]")
              .arg(settings.getFileSize() >= 1024 ? settings.getFileSize() / 1024 : settings.getFileSize())
              .arg(settings.getFileSize() >= 1024 ? "GiB" : "MiB")
              .arg(settings.getLoopsCount())
              .arg(settings.getMeasuringTime() >= 60 ? settings.getMeasuringTime() / 60 : settings.getMeasuringTime())
              .arg(settings.getMeasuringTime() >= 60 ? "min" : "sec")
              .arg(settings.getIntervalTime() >= 60 ? settings.getIntervalTime() / 60 : settings.getIntervalTime())
              .arg(settings.getIntervalTime() >= 60 ? "min" : "sec")
           << QStringLiteral("   Date: %1 %2")
              .arg(QDate::currentDate().toString("yyyy-MM-dd"))
              .arg(QTime::currentTime().toString("hh:mm:ss"))
           << QStringLiteral("     OS: %1 %2 [%3 %4]").arg(QSysInfo::productType()).arg(QSysInfo::productVersion())
              .arg(QSysInfo::kernelType()).arg(QSysInfo::kernelVersion());

    QString targetInfo = ui->comboBox_Storages->currentText();
    QString modelName = ui->deviceModel->text();
    if (!modelName.isEmpty()) {
        targetInfo += QString(" [%1]").arg(modelName);
    }

    output << QString(" Target: %1")
              .arg(targetInfo);

    return output.join("\n");
}

void MainWindow::on_actionCopy_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(getTextBenchmarkResult() + QLatin1Char('\n') + ReportData::toTextExtras(getBenchmarkReport()));
}

QJsonObject MainWindow::getBenchmarkReport()
{
    const AppSettings settings;
    const Global::PerformanceProfile profile = settings.getPerformanceProfile();
    const bool demo = profile == Global::PerformanceProfile::Demo;
    const QMetaEnum unitEnum = QMetaEnum::fromType<Global::ComparisonUnit>();

    QJsonArray results;

    auto addRow = [&](const QString &section, const QProgressBar *bar, Global::BenchmarkTest test) {
        const Global::BenchmarkParams params = settings.getBenchmarkParams(test, profile);
        const double bandwidth = bar->property(unitEnum.valueToKey(Global::ComparisonUnit::MBPerSec)).toDouble();

        results.append(QJsonObject {
            { QStringLiteral("section"), section },
            { QStringLiteral("pattern"), params.Pattern == Global::BenchmarkIOPattern::SEQ
                                         ? QStringLiteral("sequential") : QStringLiteral("random") },
            { QStringLiteral("block_size_kib"), params.BlockSize },
            { QStringLiteral("queue_depth"), params.Queues },
            { QStringLiteral("threads"), params.Threads },
            { QStringLiteral("bandwidth_mb_s"), bandwidth },
            { QStringLiteral("bandwidth_min_mb_s"), bar->property("BandwidthMin").isValid()
                                                    ? bar->property("BandwidthMin").toDouble() : bandwidth },
            { QStringLiteral("bandwidth_avg_mb_s"), bar->property("BandwidthAvg").isValid()
                                                    ? bar->property("BandwidthAvg").toDouble() : bandwidth },
            { QStringLiteral("bandwidth_max_mb_s"), bar->property("BandwidthMax").isValid()
                                                    ? bar->property("BandwidthMax").toDouble() : bandwidth },
            { QStringLiteral("iops"),
              bar->property(unitEnum.valueToKey(Global::ComparisonUnit::IOPS)).toDouble() },
            { QStringLiteral("latency_us"),
              bar->property(unitEnum.valueToKey(Global::ComparisonUnit::Latency)).toDouble() }
        });
    };

    auto addSection = [&](const QString &section, const QVector<QProgressBar*> &bars) {
        const int count = demo ? 1 : (profile == Global::PerformanceProfile::Default ? 4 : 2);
        for (int i = 0; i < count && i < bars.size(); i++)
            addRow(section, bars[i], static_cast<Global::BenchmarkTest>(i));
    };

    if (demo) {
        addSection(QStringLiteral("read"), { ui->readBar_Demo });
        addSection(QStringLiteral("write"), { ui->writeBar_Demo });
    }
    else {
        addSection(QStringLiteral("read"),  { ui->readBar_1,  ui->readBar_2,  ui->readBar_3,  ui->readBar_4 });
        addSection(QStringLiteral("write"), { ui->writeBar_1, ui->writeBar_2, ui->writeBar_3, ui->writeBar_4 });
        if (settings.getMixedState())
            addSection(QStringLiteral("mix"), { ui->mixBar_1, ui->mixBar_2, ui->mixBar_3, ui->mixBar_4 });
    }

    const QString benchmarkDir = m_benchmark->getBenchmarkFile();
    const QStorageInfo volume(benchmarkDir);

    QJsonObject report {
        { QStringLiteral("date"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) },
        { QStringLiteral("app_version"), qApp->applicationVersion() },
        { QStringLiteral("fio_version"), m_benchmark->getFIOVersion() },
        { QStringLiteral("os"), QStringLiteral("%1 %2 [%3 %4]").arg(QSysInfo::productType(), QSysInfo::productVersion(),
                                                                    QSysInfo::kernelType(), QSysInfo::kernelVersion()) },
        { QStringLiteral("target"), ui->comboBox_Storages->currentText() },
        { QStringLiteral("device_model"), ui->deviceModel->text() },
        { QStringLiteral("profile"), QString::fromLatin1(QMetaEnum::fromType<Global::PerformanceProfile>().valueToKey(profile)) },
        { QStringLiteral("mixed"), settings.getMixedState() },
        { QStringLiteral("mix_read_percent"), settings.getRandomReadPercentage() },
        { QStringLiteral("file_size_mib"), settings.getFileSize() },
        { QStringLiteral("loops"), settings.getLoopsCount() },
        { QStringLiteral("measuring_time_s"), settings.getMeasuringTime() },
        { QStringLiteral("interval_time_s"), settings.getIntervalTime() },
        { QStringLiteral("test_data"), settings.getBenchmarkTestData() == Global::BenchmarkTestData::Zeros
                                       ? QStringLiteral("zeros") : QStringLiteral("random") },
        { QStringLiteral("system"), ReportData::systemInfo() },
        { QStringLiteral("drive"), QJsonObject {
            { QStringLiteral("device"), m_driveHealth.device.isEmpty() ? QString::fromLatin1(volume.device()) : m_driveHealth.device },
            { QStringLiteral("model"), m_driveHealth.model.isEmpty() ? ui->deviceModel->text() : m_driveHealth.model },
            { QStringLiteral("firmware"), m_driveHealth.firmware },
            { QStringLiteral("protocol"), m_driveHealth.protocol },
            { QStringLiteral("filesystem"), QString::fromLatin1(volume.fileSystemType()) },
            { QStringLiteral("capacity_bytes"), static_cast<double>(m_driveHealth.capacityBytes) },
            { QStringLiteral("target_total_bytes"), static_cast<double>(volume.bytesTotal()) },
            { QStringLiteral("target_free_bytes"), static_cast<double>(volume.bytesFree()) }
        } },
        { QStringLiteral("health"), m_driveHealth.toJson() },
        { QStringLiteral("filesystem_health"), currentFilesystemHealth().toJson() },
        { QStringLiteral("summary"), ReportData::summarize(results) },
        { QStringLiteral("results"), results }
    };

    return report;
}

void MainWindow::on_actionSave_triggered()
{
    const QString baseName = QStringLiteral("KDM_%1%2").arg(QDate::currentDate().toString("yyyyMMdd"))
                                                       .arg(QTime::currentTime().toString("hhmmss"));

    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(
                this, QString(), baseName + QStringLiteral(".txt"),
                tr("Text (*.txt);;CSV (*.csv);;JSON (*.json)"), &selectedFilter);

    if (fileName.isEmpty())
        return;

    // The format follows the extension typed by the user, otherwise the chosen filter.
    QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix != QLatin1String("csv") && suffix != QLatin1String("json") && suffix != QLatin1String("txt")) {
        suffix = selectedFilter.startsWith(QLatin1String("CSV")) ? QStringLiteral("csv")
               : selectedFilter.startsWith(QLatin1String("JSON")) ? QStringLiteral("json")
               : QStringLiteral("txt");
        fileName += QLatin1Char('.') + suffix;
    }

    const QJsonObject report = getBenchmarkReport();

    QString error;
    bool ok;
    if (suffix == QLatin1String("csv"))
        ok = ReportData::writeText(fileName, ReportData::toCsv(report), &error);
    else if (suffix == QLatin1String("json"))
        ok = ReportData::writeJson(fileName, report, &error);
    else
        ok = ReportData::writeText(fileName, getTextBenchmarkResult() + QLatin1Char('\n') + ReportData::toTextExtras(report), &error);

    if (!ok) {
        QMessageBox::warning(this, QString(), tr("Failed to save the report: %1").arg(error));
        return;
    }

    // Every saved report is also kept in the JSON history for later exporting.
    if (!ReportData::appendToHistory(report, &error)) {
        QMessageBox::warning(this, QString(),
                             tr("The report was saved, but could not be added to the history (%1): %2")
                             .arg(ReportData::historyPath(), error));
    }
}

void MainWindow::exportHistory()
{
    QString error;
    const QJsonArray history = ReportData::loadHistory(&error);

    if (!error.isEmpty()) {
        QMessageBox::warning(this, QString(), tr("Cannot read the history: %1").arg(error));
        return;
    }

    if (history.isEmpty()) {
        QMessageBox::information(this, QString(), tr("There are no saved reports in the history yet."));
        return;
    }

    const QString fileName = QFileDialog::getSaveFileName(
                this, QString(), QStringLiteral("KDM_history.csv"), tr("CSV (*.csv)"));

    if (fileName.isEmpty())
        return;

    if (!ReportData::writeText(fileName, ReportData::toCsv(history), &error))
        QMessageBox::warning(this, QString(), tr("Failed to export the history: %1").arg(error));
}

void MainWindow::on_loopsCount_valueChanged(int arg1)
{
    AppSettings settings;
    settings.setLoopsCount(arg1);
}

void MainWindow::on_comboBox_MixRatio_currentIndexChanged(int index)
{
    AppSettings settings;
    settings.setRandomReadPercentage((index + 1) * 10.f);
}

void MainWindow::on_comboBox_Storages_currentIndexChanged(int index)
{
    // It is expected that at index 0 without item data there will be a field to add a directory
    if (index == 0 && ui->comboBox_Storages->itemData(index).isNull()) {
        QString dir = QFileDialog::getExistingDirectory(this, QString(), QDir::homePath(),
                                                        QFileDialog::ShowDirsOnly |
                                                        QFileDialog::DontResolveSymlinks);
        if (!dir.isNull()) {
            int foundIndex = ui->comboBox_Storages->findText(dir, Qt::MatchContains);

            if (foundIndex == -1) {
                QStorageInfo volume(dir);

                Global::Storage storage {
                    .path = dir,
                    .bytesTotal = volume.bytesTotal(),
                    .bytesOccupied = volume.bytesTotal() - volume.bytesFree(),
                    .formatedSize = formatSize(storage.bytesOccupied, storage.bytesTotal),
                    .permanentInList = true
                };

                addItemToStoragesList(storage);
                resizeComboBoxItemsPopup(ui->comboBox_Storages);

                ui->comboBox_Storages->setCurrentIndex(ui->comboBox_Storages->count() - 1);
            }
            else {
                ui->comboBox_Storages->setCurrentIndex(foundIndex);
            }

            return;
        }

        ui->comboBox_Storages->setCurrentIndex(1);
    }
    else {
        QVariant variant = ui->comboBox_Storages->itemData(index);
        if (variant.canConvert<Global::Storage>()) {
            Global::Storage volumeInfo = variant.value<Global::Storage>();
            m_benchmark->setDir(volumeInfo.path);
            ui->deviceModel->setText(DiskDriveInfo::Instance().getModelName(QStorageInfo(volumeInfo.path).device()));
            ui->extraIcon->setVisible(DiskDriveInfo::Instance().isEncrypted(QStorageInfo(volumeInfo.path).device()));

            // The drive health read for the previous target does not apply to this one
            m_driveHealth = DriveHealth();
            updateInsightsPanel();
        }
    }
}

void MainWindow::localeSelected(QAction* act)
{
    AppSettings settings;

    if (!act->data().canConvert<QLocale>()) return;

    settings.setLocale(act->data().toLocale());
}

void MainWindow::profileSelected(QAction* act)
{
    AppSettings settings;

    bool isMixed = act->property("mixed").toBool();

    settings.setMixedState(isMixed);

    ui->mixWidget->setVisible(isMixed);
    ui->comboBox_MixRatio->setVisible(isMixed);

    switch (act->property("profile").toInt())
    {
    case Global::PerformanceProfile::Default:
        m_windowTitle = "KDiskMark";
        ui->comboBox_ComparisonUnit->setVisible(true);
        break;
    case Global::PerformanceProfile::Peak:
        m_windowTitle = "KDiskMark <PEAK>";
        ui->comboBox_ComparisonUnit->setCurrentIndex(0);
        ui->comboBox_ComparisonUnit->setVisible(false);
        break;
    case Global::PerformanceProfile::RealWorld:
        m_windowTitle = "KDiskMark <REAL>";
        ui->comboBox_ComparisonUnit->setCurrentIndex(0);
        ui->comboBox_ComparisonUnit->setVisible(false);
        break;
    case Global::PerformanceProfile::Demo:
        m_windowTitle = "KDiskMark <DEMO>";
        ui->comboBox_ComparisonUnit->setVisible(true);
        break;
    }

    setWindowTitle(m_windowTitle);

    settings.setPerformanceProfile((Global::PerformanceProfile)act->property("profile").toInt());

    ui->stackedWidget->setCurrentIndex(settings.getPerformanceProfile() == Global::PerformanceProfile::Demo ? 1 : 0);

    int right = (isMixed ? ui->mixWidget->geometry().right() : ui->writeWidget->geometry().right()) + ui->stackedWidget->geometry().x();

    ui->targetLayoutWidget->resize(right - ui->targetLayoutWidget->geometry().left(),
                                   ui->targetLayoutWidget->geometry().height());
    ui->commentLayoutWidget->resize(right - ui->commentLayoutWidget->geometry().left(),
                                    ui->commentLayoutWidget->geometry().height());

    setFixedWidth(ui->commentLayoutWidget->geometry().width() + 2 * ui->commentLayoutWidget->geometry().left());

    refreshProgressBars();
    updateBenchmarkButtonsContent();
}

void MainWindow::modeSelected(QAction* act)
{
    AppSettings().setBenchmarkMode((Global::BenchmarkMode)act->property("mode").toInt());
}

void MainWindow::testDataSelected(QAction* act)
{
    AppSettings().setBenchmarkTestData((Global::BenchmarkTestData)act->property("data").toInt());
}

void MainWindow::presetSelected(QAction* act)
{
    AppSettings settings;

    Global::BenchmarkPreset preset = (Global::BenchmarkPreset)act->property("preset").toInt();

    auto updateFunc = [&] (Global::BenchmarkTest test, Global::PerformanceProfile profile) {
        settings.setBenchmarkParams(test, profile, settings.defaultBenchmarkParams(test, profile, preset));
    };

    updateFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Default);
    updateFunc(Global::BenchmarkTest::Test_2, Global::PerformanceProfile::Default);
    updateFunc(Global::BenchmarkTest::Test_3, Global::PerformanceProfile::Default);
    updateFunc(Global::BenchmarkTest::Test_4, Global::PerformanceProfile::Default);

    updateFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Peak);
    updateFunc(Global::BenchmarkTest::Test_2, Global::PerformanceProfile::Peak);

    updateFunc(Global::BenchmarkTest::Test_1, Global::PerformanceProfile::Demo);

    updateBenchmarkButtonsContent();
}


void MainWindow::updateProgressBarsStyle()
{
    QStyle *progressBarStyleFusion = QStyleFactory::create(QStringLiteral("Fusion"));
    QStyle *progressBarStyleDefault = QStyleFactory::create(QStringLiteral());

    Global::Theme theme = AppSettings().getTheme();

    for (auto const& progressBar: m_progressBars) {
        switch (theme) {
        case Global::Theme::UseFusion:
            progressBar->setStyle(progressBarStyleFusion);
            progressBar->setStyleSheet(QStringLiteral());
            break;
        case Global::Theme::StyleSheetLight:
            progressBar->setStyle(progressBarStyleDefault);
            progressBar->setStyleSheet(QStringLiteral("QProgressBar{color:black;text-align:center}QProgressBar::chunk{background:rgba(0,0,0,0)}"));
            break;
        case Global::Theme::StyleSheetDark:
            progressBar->setStyle(progressBarStyleDefault);
            progressBar->setStyleSheet(QStringLiteral("QProgressBar{color:white;text-align:center}QProgressBar::chunk{background:rgba(0,0,0,0)}"));
            break;
        case Global::Theme::DoNotApply:
            progressBar->setStyle(progressBarStyleDefault);
            progressBar->setStyleSheet(QStringLiteral());
            break;
        case Global::Theme::StyleSheetNeon:
            // Styled by the window style sheet, see applyThemeStyleSheet()
            progressBar->setStyle(progressBarStyleDefault);
            progressBar->setStyleSheet(QStringLiteral());
            break;
        }
    }

    applyThemeStyleSheet();
}

void MainWindow::applyThemeStyleSheet()
{
    if (AppSettings().getTheme() != Global::Theme::StyleSheetNeon) {
        setStyleSheet(QString());
        return;
    }

    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#centralwidget, QWidget#page, QWidget#page_2,
        QWidget#readWidget, QWidget#writeWidget, QWidget#mixWidget { background: #0a0e1a; }
        QWidget { color: #d6f6ff; }
        QLabel { color: #7fd8f0; background: transparent; }
        QMenuBar { background: #0a0e1a; color: #d6f6ff; }
        QMenuBar::item:selected { background: #15324d; }
        QMenu { background: #0f1626; color: #d6f6ff; border: 1px solid #1f4a6e; }
        QMenu::item:selected { background: #0f5a80; }
        QMenu::separator { height: 1px; background: #1f3a5a; }
        QPushButton { background: #0f1830; border: 1px solid #1d6f93; border-radius: 9px;
                      color: #9be9ff; font-weight: bold; padding: 4px; }
        QPushButton:hover { border: 1px solid #00e5ff; background: #12254a; color: #ffffff; }
        QPushButton:pressed { background: #08405c; }
        QPushButton:disabled { border: 1px solid #1a2a40; color: #3d5a73; }
        QComboBox, QSpinBox, QLineEdit { background: #0f1830; border: 1px solid #1f3f63;
                      border-radius: 7px; color: #d6f6ff; padding: 2px 6px; selection-background-color: #0f5a80; }
        QComboBox:hover, QSpinBox:hover { border: 1px solid #00b4d8; }
        QComboBox QAbstractItemView { background: #0f1626; color: #d6f6ff;
                      selection-background-color: #0f5a80; border: 1px solid #1f4a6e; }
        QProgressBar { background: #0f1626; border: 1px solid #1f3f63; border-radius: 9px;
                       color: #ffffff; text-align: center; }
        QProgressBar::chunk { border-radius: 8px; }
        QProgressBar[kdmKind="read"]::chunk  { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #0072ff, stop:1 #00f0d0); }
        QProgressBar[kdmKind="write"]::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #b400ff, stop:1 #ff3d9a); }
        QProgressBar[kdmKind="mix"]::chunk   { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #ff8a00, stop:1 #ffd400); }
        QToolTip { background: #0f1626; color: #d6f6ff; border: 1px solid #00b4d8; }
        QFrame#insightsPanel { background: #0d1424; border: 1px solid #1f4a6e; border-radius: 12px; }
        QLabel#healthTitle { color: #00e5ff; font-weight: bold; letter-spacing: 1px; }
        QLabel#scoreValue { color: #00f0d0; font-size: 30px; font-weight: bold; }
        QLabel#scoreRating { color: #ff3d9a; font-weight: bold; }
    )"));
}

void MainWindow::themeSelected(QAction* act)
{
    AppSettings().setTheme((Global::Theme)act->property("theme").toInt());

    updateProgressBarsStyle();
}


void MainWindow::benchmarkStateChanged(bool state)
{
    if (state) {
        ui->menubar->setEnabled(false);
        ui->loopsCount->setEnabled(false);
        ui->comboBox_fileSize->setEnabled(false);
        ui->comboBox_Storages->setEnabled(false);
        ui->refreshStoragesButton->setEnabled(false);
        ui->comboBox_ComparisonUnit->setEnabled(false);
        ui->comboBox_MixRatio->setEnabled(false);
        ui->pushButton_All->setText(tr("Stop"));
        ui->pushButton_Test_1->setText(tr("Stop"));
        ui->pushButton_Test_2->setText(tr("Stop"));
        ui->pushButton_Test_3->setText(tr("Stop"));
        ui->pushButton_Test_4->setText(tr("Stop"));
    }
    else {
        setWindowTitle(m_windowTitle);
        ui->pushButton_All->setEnabled(true);
        ui->pushButton_Test_1->setEnabled(true);
        ui->pushButton_Test_2->setEnabled(true);
        ui->pushButton_Test_3->setEnabled(true);
        ui->pushButton_Test_4->setEnabled(true);
        ui->menubar->setEnabled(true);
        ui->loopsCount->setEnabled(true);
        ui->comboBox_fileSize->setEnabled(true);
        ui->comboBox_Storages->setEnabled(true);
        ui->refreshStoragesButton->setEnabled(true);
        ui->comboBox_ComparisonUnit->setEnabled(true);
        ui->comboBox_MixRatio->setEnabled(true);
        ui->pushButton_All->setEnabled(true);
        ui->pushButton_Test_1->setEnabled(true);
        ui->pushButton_Test_2->setEnabled(true);
        ui->pushButton_Test_3->setEnabled(true);
        ui->pushButton_Test_4->setEnabled(true);
        ui->pushButton_All->setText(tr("All"));
        updateBenchmarkButtonsContent();
    }
}

void MainWindow::on_actionAbout_triggered()
{
    About about(m_benchmark->getFIOVersion());
    about.setFixedSize(about.size());
    about.exec();
}

void MainWindow::on_actionQueues_Threads_triggered()
{
    Settings settings;
    settings.setFixedSize(settings.size());
    settings.exec();

    updatePresetsSelection();
    updateBenchmarkButtonsContent();
}

void MainWindow::defineBenchmark(std::function<void()> bodyFunc)
{
    AppSettings settings;

    if (m_benchmark->isRunning()) {
        benchmarkStatusUpdate(tr("Stopping..."));
        ui->pushButton_All->setEnabled(false);
        ui->pushButton_Test_1->setEnabled(false);
        ui->pushButton_Test_2->setEnabled(false);
        ui->pushButton_Test_3->setEnabled(false);
        ui->pushButton_Test_4->setEnabled(false);
        m_benchmark->setRunning(false);
    }
    else {
        if (m_benchmark->getBenchmarkFile().isNull()) {
            QMessageBox::critical(this, tr("Not available"), tr("Directory is not specified."));
        }
        else {
            bodyFunc();
        }
    }
}

void MainWindow::benchmarkFailed(const QString &error)
{
    QMessageBox::critical(this, tr("Benchmark Failed"), error);
}

void MainWindow::benchmarkStatusUpdate(const QString &name)
{
    setWindowTitle(QStringLiteral("%1 - %2").arg(m_windowTitle, name));
}

void MainWindow::handleResults(QObject *target, const Benchmark::PerformanceResult &result)
{
    QProgressBar *progressBar = qobject_cast<QProgressBar*>(target);
    if (!progressBar) return;

    QMetaEnum metaEnum = QMetaEnum::fromType<Global::ComparisonUnit>();

    progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::MBPerSec), result.Bandwidth);
    progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::GBPerSec), result.Bandwidth / 1000);
    progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::IOPS), result.IOPS);
    progressBar->setProperty(metaEnum.valueToKey(Global::ComparisonUnit::Latency), result.Latency);

    if (!progressBar->property("Demo").toBool()) {
        QLocale locale = QLocale();

        progressBar->setToolTip(
                    Global::getToolTipTemplate().arg(
                        locale.toString(result.Bandwidth, 'f', 3),
                        locale.toString(result.Bandwidth / 1000, 'f', 3),
                        locale.toString(result.IOPS, 'f', 3),
                        locale.toString(result.Latency, 'f', 3)
                        )
                    );
    }

    updateProgressBar(progressBar);
    updateInsightsPanel();
}

void MainWindow::handleStats(QObject *target, float minimum, float average, float maximum, int samples)
{
    Q_UNUSED(samples)

    QProgressBar *progressBar = qobject_cast<QProgressBar*>(target);
    if (!progressBar) return;

    progressBar->setProperty("BandwidthMin", minimum);
    progressBar->setProperty("BandwidthAvg", average);
    progressBar->setProperty("BandwidthMax", maximum);
}

void MainWindow::handleDriveHealth(const DriveHealth &health)
{
    m_driveHealth = health;
    updateInsightsPanel();
}

void MainWindow::setupInsightsPanel()
{
    QFrame *panel = new QFrame(ui->centralwidget);
    panel->setObjectName(QStringLiteral("insightsPanel"));
    panel->setGeometry(10, 400, 551, 132);

    // One column per figure: the ring with what it measures underneath.
    auto column = [panel](HealthGauge *&gauge, const QString &title, const QString &toolTip) {
        gauge = new HealthGauge(panel);
        gauge->setFixedSize(72, 72);
        gauge->setToolTip(toolTip);

        QLabel *caption = new QLabel(title, panel);
        caption->setObjectName(QStringLiteral("healthTitle"));
        caption->setAlignment(Qt::AlignCenter);
        caption->setToolTip(toolTip);

        QVBoxLayout *layout = new QVBoxLayout;
        layout->setSpacing(2);
        layout->addWidget(gauge, 0, Qt::AlignHCenter);
        layout->addWidget(caption);
        return layout;
    };

    QVBoxLayout *driveColumn = column(m_healthGauge, tr("DRIVE HEALTH"),
        tr("Wear level reported by the drive itself (SMART). 100 % is a drive with no wear."));
    QVBoxLayout *fsColumn = column(m_fsGauge, tr("FILESYSTEM"),
        tr("Health of the filesystem from the kernel's error counters. 100 % means no errors recorded."));
    QVBoxLayout *perfColumn = column(m_perfGauge, tr("PERFORMANCE"),
        tr("Your results compared with a mainstream NVMe SSD. 100 % is that reference, higher is faster."));
    m_perfGauge->setPerformanceMode(true);

    m_scoreValue = new QLabel(QStringLiteral("-"), panel);
    m_scoreValue->setObjectName(QStringLiteral("scoreValue"));
    m_scoreValue->setAlignment(Qt::AlignCenter);

    QLabel *scoreCaption = new QLabel(tr("SCORE"), panel);
    scoreCaption->setAlignment(Qt::AlignCenter);

    m_scoreRating = new QLabel(panel);
    m_scoreRating->setObjectName(QStringLiteral("scoreRating"));
    m_scoreRating->setAlignment(Qt::AlignCenter);

    QVBoxLayout *scoreLayout = new QVBoxLayout;
    scoreLayout->setSpacing(0);
    scoreLayout->addStretch();
    scoreLayout->addWidget(scoreCaption);
    scoreLayout->addWidget(m_scoreValue);
    scoreLayout->addWidget(m_scoreRating);
    scoreLayout->addStretch();

    QHBoxLayout *figures = new QHBoxLayout;
    figures->setSpacing(10);
    figures->addLayout(driveColumn);
    figures->addLayout(fsColumn);
    figures->addLayout(perfColumn);
    figures->addStretch();
    figures->addLayout(scoreLayout);

    // Details of the drive health reading, or why it is not available
    m_healthDetails = new QLabel(panel);
    m_healthDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_healthDetails->setWordWrap(true);

    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 8, 16, 8);
    layout->setSpacing(4);
    layout->addLayout(figures);
    layout->addWidget(m_healthDetails);

    updateInsightsPanel();
}

FilesystemHealth MainWindow::currentFilesystemHealth() const
{
    const QStorageInfo volume(m_benchmark->getBenchmarkFile());
    return FilesystemHealth::read(QString::fromLatin1(volume.device()), QString::fromLatin1(volume.fileSystemType()));
}

void MainWindow::updateInsightsPanel()
{
    if (!m_healthGauge) return;

    const DriveHealth &h = m_driveHealth;

    if (!h.available) {
        m_healthGauge->setHealth(-1);
        m_healthDetails->setText(h.message.isEmpty()
                                 ? tr("Drive health: run a benchmark to read the drive's SMART data.")
                                 : tr("Drive health: %1").arg(h.message));
    }
    else {
        QStringList details;

        if (!h.model.isEmpty())
            details << h.model;

        if (h.smartPassed >= 0)
            details << tr("SMART: %1").arg(h.smartPassed == 1 ? tr("PASSED") : tr("FAILED"));
        if (h.hasTemperature())
            details << tr("%1 °C").arg(h.temperatureC);
        if (h.powerOnHours >= 0)
            details << tr("%1 h powered on").arg(h.powerOnHours);
        if (!h.hasHealthPercent())
            details << tr("This drive does not report a wear level.");

        m_healthGauge->setHealth(h.healthPercent);
        m_healthDetails->setText(details.join(QStringLiteral("  ·  ")));
    }

    const FilesystemHealth fs = currentFilesystemHealth();
    m_fsGauge->setHealth(fs.available ? fs.healthPercent : -1);
    m_fsGauge->setToolTip(fs.available
                          ? tr("%1: %2 recorded errors").arg(fs.filesystem).arg(fs.errorCount)
                          : fs.message);

    // Score and performance from the bars that are currently filled in
    const QJsonObject summary = ReportData::summarize(getBenchmarkReport().value(QStringLiteral("results")).toArray());
    const int score = summary.value(QStringLiteral("score")).toInt();
    m_scoreValue->setText(score > 0 ? QString::number(score) : QStringLiteral("-"));
    m_scoreRating->setText(score > 0 ? summary.value(QStringLiteral("rating")).toString() : QString());

    const QJsonValue performance = summary.value(QStringLiteral("performance_percent"));
    m_perfGauge->setHealth(performance.isDouble() ? performance.toInt() : -1);
}

void MainWindow::updateProgressBar(QProgressBar *progressBar)
{
    const AppSettings settings;

    QMetaEnum metaEnum = QMetaEnum::fromType<Global::ComparisonUnit>();

    float score = progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::MBPerSec)).toFloat();

    float value;

    Global::ComparisonUnit comparisonUnit = Global::ComparisonUnit::MBPerSec;

    switch (settings.getPerformanceProfile()) {
    case Global::PerformanceProfile::Peak:
    case Global::PerformanceProfile::RealWorld:
        if (progressBar == ui->readBar_3 || progressBar == ui->writeBar_3 || progressBar == ui->mixBar_3) {
            comparisonUnit = Global::ComparisonUnit::IOPS;
        }
        else if (progressBar == ui->readBar_4 || progressBar == ui->writeBar_4 || progressBar == ui->mixBar_4) {
            comparisonUnit = Global::ComparisonUnit::Latency;
        }
        break;
    default:
        comparisonUnit = settings.getComparisonUnit();
        break;
    }

    QLocale locale = QLocale();

    switch (comparisonUnit) {
    case Global::ComparisonUnit::MBPerSec:
        progressBar->setFormat(score >= 1000000.0 ? locale.toString((int)score) : locale.toString(score, 'f', progressBar->property("Demo").toBool() ? 1 : 2));
        break;
    case Global::ComparisonUnit::GBPerSec:
        value = progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::GBPerSec)).toFloat();
        progressBar->setFormat(locale.toString(value, 'f', progressBar->property("Demo").toBool() ? 1 : 3));
        break;
    case Global::ComparisonUnit::IOPS:
        value = progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::IOPS)).toFloat();
        progressBar->setFormat(value >= 1000000.0 ? locale.toString((int)value) : locale.toString(value, 'f', progressBar->property("Demo").toBool() ? 0 : 2));
        break;
    case Global::ComparisonUnit::Latency:
        value = progressBar->property(metaEnum.valueToKey(Global::ComparisonUnit::Latency)).toFloat();
        progressBar->setFormat(value >= 1000000.0 ? locale.toString((int)value) : locale.toString(value, 'f', progressBar->property("Demo").toBool() ? 1 : 2));
        break;
    }

    if (!progressBar->property("Demo").toBool()) {
        if (comparisonUnit == Global::ComparisonUnit::Latency) {
            progressBar->setValue(value <= 0.0000000001 ? 0 : 100 - 16.666666666666 * log10(value));
        }
        else {
            progressBar->setValue(score <= 0.1 ? 0 : 16.666666666666 * log10(score * 10));
        }
    }
}

void MainWindow::handleCowCheck()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("KDiskMark");
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(tr("Copy-on-Write (CoW) is enabled on the selected directory."));
    msgBox.setInformativeText(tr("This may affect performance results. Would you like to create a new subdirectory with CoW disabled?"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    bool createNew = (msgBox.exec() == QMessageBox::Yes);
    emit m_benchmark->createNoCowDirectoryResponse(createNew);
}

void MainWindow::handleDirectoryChanged(const QString &newDir)
{
    int existingIndex = ui->comboBox_Storages->findText(newDir, Qt::MatchContains);
    if (existingIndex != -1) {
        ui->comboBox_Storages->setCurrentIndex(existingIndex);
        return;
    }

    Global::Storage storage {
        .path = newDir,
        .bytesTotal = QStorageInfo(newDir).bytesTotal(),
        .bytesOccupied = QStorageInfo(newDir).bytesTotal() - QStorageInfo(newDir).bytesFree(),
        .formatedSize = formatSize(storage.bytesOccupied, storage.bytesTotal),
        .permanentInList = true
    };

    addItemToStoragesList(storage);
    resizeComboBoxItemsPopup(ui->comboBox_Storages);

    int index = ui->comboBox_Storages->findText(newDir, Qt::MatchContains);
    if (index != -1) {
        ui->comboBox_Storages->setCurrentIndex(index);
    }
}

bool MainWindow::runCombinedRandomTest()
{
    const AppSettings settings;

    if (settings.getPerformanceProfile() == Global::PerformanceProfile::Peak || settings.getPerformanceProfile() == Global::PerformanceProfile::RealWorld) {
        QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> set {
            { { Global::Test_2, Global::Read  }, { ui->readBar_2,  ui->readBar_3,  ui->readBar_4  } },
            { { Global::Test_2, Global::Write }, { ui->writeBar_2, ui->writeBar_3, ui->writeBar_4 } }
        };

        if (settings.getMixedState()) {
            set << QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>
            { { Global::Test_2, Global::Mix   }, {  ui->mixBar_2,  ui->mixBar_3,   ui->mixBar_4   } };
        }

        m_benchmark->runBenchmark(set);

        return true;
    }

    return false;
}

void MainWindow::on_pushButton_Test_1_clicked()
{
    defineBenchmark([&]() {
        QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> set {
            { { Global::Test_1, Global::Read  }, { ui->readBar_1  } },
            { { Global::Test_1, Global::Write }, { ui->writeBar_1 } }
        };

        if (AppSettings().getMixedState()) {
            set << QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>
            { { Global::Test_1, Global::Mix   }, { ui->mixBar_1   } };
        }

        m_benchmark->runBenchmark(set);
    });
}

void MainWindow::on_pushButton_Test_2_clicked()
{
    defineBenchmark([&]() {
        if (runCombinedRandomTest()) return;

        QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> set {
            { { Global::Test_2, Global::Read  }, { ui->readBar_2  } },
            { { Global::Test_2, Global::Write }, { ui->writeBar_2 } }
        };

        if (AppSettings().getMixedState()) {
            set << QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>
            { { Global::Test_2, Global::Mix   }, { ui->mixBar_2   } };
        }

        m_benchmark->runBenchmark(set);
    });
}

void MainWindow::on_pushButton_Test_3_clicked()
{
    defineBenchmark([&]() {
        if (runCombinedRandomTest()) return;

        QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> set {
            { { Global::Test_3, Global::Read  }, { ui->readBar_3  } },
            { { Global::Test_3, Global::Write }, { ui->writeBar_3 } }
        };

        if (AppSettings().getMixedState()) {
            set << QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>
            { { Global::Test_3, Global::Mix   }, { ui->mixBar_3   } };
        }

        m_benchmark->runBenchmark(set);
    });
}

void MainWindow::on_pushButton_Test_4_clicked()
{
    defineBenchmark([&]() {
        if (runCombinedRandomTest()) return;

        QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> set {
            { { Global::Test_4, Global::Read  }, { ui->readBar_4  } },
            { { Global::Test_4, Global::Write }, { ui->writeBar_4 } }
        };

        if (AppSettings().getMixedState()) {
            set << QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>
            { { Global::Test_4, Global::Mix   }, { ui->mixBar_4   } };
        }

        m_benchmark->runBenchmark(set);
    });
}

void MainWindow::on_pushButton_All_clicked()
{
    defineBenchmark([&]() {
        const AppSettings settings;

        QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> set;

        if (settings.getPerformanceProfile() == Global::PerformanceProfile::Default) {
            set << QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> {
                { { Global::Test_1, Global::Read  }, { ui->readBar_1  } },
                { { Global::Test_2, Global::Read  }, { ui->readBar_2  } },
                { { Global::Test_3, Global::Read  }, { ui->readBar_3  } },
                { { Global::Test_4, Global::Read  }, { ui->readBar_4  } },
                { { Global::Test_1, Global::Write }, { ui->writeBar_1 } },
                { { Global::Test_2, Global::Write }, { ui->writeBar_2 } },
                { { Global::Test_3, Global::Write }, { ui->writeBar_3 } },
                { { Global::Test_4, Global::Write }, { ui->writeBar_4 } }
            };

            if (settings.getMixedState()) {
                set << QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> {
                { { Global::Test_1, Global::Mix   }, { ui->mixBar_1   } },
                { { Global::Test_2, Global::Mix   }, { ui->mixBar_2   } },
                { { Global::Test_3, Global::Mix   }, { ui->mixBar_3   } },
                { { Global::Test_4, Global::Mix   }, { ui->mixBar_4   } }
            };
            }
        }
        else if (settings.getPerformanceProfile() == Global::PerformanceProfile::Demo) {
            set << QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> {
                { { Global::Test_1, Global::Read  }, { ui->readBar_Demo  } },
                { { Global::Test_1, Global::Write }, { ui->writeBar_Demo } }
            };
        }
        else {
            set << QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> {
                { { Global::Test_1, Global::Read  }, { ui->readBar_1  } },
                { { Global::Test_2, Global::Read  }, { ui->readBar_2,  ui->readBar_3,  ui->readBar_4  } },
                { { Global::Test_1, Global::Write }, { ui->writeBar_1 } },
                { { Global::Test_2, Global::Write }, { ui->writeBar_2, ui->writeBar_3, ui->writeBar_4 } }
            };

            if (settings.getMixedState()) {
                set << QList<QPair<QPair<Global::BenchmarkTest, Global::BenchmarkIOReadWrite>, QVector<QObject*>>> {
                { { Global::Test_1, Global::Mix   }, { ui->mixBar_1   } },
                { { Global::Test_2, Global::Mix   }, { ui->mixBar_2,   ui->mixBar_3,   ui->mixBar_4   } }
            };
            }
        }

        m_benchmark->runBenchmark(set);
    });
}
