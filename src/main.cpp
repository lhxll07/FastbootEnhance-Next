#include "payload.h"
#include "platform_paths.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QMimeData>
#include <QSet>

#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

#include <algorithm>
#include <functional>
#include <memory>

struct FastbootState {
    QString serial;
    QString product;
    QString currentSlot;
    QString snapshotStatus;
    bool secure = false;
    bool secureKnown = false;
    bool fastbootd = false;
    bool fastbootdKnown = false;
    QMap<QString, QString> vars;
    QMap<QString, quint64> partitionSizes;
    QMap<QString, bool> logicalPartitions;
};

struct PayloadLoadResult {
    std::shared_ptr<PayloadReader> reader;
    QString error;
};

struct PayloadExtractResult {
    bool ok = false;
    QString error;
    int extractedCount = 0;
};

struct PayloadFlashPrepareResult {
    bool ok = false;
    QString error;
    QStringList names;
    QStringList paths;
    QStringList skippedNames;
    std::shared_ptr<QTemporaryDir> tempDirectory;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    QWidget *buildFastbootPage();
    QWidget *buildPayloadPage();
    QWidget *buildAboutPage();

    QString resolveTool(const QString &name) const;
    QStringList deviceArgs(const QStringList &args) const;
    void runFastboot(const QStringList &args,
                     const std::function<void(bool, const QString &)> &done,
                     bool showProgress = true);
    void appendLog(const QString &line);
    void setBusy(bool busy, bool payload = false);
    void updateActionAvailability();

    void refreshDevices();
    void loadDeviceVars();
    void parseFastbootVars(const QString &output);
    void refreshFastbootTables();
    void refreshPartitionTable();
    QString selectedPartition() const;
    bool hasDevice() const;
    bool confirmVabState() const;
    bool confirmAction(const QString &title, const QString &text) const;

    void flashPartition();
    void erasePartition();
    void deleteLogicalPartition();
    void createLogicalPartition();
    void resizeLogicalPartition();
    void rebootToBootloader();
    void switchSlot();
    void rebootSystem();
    void rebootRecovery();
    void cancelUpdate();
    void finishUpdate();
    void flashPayload();
    void flashNextPayloadImage();

    void openPayloadDialog();
    void loadPayload(const QString &path);
    void fillPayloadUi();
    void extractPayload();

    static void setTableRow(QTableWidget *table, int row, const QStringList &values);
    static void clearTable(QTableWidget *table, int columns);
    static QString valueFor(const QMap<QString, QString> &vars, const QString &key);
    static bool isSafeName(const QString &value);

    QString m_fastbootTool;
    QString m_adbTool;
    QProcess *m_activeCommand = nullptr;
    bool m_refreshInProgress = false;
    bool m_payloadOperation = false;

    FastbootState m_state;
    QTimer *m_deviceTimer = nullptr;

    QComboBox *m_deviceCombo = nullptr;
    QLabel *m_deviceLabel = nullptr;
    QLabel *m_toolLabel = nullptr;
    QTableWidget *m_fastbootInfo = nullptr;
    QTableWidget *m_partitionTable = nullptr;
    QLineEdit *m_partitionFilter = nullptr;
    QProgressBar *m_fastbootProgress = nullptr;
    QPlainTextEdit *m_fastbootLog = nullptr;
    QCheckBox *m_showLogs = nullptr;
    QCheckBox *m_ignoreUnknown = nullptr;
    QPushButton *m_flashButton = nullptr;
    QPushButton *m_eraseButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_resizeButton = nullptr;
    QPushButton *m_flashPayloadButton = nullptr;
    QPushButton *m_cancelUpdateButton = nullptr;
    QPushButton *m_finishUpdateButton = nullptr;
    QPushButton *m_switchSlotButton = nullptr;
    QPushButton *m_rebootBootloaderButton = nullptr;
    QPushButton *m_rebootSystemButton = nullptr;
    QPushButton *m_rebootRecoveryButton = nullptr;

    QLabel *m_payloadPathLabel = nullptr;
    QLabel *m_payloadStatusLabel = nullptr;
    QTableWidget *m_payloadInfo = nullptr;
    QTableWidget *m_payloadPartitions = nullptr;
    QListWidget *m_payloadMetadata = nullptr;
    QLineEdit *m_payloadFilter = nullptr;
    QProgressBar *m_payloadProgress = nullptr;
    QCheckBox *m_payloadIgnoreDelta = nullptr;
    QCheckBox *m_payloadIgnoreUnknown = nullptr;
    QCheckBox *m_payloadIgnoreChecks = nullptr;
    std::shared_ptr<PayloadReader> m_payload;
    QFutureWatcher<PayloadLoadResult> *m_payloadLoadWatcher = nullptr;
    QFutureWatcher<PayloadExtractResult> *m_payloadExtractWatcher = nullptr;
    QFutureWatcher<PayloadFlashPrepareResult> *m_payloadFlashWatcher = nullptr;
    PayloadFlashPrepareResult m_payloadFlashData;
    int m_payloadFlashIndex = 0;
};

namespace {

constexpr int kFastbootTimeoutMs = 10 * 60 * 1000;

QPushButton *makeButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setMinimumHeight(30);
    return button;
}

QTableWidget *makeTable(int columns, const QStringList &headers)
{
    auto *table = new QTableWidget(0, columns);
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return table;
}

}

MainWindow::MainWindow()
{
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("Fastboot Enhance"));
    resize(980, 680);
    m_fastbootTool = resolveTool(QStringLiteral("fastboot"));
    m_adbTool = resolveTool(QStringLiteral("adb"));
    buildUi();
    updateActionAvailability();

    m_deviceTimer = new QTimer(this);
    m_deviceTimer->setInterval(2500);
    connect(m_deviceTimer, &QTimer::timeout, this, &MainWindow::refreshDevices);
    m_deviceTimer->start();
    QTimer::singleShot(0, this, &MainWindow::refreshDevices);
}

MainWindow::~MainWindow()
{
    if (m_deviceTimer)
        m_deviceTimer->stop();
    if (m_activeCommand) {
        m_activeCommand->kill();
        m_activeCommand->waitForFinished(1000);
    }
    const auto waitForWatcher = [](auto *watcher) {
        if (watcher) {
            watcher->cancel();
            watcher->waitForFinished();
        }
    };
    waitForWatcher(m_payloadLoadWatcher);
    waitForWatcher(m_payloadExtractWatcher);
    waitForWatcher(m_payloadFlashWatcher);
}

void MainWindow::buildUi()
{
    auto *tabs = new QTabWidget;
    tabs->addTab(buildFastbootPage(), QStringLiteral("Fastboot 可视化"));
    tabs->addTab(buildPayloadPage(), QStringLiteral("Payload Dumper"));
    tabs->addTab(buildAboutPage(), QStringLiteral("关于"));
    setCentralWidget(tabs);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f6f6f6; }"
        "QGroupBox { font-weight: 600; margin-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QPushButton { padding: 5px 10px; }"
        "QTableWidget { background: white; }"
        "QLineEdit, QComboBox { min-height: 26px; }"
        "QPlainTextEdit { font-family: monospace; }"));
}

QWidget *MainWindow::buildFastbootPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(10, 10, 10, 10);

    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(new QLabel(QStringLiteral("设备：")));
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setMinimumWidth(280);
    deviceRow->addWidget(m_deviceCombo, 1);
    auto *refresh = makeButton(QStringLiteral("刷新设备"));
    deviceRow->addWidget(refresh);
    m_toolLabel = new QLabel;
    m_toolLabel->setStyleSheet(QStringLiteral("color: #666;"));
    deviceRow->addWidget(m_toolLabel);
    root->addLayout(deviceRow);

    auto *tabs = new QTabWidget;
    m_fastbootInfo = makeTable(2, {QStringLiteral("属性"), QStringLiteral("值")});
    tabs->addTab(m_fastbootInfo, QStringLiteral("基本属性"));

    auto *partitionPage = new QWidget;
    auto *partitionLayout = new QHBoxLayout(partitionPage);
    auto *partitionLeft = new QVBoxLayout;
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(QStringLiteral("根据分区名过滤：")));
    m_partitionFilter = new QLineEdit;
    filterRow->addWidget(m_partitionFilter);
    partitionLeft->addLayout(filterRow);
    m_partitionTable = makeTable(3, {QStringLiteral("分区名"), QStringLiteral("大小"), QStringLiteral("动态分区")});
    partitionLeft->addWidget(m_partitionTable, 1);
    partitionLayout->addLayout(partitionLeft, 1);

    auto *actionsBox = new QGroupBox(QStringLiteral("分区操作"));
    auto *actions = new QVBoxLayout(actionsBox);
    m_flashButton = makeButton(QStringLiteral("刷写"));
    m_eraseButton = makeButton(QStringLiteral("擦除"));
    m_deleteButton = makeButton(QStringLiteral("删除"));
    m_createButton = makeButton(QStringLiteral("创建"));
    m_resizeButton = makeButton(QStringLiteral("扩容"));
    actions->addWidget(m_flashButton);
    actions->addWidget(m_eraseButton);
    actions->addWidget(m_deleteButton);
    actions->addWidget(m_createButton);
    actions->addWidget(m_resizeButton);
    actions->addStretch();
    m_showLogs = new QCheckBox(QStringLiteral("显示日志"));
    m_ignoreUnknown = new QCheckBox(QStringLiteral("忽略未知分区"));
    actions->addWidget(m_showLogs);
    actions->addWidget(m_ignoreUnknown);
    partitionLayout->addWidget(actionsBox);
    tabs->addTab(partitionPage, QStringLiteral("分区表"));
    root->addWidget(tabs, 1);

    m_fastbootProgress = new QProgressBar;
    m_fastbootProgress->setRange(0, 0);
    m_fastbootProgress->setVisible(false);
    root->addWidget(m_fastbootProgress);

    auto *actionBar = new QGridLayout;
    m_rebootBootloaderButton = makeButton(QStringLiteral("重启到 Bootloader"));
    m_switchSlotButton = makeButton(QStringLiteral("切换分区"));
    m_flashPayloadButton = makeButton(QStringLiteral("刷写 Payload.bin"));
    m_cancelUpdateButton = makeButton(QStringLiteral("清除更新状态"));
    m_finishUpdateButton = makeButton(QStringLiteral("完成更新"));
    m_rebootSystemButton = makeButton(QStringLiteral("重启系统"));
    m_rebootRecoveryButton = makeButton(QStringLiteral("重启 Recovery"));
    m_deviceLabel = new QLabel(QStringLiteral("当前选择的设备：未选择"));
    actionBar->addWidget(m_rebootBootloaderButton, 0, 0);
    actionBar->addWidget(m_switchSlotButton, 0, 1);
    actionBar->addWidget(m_flashPayloadButton, 0, 2);
    actionBar->addWidget(m_cancelUpdateButton, 0, 3);
    actionBar->addWidget(m_finishUpdateButton, 0, 4);
    actionBar->addWidget(m_deviceLabel, 1, 0, 1, 2);
    actionBar->addWidget(m_rebootSystemButton, 1, 3);
    actionBar->addWidget(m_rebootRecoveryButton, 1, 4);
    root->addLayout(actionBar);

    m_fastbootLog = new QPlainTextEdit;
    m_fastbootLog->setReadOnly(true);
    m_fastbootLog->setMaximumBlockCount(2000);
    m_fastbootLog->setMinimumHeight(110);
    m_fastbootLog->setVisible(false);
    root->addWidget(m_fastbootLog);

    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this] { loadDeviceVars(); });
    connect(m_partitionFilter, &QLineEdit::textChanged, this, [this] { refreshPartitionTable(); });
    connect(m_partitionTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateActionAvailability);
    connect(m_showLogs, &QCheckBox::toggled, m_fastbootLog, &QWidget::setVisible);
    connect(m_flashButton, &QPushButton::clicked, this, &MainWindow::flashPartition);
    connect(m_eraseButton, &QPushButton::clicked, this, &MainWindow::erasePartition);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::deleteLogicalPartition);
    connect(m_createButton, &QPushButton::clicked, this, &MainWindow::createLogicalPartition);
    connect(m_resizeButton, &QPushButton::clicked, this, &MainWindow::resizeLogicalPartition);
    connect(m_rebootBootloaderButton, &QPushButton::clicked, this, &MainWindow::rebootToBootloader);
    connect(m_switchSlotButton, &QPushButton::clicked, this, &MainWindow::switchSlot);
    connect(m_flashPayloadButton, &QPushButton::clicked, this, &MainWindow::flashPayload);
    connect(m_cancelUpdateButton, &QPushButton::clicked, this, &MainWindow::cancelUpdate);
    connect(m_finishUpdateButton, &QPushButton::clicked, this, &MainWindow::finishUpdate);
    connect(m_rebootSystemButton, &QPushButton::clicked, this, &MainWindow::rebootSystem);
    connect(m_rebootRecoveryButton, &QPushButton::clicked, this, &MainWindow::rebootRecovery);

    return page;
}

QWidget *MainWindow::buildPayloadPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(10, 10, 10, 10);

    auto *fileRow = new QHBoxLayout;
    m_payloadPathLabel = new QLabel(QStringLiteral("尚未打开 payload.bin"));
    m_payloadPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileRow->addWidget(m_payloadPathLabel, 1);
    auto *open = makeButton(QStringLiteral("打开"));
    fileRow->addWidget(open);
    root->addLayout(fileRow);

    auto *tabs = new QTabWidget;
    m_payloadInfo = makeTable(2, {QStringLiteral("属性"), QStringLiteral("值")});
    tabs->addTab(m_payloadInfo, QStringLiteral("基本属性"));

    auto *partPage = new QWidget;
    auto *partLayout = new QHBoxLayout(partPage);
    auto *partLeft = new QVBoxLayout;
    auto *partFilter = new QHBoxLayout;
    partFilter->addWidget(new QLabel(QStringLiteral("根据分区名过滤：")));
    m_payloadFilter = new QLineEdit;
    partFilter->addWidget(m_payloadFilter);
    partLeft->addLayout(partFilter);
    m_payloadPartitions = makeTable(3, {QStringLiteral("分区名"), QStringLiteral("大小"), QStringLiteral("SHA-256")});
    m_payloadPartitions->setSelectionMode(QAbstractItemView::ExtendedSelection);
    partLeft->addWidget(m_payloadPartitions, 1);
    partLayout->addLayout(partLeft, 1);

    auto *extractBox = new QGroupBox(QStringLiteral("提取选项"));
    auto *extractLayout = new QVBoxLayout(extractBox);
    auto *extract = makeButton(QStringLiteral("提取镜像"));
    m_payloadIgnoreDelta = new QCheckBox(QStringLiteral("允许提取增量 Payload"));
    m_payloadIgnoreUnknown = new QCheckBox(QStringLiteral("忽略未知操作"));
    m_payloadIgnoreChecks = new QCheckBox(QStringLiteral("忽略校验"));
    extractLayout->addWidget(extract);
    extractLayout->addWidget(m_payloadIgnoreDelta);
    extractLayout->addWidget(m_payloadIgnoreUnknown);
    extractLayout->addWidget(m_payloadIgnoreChecks);
    extractLayout->addStretch();
    partLayout->addWidget(extractBox);
    tabs->addTab(partPage, QStringLiteral("分区"));

    m_payloadMetadata = new QListWidget;
    tabs->addTab(m_payloadMetadata, QStringLiteral("动态分区元数据"));
    root->addWidget(tabs, 1);

    m_payloadProgress = new QProgressBar;
    m_payloadProgress->setRange(0, 100);
    m_payloadProgress->setVisible(false);
    root->addWidget(m_payloadProgress);
    m_payloadStatusLabel = new QLabel(QStringLiteral("请选择 payload.bin 或包含它的 ZIP 文件"));
    root->addWidget(m_payloadStatusLabel);

    connect(open, &QPushButton::clicked, this, &MainWindow::openPayloadDialog);
    connect(extract, &QPushButton::clicked, this, &MainWindow::extractPayload);
    connect(m_payloadFilter, &QLineEdit::textChanged, this, [this] {
        for (int row = 0; row < m_payloadPartitions->rowCount(); ++row) {
            const bool visible = m_payloadFilter->text().isEmpty()
                || m_payloadPartitions->item(row, 0)->text().contains(m_payloadFilter->text(), Qt::CaseInsensitive);
            m_payloadPartitions->setRowHidden(row, !visible);
        }
    });
    return page;
}

QWidget *MainWindow::buildAboutPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignCenter);
    auto *title = new QLabel(QStringLiteral("Fastboot Enhance"));
    title->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 600;"));
    layout->addWidget(title, 0, Qt::AlignCenter);
    layout->addWidget(new QLabel(QStringLiteral("%1 Qt 版本 %2")
                                 .arg(PlatformPaths::platformName(), QStringLiteral(FASTBOOT_ENHANCE_VERSION))),
                      0, Qt::AlignCenter);
    layout->addWidget(new QLabel(QStringLiteral("基于 LibXZR 原项目，保留 MIT License")), 0, Qt::AlignCenter);
    auto *source = makeButton(QStringLiteral("打开源代码"));
    source->setMaximumWidth(150);
    layout->addWidget(source, 0, Qt::AlignCenter);
    connect(source, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/libxzr/FastbootEnhance")));
    });
    return page;
}

QString MainWindow::resolveTool(const QString &name) const
{
    return PlatformPaths::resolveTool(name);
}

QStringList MainWindow::deviceArgs(const QStringList &args) const
{
    if (m_state.serial.isEmpty())
        return args;
    QStringList result = args;
    result.prepend(m_state.serial);
    result.prepend(QStringLiteral("-s"));
    return result;
}

void MainWindow::runFastboot(const QStringList &args,
                             const std::function<void(bool, const QString &)> &done,
                             bool showProgress)
{
    if (m_activeCommand) {
        QMessageBox::information(this, QStringLiteral("操作进行中"), QStringLiteral("请等待当前操作完成。"));
        return;
    }
    if (m_fastbootTool.isEmpty()) {
        done(false, QStringLiteral("找不到 fastboot"));
        return;
    }

    if (showProgress) {
        m_fastbootProgress->setVisible(true);
        m_fastbootProgress->setRange(0, 0);
    }
    auto *process = new QProcess(this);
    m_activeCommand = process;
    auto output = std::make_shared<QString>();
    auto reported = std::make_shared<bool>(false);
    auto finish = [this, process, output, reported, done, showProgress](bool ok, const QString &message) {
        if (*reported)
            return;
        *reported = true;
        if (m_activeCommand == process)
            m_activeCommand = nullptr;
        if (showProgress)
            m_fastbootProgress->setVisible(false);
        updateActionAvailability();
        process->deleteLater();
        done(ok, message);
    };
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [process, output, finish] {
        if (process->state() == QProcess::NotRunning)
            return;
        process->kill();
        finish(false, QStringLiteral("fastboot 命令超时，进程已终止。\n") + *output);
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output] {
        const QString text = QString::fromLocal8Bit(process->readAllStandardOutput());
        output->append(text);
        const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines)
            appendLog(line.trimmed());
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, output] {
        const QString text = QString::fromLocal8Bit(process->readAllStandardError());
        output->append(text);
        const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines)
            appendLog(line.trimmed());
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false, QStringLiteral("无法启动 fastboot，请检查权限和平台工具。"));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finish, output](int exitCode, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit && exitCode == 0, *output);
    });
    process->setProgram(m_fastbootTool);
    process->setArguments(args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start();
    timeout->start(kFastbootTimeoutMs);
    updateActionAvailability();
}

void MainWindow::appendLog(const QString &line)
{
    if (line.isEmpty())
        return;
    m_fastbootLog->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
}

void MainWindow::updateActionAvailability()
{
    if (!m_flashButton || !m_partitionTable)
        return;

    const bool stateReady = hasDevice() && !m_state.vars.isEmpty();
    const bool payloadBusy = m_payloadOperation || m_payloadLoadWatcher
        || m_payloadExtractWatcher || m_payloadFlashWatcher;
    const bool commandReady = stateReady && !m_activeCommand && !payloadBusy;
    const bool fastbootd = m_state.fastbootdKnown && m_state.fastbootd;
    const bool hasSlot = m_state.currentSlot == QStringLiteral("a")
        || m_state.currentSlot == QStringLiteral("b");
    const QString target = selectedPartition();
    const bool logical = m_state.logicalPartitions.value(target, false);

    m_flashButton->setEnabled(commandReady);
    m_eraseButton->setEnabled(commandReady);
    m_createButton->setEnabled(commandReady && fastbootd);
    m_deleteButton->setEnabled(commandReady && fastbootd && logical);
    m_resizeButton->setEnabled(commandReady && fastbootd && logical);
    m_rebootBootloaderButton->setEnabled(commandReady);
    m_switchSlotButton->setEnabled(commandReady && hasSlot);
    m_rebootSystemButton->setEnabled(commandReady);
    m_rebootRecoveryButton->setEnabled(commandReady);
    m_flashPayloadButton->setEnabled(commandReady && fastbootd
                                     && m_state.snapshotStatus == QStringLiteral("none"));
    m_cancelUpdateButton->setEnabled(commandReady
                                     && m_state.snapshotStatus == QStringLiteral("snapshotted"));
    m_finishUpdateButton->setEnabled(commandReady
                                     && m_state.snapshotStatus == QStringLiteral("merging"));
}

void MainWindow::setBusy(bool busy, bool payload)
{
    m_payloadOperation = payload && busy;
    if (busy)
        m_payloadProgress->setVisible(payload);
    else
        m_payloadProgress->setVisible(false);
    updateActionAvailability();
}

bool MainWindow::hasDevice() const
{
    return !m_state.serial.isEmpty();
}

void MainWindow::refreshDevices()
{
    if (m_refreshInProgress || m_activeCommand || m_payloadOperation)
        return;
    m_refreshInProgress = true;
    const QString oldSerial = m_state.serial;
    m_state = FastbootState{};
    runFastboot({QStringLiteral("devices")}, [this, oldSerial](bool ok, const QString &output) {
        m_refreshInProgress = false;
        if (!ok) {
            m_deviceCombo->clear();
            updateActionAvailability();
            m_deviceLabel->setText(QStringLiteral("当前选择的设备：无法运行 fastboot"));
            m_toolLabel->setText(QStringLiteral("fastboot: ") + m_fastbootTool);
            return;
        }
        QList<QPair<QString, QString>> devices;
        for (const QString &rawLine : output.split('\n', Qt::SkipEmptyParts)) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty() || line.startsWith(QStringLiteral("< waiting")))
                continue;
            if (line.contains(QStringLiteral("no permissions"), Qt::CaseInsensitive))
                continue;
            const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (fields.isEmpty())
                continue;
            devices.append({fields.first(), fields.size() > 1 ? fields.mid(1).join(' ') : QStringLiteral("fastboot")});
        }
        m_deviceCombo->blockSignals(true);
        m_deviceCombo->clear();
        int selected = 0;
        for (int i = 0; i < devices.size(); ++i) {
            m_deviceCombo->addItem(devices[i].first + QStringLiteral("  (") + devices[i].second + QStringLiteral(")"), devices[i].first);
            if (devices[i].first == oldSerial)
                selected = i;
        }
        m_toolLabel->setText(QStringLiteral("fastboot: ") + m_fastbootTool);
        if (devices.isEmpty()) {
            m_deviceCombo->blockSignals(false);
            m_state = FastbootState{};
            refreshFastbootTables();
            m_deviceLabel->setText(QStringLiteral("当前选择的设备：未检测到设备"));
            return;
        }
        m_deviceCombo->setCurrentIndex(selected);
        m_deviceCombo->blockSignals(false);
        m_state.serial = m_deviceCombo->currentData().toString();
        loadDeviceVars();
    }, false);
}

void MainWindow::loadDeviceVars()
{
    if (m_deviceCombo->currentIndex() < 0)
        return;
    m_state = FastbootState{};
    m_state.serial = m_deviceCombo->currentData().toString();
    m_deviceLabel->setText(QStringLiteral("当前选择的设备：") + m_state.serial);
    refreshFastbootTables();
    if (m_state.serial.isEmpty())
        return;
    runFastboot(deviceArgs({QStringLiteral("getvar"), QStringLiteral("all")}), [this](bool ok, const QString &output) {
        if (!ok) {
            refreshFastbootTables();
            m_deviceLabel->setText(QStringLiteral("当前选择的设备：") + m_state.serial + QStringLiteral("（读取失败）"));
            return;
        }
        parseFastbootVars(output);
        refreshFastbootTables();
    });
}

void MainWindow::parseFastbootVars(const QString &output)
{
    m_state.vars.clear();
    m_state.partitionSizes.clear();
    m_state.logicalPartitions.clear();
    for (const QString &raw : output.split('\n', Qt::SkipEmptyParts)) {
        QString line = raw.trimmed();
        const int marker = line.indexOf(')');
        if (line.startsWith('(') && marker >= 0)
            line = line.mid(marker + 1).trimmed();
        const int separator = line.lastIndexOf(':');
        if (separator <= 0)
            continue;
        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();
        if (key.isEmpty())
            continue;
        m_state.vars.insert(key, value);
        if (key == QStringLiteral("product")) m_state.product = value;
        if (key == QStringLiteral("secure")) {
            m_state.secureKnown = true;
            m_state.secure = value == QStringLiteral("yes");
        }
        if (key == QStringLiteral("current-slot")) m_state.currentSlot = value;
        if (key == QStringLiteral("is-userspace")) {
            m_state.fastbootdKnown = true;
            m_state.fastbootd = value == QStringLiteral("yes");
        }
        if (key == QStringLiteral("snapshot-update-status")) m_state.snapshotStatus = value;
        if (key.startsWith(QStringLiteral("partition-size:"))) {
            bool valid = false;
            const quint64 size = value.toULongLong(&valid, 0);
            if (valid)
                m_state.partitionSizes.insert(key.mid(QStringLiteral("partition-size:").size()), size);
        }
        if (key.startsWith(QStringLiteral("is-logical:")))
            m_state.logicalPartitions.insert(key.mid(QStringLiteral("is-logical:").size()), value == QStringLiteral("yes"));
    }
    m_state.serial = m_deviceCombo->currentData().toString();
}

QString MainWindow::valueFor(const QMap<QString, QString> &vars, const QString &key)
{
    return vars.value(key, QStringLiteral("未知"));
}

void MainWindow::refreshFastbootTables()
{
    clearTable(m_fastbootInfo, 2);
    const QList<QPair<QString, QString>> basic = {
        {QStringLiteral("设备型号"), m_state.product},
        {QStringLiteral("安全状态"), !m_state.secureKnown ? QStringLiteral("未知")
            : (m_state.secure ? QStringLiteral("是") : QStringLiteral("否"))},
        {QStringLiteral("当前 Slot"), m_state.currentSlot},
        {QStringLiteral("用户空间 Fastbootd"), !m_state.fastbootdKnown ? QStringLiteral("未知")
            : (m_state.fastbootd ? QStringLiteral("是") : QStringLiteral("否"))},
        {QStringLiteral("最大下载大小"), valueFor(m_state.vars, QStringLiteral("max-download-size"))},
        {QStringLiteral("更新状态"), m_state.snapshotStatus.isEmpty() ? QStringLiteral("未知") : m_state.snapshotStatus},
    };
    for (int i = 0; i < basic.size(); ++i)
        setTableRow(m_fastbootInfo, i, {basic[i].first, basic[i].second.isEmpty() ? QStringLiteral("未知") : basic[i].second});
    const QString mode = !m_state.fastbootdKnown ? QStringLiteral("未知")
        : (m_state.fastbootd ? QStringLiteral("fastbootd") : QStringLiteral("bootloader"));
    if (m_rebootBootloaderButton)
        m_rebootBootloaderButton->setText(!m_state.fastbootdKnown
            ? QStringLiteral("重启到目标模式")
            : (m_state.fastbootd
            ? QStringLiteral("重启到 Bootloader")
            : QStringLiteral("重启到 fastbootd")));
    m_deviceLabel->setText(QStringLiteral("当前选择的设备：") + (hasDevice() ? m_state.serial : QStringLiteral("未选择"))
                           + QStringLiteral("  [") + mode + QStringLiteral("]"));
    refreshPartitionTable();
    const bool logicalEnabled = hasDevice() && m_state.fastbootd;
    m_createButton->setEnabled(logicalEnabled);
    m_deleteButton->setEnabled(false);
    m_resizeButton->setEnabled(false);
    updateActionAvailability();
}

void MainWindow::refreshPartitionTable()
{
    const QString filter = m_partitionFilter ? m_partitionFilter->text() : QString();
    QSet<QString> names;
    for (auto it = m_state.partitionSizes.cbegin(); it != m_state.partitionSizes.cend(); ++it)
        names.insert(it.key());
    for (auto it = m_state.logicalPartitions.cbegin(); it != m_state.logicalPartitions.cend(); ++it)
        names.insert(it.key());
    QStringList sorted = names.values();
    std::sort(sorted.begin(), sorted.end());
    clearTable(m_partitionTable, 3);
    int row = 0;
    for (const QString &name : sorted) {
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive))
            continue;
        const quint64 size = m_state.partitionSizes.value(name, 0);
        setTableRow(m_partitionTable, row++, {name, size ? formatByteSize(size) : QStringLiteral("未知"),
                                               m_state.logicalPartitions.value(name, false) ? QStringLiteral("是") : QStringLiteral("否")});
    }
}

QString MainWindow::selectedPartition() const
{
    if (!m_partitionTable || m_partitionTable->currentRow() < 0)
        return {};
    const auto *item = m_partitionTable->item(m_partitionTable->currentRow(), 0);
    return item ? item->text() : QString();
}

bool MainWindow::confirmAction(const QString &title, const QString &text) const
{
    return QMessageBox::question(const_cast<MainWindow *>(this), title, text,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

bool MainWindow::confirmVabState() const
{
    if (!m_state.snapshotStatus.isEmpty() && m_state.snapshotStatus != QStringLiteral("none")) {
        if (!confirmAction(QStringLiteral("Virtual A/B 更新状态"),
                           QStringLiteral("设备存在未完成的 Virtual A/B 更新，继续操作可能导致空间不足或无法启动。\n\n仍要继续吗？")))
            return false;
    }
    for (auto it = m_state.partitionSizes.cbegin(); it != m_state.partitionSizes.cend(); ++it) {
        if (it.key().endsWith(QStringLiteral("cow"))
            && !confirmAction(QStringLiteral("Virtual A/B 临时分区"),
                              QStringLiteral("设备中存在 %1 临时分区，继续操作可能导致刷写失败。\n\n仍要继续吗？").arg(it.key())))
            return false;
    }
    return true;
}

bool MainWindow::isSafeName(const QString &value)
{
    return !value.isEmpty() && QRegularExpression(QStringLiteral("^[A-Za-z0-9._+-]+$")).match(value).hasMatch();
}

void MainWindow::flashPartition()
{
    const QString target = selectedPartition();
    if (!hasDevice() || target.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("刷写失败"), QStringLiteral("请先选择设备和分区。"));
        return;
    }
    if (!confirmVabState()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择镜像"), {}, QStringLiteral("镜像文件 (*.img *.image);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QStringList args;
    if (target == QStringLiteral("vbmeta") || target.startsWith(QStringLiteral("vbmeta_"))) {
        if (confirmAction(QStringLiteral("禁用校验"), QStringLiteral("是否在刷写 vbmeta 时禁用 AVB verity 和 verification？")))
            args << QStringLiteral("--disable-verity") << QStringLiteral("--disable-verification");
    }
    args << QStringLiteral("flash") << target << path;
    if (!confirmAction(QStringLiteral("确认刷写"), QStringLiteral("即将刷写分区 %1。\n\n错误镜像可能导致设备无法启动，继续吗？").arg(target))) return;
    runFastboot(deviceArgs(args), [this](bool ok, const QString &output) {
        if (ok) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("操作完成。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("刷写失败"), output.isEmpty() ? QStringLiteral("fastboot 返回失败") : output);
    });
}

void MainWindow::erasePartition()
{
    const QString target = selectedPartition();
    if (!hasDevice() || target.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("擦除失败"), QStringLiteral("请先选择设备和分区。"));
        return;
    }
    if (!confirmAction(QStringLiteral("确认擦除"), QStringLiteral("确定要擦除分区 %1 吗？").arg(target))) return;
    runFastboot(deviceArgs({QStringLiteral("erase"), target}), [this](bool ok, const QString &output) {
        if (ok) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("操作完成。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("擦除失败"), output);
    });
}

void MainWindow::deleteLogicalPartition()
{
    const QString target = selectedPartition();
    if (!hasDevice() || !m_state.fastbootd || !m_state.logicalPartitions.value(target, false)) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("该操作只适用于 fastbootd 中的逻辑分区。"));
        return;
    }
    if (!confirmAction(QStringLiteral("确认删除"), QStringLiteral("确定要删除逻辑分区 %1 吗？").arg(target))) return;
    runFastboot(deviceArgs({QStringLiteral("delete-logical-partition"), target}), [this](bool ok, const QString &output) {
        if (ok) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("操作完成。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("删除失败"), output);
    });
}

void MainWindow::createLogicalPartition()
{
    if (!hasDevice() || !m_state.fastbootd) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("请先进入 fastbootd。"));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("创建动态分区"), QStringLiteral("分区名："), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    if (!isSafeName(name)) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("分区名只能包含字母、数字、点、下划线、加号或连字符。"));
        return;
    }
    const QString size = QInputDialog::getText(this, QStringLiteral("创建动态分区"), QStringLiteral("大小（字节）："), QLineEdit::Normal, QStringLiteral("0"), &accepted);
    if (!accepted) return;
    bool validSize = false;
    const quint64 newSize = size.toULongLong(&validSize);
    if (!validSize || newSize == 0) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("请输入有效的正整数大小。"));
        return;
    }
    runFastboot(deviceArgs({QStringLiteral("create-logical-partition"), name, QString::number(newSize)}), [this](bool success, const QString &output) {
        if (success) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("操作完成。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("创建失败"), output);
    });
}

void MainWindow::resizeLogicalPartition()
{
    const QString target = selectedPartition();
    if (!hasDevice() || !m_state.fastbootd || !m_state.logicalPartitions.value(target, false)) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("该操作只适用于 fastbootd 中的逻辑分区。"));
        return;
    }
    bool accepted = false;
    const QString size = QInputDialog::getText(this, QStringLiteral("扩容动态分区"), QStringLiteral("新大小（字节）："), QLineEdit::Normal,
                                                QString::number(m_state.partitionSizes.value(target)), &accepted);
    if (!accepted) return;
    bool validSize = false;
    const quint64 newSize = size.toULongLong(&validSize);
    const quint64 currentSize = m_state.partitionSizes.value(target);
    if (!validSize || newSize == 0) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("请输入有效的正整数大小。"));
        return;
    }
    if (currentSize > 0 && newSize <= currentSize) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("动态分区只支持扩容，不能缩小或保持原大小。"));
        return;
    }
    runFastboot(deviceArgs({QStringLiteral("resize-logical-partition"), target, QString::number(newSize)}), [this](bool success, const QString &output) {
        if (success) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("操作完成。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("扩容失败"), output);
    });
}

void MainWindow::rebootToBootloader()
{
    if (!hasDevice()) return;
    const QString command = m_state.fastbootd ? QStringLiteral("reboot bootloader") : QStringLiteral("reboot fastboot");
    runFastboot(deviceArgs(command.split(' ')), [this](bool ok, const QString &output) {
        if (!ok) QMessageBox::warning(this, QStringLiteral("重启失败"), output);
        else QTimer::singleShot(1200, this, &MainWindow::refreshDevices);
    });
}

void MainWindow::switchSlot()
{
    if (!hasDevice() || (m_state.currentSlot != QStringLiteral("a") && m_state.currentSlot != QStringLiteral("b"))) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"), QStringLiteral("设备没有可用的 A/B Slot 信息。"));
        return;
    }
    const QString target = m_state.currentSlot == QStringLiteral("a") ? QStringLiteral("b") : QStringLiteral("a");
    if (!confirmAction(QStringLiteral("切换 Slot"), QStringLiteral("确定将活动 Slot 切换到 %1 吗？").arg(target))) return;
    runFastboot(deviceArgs({QStringLiteral("set_active"), target}), [this](bool ok, const QString &output) {
        if (ok) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("活动 Slot 已切换。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("切换失败"), output);
    });
}

void MainWindow::rebootSystem()
{
    if (!hasDevice()) return;
    runFastboot(deviceArgs({QStringLiteral("reboot")}), [this](bool ok, const QString &output) {
        if (!ok) QMessageBox::warning(this, QStringLiteral("重启失败"), output);
        else { m_state = FastbootState{}; refreshFastbootTables(); QTimer::singleShot(1500, this, &MainWindow::refreshDevices); }
    });
}

void MainWindow::rebootRecovery()
{
    if (!hasDevice()) return;
    runFastboot(deviceArgs({QStringLiteral("reboot"), QStringLiteral("recovery")}), [this](bool ok, const QString &output) {
        if (!ok) QMessageBox::warning(this, QStringLiteral("重启失败"), output);
        else { m_state = FastbootState{}; refreshFastbootTables(); QTimer::singleShot(1500, this, &MainWindow::refreshDevices); }
    });
}

void MainWindow::cancelUpdate()
{
    if (!hasDevice() || m_state.snapshotStatus != QStringLiteral("snapshotted")) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"),
                             QStringLiteral("只有 snapshot-update-status 为 snapshotted 时才能清除更新状态。"));
        return;
    }
    runFastboot(deviceArgs({QStringLiteral("snapshot-update"), QStringLiteral("cancel")}), [this](bool ok, const QString &output) {
        if (ok) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("更新状态已清除。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("操作失败"), output);
    });
}

void MainWindow::finishUpdate()
{
    if (!hasDevice() || m_state.snapshotStatus != QStringLiteral("merging")) {
        QMessageBox::warning(this, QStringLiteral("操作不可用"),
                             QStringLiteral("只有 snapshot-update-status 为 merging 时才能完成更新。"));
        return;
    }
    runFastboot(deviceArgs({QStringLiteral("snapshot-update"), QStringLiteral("merge")}), [this](bool ok, const QString &output) {
        if (ok) { QMessageBox::information(this, QStringLiteral("完成"), QStringLiteral("当前更新已完成。")); loadDeviceVars(); }
        else QMessageBox::warning(this, QStringLiteral("操作失败"), output);
    });
}

void MainWindow::flashPayload()
{
    if (!hasDevice()) {
        QMessageBox::warning(this, QStringLiteral("刷写失败"), QStringLiteral("请先选择 Fastboot 设备。"));
        return;
    }
    if (!m_state.fastbootdKnown || !m_state.fastbootd) {
        QMessageBox::warning(this, QStringLiteral("刷写失败"),
                             QStringLiteral("Payload 只能在 fastbootd 中刷写，请先重启到 fastbootd。"));
        return;
    }
    if (m_state.snapshotStatus.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("刷写失败"),
                             QStringLiteral("无法确认 Virtual A/B 更新状态，已停止刷写。"));
        return;
    }
    if (m_state.snapshotStatus != QStringLiteral("none")) {
        QMessageBox::warning(this, QStringLiteral("刷写失败"),
                             QStringLiteral("设备存在未完成的 Virtual A/B 更新，请先处理更新状态后再刷写 Payload。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 Payload"), {}, QStringLiteral("Payload (*.bin *.zip);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (!confirmAction(QStringLiteral("确认刷写 Payload"),
                       QStringLiteral("即将从该 Payload 提取并刷写当前设备可识别的全部分区。\n\n"
                                      "请确认 ROM 与设备型号完全匹配，并已确认当前处于正确的 fastbootd 和 Slot。\n"
                                      "错误的 Payload 可能导致设备无法启动，继续吗？")))
        return;
    const bool ignoreUnknown = m_ignoreUnknown->isChecked();
    setBusy(true, true);
    m_payloadProgress->setRange(0, 100);
    m_payloadProgress->setValue(0);
    auto reader = std::make_shared<PayloadReader>(path);
    m_payloadFlashWatcher = new QFutureWatcher<PayloadFlashPrepareResult>(this);
    auto state = m_state;
    QPointer<MainWindow> window(this);
    auto watcher = m_payloadFlashWatcher;
    connect(watcher, &QFutureWatcher<PayloadFlashPrepareResult>::finished, this, [this, watcher] {
        m_payloadFlashData = watcher->result();
        watcher->deleteLater();
        m_payloadFlashWatcher = nullptr;
        updateActionAvailability();
        if (!m_payloadFlashData.ok) {
            const QString error = m_payloadFlashData.error;
            m_payloadFlashData = {};
            setBusy(false, true);
            QMessageBox::warning(this, QStringLiteral("Payload 失败"), error);
            return;
        }
        m_payloadFlashIndex = 0;
        flashNextPayloadImage();
    });
    m_payloadFlashWatcher->setFuture(QtConcurrent::run([window, reader, state, ignoreUnknown] {
        PayloadFlashPrepareResult result;
        QString error;
        if (!reader->load(&error)) { result.error = error; return result; }
        if (reader->isIncremental()) {
            result.error = QStringLiteral("暂不支持直接刷写增量 Payload，请使用完整 OTA 包。\n"
                                          "增量包可以在分区页中尝试提取不含源数据操作的分区。");
            return result;
        }
        const QStringList unsupported = reader->unsupportedOperations();
        if (!unsupported.isEmpty()) {
            result.error = QStringLiteral("Payload 包含暂不支持的操作：") + unsupported.join(QStringLiteral(", "));
            return result;
        }
        result.tempDirectory = std::make_shared<QTemporaryDir>(
            PlatformPaths::temporaryDirectoryPattern(QStringLiteral("payload-flash")));
        if (!result.tempDirectory->isValid()) { result.error = QStringLiteral("无法创建临时目录"); return result; }
        const auto rows = reader->partitions();
        QList<PayloadPartitionRow> flashRows;
        QStringList unknown;
        for (const auto &row : rows) {
            if (!state.partitionSizes.contains(row.name) && !state.partitionSizes.contains(row.name + QStringLiteral("_") + state.currentSlot)) {
                unknown.append(row.name);
                if (ignoreUnknown)
                    result.skippedNames.append(row.name);
                continue;
            }
            flashRows.append(row);
        }
        if (!ignoreUnknown && !unknown.isEmpty()) {
            result.error = QStringLiteral("设备中不存在这些 Payload 分区：") + unknown.join(QStringLiteral(", "));
            return result;
        }
        if (flashRows.isEmpty()) {
            result.error = QStringLiteral("没有可在当前设备上刷写的 Payload 分区");
            return result;
        }
        const auto hasPartition = [&rows](const QString &name) {
            for (const auto &row : rows) {
                if (row.name == name)
                    return true;
            }
            return false;
        };
        if ((hasPartition(QStringLiteral("xbl")) && hasPartition(QStringLiteral("xbl_lp5")))
            || (hasPartition(QStringLiteral("xbl_config")) && hasPartition(QStringLiteral("xbl_config_lp5")))) {
            result.error = QStringLiteral("Payload 同时包含 xbl/xbl_lp5 或对应的 xbl_config 固件。\n"
                                          "这些是不同内存型号的互斥固件，为避免刷错导致设备变砖，已停止自动刷写。\n"
                                          "请在 Payload 分区页提取与设备内存型号匹配的固件后单独刷写。");
            return result;
        }
        for (int i = 0; i < flashRows.size(); ++i) {
            const auto &row = flashRows[i];
            const QString output = result.tempDirectory->filePath(row.name + QStringLiteral(".img"));
            if (!reader->extract(row.name, result.tempDirectory->path(), false, false, &error,
                                 [window, i, total = flashRows.size()](int progress, const QString &message) {
                if (!window)
                    return;
                QMetaObject::invokeMethod(window.data(), [window, i, total, progress, message] {
                    if (!window)
                        return;
                    const int denominator = std::max(1, static_cast<int>(total));
                    window->m_payloadProgress->setValue((i * 50 + progress / 2) / denominator);
                    window->m_payloadStatusLabel->setText(message);
                }, Qt::QueuedConnection);
            })) {
                result.error = row.name + QStringLiteral("：") + error;
                return result;
            }
            result.names.append(row.name);
            result.paths.append(output);
        }
        result.ok = true;
        return result;
    }));
}

void MainWindow::flashNextPayloadImage()
{
    if (m_payloadFlashIndex >= m_payloadFlashData.names.size()) {
        const QStringList skipped = m_payloadFlashData.skippedNames;
        setBusy(false, true);
        const QString message = skipped.isEmpty()
            ? QStringLiteral("Payload 已全部刷写完成。")
            : QStringLiteral("Payload 已刷写完成，已跳过未知分区：%1").arg(skipped.join(QStringLiteral(", ")));
        QMessageBox::information(this, QStringLiteral("完成"), message);
        loadDeviceVars();
        m_payloadFlashData = {};
        return;
    }
    const QString name = m_payloadFlashData.names[m_payloadFlashIndex];
    const QString path = m_payloadFlashData.paths[m_payloadFlashIndex];
    m_payloadStatusLabel->setText(QStringLiteral("正在刷写：") + name);
    runFastboot(deviceArgs({QStringLiteral("flash"), name, path}), [this](bool ok, const QString &output) {
        if (!ok) {
            setBusy(false, true);
            QMessageBox::warning(this, QStringLiteral("Payload 刷写失败"), output);
            m_payloadFlashData = {};
            return;
        }
        ++m_payloadFlashIndex;
        const int total = std::max(1, static_cast<int>(m_payloadFlashData.names.size()));
        m_payloadProgress->setValue(50 + (m_payloadFlashIndex * 50 / total));
        flashNextPayloadImage();
    });
}

void MainWindow::openPayloadDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 Payload"), {}, QStringLiteral("Payload (*.bin *.zip);;所有文件 (*)"));
    if (!path.isEmpty())
        loadPayload(path);
}

void MainWindow::loadPayload(const QString &path)
{
    if (m_payloadLoadWatcher || m_payloadExtractWatcher || m_payloadFlashWatcher || m_payloadOperation)
        return;
    m_payload.reset();
    fillPayloadUi();
    m_payloadStatusLabel->setText(QStringLiteral("正在读取 Payload..."));
    m_payloadPathLabel->setText(path);
    m_payloadProgress->setRange(0, 0);
    m_payloadProgress->setVisible(true);
    auto reader = std::make_shared<PayloadReader>(path);
    m_payloadLoadWatcher = new QFutureWatcher<PayloadLoadResult>(this);
    updateActionAvailability();
    auto watcher = m_payloadLoadWatcher;
    connect(watcher, &QFutureWatcher<PayloadLoadResult>::finished, this, [this, watcher] {
        const PayloadLoadResult result = watcher->result();
        watcher->deleteLater();
        m_payloadLoadWatcher = nullptr;
        updateActionAvailability();
        m_payloadProgress->setVisible(false);
        if (!result.reader || !result.error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Payload 失败"), result.error);
            m_payloadStatusLabel->setText(QStringLiteral("读取失败"));
            return;
        }
        m_payload = result.reader;
        fillPayloadUi();
        m_payloadStatusLabel->setText(QStringLiteral("已加载 %1 个分区").arg(m_payloadPartitions->rowCount()));
    });
    m_payloadLoadWatcher->setFuture(QtConcurrent::run([reader] {
        PayloadLoadResult result;
        result.reader = reader;
        if (!reader->load(&result.error))
            result.reader.reset();
        return result;
    }));
}

void MainWindow::fillPayloadUi()
{
    clearTable(m_payloadInfo, 2);
    clearTable(m_payloadPartitions, 3);
    m_payloadMetadata->clear();
    if (!m_payload)
        return;
    int row = 0;
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Payload 版本"), QString::number(m_payload->fileFormatVersion())});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Manifest 大小"), formatByteSize(m_payload->manifestSize())});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Metadata 签名大小"), formatByteSize(m_payload->metadataSignatureSize())});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Metadata 签名"), m_payload->metadataSignature().isEmpty() ? QStringLiteral("未知") : m_payload->metadataSignature()});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("数据大小"), formatByteSize(m_payload->dataSize())});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Payload 签名大小"), formatByteSize(m_payload->payloadSignatureSize())});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Payload 签名"), m_payload->payloadSignature().isEmpty() ? QStringLiteral("未知") : m_payload->payloadSignature()});
    setTableRow(m_payloadInfo, row++, {QStringLiteral("完整包"), m_payload->manifest().minor_version() == 0 ? QStringLiteral("是") : QStringLiteral("否（增量 %1）").arg(m_payload->manifest().minor_version())});
    if (m_payload->manifest().has_max_timestamp()) {
        const QDateTime timestamp = QDateTime::fromSecsSinceEpoch(m_payload->manifest().max_timestamp()).toLocalTime();
        setTableRow(m_payloadInfo, row++, {QStringLiteral("时间戳"), timestamp.toString(Qt::ISODate)});
    }
    setTableRow(m_payloadInfo, row++, {QStringLiteral("Block 大小"), formatByteSize(m_payload->manifest().block_size())});
    for (const QString &line : m_payload->imageInfo()) {
        const int separator = line.indexOf(": ");
        setTableRow(m_payloadInfo, row++, {separator > 0 ? line.left(separator) : QStringLiteral("信息"), separator > 0 ? line.mid(separator + 2) : line});
    }
    const auto rows = m_payload->partitions();
    for (int i = 0; i < rows.size(); ++i)
        setTableRow(m_payloadPartitions, i, {rows[i].name, rows[i].size, rows[i].hash});
    for (const QString &line : m_payload->dynamicMetadata())
        m_payloadMetadata->addItem(line);
}

void MainWindow::extractPayload()
{
    if (!m_payload) {
        QMessageBox::warning(this, QStringLiteral("提取失败"), QStringLiteral("请先打开 Payload 并选择分区。"));
        return;
    }
    QStringList names;
    const QModelIndexList selectedRows = m_payloadPartitions->selectionModel()
        ? m_payloadPartitions->selectionModel()->selectedRows(0) : QModelIndexList{};
    for (const QModelIndex &index : selectedRows) {
        if (index.isValid() && m_payloadPartitions->item(index.row(), 0))
            names.append(m_payloadPartitions->item(index.row(), 0)->text());
    }
    if (names.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提取失败"), QStringLiteral("请先打开 Payload 并选择分区。"));
        return;
    }
    if (m_payload->isIncremental() && !m_payloadIgnoreDelta->isChecked()) {
        QMessageBox::warning(this, QStringLiteral("无法提取增量 Payload"),
                             QStringLiteral("当前 Payload 是增量包，通常需要源镜像才能还原。\n"
                                            "确认理解风险后，勾选“允许提取增量 Payload”再继续。"));
        return;
    }
    if (m_payloadLoadWatcher || m_payloadExtractWatcher || m_payloadFlashWatcher || m_payloadOperation)
        return;
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"));
    if (directory.isEmpty()) return;
    m_payloadProgress->setRange(0, 100);
    m_payloadProgress->setValue(0);
    m_payloadProgress->setVisible(true);
    m_payloadStatusLabel->setText(QStringLiteral("正在提取 %1 个镜像...").arg(names.size()));
    m_payloadExtractWatcher = new QFutureWatcher<PayloadExtractResult>(this);
    updateActionAvailability();
    auto watcher = m_payloadExtractWatcher;
    auto reader = m_payload;
    const bool ignoreUnknown = m_payloadIgnoreUnknown->isChecked();
    const bool ignoreChecks = m_payloadIgnoreChecks->isChecked();
    QPointer<MainWindow> window(this);
    connect(watcher, &QFutureWatcher<PayloadExtractResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_payloadExtractWatcher = nullptr;
        updateActionAvailability();
        m_payloadProgress->setVisible(false);
        if (result.ok) {
            m_payloadStatusLabel->setText(QStringLiteral("提取完成"));
            QMessageBox::information(this, QStringLiteral("完成"),
                                     QStringLiteral("已提取 %1 个镜像到选择的目录。")
                                         .arg(result.extractedCount));
        } else {
            m_payloadStatusLabel->setText(QStringLiteral("提取失败"));
            QMessageBox::warning(this, QStringLiteral("提取失败"), result.error);
        }
    });
    m_payloadExtractWatcher->setFuture(QtConcurrent::run([window, reader, names, directory, ignoreUnknown, ignoreChecks] {
        PayloadExtractResult result;
        const int total = std::max(1, static_cast<int>(names.size()));
        for (int index = 0; index < names.size(); ++index) {
            const QString &name = names.at(index);
            QString error;
            const bool ok = reader->extract(name, directory, ignoreUnknown, ignoreChecks, &error,
                                     [window, index, total](int progress, const QString &message) {
                if (!window)
                    return;
                QMetaObject::invokeMethod(window.data(), [window, index, total, progress, message] {
                    if (!window)
                        return;
                    window->m_payloadProgress->setValue((index * 100 + progress) / total);
                    window->m_payloadStatusLabel->setText(message);
                }, Qt::QueuedConnection);
            });
            if (!ok) {
                result.error = name + QStringLiteral("：") + error;
                return result;
            }
            ++result.extractedCount;
        }
        result.ok = true;
        return result;
    }));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() != 1)
        return;
    const QString path = urls.first().toLocalFile();
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".bin")) || lower.endsWith(QStringLiteral(".zip")))
        loadPayload(path);
}

void MainWindow::setTableRow(QTableWidget *table, int row, const QStringList &values)
{
    table->insertRow(row);
    for (int column = 0; column < values.size() && column < table->columnCount(); ++column)
        table->setItem(row, column, new QTableWidgetItem(values[column]));
}

void MainWindow::clearTable(QTableWidget *table, int columns)
{
    if (!table) return;
    table->setColumnCount(columns);
    table->setRowCount(0);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--version")) {
            QTextStream(stdout) << QStringLiteral("Fastboot Enhance %1\n")
                                       .arg(QStringLiteral(FASTBOOT_ENHANCE_VERSION));
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FastbootEnhance"));
    QApplication::setApplicationVersion(QStringLiteral(FASTBOOT_ENHANCE_VERSION));
    QApplication::setOrganizationName(QStringLiteral("FastbootEnhance"));
    MainWindow window;
    window.show();
    return app.exec();
}
