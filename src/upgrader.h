#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Rockchip Linux 官方命令行工具 upgrade_tool 的封装，
// 行为与 Windows 版 RKDevTool 的每个按钮一一对应。
class Upgrader : public QObject
{
    Q_OBJECT

public:
    enum class Job {
        None,
        ListDevices,
        TestDevice,
        WriteLoader,
        WriteParameter,
        WriteImage,
        WriteFirmwareFull,   // Loader + 参数 + 完整固件，等价 Win 版“升级文件”
        EraseFlash,
        PartitionList,
        ReadChipInfo,
        ReadFlashId,
        ReadFlashInfo,
        ReadCapability,
        ReadSN,
        ResetDevice,
        RebootToMaskrom,
        ReadSector,
        WriteSector,
        ReadLba,
        WriteLba,
    };

    explicit Upgrader(QObject *parent = nullptr);
    ~Upgrader() override;

    void setToolPath(const QString &path);
    QString toolPath() const { return m_tool; }
    bool toolAvailable() const;

    bool busy() const { return m_proc != nullptr; }
    Job currentJob() const { return m_job; }
    int currentPercent() const { return m_percent; }

    // 统一的执行入口；args 为 upgrade_tool 的参数列表
    bool run(Job job, const QStringList &args, const QString &desc);
    void abort();

    // 以 pkexec 提权执行（普通用户无 udev 权限时使用）
    void setUsePrivilege(bool on) { m_privileged = on; }
    bool usePrivilege() const { return m_privileged; }

signals:
    void logLine(const QString &text);
    void percentChanged(int percent);
    void jobFinished(bool ok, const QString &summary);
    void busyChanged(bool busy);

private slots:
    void onReadyRead();
    void onFinished(int code, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError err);

private:
    void handleLine(const QString &raw);
    void reset();
    void setPercent(int p);

    QProcess *m_proc = nullptr;
    QString m_tool;
    QString m_desc;
    QString m_buf;
    Job m_job = Job::None;
    int m_percent = 0;
    int m_stageIndex = 0;
    int m_stageTotal = 1;
    bool m_failed = false;
    bool m_privileged = false;
    QStringList m_stagePercent;
    QString m_lastLine;
};
