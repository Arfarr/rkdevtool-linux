#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>
#include <QDateTime>

// 单个瑞芯微 USB 设备信息
struct RockchipDevice {
    int index = 0;          // 设备序号（从 0 开始）
    quint16 vid = 0;
    quint16 pid = 0;
    QString bus = QString();
    QString address = QString();
    QString serial = QString();
    QString type;           // MASKROM / LOADER / UVC
    bool hasUvc = false;
    bool accessible = true; // 当前用户是否有权限访问
    bool transferReady = false; // 是否成功 claim 接口（VMware 直通故障时为 false）
    int claimError = 0;     // libusb 错误码

    bool isMaskrom() const { return type == QStringLiteral("MASKROM"); }
    QString describe() const;
};

// 周期性扫描 USB 总线上的瑞芯微设备，等价于 Windows 版 RKDevTool 的自动检测
class UsbScanner : public QObject
{
    Q_OBJECT

public:
    explicit UsbScanner(QObject *parent = nullptr);
    ~UsbScanner() override;

    void start(int intervalMs = 1000);
    void stop();
    void scanOnce();
    // 烧写进行中调用，暂停接口 claim 探测以免与 upgrade_tool 抢占设备
    void setPassive(bool on) { m_passive = on; }
    bool hasTransferProblem() const { return m_transferProblem; }
    int transferProblemCount() const { return m_transferProblemCount; }

    const QVector<RockchipDevice> &devices() const { return m_devices; }
    int maskromCount() const;
    int loaderCount() const;
    int uvcCount() const;
    int total() const { return m_devices.size(); }
    bool denied() const { return m_denied; }

    // 与 Windows 版一致的状态栏文案
    QString statusText() const;

signals:
    void changed();

private:
    QTimer *m_timer = nullptr;
    QVector<RockchipDevice> m_devices;
    bool m_denied = false;
    bool m_inScan = false;
    bool m_passive = false;
    bool m_transferProblem = false;
    int m_transferProblemCount = 0;
};
