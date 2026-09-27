#include "usbscanner.h"

#include <libusb-1.0/libusb.h>

#include <QSet>
#include <algorithm>
#include <cstring>

namespace {

constexpr quint16 kRockchipVid = 0x2207;

bool isRockchipPid(quint16 pid)
{
    // 0x0006 MaskRom；0x350a~0x3520 为各 SoC 的 Loader / UVC 复合设备
    return pid == 0x0006 || (pid >= 0x350a && pid <= 0x3520);
}

QString deviceSerial(libusb_device *dev, libusb_device_handle *handle)
{
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
        return QString();
    if (desc.iSerialNumber) {
        if (handle) {
            unsigned char buf[128];
            const int n = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber,
                                                              buf, sizeof(buf));
            if (n > 0)
                return QString::fromLatin1(reinterpret_cast<char *>(buf), n).trimmed();
        }
    }
    return QString();
}

} // namespace

QString RockchipDevice::describe() const
{
    if (!accessible)
        return QStringLiteral("设备 %1 (%2) - 无访问权限").arg(index).arg(type);
    return QStringLiteral("设备 %1: %2  %3:%4  %5")
        .arg(index)
        .arg(type)
        .arg(vid, 4, 16, QLatin1Char('0'))
        .arg(pid, 4, 16, QLatin1Char('0'))
        .arg(serial.isEmpty() ? QStringLiteral("-") : serial);
}

UsbScanner::UsbScanner(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &UsbScanner::scanOnce);
}

UsbScanner::~UsbScanner()
{
    libusb_exit(nullptr);
}

void UsbScanner::start(int intervalMs)
{
    m_timer->setInterval(intervalMs);
    m_timer->start();
}

void UsbScanner::stop()
{
    m_timer->stop();
}

void UsbScanner::scanOnce()
{
    if (m_inScan)
        return;
    m_inScan = true;

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != LIBUSB_SUCCESS) {
        m_inScan = false;
        return;
    }

    QVector<RockchipDevice> found;
    bool denied = false;
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(ctx, &list);

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = list[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
            continue;
        if (desc.idVendor != kRockchipVid || !isRockchipPid(desc.idProduct))
            continue;

        RockchipDevice d;
        d.vid = desc.idVendor;
        d.pid = desc.idProduct;
        d.bus = QString::number(libusb_get_bus_number(dev));
        d.address = QString::number(libusb_get_device_address(dev));

        bool uvc = false;
        {
            libusb_config_descriptor *cfg = nullptr;
            if (libusb_get_active_config_descriptor(dev, &cfg) == LIBUSB_SUCCESS) {
                for (int i2 = 0; i2 < cfg->bNumInterfaces; ++i2) {
                    const libusb_interface *itf = &cfg->interface[i2];
                    for (int j = 0; j < itf->num_altsetting; ++j) {
                        const libusb_interface_descriptor *alt = &itf->altsetting[j];
                        if (alt->bInterfaceClass == 0x0c /* video */)
                            uvc = true;
                    }
                }
                libusb_free_config_descriptor(cfg);
            }
        }
        d.hasUvc = uvc;
        d.type = (d.pid == 0x0006) ? QStringLiteral("MASKROM") : QStringLiteral("LOADER");

        libusb_device_handle *handle = nullptr;
        const int rc = libusb_open(dev, &handle);
        if (rc == LIBUSB_SUCCESS) {
            d.accessible = true;
            d.serial = deviceSerial(dev, handle);
            // 实际抢占接口：虚拟化 USB 直通场景下会返回 -6(LIBUSB_ERROR_NO_DEVICE)
            if (!m_passive) {
                const int cr = libusb_claim_interface(handle, 0);
                if (cr == LIBUSB_SUCCESS) {
                    d.transferReady = true;
                    libusb_release_interface(handle, 0);
                } else {
                    d.claimError = cr;
                }
            } else {
                d.transferReady = true;
            }
            libusb_close(handle);
        } else {
            d.accessible = false;
            denied = true;
        }
        found.append(d);
    }
    if (list)
        libusb_free_device_list(list, 1);
    libusb_exit(ctx);

    std::sort(found.begin(), found.end(), [](const RockchipDevice &a, const RockchipDevice &b) {
        return a.bus.toInt() * 1000 + a.address.toInt()
            < b.bus.toInt() * 1000 + b.address.toInt();
    });
    for (int i = 0; i < found.size(); ++i)
        found[i].index = i;

    int problem = 0;
    for (const RockchipDevice &d : found)
        problem += (d.accessible && !d.transferReady) ? 1 : 0;

    // 空列表时不能调用 first()/last()，否则重复扫描空设备列表会崩溃
    bool same;
    if (found.isEmpty() && m_devices.isEmpty()) {
        same = true; // 前后都无设备，视为无变化
    } else if (found.isEmpty() || m_devices.isEmpty()) {
        same = false;
    } else {
        same = found.size() == m_devices.size() && found.first().pid == m_devices.first().pid
            && found.last().address == m_devices.last().address
            && found.last().accessible == m_devices.last().accessible
            && found.last().bus == m_devices.last().bus;
    }
    const bool deniedChanged = denied != m_denied;
    const bool problemChanged = problem != m_transferProblemCount;

    m_devices = found;
    m_denied = denied;
    m_transferProblem = problem > 0;
    m_transferProblemCount = problem;
    if (!same || deniedChanged || problemChanged)
        emit changed();

    m_inScan = false;
}

int UsbScanner::maskromCount() const
{
    int n = 0;
    for (const RockchipDevice &d : m_devices)
        n += d.isMaskrom() ? 1 : 0;
    return n;
}

int UsbScanner::loaderCount() const
{
    int n = 0;
    for (const RockchipDevice &d : m_devices)
        n += (!d.isMaskrom() && !d.hasUvc) ? 1 : 0;
    return n;
}

int UsbScanner::uvcCount() const
{
    int n = 0;
    for (const RockchipDevice &d : m_devices)
        n += d.hasUvc ? 1 : 0;
    return n;
}

QString UsbScanner::statusText() const
{
    if (m_devices.isEmpty())
        return QStringLiteral("找到0个设备");

    QStringList parts;
    const int mr = maskromCount();
    const int ld = loaderCount();
    const int uv = uvcCount();

    if (mr == 1 && m_devices.size() == 1)
        parts << QStringLiteral("发现1个MASKROM设备");
    else if (mr > 1)
        parts << QStringLiteral("发现%1个MASKROM设备").arg(mr);
    else if (mr == 1)
        parts << QStringLiteral("发现1个MASKROM设备");

    if (ld == 1 && parts.isEmpty())
        parts << QStringLiteral("发现1个LOADER设备");
    else if (ld > 1)
        parts << QStringLiteral("发现%1个LOADER设备").arg(ld);
    else if (ld == 1)
        parts << QStringLiteral("发现1个LOADER设备");

    if (uv == 1 && parts.isEmpty())
        parts << QStringLiteral("发现1个可用的复合设备");
    else if (uv > 1)
        parts << QStringLiteral("发现%1个可用的复合设备").arg(uv);
    else if (uv == 1)
        parts << QStringLiteral("发现1个可用的复合设备");

    return parts.join(QStringLiteral(", "));
}
