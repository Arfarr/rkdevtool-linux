#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include "paths.h"
#include "upgrader.h"
#include "usbscanner.h"

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;

// 按名称挑选第一个系统实际安装的字体；中文字体缺失会导致方块乱码，
// 所以优先从候选列表里挑，而不是硬编码 "Microsoft YaHei"
QString pickFontFamily(const QStringList &candidates);
// 过滤出系统里真实安装的候选，作为 QFont::setFamilies 的回退链
QStringList installedCjkFonts(const QStringList &candidates);
QFont uiFont(int pointSize, bool bold = false);

// 复刻 Windows 版 RKDevTool v2.93 布局：顶部三页签、右侧固定日志栏、底部状态栏
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // 按名字切到指定页签：download / upgrade / advanced
    bool showPage(const QString &name);

public slots:
    void dumpSelfTest();
    void runTestCommand(const QString &cmd);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onDeviceChanged();
    void onLogLine(const QString &text);
    void onPercentChanged(int percent);
    void onJobFinished(bool ok, const QString &summary);
    void onBusyChanged(bool busy);
    void onRun();
    void onSaveConfig();
    void onLoadConfig();
    void onClearLog();
    void onRefreshDevices();
    void onStorageChanged();
    void onPickFirmware();
    void onPickSn();
    void onUnpackFirmware();

private:
    struct Step {
        Upgrader::Job job = Upgrader::Job::None;
        QStringList args;
        QString desc;
        QString progressText;
        int begin = 0;
        int end = 100;
    };

    // 下载镜像页的一行：Rockchip 标准分区 + 对应的镜像文件
    struct ImgPart {
        QString tag;   // 4 字节 magic，如 KERN / SYST
        QString name;  // 显示名
        QString flag;  // upgrade_tool di 的参数，如 -k / -s；空表示需整包刷
        qint64 offset = 0;  // 在整包固件内的偏移，手动指定时为 0
        qint64 size = 0;  // 镜像文件大小，未指定文件时为 0
        QString path;  // 镜像文件路径（手动指定或解包导出）
        bool selected = false;          // 是否勾选刷入
        bool fromUnpack = false;        // 是否由解包自动填入
    };

    void buildUi();
    QWidget *buildTabDownload();
    QLayout *buildDeviceTools(QWidget *parent);
    void initStandardParts();
    void onPickPartFile(int row);
    void refreshPartTable();
    void onSetAllParts(bool checked);
    void onDownloadSelected();
    void clearPartitionTable();
    void refreshImgTable();
    QWidget *buildTabUpgrade();
    QWidget *buildTabAdvanced();
    void setupMenus();

    QPushButton *mkBtn(const QString &id, const QString &text, int w = 92);
    QLineEdit *mkEdit(const QString &id, const QString &placeholder = QString());
    QLabel *mkLabel(const QString &text, int w = 0);
    QCheckBox *mkCheck(const QString &id, const QString &text);

    QLineEdit *ed(const QString &id) const;
    QRadioButton *currentStorage() const;
    void link(QLineEdit *edit, const QString &btnId, const QString &title, const QString &filter);
    void runCommand(Upgrader::Job job, const QStringList &args, const QString &desc);

    void setStatus(const QString &text);
    void setDeviceSerial(const QString &text);
    void setStageText(const QString &text);
    void appendLog(const QString &text);
    void refreshEnabled();
    void autoReadDeviceInfo();
    bool requireDevice();
    bool toolUsable() const;
    void startSteps(const QVector<Step> &steps);
    void runNextStep();
    void collectUpgradeSteps(QVector<Step> &steps, QString &err);

    UsbScanner *m_scanner = nullptr;
    Upgrader *m_upgrader = nullptr;

    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QComboBox *m_cbDevice = nullptr;

    QLabel *m_statusDevice = nullptr;
    QLabel *m_statusSerial = nullptr;
    QLabel *m_statusText = nullptr;
    QProgressBar *m_progress = nullptr;

    // 下载镜像页
    QLineEdit *m_edImgPath = nullptr;
    QTableWidget *m_tblImg = nullptr;
    QLabel *m_lbImgInfo = nullptr;
    QPushButton *m_btnDownload = nullptr;

    // 升级固件页
    QLineEdit *m_edFirmware = nullptr;
    QTextEdit *m_lbFwVer = nullptr;
    QTextEdit *m_lbLoaderVer = nullptr;
    QTextEdit *m_lbChip = nullptr;

    // 高级功能页
    QLineEdit *m_edLoader = nullptr;
    QLineEdit *m_edParameter = nullptr;
    QCheckBox *m_chkAddress = nullptr;
    QTableWidget *m_tbl = nullptr;

    QVector<QPushButton *> m_buttons;
    QVector<QAction *> m_actions;
    QVector<Step> m_steps;
    QVector<ImgPart> m_imgParts;
    QString m_unpackDir;
    int m_stepIndex = -1;
    bool m_runOk = true;
    bool m_advancing = false;
    bool m_selftest = false;
    bool m_infoReading = false;
    bool m_loggedTransfer = false;
    bool m_loggedDenied = false;
    QString m_selftestCmd;
    Paths::Config m_cfg;
};
