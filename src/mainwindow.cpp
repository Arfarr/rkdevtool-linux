#include "mainwindow.h"

#include <clocale>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRawFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QHeaderView>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>

namespace {

constexpr int kBtnH = 25;    // 与 Win 版按钮高度一致
constexpr int kEditH = 23;
constexpr int kLogW = 357;   // 右侧日志栏宽度（Win 版 x=601~958）
constexpr int kSideW = 601;  // 左侧操作区宽度

const QStringList kStorages{
    QStringLiteral("FLASH"),  QStringLiteral("EMMC"),   QStringLiteral("SD"),
    QStringLiteral("SD1"),    QStringLiteral("SPINOR"), QStringLiteral("SPINAND"),
    QStringLiteral("RAM"),    QStringLiteral("USB"),     QStringLiteral("SATA"),
    QStringLiteral("PCIE"),
};

// 存储单选名 -> upgrade_tool 的 ss 参数
QString storageArg(const QString &s)
{
    const QString u = s.toUpper();
    if (u == QStringLiteral("SD1"))
        return QStringLiteral("SD");
    if (u == QStringLiteral("SPINOR") || u == QStringLiteral("SPINAND")
        || u == QStringLiteral("EMMC") || u == QStringLiteral("FLASH"))
        return u;
    return QString();
}

} // namespace

QString pickFontFamily(const QStringList &candidates)
{
    const QStringList installed = QFontDatabase().families();
    for (const QString &c : candidates) {
        if (installed.contains(c, Qt::CaseInsensitive))
            return c;
    }
    // 候选都没装时，退回系统默认；中文由 fontconfig 负责回退
    return QString();
}

// 界面统一使用的中文字体候选，按优先级排列
// 本机 fontconfig 的默认 sans/mono 都是 DejaVu LGC Sans，不含任何中文字形，
// 所以必须显式指定含 CJK 的字体，并把整张列表交给 Qt 做逐级回退。
const QStringList kUiFonts{
    QStringLiteral("Microsoft YaHei"),   // Windows 原版
    QStringLiteral("Noto Sans CJK SC"),
    QStringLiteral("Source Han Sans SC"),
    QStringLiteral("Noto Sans CJK JP"),
    QStringLiteral("WenQuanYi Micro Hei"),
    QStringLiteral("Droid Sans Fallback"),
    QStringLiteral("AR PL UMing CN"),
};

QStringList installedCjkFonts(const QStringList &candidates)
{
    const QStringList installed = QFontDatabase().families();
    QStringList out;
    for (const QString &c : candidates) {
        if (installed.contains(c, Qt::CaseInsensitive))
            out << c;
    }
    return out;
}

// 分区表里"尚未指定镜像"的占位文字
const QString kNoFileHint = QStringLiteral("（双击选择镜像）");

QFont uiFont(int pointSize, bool bold)
{
    QFont f(pickFontFamily(kUiFonts), pointSize);
    const QStringList chain = installedCjkFonts(kUiFonts);
    if (!chain.isEmpty())
        f.setFamilies(chain); // 首选缺失时按顺序回退，不会掉回无中文的默认字体
    f.setBold(bold);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

// ---------------------------------------------------------------- 构件

QPushButton *MainWindow::mkBtn(const QString &id, const QString &text, int w)
{
    auto *b = new QPushButton(text);
    b->setObjectName(id);
    b->setFixedSize(w, kBtnH);
    m_buttons.append(b);
    return b;
}

QLineEdit *MainWindow::mkEdit(const QString &id, const QString &placeholder)
{
    auto *e = new QLineEdit;
    e->setObjectName(id);
    e->setFixedHeight(kEditH);
    if (!placeholder.isEmpty())
        e->setPlaceholderText(placeholder);
    return e;
}

QLabel *MainWindow::mkLabel(const QString &text, int w)
{
    auto *l = new QLabel(text);
    l->setFixedHeight(kEditH);
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (w > 0)
        l->setFixedWidth(w);
    return l;
}

QCheckBox *MainWindow::mkCheck(const QString &id, const QString &text)
{
    auto *c = new QCheckBox(text);
    c->setObjectName(id);
    return c;
}

QLineEdit *MainWindow::ed(const QString &id) const
{
    return findChild<QLineEdit *>(id);
}

QRadioButton *MainWindow::currentStorage() const
{
    const QList<QRadioButton *> all = findChildren<QRadioButton *>();
    QRadioButton *fallback = nullptr;
    for (QRadioButton *b : all) {
        if (!b->objectName().startsWith(QStringLiteral("radioStorage_")))
            continue;
        if (b->isChecked())
            return b;
        if (!fallback)
            fallback = b;
    }
    return fallback;
}

void MainWindow::link(QLineEdit *edit, const QString &btnId, const QString &title,
                      const QString &filter)
{
    auto *b = findChild<QPushButton *>(btnId);
    if (!b || !edit)
        return;
    connect(b, &QPushButton::clicked, this, [this, edit, title, filter] {
        if (m_selftest)
            return;
        const QString p = QFileDialog::getOpenFileName(this, title, QString(), filter);
        if (!p.isEmpty())
            edit->setText(p);
    });
}

// ---------------------------------------------------------------- 构造

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("瑞芯微开发工具v%1").arg(QStringLiteral(RKDEVTOOL_VERSION)));

    m_selftest = QCoreApplication::arguments().contains(QStringLiteral("--selftest"));
    for (const QString &a : QCoreApplication::arguments()) {
        if (a.startsWith(QStringLiteral("--test-cmd="))) {
            m_selftestCmd = a.mid(11);
            m_selftest = true;
        }
    }

    m_scanner = new UsbScanner(this);
    m_upgrader = new Upgrader(this);

    buildUi();
    setupMenus();

    connect(m_scanner, &UsbScanner::changed, this, &MainWindow::onDeviceChanged);
    connect(m_upgrader, &Upgrader::logLine, this, &MainWindow::onLogLine);
    connect(m_upgrader, &Upgrader::percentChanged, this, &MainWindow::onPercentChanged);
    connect(m_upgrader, &Upgrader::jobFinished, this, &MainWindow::onJobFinished);
    connect(m_upgrader, &Upgrader::busyChanged, this, &MainWindow::onBusyChanged);

    m_cfg = Paths::loadConfig();
    m_edFirmware->setText(m_cfg.firmware);
    m_edLoader->setText(m_cfg.loader);
    m_edParameter->setText(m_cfg.parameter);
    m_chkAddress->setChecked(m_cfg.forceByAddress);
    if (auto *r = findChild<QRadioButton *>(QStringLiteral("radioStorage_1")))
        r->setChecked(true);

    m_scanner->scanOnce();
    m_scanner->start(1000);
    onDeviceChanged();
    refreshEnabled();

    appendLog(QStringLiteral("瑞芯微开发工具 v%1 (Linux) 已启动")
                  .arg(QStringLiteral(RKDEVTOOL_VERSION)));
    if (toolUsable()) {
        appendLog(QStringLiteral("后端工具：%1").arg(m_upgrader->toolPath()));
        appendLog(QStringLiteral("设备检测已启动，等待 Rockchip 设备接入…"));
    } else {
        appendLog(QStringLiteral("[警告] 未找到 upgrade_tool，请检查安装路径"));
    }

    if (m_selftest) {
        QTimer::singleShot(400, this, [this] {
            if (!m_selftestCmd.isEmpty())
                runTestCommand(m_selftestCmd);
            else
                dumpSelfTest();
            if (m_selftestCmd.isEmpty())
                QCoreApplication::quit();
        });
    }
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- 主窗口

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tabs = new QTabWidget(central);
    m_tabs->setObjectName(QStringLiteral("tabs"));
    m_tabs->setFixedWidth(kSideW);
    m_tabs->addTab(buildTabDownload(), QStringLiteral("下载镜像"));
    m_tabs->addTab(buildTabUpgrade(), QStringLiteral("升级固件"));
    m_tabs->addTab(buildTabAdvanced(), QStringLiteral("高级功能"));
    m_tabs->setCurrentIndex(1);

    m_log = new QPlainTextEdit(central);
    m_log->setObjectName(QStringLiteral("logView"));
    m_log->setReadOnly(true);
    // FixedFont 默认是 DejaVu Sans Mono，没有任何中文字形，中文会显示成方块，
    // 所以必须挑一个含中文的等宽字体
    const QStringList logCandidates{
        QStringLiteral("Noto Sans Mono CJK SC"),
        QStringLiteral("Noto Sans Mono CJK JP"),
        QStringLiteral("WenQuanYi Micro Hei Mono"),
        QStringLiteral("Sarasa Mono SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Droid Sans Fallback"),
    };
    QFont logFont(pickFontFamily(logCandidates));
    const QStringList logChain = installedCjkFonts(logCandidates);
    if (!logChain.isEmpty()) {
        logFont.setFamilies(logChain);
        logFont.setStyleHint(QFont::Monospace);
    } else {
        // 没有任何中文字体，只能退回系统等宽字体
        logFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    logFont.setPointSize(9);
    m_log->setFont(logFont);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setFixedWidth(kLogW);
    m_log->setStyleSheet(
        QStringLiteral("QPlainTextEdit{background:#ffffff;border:1px solid #7a7a7a;}"));
    m_log->setPlaceholderText(QStringLiteral("运行日志…"));

    root->addWidget(m_tabs);
    root->addWidget(m_log, 1);
    setCentralWidget(central);

    m_statusDevice = new QLabel(this);
    m_statusDevice->setObjectName(QStringLiteral("statusDevice"));
    m_statusDevice->setFixedWidth(250);
    m_statusText = new QLabel(this);
    m_statusText->setObjectName(QStringLiteral("statusText"));
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("progress"));
    m_progress->setFixedSize(234, 20);
    m_progress->setRange(0, 100);
    m_statusSerial = new QLabel(this);
    m_statusSerial->setObjectName(QStringLiteral("statusSerial"));
    m_statusSerial->setFixedWidth(200);
    m_statusSerial->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    statusBar()->addWidget(m_statusDevice, 1);
    statusBar()->addPermanentWidget(m_statusText, 1);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->addPermanentWidget(m_statusSerial);
    statusBar()->setSizeGripEnabled(false);

    resize(970, 430);

    // 下载镜像页默认载入标准分区清单，用户勾选+指定镜像即可刷入
    initStandardParts();
}

// ---------------------------------------------------------------- 下载镜像

// 设备工具按钮矩阵：读取 Flash/切换存储/擦除等高级操作，归属于"高级功能"页
QLayout *MainWindow::buildDeviceTools(QWidget *parent)
{
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    struct Def {
        const char *id;
        const char *text;
        Upgrader::Job job;
        const char *args;
        const char *desc;
    };
    constexpr int kToolCols = 5;
    const Def defs[]{
        {"btnReadFlashId", "读取FlashID", Upgrader::Job::ReadFlashId, "rid", "读取 FlashID"},
        {"btnReadFlashInfo", "读取Flash信息", Upgrader::Job::ReadFlashInfo, "rfi", "读取 Flash 信息"},
        {"btnReadChipInfo", "读取chip信息", Upgrader::Job::ReadChipInfo, "rci", "读取 chip 信息"},
        {"btnReadCapability", "读取Capability", Upgrader::Job::ReadCapability, "rcb", "读取 Capability"},
        {"btnTestDevice", "测试设备", Upgrader::Job::TestDevice, "td", "测试设备"},
        {"btnResetDevice", "重启设备", Upgrader::Job::ResetDevice, "rd", "重启设备"},
        {"btnToMaskrom", "进入Maskrom", Upgrader::Job::RebootToMaskrom, "rd 2", "进入 Maskrom"},
        {"btnSwitchStorage", "切换存储", Upgrader::Job::TestDevice, "ss", "切换存储"},
        {"btnClearSn", "清空序列号", Upgrader::Job::None, "", "清空序列号"},
        {"btnSecureMode", "检测安全模式", Upgrader::Job::TestDevice, "rsm", "检测安全模式"},
        {"btnExportLog", "导出串口日志", Upgrader::Job::TestDevice, "rcl", "导出串口日志"},
        {"btnGetStorage", "获取当前存储", Upgrader::Job::TestDevice, "ss", "获取当前存储"},
        {"btnExportImage", "导出镜像", Upgrader::Job::PartitionList, "pl", "导出镜像"},
        {"btnEraseSector", "擦除扇区", Upgrader::Job::None, "", "擦除扇区"},
        {"btnEraseAll", "擦除所有", Upgrader::Job::None, "", "擦除所有"},
    };
    for (int i = 0; i < int(sizeof(defs) / sizeof(defs[0])); ++i) {
        const Def &d = defs[i];
        // 文本是 UTF-8 中文源码，必须用 fromUtf8；fromLatin1 会逐字节错解成乱码
        auto *b = mkBtn(QString::fromLatin1(d.id), QString::fromUtf8(d.text), 104);
        grid->addWidget(b, i / kToolCols, i % kToolCols);
        const QString id = QString::fromLatin1(d.id);
        const QString args = QString::fromLatin1(d.args);
        const QString desc = QString::fromUtf8(d.desc);
        const Upgrader::Job job = d.job;
        connect(b, &QPushButton::clicked, this, [this, id, job, args, desc] {
            if (job == Upgrader::Job::None) {
                appendLog(QStringLiteral("[提示] “%1” 需要指定地址或文件，"
                                         "请在“高级功能”页配置 Loader 与参数后执行")
                              .arg(desc));
                return;
            }
            QStringList a = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (id == QStringLiteral("btnSwitchStorage") || id == QStringLiteral("btnGetStorage")) {
                if (QRadioButton *r = currentStorage()) {
                    const QString sa = storageArg(r->text());
                    if (!sa.isEmpty())
                        a.append(sa);
                }
            }
            runCommand(job, a, desc);
        });
    }
    for (int c = 0; c < kToolCols; ++c)
        grid->setColumnStretch(c, 1);
    return grid;
}

QWidget *MainWindow::buildTabDownload()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("tabDownload"));
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(6, 8, 4, 4);
    root->setSpacing(8);

    auto *left = new QVBoxLayout;
    left->setSpacing(6);

    // 固件镜像 + 解包
    auto *rowFw = new QHBoxLayout;
    rowFw->setSpacing(4);
    rowFw->addWidget(mkLabel(QStringLiteral("固件："), 66));
    m_edImgPath = mkEdit(QStringLiteral("edDownloadFirmware"),
                          QStringLiteral("选择要下载的固件镜像 (.img)"));
    rowFw->addWidget(m_edImgPath, 1);
    auto *bFwBrowse = mkBtn(QStringLiteral("btnBrowseDownloadFw"), QStringLiteral("浏览"), 60);
    rowFw->addWidget(bFwBrowse);
    auto *bUnpack = mkBtn(QStringLiteral("btnUnpack"), QStringLiteral("解包"), 76);
    rowFw->addWidget(bUnpack);
    left->addLayout(rowFw);
    link(m_edImgPath, QStringLiteral("btnBrowseDownloadFw"), QStringLiteral("选择固件镜像"),
         QStringLiteral("固件镜像 (*.img);;所有文件 (*)"));
    connect(bUnpack, &QPushButton::clicked, this, &MainWindow::onUnpackFirmware);
    // 换镜像后旧的分区列表失效
    connect(m_edImgPath, &QLineEdit::textChanged, this, [this] {
        // 换固件后，之前解包自动填入的镜像失效；手动指定的保留
        for (ImgPart &p : m_imgParts) {
            if (p.fromUnpack) {
                p.fromUnpack = false;
                p.path.clear();
                p.size = 0;
                p.selected = false;
            }
        }
        refreshPartTable();
    });

    // 分区清单：勾选若干分区 -> 逐个刷入开发板
    auto *boxPart = new QGroupBox(QStringLiteral("分区清单（勾选要刷入开发板的分区）"), page);
    auto *pv = new QVBoxLayout(boxPart);
    pv->setContentsMargins(6, 6, 6, 6);
    m_tblImg = new QTableWidget(boxPart);
    m_tblImg->setObjectName(QStringLiteral("tblImageParts"));
    m_tblImg->setColumnCount(4);
    m_tblImg->setRowCount(0);
    m_tblImg->setMinimumHeight(150);
    m_tblImg->verticalHeader()->setVisible(false);
    m_tblImg->verticalHeader()->setDefaultSectionSize(19);
    m_tblImg->setHorizontalHeaderLabels({QStringLiteral("刷入"), QStringLiteral("分区"),
                                         QStringLiteral("镜像文件"), QStringLiteral("大小")});
    m_tblImg->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tblImg->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tblImg->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tblImg->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tblImg->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tblImg->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 双击"镜像文件"列可直接挑选该分区的镜像
    connect(m_tblImg, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c == 2)
            onPickPartFile(r);
    });
    pv->addWidget(m_tblImg);

    auto *rowSel = new QHBoxLayout;
    rowSel->setSpacing(6);
    auto *bAll = mkBtn(QStringLiteral("btnSelectAll"), QStringLiteral("全选"), 56);
    auto *bNone = mkBtn(QStringLiteral("btnUnselectAll"), QStringLiteral("全不选"), 64);
    rowSel->addWidget(bAll);
    rowSel->addWidget(bNone);
    auto *bReload = mkBtn(QStringLiteral("btnReloadParts"), QStringLiteral("重置清单"), 76);
    rowSel->addWidget(bReload);
    m_lbImgInfo = mkLabel(QStringLiteral("未指定镜像"));
    rowSel->addWidget(m_lbImgInfo, 1);
    pv->addLayout(rowSel);
    connect(bAll, &QPushButton::clicked, this, [this] { onSetAllParts(true); });
    connect(bNone, &QPushButton::clicked, this, [this] { onSetAllParts(false); });
    connect(bReload, &QPushButton::clicked, this, [this] {
        initStandardParts();
        appendLog(QStringLiteral("已重置为标准分区清单"));
    });
    left->addWidget(boxPart, 1);

    // 刷入
    auto *rowDl = new QHBoxLayout;
    rowDl->setSpacing(6);
    m_btnDownload = mkBtn(QStringLiteral("btnDownload"), QStringLiteral("下载"), 92);
    rowDl->addWidget(m_btnDownload);
    auto *lbTip = new QLabel(QStringLiteral("按勾选顺序逐个写入开发板"), page);
    lbTip->setStyleSheet(QStringLiteral("color:#707070;"));
    rowDl->addWidget(lbTip);
    rowDl->addStretch(1);
    left->addLayout(rowDl);
    connect(m_btnDownload, &QPushButton::clicked, this, &MainWindow::onDownloadSelected);

    // 右侧存储列表
    auto *right = new QGroupBox(QStringLiteral("存储"), page);
    auto *rg = new QVBoxLayout(right);
    rg->setContentsMargins(8, 6, 6, 4);
    rg->setSpacing(1);
    for (int i = 0; i < kStorages.size(); ++i) {
        auto *r = new QRadioButton(kStorages[i]);
        r->setObjectName(QStringLiteral("radioStorage_%1").arg(i));
        r->setFixedHeight(17);
        rg->addWidget(r);
    }
    rg->addStretch(1);
    right->setFixedWidth(96);
    for (QRadioButton *r : right->findChildren<QRadioButton *>())
        connect(r, &QRadioButton::toggled, this, &MainWindow::onStorageChanged);

    root->addLayout(left, 1);
    root->addWidget(right, 0);
    return page;
}

// ---------------------------------------------------------------- 升级固件

QWidget *MainWindow::buildTabUpgrade()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("tabUpgrade"));
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 10, 8, 8);
    lay->setSpacing(10);

    auto *row1 = new QHBoxLayout;
    row1->setSpacing(4);
    row1->addWidget(mkLabel(QStringLiteral("固件："), 66));
    m_edFirmware = mkEdit(QStringLiteral("edFirmware"));
    row1->addWidget(m_edFirmware, 1);
    auto *bBrowse = mkBtn(QStringLiteral("btnBrowseFirmware"), QStringLiteral("..."), 34);
    row1->addWidget(bBrowse);
    auto *bUpgrade = mkBtn(QStringLiteral("btnUpgrade"), QStringLiteral("升级"), 76);
    row1->addWidget(bUpgrade);
    auto *bSwitch = mkBtn(QStringLiteral("btnSwitchFw"), QStringLiteral("切换"), 76);
    row1->addWidget(bSwitch);
    lay->addLayout(row1);

    link(m_edFirmware, QStringLiteral("btnBrowseFirmware"), QStringLiteral("选择固件镜像"),
         QStringLiteral("固件镜像 (*.img *.bin);;所有文件 (*)"));
    connect(bUpgrade, &QPushButton::clicked, this, &MainWindow::onRun);
    connect(bSwitch, &QPushButton::clicked, this, [this] {
        QStringList a{QStringLiteral("ss")};
        if (QRadioButton *r = currentStorage()) {
            const QString sa = storageArg(r->text());
            if (!sa.isEmpty())
                a << sa;
        }
        runCommand(Upgrader::Job::TestDevice, a, QStringLiteral("切换存储"));
    });

    auto *row2 = new QHBoxLayout;
    row2->setSpacing(6);
    const char *ids[] = {"lbFwVer", "lbLoaderVer", "lbChip"};
    const char *titles[] = {"固件版本：", "Loader版本：", "芯片信息："};
    for (int i = 0; i < 3; ++i) {
        auto *box = new QGroupBox(QString::fromUtf8(titles[i]), page);
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(6, 4, 6, 4);
        auto *v = new QTextEdit(box);
        v->setObjectName(QString::fromUtf8(ids[i]));
        v->setReadOnly(true);
        v->setFixedHeight(88);
        v->setFrameStyle(QFrame::NoFrame);
        v->setStyleSheet(
            QStringLiteral("QTextEdit{background:#ffffff;border:1px solid #a0a0a0;}"));
        bl->addWidget(v);
        row2->addWidget(box, 1);
    }
    lay->addLayout(row2);
    m_lbFwVer = page->findChild<QTextEdit *>(QStringLiteral("lbFwVer"));
    m_lbLoaderVer = page->findChild<QTextEdit *>(QStringLiteral("lbLoaderVer"));
    m_lbChip = page->findChild<QTextEdit *>(QStringLiteral("lbChip"));

    auto *boxCfg = new QGroupBox(QStringLiteral("烧写选项"), page);
    auto *cg = new QGridLayout(boxCfg);
    cg->setContentsMargins(8, 6, 8, 6);
    cg->setHorizontalSpacing(6);
    cg->setVerticalSpacing(5);
    m_chkAddress = mkCheck(QStringLiteral("chkAddress"), QStringLiteral("强制按地址写"));
    cg->addWidget(m_chkAddress, 0, 0);
    auto *lbTip = new QLabel(QStringLiteral("（等效 Windows 版“高级功能 → 强制按地址写”）"),
                             boxCfg);
    lbTip->setStyleSheet(QStringLiteral("color:#707070;"));
    cg->addWidget(lbTip, 0, 1);
    cg->setColumnStretch(1, 1);
    lay->addWidget(boxCfg);
    lay->addStretch(1);
    return page;
}

// ---------------------------------------------------------------- 高级功能

QWidget *MainWindow::buildTabAdvanced()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("tabAdvanced"));
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 8, 8, 6);
    lay->setSpacing(6);

    auto *boxTbl = new QGroupBox(QStringLiteral("分区表"), page);
    auto *tv = new QVBoxLayout(boxTbl);
    tv->setContentsMargins(6, 6, 6, 6);
    m_tbl = new QTableWidget(boxTbl);
    m_tbl->setObjectName(QStringLiteral("tblPartitions"));
    m_tbl->setColumnCount(6);
    m_tbl->setRowCount(0);
    m_tbl->setFixedHeight(96);
    m_tbl->verticalHeader()->setVisible(false);
    m_tbl->verticalHeader()->setDefaultSectionSize(19);
    m_tbl->horizontalHeader()->setDefaultSectionSize(60);
    m_tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tbl->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tbl->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tbl->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tbl->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_tbl->setHorizontalHeaderLabels({QStringLiteral("#"), QString(), QStringLiteral("存储"),
                                      QStringLiteral("地址"), QStringLiteral("名字"),
                                      QStringLiteral("路径")});
    m_tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tv->addWidget(m_tbl);
    lay->addWidget(boxTbl);

    // Loader 与参数并排，省一行高度
    auto *rowFile = new QHBoxLayout;
    rowFile->setSpacing(4);
    rowFile->addWidget(mkLabel(QStringLiteral("Loader:"), 56));
    m_edLoader = mkEdit(QStringLiteral("edLoader"));
    rowFile->addWidget(m_edLoader, 1);
    auto *bL = mkBtn(QStringLiteral("btnBrowseLoader"), QStringLiteral("..."), 30);
    rowFile->addWidget(bL);
    rowFile->addSpacing(12);
    rowFile->addWidget(mkLabel(QStringLiteral("参数"), 44));
    m_edParameter = mkEdit(QStringLiteral("edParameter"));
    rowFile->addWidget(m_edParameter, 1);
    auto *bP = mkBtn(QStringLiteral("btnBrowseParameter"), QStringLiteral("..."), 30);
    rowFile->addWidget(bP);
    lay->addLayout(rowFile);
    link(m_edLoader, QStringLiteral("btnBrowseLoader"), QStringLiteral("选择 Loader"),
         QStringLiteral("Loader (*.bin);;所有文件 (*)"));
    link(m_edParameter, QStringLiteral("btnBrowseParameter"),
         QStringLiteral("选择 parameter.txt"), QStringLiteral("参数文件 (*.txt);;所有文件 (*)"));

    auto *rowBtn = new QHBoxLayout;
    rowBtn->setSpacing(4);
    auto *bExec = mkBtn(QStringLiteral("btnExec"), QStringLiteral("执行"), 62);
    auto *bSwitch = mkBtn(QStringLiteral("btnSwitchPart"), QStringLiteral("切换"), 62);
    auto *bPart = mkBtn(QStringLiteral("btnPartTable"), QStringLiteral("设备分区表"), 88);
    auto *bClear = mkBtn(QStringLiteral("btnClearTbl"), QStringLiteral("清空"), 56);
    auto *bReadSn = mkBtn(QStringLiteral("btnReadSn"), QStringLiteral("读序列号"), 76);
    auto *bSetSn = mkBtn(QStringLiteral("btnSetSn"), QStringLiteral("写序列号"), 76);
    for (QPushButton *b : {bExec, bSwitch, bPart, bClear, bReadSn, bSetSn})
        rowBtn->addWidget(b);
    rowBtn->addStretch(1);
    lay->addLayout(rowBtn);

    connect(bExec, &QPushButton::clicked, this, &MainWindow::onRun);
    connect(bSwitch, &QPushButton::clicked, this, [this] {
        QStringList a{QStringLiteral("ss")};
        if (QRadioButton *r = currentStorage()) {
            const QString sa = storageArg(r->text());
            if (!sa.isEmpty())
                a << sa;
        }
        runCommand(Upgrader::Job::TestDevice, a, QStringLiteral("切换存储"));
    });
    connect(bPart, &QPushButton::clicked, this, [this] {
        runCommand(Upgrader::Job::PartitionList, {QStringLiteral("pl")},
                   QStringLiteral("读取设备分区表"));
    });
    connect(bClear, &QPushButton::clicked, this, &MainWindow::clearPartitionTable);
    connect(bReadSn, &QPushButton::clicked, this, [this] {
        runCommand(Upgrader::Job::ReadSN, {QStringLiteral("rsn")}, QStringLiteral("读取序列号"));
    });
    connect(bSetSn, &QPushButton::clicked, this, &MainWindow::onPickSn);

    // 扇区范围 + 设备选择 + 重新枚举 合并一行
    auto *rowSec = new QHBoxLayout;
    rowSec->setSpacing(4);
    rowSec->addWidget(mkLabel(QStringLiteral("起始扇区："), 64));
    auto *edStart = mkEdit(QStringLiteral("edStartSector"));
    edStart->setFixedWidth(64);
    rowSec->addWidget(edStart);
    rowSec->addWidget(mkLabel(QStringLiteral("扇区数："), 60));
    auto *edLen = mkEdit(QStringLiteral("edSectorLen"));
    edLen->setFixedWidth(64);
    rowSec->addWidget(edLen);
    rowSec->addSpacing(10);
    rowSec->addWidget(mkLabel(QStringLiteral("设备"), 40));
    m_cbDevice = new QComboBox;
    m_cbDevice->setObjectName(QStringLiteral("cbDevice"));
    m_cbDevice->setFixedHeight(kEditH);
    m_cbDevice->setMinimumWidth(120);
    rowSec->addWidget(m_cbDevice, 1);
    auto *bRefresh = mkBtn(QStringLiteral("btnRefresh"), QStringLiteral("重新枚举"), 80);
    rowSec->addWidget(bRefresh);
    connect(bRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshDevices);
    lay->addLayout(rowSec);

    lay->addLayout(buildDeviceTools(page));
    lay->addStretch(1);
    return page;
}

// ---------------------------------------------------------------- 菜单

void MainWindow::setupMenus()
{
    QMenu *mDev = menuBar()->addMenu(QStringLiteral("设备"));
    auto *aEnum = mDev->addAction(QStringLiteral("枚举设备"));
    connect(aEnum, &QAction::triggered, this, &MainWindow::onRefreshDevices);
    m_actions.append(aEnum);
    mDev->addSeparator();
    auto *aPart = mDev->addAction(QStringLiteral("读取分区表"));
    connect(aPart, &QAction::triggered, this, [this] {
        runCommand(Upgrader::Job::PartitionList, {QStringLiteral("pl")},
                   QStringLiteral("读取设备分区表"));
    });
    m_actions.append(aPart);
    auto *aChip = mDev->addAction(QStringLiteral("读取 Flash 信息"));
    connect(aChip, &QAction::triggered, this, &MainWindow::autoReadDeviceInfo);
    m_actions.append(aChip);

    QMenu *mTool = menuBar()->addMenu(QStringLiteral("工具"));
    auto *aClear = mTool->addAction(QStringLiteral("清空日志"), this, &MainWindow::onClearLog);
    m_actions.append(aClear);
    auto *aSave = mTool->addAction(QStringLiteral("保存配置"), this, &MainWindow::onSaveConfig);
    m_actions.append(aSave);
    auto *aLoad = mTool->addAction(QStringLiteral("加载配置"), this, &MainWindow::onLoadConfig);
    m_actions.append(aLoad);
    mTool->addSeparator();
    auto *aPriv = mTool->addAction(QStringLiteral("使用管理员权限运行"));
    aPriv->setCheckable(true);
    connect(aPriv, &QAction::toggled, this, [this](bool on) {
        m_upgrader->setUsePrivilege(on);
        appendLog(on ? QStringLiteral("[权限] 已启用管理员权限模式（pkexec）")
                     : QStringLiteral("[权限] 已关闭管理员权限模式"));
    });
    m_actions.append(aPriv);

    QMenu *mHelp = menuBar()->addMenu(QStringLiteral("帮助"));
    auto *aAbout = mHelp->addAction(QStringLiteral("关于"));
    connect(aAbout, &QAction::triggered, this, [this] {
        if (m_selftest)
            return;
        QMessageBox::about(
            this, QStringLiteral("关于"),
            QStringLiteral("<h3>瑞芯微开发工具 v%1 for Linux</h3>"
                           "<p>Windows 版 RKDevTool 桌面复刻版。<br>后端：Rockchip 官方 "
                           "upgrade_tool</p>")
                .arg(QStringLiteral(RKDEVTOOL_VERSION)));
    });
    m_actions.append(aAbout);
}

// ---------------------------------------------------------------- 事件

void MainWindow::onDeviceChanged()
{
    const QVector<RockchipDevice> &devs = m_scanner->devices();
    const QString prev = m_cbDevice->currentText();
    m_cbDevice->blockSignals(true);
    m_cbDevice->clear();
    for (const RockchipDevice &d : devs) {
        m_cbDevice->addItem(QStringLiteral("%1 - %2 - %3")
                                .arg(QString::number(d.index + 1))
                                .arg(d.type)
                                .arg(d.serial.isEmpty() ? QStringLiteral("-") : d.serial),
                            QString::number(d.index));
    }
    const int i = m_cbDevice->findText(prev);
    if (i >= 0)
        m_cbDevice->setCurrentIndex(i);
    m_cbDevice->setEnabled(devs.size() > 1);
    m_cbDevice->blockSignals(false);

    setStatus(m_scanner->statusText());
    setDeviceSerial(devs.isEmpty() ? QString() : devs.first().serial);
    setStageText(devs.isEmpty() ? QStringLiteral("未检测到设备") : devs.first().describe());
    if (devs.isEmpty())
        m_progress->setValue(0);

    if (m_scanner->denied() && !devs.isEmpty() && !m_loggedDenied) {
        m_loggedDenied = true;
        appendLog(QStringLiteral("[权限] 当前用户无 USB 访问权限，可在“工具”中勾选"
                                 "“使用管理员权限运行”或安装 udev 规则。"));
    } else if (!m_scanner->denied()) {
        m_loggedDenied = false;
    }

    if (m_scanner->hasTransferProblem() && !m_loggedTransfer) {
        m_loggedTransfer = true;
        appendLog(QStringLiteral("[USB] 设备已枚举但无法建立传输通道（libusb 错误码 %1）。")
                      .arg(devs.isEmpty() ? -1 : devs.first().claimError));
        appendLog(QStringLiteral("      多为虚拟机 USB 直通使用 USB 3.0：把 USB 控制器改为 "
                                 "USB 2.0，或在虚拟机菜单中先断开再连接设备。"));
        if (!m_selftest) {
            QMessageBox::warning(
                this, QStringLiteral("USB 传输不可用"),
                QStringLiteral("已检测到 Rockchip 设备，但 USB 传输通道建立失败。\n\n"
                               "通常是虚拟机 USB 直通使用 USB 3.0 导致。\n\n"
                               "1) 将虚拟机 USB 控制器改为 USB 2.0 后重启虚拟机；\n"
                               "2) 重新插拔设备 USB 线；\n"
                               "3) 状态栏正常显示且日志无报错后即可烧写。"));
        }
    } else if (!m_scanner->hasTransferProblem()) {
        m_loggedTransfer = false;
    }

    refreshEnabled();
    if (!devs.isEmpty() && !m_infoReading && !m_upgrader->busy())
        QTimer::singleShot(600, this, &MainWindow::autoReadDeviceInfo);
}

void MainWindow::onRefreshDevices()
{
    m_scanner->scanOnce();
    appendLog(QStringLiteral("已重新枚举 USB 设备：%1 个").arg(m_scanner->total()));
}

void MainWindow::onStorageChanged()
{
    if (QRadioButton *r = currentStorage())
        m_cfg.storage = r->text();
}

void MainWindow::autoReadDeviceInfo()
{
    if (m_scanner->total() == 0 || m_upgrader->busy() || m_infoReading || m_selftest)
        return;
    m_infoReading = true;
    m_lbChip->setPlainText(QStringLiteral("读取中…"));
    // rci 只输出原始字节，rfi 才有可读的 Flash 信息
    runCommand(Upgrader::Job::ReadFlashInfo, {QStringLiteral("rfi")},
               QStringLiteral("读取 Flash 信息"));
}

void MainWindow::onLogLine(const QString &text)
{
    appendLog(text);
    if (text.contains(QStringLiteral("Loader Version:"))
        || text.contains(QStringLiteral("Firmware Version:"))) {
        const QString keys[]{QStringLiteral("Loader Version:"), QStringLiteral("Firmware Version:")};
        for (const QString &k : keys) {
            const int i = text.indexOf(k);
            if (i < 0)
                continue;
            const QString v = text.mid(i + k.length()).trimmed();
            if (v.isEmpty())
                continue;
            if (k.startsWith(QLatin1String("Loader")))
                m_lbLoaderVer->setPlainText(v);
            else
                m_lbFwVer->setPlainText(v);
        }
    }
    // rfi 输出形如 "\tManufacturer: SAMSUNG,value=00" / "\tFlash Size: 14910MB"
    // 累积到“芯片信息”框，顺序与 Win 版一致
    struct KV { const char *key; const char *label; };
    static const KV kv[]{ {"Manufacturer:", "厂商："},   {"Flash Size:", "容量："},
                          {"Block Size:", "块大小："},   {"Page Size:", "页大小："},
                          {"ECC Bits:", "ECC 位数："},  {"Access Time:", "访问时间："} };
    for (const KV &k : kv) {
        const QString key = QString::fromUtf8(k.key);
        const int i = text.indexOf(key);
        if (i < 0)
            continue;
        QString v = text.mid(i + key.length()).trimmed();
        v.remove(QStringLiteral(",value=00"));
        if (v.isEmpty())
            continue;
        const QString entry = QString::fromUtf8(k.label) + v;
        QStringList lines = m_lbChip->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.contains(QStringLiteral("读取中…")))
            lines.clear();
        if (!lines.contains(entry))
            lines << entry;
        m_lbChip->setPlainText(lines.join(QLatin1Char('\n')));
    }

    // pl 输出形如 "01  0x00002000 0x00002000 security"，填入高级功能页表格
    static const QRegularExpression row(
        QStringLiteral("^(\\d{2})\\s+(0x[0-9a-fA-F]+)\\s+(0x[0-9a-fA-F]+)\\s+(\\S+)"));
    const QRegularExpressionMatch mm = row.match(text.trimmed());
    if (!mm.hasMatch())
        return;
    const int rowNo = mm.captured(1).toInt();
    if (rowNo < 1 || !m_tbl)
        return;
    const int r = rowNo - 1;
    if (m_tbl->rowCount() <= r)
        m_tbl->setRowCount(r + 1);
    m_tbl->setItem(r, 0, new QTableWidgetItem(mm.captured(1)));
    if (!m_tbl->item(r, 1)) {
        auto *chk = new QTableWidgetItem;
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        chk->setCheckState(Qt::Checked);
        m_tbl->setItem(r, 1, chk);
    }
    m_tbl->setItem(r, 2, new QTableWidgetItem(currentStorage() ? currentStorage()->text()
                                                               : QString()));
    m_tbl->setItem(r, 3, new QTableWidgetItem(mm.captured(2)));
    m_tbl->setItem(r, 4, new QTableWidgetItem(mm.captured(4)));
}

void MainWindow::appendLog(const QString &text)
{
    if (!m_log)
        return;
    m_log->appendPlainText(text);
    if (m_log->document()->maximumBlockCount() == 0)
        m_log->document()->setMaximumBlockCount(4000);
}

void MainWindow::onPercentChanged(int percent)
{
    if (m_stepIndex < 0 || m_stepIndex >= m_steps.size()) {
        m_progress->setValue(percent);
        return;
    }
    const Step &s = m_steps.at(m_stepIndex);
    const int span = qMax(1, s.end - s.begin);
    m_progress->setValue(s.begin + span * percent / 100);
    setStageText(QStringLiteral("%1...%2%").arg(s.progressText).arg(percent));
}

void MainWindow::onBusyChanged(bool busy)
{
    m_scanner->setPassive(busy);
    refreshEnabled();
}

void MainWindow::onJobFinished(bool ok, const QString &summary)
{
    if (!m_selftestCmd.isEmpty()) {
        QTextStream o(stdout);
        o << "=== CMD TEST [" << m_selftestCmd << "] ok=" << (ok ? 1 : 0)
          << " summary=" << summary << "\n--- log ---\n"
          << m_log->toPlainText() << "\n=== END ===\n";
        o.flush();
        const QString done = m_selftestCmd;
        m_selftestCmd.clear();
        QTimer::singleShot(0, this, [this, done] { runTestCommand(done + QStringLiteral(" #done")); });
        return;
    }
    appendLog(QStringLiteral("==== %1 ====").arg(summary));
    setStageText(summary);
    if (ok)
        m_progress->setValue(100);
    m_infoReading = false;

    if (m_stepIndex < 0)
        return;
    if (!ok)
        m_runOk = false;
    if (m_runOk && m_advancing) {
        m_advancing = false;
        runNextStep();
        return;
    }
    m_steps.clear();
    m_stepIndex = -1;
    m_advancing = false;
    if (!ok)
        m_progress->setValue(0);
}

void MainWindow::refreshEnabled()
{
    const bool busy = m_upgrader->busy();
    for (QPushButton *b : m_buttons)
        b->setEnabled(!busy);
    for (QAction *a : m_actions)
        a->setEnabled(!busy);
}

void MainWindow::setStatus(const QString &text)
{
    m_statusDevice->setText(text);
}

void MainWindow::setDeviceSerial(const QString &text)
{
    m_statusSerial->setText(text);
}

void MainWindow::setStageText(const QString &text)
{
    m_statusText->setText(text);
}

bool MainWindow::toolUsable() const
{
    return m_upgrader->toolAvailable();
}

bool MainWindow::requireDevice()
{
    if (m_selftest)
        return m_scanner->total() > 0;
    if (m_scanner->total() == 0) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("没有发现 Rockchip 设备。\n\n"
                                            "请让设备进入 Maskrom 或 Loader 模式后重新插入 USB 线，"
                                            "状态栏会实时显示检测结果。"));
        return false;
    }
    return true;
}

void MainWindow::runCommand(Upgrader::Job job, const QStringList &args, const QString &desc)
{
    if (m_upgrader->busy())
        return;
    if (!toolUsable()) {
        appendLog(QStringLiteral("[错误] 未找到 upgrade_tool"));
        return;
    }
    m_steps.clear();
    m_stepIndex = -1;
    m_advancing = false;
    m_progress->setValue(0);
    setStageText(desc + QStringLiteral("…"));
    m_upgrader->run(job, args, desc);
}

// ---------------------------------------------------------------- 烧写

void MainWindow::collectUpgradeSteps(QVector<Step> &steps, QString &err)
{
    steps.clear();
    const QString storage = currentStorage() ? currentStorage()->text() : QStringLiteral("EMMC");

    const QString fw = m_edFirmware->text().trimmed();
    const QString ld = m_edLoader->text().trimmed();
    const QString par = m_edParameter->text().trimmed();

    if (fw.isEmpty() && ld.isEmpty() && par.isEmpty()) {
        err = QStringLiteral("请先选择固件镜像文件");
        return;
    }
    for (const QString &p : {fw, ld, par}) {
        if (!p.isEmpty() && !QFileInfo(p).isFile()) {
            err = QStringLiteral("文件不存在：%1").arg(p);
            return;
        }
    }

    int total = (ld.isEmpty() ? 0 : 1) + (par.isEmpty() ? 0 : 1) + (fw.isEmpty() ? 0 : 1);
    int i = 0;
    if (!ld.isEmpty()) {
        Step s;
        s.job = Upgrader::Job::WriteLoader;
        s.args << QStringLiteral("ul") << ld;
        const QString sa = storageArg(storage);
        if (!sa.isEmpty())
            s.args << sa;
        s.desc = QStringLiteral("升级 Loader");
        s.progressText = QStringLiteral("正在升级 Loader");
        s.begin = i * 100 / total;
        s.end = (i + 1) * 100 / total;
        steps.append(s);
        ++i;
    }
    if (!par.isEmpty()) {
        Step s;
        s.job = Upgrader::Job::WriteParameter;
        s.args << QStringLiteral("di") << QStringLiteral("-p") << par;
        s.desc = QStringLiteral("写入分区表参数");
        s.progressText = QStringLiteral("正在写入参数");
        s.begin = i * 100 / total;
        s.end = (i + 1) * 100 / total;
        steps.append(s);
        ++i;
    }
    if (!fw.isEmpty()) {
        Step s;
        s.job = Upgrader::Job::WriteFirmwareFull;
        s.args << QStringLiteral("uf") << fw;
        s.desc = QStringLiteral("升级固件");
        s.progressText = QStringLiteral("正在下载固件");
        s.begin = i * 100 / total;
        s.end = 100;
        steps.append(s);
    }
}

void MainWindow::onRun()
{
    if (m_upgrader->busy()) {
        if (!m_selftest
            && QMessageBox::question(this, QStringLiteral("提示"),
                                     QStringLiteral("操作正在进行，确定要中止吗？"))
                    == QMessageBox::Yes) {
            m_runOk = false;
            m_advancing = false;
            m_upgrader->abort();
        }
        return;
    }
    if (!toolUsable()) {
        appendLog(QStringLiteral("[测试] upgrade_tool 不可用"));
        if (!m_selftest)
            QMessageBox::critical(
                this, QStringLiteral("错误"),
                QStringLiteral("找不到 Rockchip 官方工具 upgrade_tool。\n\n"
                               "请将 Linux_Upgrade_Tool 目录放到 ~/flash/ 下，"
                               "或设置 RKDEVTOOL_UPGRADE_TOOL 环境变量。"));
        return;
    }
    if (!requireDevice())
        return;

    QVector<Step> steps;
    QString err;
    collectUpgradeSteps(steps, err);
    if (!err.isEmpty()) {
        appendLog(QStringLiteral("[错误] %1").arg(err));
        if (!m_selftest)
            QMessageBox::warning(this, QStringLiteral("错误"), err);
        return;
    }
    if (m_scanner->denied() && !m_upgrader->usePrivilege() && !m_selftest) {
        if (QMessageBox::question(this, QStringLiteral("权限不足"),
                                  QStringLiteral("当前用户无权访问 Rockchip USB 设备。\n\n"
                                                 "是否使用管理员权限（pkexec）执行本次操作？"),
                                  QMessageBox::Yes | QMessageBox::No)
            == QMessageBox::Yes)
            m_upgrader->setUsePrivilege(true);
        else
            return;
    }
    onSaveConfig();
    startSteps(steps);
}

void MainWindow::startSteps(const QVector<Step> &steps)
{
    m_steps = steps;
    m_stepIndex = -1;
    m_runOk = true;
    m_advancing = false;
    runNextStep();
}

void MainWindow::runNextStep()
{
    ++m_stepIndex;
    if (m_stepIndex >= m_steps.size()) {
        m_stepIndex = -1;
        m_advancing = false;
        return;
    }
    const Step &s = m_steps.at(m_stepIndex);
    // 仅当还有后续步骤时才需要链式推进
    m_advancing = m_stepIndex < m_steps.size() - 1;
    appendLog(QStringLiteral("==== 步骤 %1/%2：%3 ====")
                  .arg(m_stepIndex + 1)
                  .arg(m_steps.size())
                  .arg(s.desc));
    m_progress->setValue(s.begin);
    setStageText(QStringLiteral("%1...").arg(s.progressText));
    m_upgrader->run(s.job, s.args, s.desc);
}

// ---------------------------------------------------------------- 其他

void MainWindow::onPickFirmware()
{
    if (m_selftest)
        return;
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择固件镜像"), QString(),
        QStringLiteral("固件镜像 (*.img *.bin);;所有文件 (*)"));
    if (!p.isEmpty())
        m_edFirmware->setText(p);
}

void MainWindow::onPickSn()
{
    if (m_selftest)
        return;
    bool ok = false;
    const QString sn = QInputDialog::getText(
        this, QStringLiteral("写入序列号"), QStringLiteral("请输入序列号（最长 31 字符）"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || sn.trimmed().isEmpty())
        return;
    runCommand(Upgrader::Job::ReadSN, {QStringLiteral("sn"), sn.trimmed()},
               QStringLiteral("写入序列号"));
}

// 解包固件镜像：扫描 Rockchip 分区头，并把每个分区导出成独立文件。
// upgrade_tool 的 di 每次只能写一个分区，所以必须先拆成单独文件。
// 镜像由若干 4KB 对齐的分区块组成，每块头部为:
//   offset 0x00: 4 字节 magic(LOAD/UBOO/TRUS/KERN/SYST/RECO/MISC/PARM/BOOT/RSRC/ROOT)
//   offset 0x04: 4 字节大端长度
void MainWindow::onUnpackFirmware()
{
    const QString path = m_edImgPath ? m_edImgPath->text().trimmed() : QString();
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        appendLog(QStringLiteral("[提示] 请先选择要解包的固件镜像"));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("[错误] 无法打开固件镜像：%1").arg(f.errorString()));
        return;
    }

    // tag -> (显示名, di 参数)。di 参数为空表示该分区不能单独刷入
    // magic 为 Rockchip 的 4 字节标识，不是完整单词（"UBOO" 即 U-Boot）
    struct TagMap { const char *tag; const char *name; const char *flag; };
    static const TagMap map[]{
        {"LOAD", "Loader",    ""},     // 走 ul，不属于 di
        {"UBOO", "U-Boot",    "-u"},
        {"TRUS", "Trust",     "-t"},
        {"KERN", "Kernel",    "-k"},
        {"SYST", "System",    "-s"},
        {"RECO", "Recovery",  "-r"},
        {"MISC", "Misc",      "-m"},
        {"PARM", "Parameter", "-p"},
        {"BOOT", "Boot",      "-b"},
        {"RSRC", "Resource",  ""},
        {"ROOT", "Rootfs",    ""},
    };

    // 导出目录：~/.cache/rkdevtool/unpack
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_unpackDir = base + QStringLiteral("/unpack");
    QDir(m_unpackDir).removeRecursively();
    if (!QDir().mkpath(m_unpackDir)) {
        appendLog(QStringLiteral("[错误] 无法创建解包目录：%1").arg(m_unpackDir));
        return;
    }

    const qint64 fileSize = f.size();
    QStringList skipped;
    QVector<ImgPart> found;
    qint64 totalSize = 0;
    int usable = 0;

    appendLog(QStringLiteral("解包固件：%1（%2 字节）")
                  .arg(QFileInfo(path).fileName()).arg(fileSize));
    appendLog(QStringLiteral("  导出目录：%1").arg(m_unpackDir));

    // 按 4KB 块边界遍历：只有块首才是分区头，避免命中分区内容里的同名字符串
    // （例如 "UBOOT" 内含 "BOOT"）
    constexpr qint64 kBlock = 4096;
    constexpr qint64 kHdrSize = 8;
    const qint64 kMaxBlock = 4LL * 1024 * 1024 * 1024; // 单个分区 4GB 上限
    for (qint64 off = 0; off + kHdrSize <= fileSize; off += kBlock) {
        if (!f.seek(off))
            break;
        const QByteArray magic = f.read(4);
        if (magic.size() != 4)
            break;
        const QByteArray lenRaw = f.read(4);
        if (lenRaw.size() != 4)
            break;
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(lenRaw.constData()));
        if (len < kHdrSize || qint64(len) > kMaxBlock)
            continue; // 不是分区头，继续下一个块

        // 查 tag 对应的名称与 di 参数
        const TagMap *hit = nullptr;
        for (const TagMap &tm : map) {
            if (magic == QByteArray(tm.tag)) {
                hit = &tm;
                break;
            }
        }
        ImgPart p;
        p.tag = QString::fromLatin1(magic);
        p.name = hit ? QString::fromLatin1(hit->name) : p.tag;
        p.flag = hit ? QString::fromLatin1(hit->flag) : QString();
        p.offset = off;
        p.size = len;
        if (p.flag.isEmpty())
            skipped << p.name;
        else {
            ++usable;
            totalSize += len;
        }
        found.append(p);

        // 跳到该分区之后（长度向上对齐到 4KB）
        const qint64 next = (off + len + kBlock - 1) / kBlock * kBlock;
        if (next <= off)
            break;
        off = next - kBlock;
    }

    if (found.isEmpty()) {
        appendLog(QStringLiteral("  未识别到 Rockchip 分区头，该文件可能不是固件镜像"));
        appendLog(QStringLiteral("  RKFW 整包需用“升级固件”页整包刷入（uf）"));
        if (m_lbImgInfo)
            m_lbImgInfo->setText(QStringLiteral("解包失败：未识别分区"));
        refreshPartTable();
        return;
    }

    // 导出可刷入的分区
    for (ImgPart &p : found) {
        if (p.flag.isEmpty())
            continue;
        p.path = m_unpackDir + QLatin1Char('/') + p.tag.toLower() + QStringLiteral(".img");
        if (!f.seek(p.offset)) {
            appendLog(QStringLiteral("  [失败] 定位 %1 失败").arg(p.name));
            p.path.clear();
            continue;
        }
        QFile out(p.path);
        if (!out.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("  [失败] 写入 %1 失败：%2").arg(p.name, out.errorString()));
            p.path.clear();
            continue;
        }
        qint64 left = p.size;
        const qint64 bufSize = 4 * 1024 * 1024;
        QByteArray buf;
        buf.resize(int(bufSize));
        while (left > 0) {
            const qint64 want = qMin(bufSize, left);
            const qint64 got = f.read(buf.data(), want);
            if (got <= 0)
                break;
            out.write(buf.constData(), got);
            left -= got;
        }
        out.close();
        if (left > 0) {
            appendLog(QStringLiteral("  [失败] %1 只导出 %2/%3 字节")
                          .arg(p.name).arg(p.size - left).arg(p.size));
            p.path.clear();
            continue;
        }
        appendLog(QStringLiteral("  导出 %1  %2 字节  ->  %3")
                      .arg(p.name, -8)
                      .arg(p.size)
                      .arg(QFileInfo(p.path).fileName()));
    }

    // 合并进标准清单：已存在的分区改写文件路径并勾选，没有的行追加到末尾
    for (const ImgPart &fp : found) {
        if (fp.flag.isEmpty() || fp.path.isEmpty())
            continue;
        int at = -1;
        for (int i = 0; i < m_imgParts.size(); ++i) {
            if (m_imgParts.at(i).tag == fp.tag) {
                at = i;
                break;
            }
        }
        if (at < 0) {
            ImgPart np = fp;
            np.selected = true;
            np.fromUnpack = true;
            m_imgParts.append(np);
        } else {
            m_imgParts[at].path = fp.path;
            m_imgParts[at].size = fp.size;
            m_imgParts[at].offset = fp.offset;
            m_imgParts[at].fromUnpack = true;
            m_imgParts[at].selected = true;
        }
    }
    appendLog(QStringLiteral("  共识别 %1 个分区，其中 %2 个可单独刷入，合计 %3 KB")
                  .arg(found.size()).arg(usable).arg((totalSize + 1023) / 1024));
    if (!skipped.isEmpty())
        appendLog(QStringLiteral("  不可单独刷入（需整包 uf）：%1").arg(skipped.join(QStringLiteral("、"))));

    m_lbFwVer->setPlainText(QStringLiteral("%1\n分区 %2 个\n可刷入 %3 个")
                                .arg(QFileInfo(path).fileName())
                                .arg(found.size()).arg(usable));
    setStageText(QStringLiteral("解包完成：%1 个分区可刷入").arg(usable));
    refreshPartTable();
}

// 初始化 Rockchip 标准分区清单，用户勾选并指定镜像后即可刷入
void MainWindow::initStandardParts()
{
    struct Def { const char *tag; const char *name; const char *flag; };
    static const Def defs[]{
        {"LOAD", "Loader",   "ul"},   // Loader 走 ul
        {"UBOO", "U-Boot",   "-u"},
        {"TRUS", "Trust",    "-t"},
        {"KERN", "Kernel",   "-k"},
        {"SYST", "System",   "-s"},
        {"RECO", "Recovery", "-r"},
        {"MISC", "Misc",     "-m"},
        {"PARM", "Parameter","-p"},
        {"BOOT", "Boot",     "-b"},
    };
    m_imgParts.clear();
    for (const Def &d : defs) {
        ImgPart p;
        p.tag = QString::fromLatin1(d.tag);
        p.name = QString::fromLatin1(d.name);
        p.flag = QString::fromLatin1(d.flag);
        m_imgParts.append(p);
    }
    refreshPartTable();
}

// 为某一行挑选镜像文件
void MainWindow::onPickPartFile(int row)
{
    if (row < 0 || row >= m_imgParts.size())
        return;
    const QString name = m_imgParts.at(row).name;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 %1 分区镜像").arg(name), QString(),
        QStringLiteral("镜像文件 (*.img *.bin *.tar);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    ImgPart &p = m_imgParts[row];
    p.path = path;
    p.size = QFileInfo(path).size();
    p.fromUnpack = false;
    if (!p.selected) {
        p.selected = true; // 选了文件就默认勾选，符合直觉
    }
    refreshPartTable();
    appendLog(QStringLiteral("[选择] %1 镜像：%2（%3 字节）")
                  .arg(p.name, QFileInfo(path).fileName()).arg(p.size));
}

void MainWindow::refreshPartTable()
{
    if (!m_tblImg)
        return;
    m_tblImg->setRowCount(m_imgParts.size());
    int checked = 0;
    for (int r = 0; r < m_imgParts.size(); ++r) {
        const ImgPart &p = m_imgParts.at(r);
        if (p.selected)
            ++checked;

        auto *chk = new QTableWidgetItem(p.flag);
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        chk->setCheckState(p.selected ? Qt::Checked : Qt::Unchecked);
        chk->setToolTip(QStringLiteral("双击“镜像文件”列可更换该分区的镜像"));
        m_tblImg->setItem(r, 0, chk);

        m_tblImg->setItem(r, 1, new QTableWidgetItem(p.name));

        const QString fileText = p.path.isEmpty()
                                     ? kNoFileHint
                                     : p.path;
        auto *fitem = new QTableWidgetItem(fileText);
        if (p.path.isEmpty())
            fitem->setForeground(QColor(0x90, 0x90, 0x90));
        else
            fitem->setForeground(QColor(0x20, 0x60, 0x20));
        fitem->setToolTip(p.path);
        m_tblImg->setItem(r, 2, fitem);

        m_tblImg->setItem(r, 3,
                          new QTableWidgetItem(p.size > 0
                                                   ? QStringLiteral("%1 KB").arg((p.size + 1023) / 1024)
                                                   : QString()));
    }
    if (m_lbImgInfo) {
        m_lbImgInfo->setText(QStringLiteral("已勾选 %1 / %2 个分区")
                                 .arg(checked).arg(m_imgParts.size()));
    }
}

void MainWindow::onSetAllParts(bool checked)
{
    for (int r = 0; r < m_imgParts.size(); ++r) {
        // 只有已指定镜像文件的分区才允许勾选
        if (!m_imgParts[r].path.isEmpty())
            m_imgParts[r].selected = checked;
    }
    refreshPartTable();
}

// 把勾选的分区按 Loader -> U-Boot -> Trust -> Kernel -> ... 的顺序排成 di 步骤
void MainWindow::onDownloadSelected()
{
    if (m_upgrader->busy())
        return;
    if (m_imgParts.isEmpty()) {
        appendLog(QStringLiteral("[提示] 请先点“读取分区”载入分区清单"));
        if (!m_selftest)
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先点“读取分区”载入分区清单。"));
        return;
    }
    if (!toolUsable()) {
        appendLog(QStringLiteral("[错误] 未找到 upgrade_tool"));
        return;
    }
    if (!requireDevice())
        return;

    QVector<Step> steps;
    for (int i = 0; i < m_imgParts.size(); ++i) {
        const ImgPart &p = m_imgParts.at(i);
        if (!p.selected)
            continue;
        if (p.path.isEmpty() || !QFileInfo(p.path).isFile()) {
            appendLog(QStringLiteral("[跳过] %1 尚未指定有效的镜像文件").arg(p.name));
            continue;
        }
        Step s;
        s.job = Upgrader::Job::WriteImage;
        // Loader 用 ul，其余用 di <flag>
        s.args << (p.flag == QLatin1String("ul") ? QStringLiteral("ul") : QStringLiteral("di"))
               << (p.flag == QLatin1String("ul") ? p.path : p.flag) ;
        if (p.flag != QLatin1String("ul"))
            s.args << p.path;
        s.desc = QStringLiteral("下载 %1").arg(p.name);
        s.progressText = QStringLiteral("正在写入 %1").arg(p.name);
        steps.append(s);
    }
    if (steps.isEmpty()) {
        appendLog(QStringLiteral("[提示] 没有勾选任何已指定镜像的分区"));
        if (!m_selftest)
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请至少勾选一个分区，并为它指定镜像文件。\n\n"
                                                    "双击“镜像文件”列即可选择。"));
        return;
    }

    for (int i = 0; i < steps.size(); ++i) {
        steps[i].begin = i * 100 / steps.size();
        steps[i].end = (i + 1) * 100 / steps.size();
    }
    if (m_scanner->denied() && !m_upgrader->usePrivilege() && !m_selftest) {
        if (QMessageBox::question(this, QStringLiteral("权限不足"),
                                  QStringLiteral("当前用户无权访问 Rockchip USB 设备。\n\n"
                                                 "是否使用管理员权限（pkexec）执行本次操作？"),
                                  QMessageBox::Yes | QMessageBox::No)
            == QMessageBox::Yes)
            m_upgrader->setUsePrivilege(true);
        else
            return;
    }
    if (m_selftestCmd.contains(QLatin1String("plan"))) {
        // 只打印执行计划，不真正写入设备
        QTextStream o(stdout);
        o << "=== DL PLAN (" << steps.size() << " steps) ===\n";
        for (int i = 0; i < steps.size(); ++i)
            o << i + 1 << ". " << steps.at(i).args.join(QLatin1Char(' ')) << "   # "
              << steps.at(i).desc << "\n";
        o << "=== END ===\n";
        o.flush();
        return;
    }
    appendLog(QStringLiteral("开始刷入 %1 个分区").arg(steps.size()));
    startSteps(steps);
}

void MainWindow::refreshImgTable()
{
    refreshPartTable();
}

void MainWindow::clearPartitionTable()
{
    if (m_tbl)
        m_tbl->setRowCount(0);
    appendLog(QStringLiteral("分区表已清空"));
}

void MainWindow::onSaveConfig()
{
    m_cfg.firmware = m_edFirmware->text();
    m_cfg.loader = m_edLoader->text();
    m_cfg.parameter = m_edParameter->text();
    m_cfg.forceByAddress = m_chkAddress->isChecked();
    if (QRadioButton *r = currentStorage())
        m_cfg.storage = r->text();
    m_cfg.mode = m_tabs->currentIndex() == 0 ? QStringLiteral("download")
                                             : QStringLiteral("write");
    m_cfg.advanced = m_tabs->currentIndex() == 2;
    Paths::saveConfig(m_cfg);
}

void MainWindow::onLoadConfig()
{
    m_cfg = Paths::loadConfig();
    m_edFirmware->setText(m_cfg.firmware);
    m_edLoader->setText(m_cfg.loader);
    m_edParameter->setText(m_cfg.parameter);
    m_chkAddress->setChecked(m_cfg.forceByAddress);
    appendLog(QStringLiteral("配置已载入：%1").arg(Paths::configFile()));
}

void MainWindow::onClearLog()
{
    m_log->clear();
    setStageText(QStringLiteral("日志已清空"));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_upgrader->busy()) {
        if (!m_selftest
            && QMessageBox::question(this, QStringLiteral("提示"),
                                     QStringLiteral("烧写操作仍在进行中，确定退出吗？"))
                    != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_upgrader->abort();
    }
    onSaveConfig();
    e->accept();
}

// ---------------------------------------------------------------- 自测

// 按名字切页签，供启动参数 --page=advanced 之类使用
bool MainWindow::showPage(const QString &name)
{
    static const QMap<QString, int> names{
        {QStringLiteral("download"), 0},
        {QStringLiteral("upgrade"), 1},
        {QStringLiteral("advanced"), 2},
    };
    const int idx = names.value(name.trimmed().toLower(), -1);
    if (idx < 0 || idx >= m_tabs->count())
        return false;
    m_tabs->setCurrentIndex(idx);
    return true;
}

void MainWindow::dumpSelfTest()
{
    QTextStream o(stdout);
    o.setCodec("UTF-8");
    o << "=== SELFTEST ===\n";
    o << "title: " << windowTitle() << "  client=" << width() << "x" << height() << "\n";
    o << "tabs:";
    for (int i = 0; i < m_tabs->count(); ++i)
        o << " [" << m_tabs->tabText(i) << "]";
    o << "  current=" << m_tabs->tabText(m_tabs->currentIndex()) << "\n";
    o << "statusbar: device=[" << m_statusDevice->text() << "] serial=[" << m_statusSerial->text()
      << "] stage=[" << m_statusText->text() << "] progress=" << m_progress->value() << "%\n";
    o << "firmware='" << m_edFirmware->text() << "'\n";
    o << "loader='" << m_edLoader->text() << "' parameter='" << m_edParameter->text() << "'\n";
    o << "info: fwver='" << m_lbFwVer->toPlainText() << "' loaderver='"
      << m_lbLoaderVer->toPlainText() << "' chip='" << m_lbChip->toPlainText() << "'\n";

    // 分区表单元格内容
    // 下载镜像页的分区清单
    o << "imgparts:";
    if (m_tblImg) {
        for (int r = 0; r < m_tblImg->rowCount(); ++r) {
            QTableWidgetItem *ck = m_tblImg->item(r, 0);
            QTableWidgetItem *nm = m_tblImg->item(r, 1);
            QTableWidgetItem *fp = m_tblImg->item(r, 2);
            o << " [" << (ck ? ck->text() : QStringLiteral("?"))
              << (ck && ck->checkState() == Qt::Checked ? "*" : " ") << "|"
              << (nm ? nm->text() : QString()) << "|"
              << (fp && fp->text() != kNoFileHint ? QStringLiteral("有镜像")
                                                   : QStringLiteral("无镜像"))
              << "]";
        }
        o << "  rows=" << m_tblImg->rowCount();
    }
    o << "\n";
    // 直接校验程序实际在用的字体是否含中文字形，避免豆腐块
    const QString probe = QStringLiteral("瑞芯微开发工具固件下载镜像分区勾选刷入");
    auto missingIn = [&probe](const QFont &f) {
        const QRawFont raw = QRawFont::fromFont(f);
        if (!raw.isValid())
            return -1;
        int miss = 0;
        for (const QChar c : probe)
            if (!raw.supportsCharacter(c))
                ++miss;
        return miss;
    };
    const QFont uiF = QApplication::font();
    const QFont lgF = m_log->font();
    o << "font: ui='" << uiF.family() << "' missing=" << missingIn(uiF) << "/" << probe.size()
      << "  log='" << lgF.family() << "' missing=" << missingIn(lgF) << "/" << probe.size() << "  "
      << (missingIn(uiF) == 0 ? QStringLiteral("中文OK") : QStringLiteral("中文乱码")) << "\n";

    o << "parttable:";
    if (m_tbl) {
        for (int r = 0; r < m_tbl->rowCount(); ++r) {
            QStringList cells;
            for (int c : {0, 2, 3, 4}) {
                QTableWidgetItem *it = m_tbl->item(r, c);
                cells << (it ? it->text() : QStringLiteral("?"));
            }
            o << " [" << cells.join(QLatin1Char(' ')) << "]";
        }
        o << "  rows=" << m_tbl->rowCount();
    }
    o << "\n";

    // 遍历所有可见控件的文本，报告无法用当前字体渲染的字符。
    // 这能自动抓出 QString::fromLatin1 误用中文源码导致的乱码。
    if (m_selftestCmd.contains(QLatin1String("text"))) {
        o << "--- widget text glyph check ---\n";
        int bad = 0;
        for (QWidget *w : findChildren<QWidget *>()) {
            QString text;
            const QString id = w->objectName();
            if (w->isWidgetType() && qobject_cast<QPushButton *>(w))
                text = qobject_cast<QPushButton *>(w)->text();
            else if (qobject_cast<QLabel *>(w))
                text = qobject_cast<QLabel *>(w)->text();
            else if (qobject_cast<QGroupBox *>(w))
                text = qobject_cast<QGroupBox *>(w)->title();
            else if (auto *e = qobject_cast<QLineEdit *>(w))
                text = e->placeholderText();
            if (text.isEmpty())
                continue;
            // 只查非 ASCII 字符（即中文等），ASCII 不受字体影响
            const QFont f = w->font();
            const QRawFont raw = QRawFont::fromFont(f);
            if (!raw.isValid())
                continue;
            QStringList missing;
            for (const QChar c : text) {
                if (c.unicode() < 128)
                    continue;
                if (!raw.supportsCharacter(c))
                    missing << QStringLiteral("U+%1").arg(c.unicode(), 4, 16, QLatin1Char('0'));
            }
            if (!missing.isEmpty()) {
                ++bad;
                o << "  BAD " << (id.isEmpty() ? w->metaObject()->className() : id)
                  << " text='" << text << "' missing=" << missing.join(QLatin1Char(',')) << "\n";
            }
        }
        o << "  glyph-bad widgets=" << bad << "\n";
    }

    int overflow = 0;
    for (QWidget *w : findChildren<QWidget *>()) {
        if (!w->isVisible() || w->isWindow())
            continue;
        const QRect r(w->mapTo(this, QPoint(0, 0)), w->size());
        if (r.width() <= 0 || r.height() <= 0)
            continue;
        if (r.left() < -2 || r.top() < -2 || r.right() > width() + 2 || r.bottom() > height() + 2) {
            o << "  OVERFLOW " << w->metaObject()->className() << " '" << w->objectName() << "' "
              << r.x() << "," << r.y() << " " << r.width() << "x" << r.height() << "\n";
            ++overflow;
        }
    }
    o << "layout overflow=" << overflow << "\n";
    if (m_selftestCmd.contains(QStringLiteral("geo"))) {
        o << "geometry: client=" << width() << "x" << height() << " tabH=" << m_tabs->height()
          << " tblImgH=" << (m_tblImg ? m_tblImg->height() : -1) << "\n";
        for (QWidget *w : findChildren<QWidget *>()) {
            if (!w->isVisible() || w->isWindow())
                continue;
            const QString n = w->objectName();
            if (n.startsWith(QLatin1String("btn")) && w->isVisible())
                o << "  " << n << " y=" << w->mapTo(this, QPoint(0, 0)).y()
                  << " h=" << w->height() << "\n";
        }
    }
    o << "log:\n" << m_log->toPlainText() << "\n=== END ===\n";
    o.flush();
}

void MainWindow::runTestCommand(const QString &cmd)
{
    if (cmd.endsWith(QStringLiteral(" #done"))) {
        dumpSelfTest();
        QCoreApplication::quit();
        return;
    }
    if (cmd == QLatin1String("ld")) {
        m_scanner->stop();
        runCommand(Upgrader::Job::ListDevices, {QStringLiteral("ld")}, QStringLiteral("枚举设备"));
    } else if (cmd == QLatin1String("ui-collect")) {
        m_edFirmware->setText(QStringLiteral("/tmp/test_firmware.img"));
        onRun();
    } else if (cmd == QLatin1String("tab") || cmd.startsWith(QLatin1String("tab "))) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            m_tabs->setCurrentIndex(i);
            QCoreApplication::processEvents();
        }
        m_tabs->setCurrentIndex(1);
        dumpSelfTest();
        QCoreApplication::quit();
    } else if (cmd.startsWith(QLatin1String("dlplan "))) {
        // 解包该镜像填入分区，再打印将执行的 di 命令（不真正写设备）
        m_edImgPath->setText(cmd.mid(7).trimmed());
        onUnpackFirmware();
        m_selftestCmd = QStringLiteral("dlplan plan");
        onDownloadSelected();
        QCoreApplication::quit();
    } else if (cmd.startsWith(QLatin1String("imgparts "))) {
        m_edImgPath->setText(cmd.mid(8).trimmed());
        onUnpackFirmware();
        QTextStream o(stdout);
        o << "=== IMG PART TEST ===\n" << m_log->toPlainText() << "\n";
        int checked = 0;
        for (int r = 0; r < m_tblImg->rowCount(); ++r) {
            QTableWidgetItem *it = m_tblImg->item(r, 0);
            QTableWidgetItem *nm = m_tblImg->item(r, 1);
            QTableWidgetItem *sz = m_tblImg->item(r, 2);
            QTableWidgetItem *fp = m_tblImg->item(r, 3);
            if (it && it->checkState() == Qt::Checked)
                ++checked;
            o << (it ? it->text() : QStringLiteral("?")) << " | "
              << (nm ? nm->text() : QString()) << " | "
              << (sz ? sz->text() : QString()) << " | "
              << (fp ? fp->text() : QString()) << " | chk="
              << (it && it->checkState() == Qt::Checked ? 1 : 0) << "\n";
        }
        o << "rows=" << m_tblImg->rowCount() << " checked=" << checked << "\n"
          << "=== END ===\n";
        o.flush();
        QCoreApplication::quit();
    } else {
        runCommand(Upgrader::Job::TestDevice, cmd.split(QLatin1Char(' '), Qt::SkipEmptyParts),
                   QStringLiteral("测试指令"));
    }
}
