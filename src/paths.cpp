#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

// 后端可执行文件名，官方压缩包解压后就是这个名，改名时只改这一处
const QString kToolName = QStringLiteral("upgrade_tool");

QString findToolRecursive(const QString &root, int depth)
{
    if (depth > 4 || !QDir(root).exists())
        return QString();
    QDir dir(root);
    const QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                               QDir::Name);
    for (const QString &name : entries) {
        const QString full = dir.filePath(name);
        const QFileInfo fi(full);
        if (fi.isFile() && fi.isExecutable()
            && name == kToolName) {
            return full;
        }
    }
    for (const QString &name : entries) {
        const QString full = dir.filePath(name);
        if (QFileInfo(full).isDir()) {
            const QString hit = findToolRecursive(full, depth + 1);
            if (!hit.isEmpty())
                return hit;
        }
    }
    return QString();
}

} // namespace

QString Paths::toolDir()
{
    const QString env = qEnvironmentVariable("RKDEVTOOL_UPGRADE_TOOL");
    if (!env.isEmpty())
        return QFileInfo(env).absolutePath();

    const QStringList candidates{
        QCoreApplication::applicationDirPath(),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/rkdevtool"),
        QDir::homePath() + QStringLiteral("/.local/share/rkdevtool"),
        QDir::homePath() + QStringLiteral("/flash/Linux_Upgrade_Tool_v2.1"),
        QDir::homePath() + QStringLiteral("/Downloads/Linux_Upgrade_Tool_v2.1"),
        QDir::homePath() + QStringLiteral("/downloads/Linux_Upgrade_Tool_v2.1"),
        QDir::homePath() + QStringLiteral("/rockchip/Linux_Upgrade_Tool_v2.1"),
        QStringLiteral("/opt/rkdevtool"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo(QDir(c).filePath(kToolName)).isFile())
            return QDir(c).absolutePath();
    }
    for (const QString &c : candidates) {
        const QString hit = findToolRecursive(QDir(c).absolutePath(), 0);
        if (!hit.isEmpty())
            return QFileInfo(hit).absolutePath();
    }
    return QDir::homePath() + QStringLiteral("/flash/Linux_Upgrade_Tool_v2.1");
}

QString Paths::upgradeTool()
{
    const QString env = qEnvironmentVariable("RKDEVTOOL_UPGRADE_TOOL");
    if (!env.isEmpty())
        return env;
    return toolDir() + QLatin1Char('/') + kToolName;
}

QString Paths::configFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/rkdevtool.conf");
}

Paths::Config Paths::loadConfig()
{
    Config cfg;
    QSettings s(configFile(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("firmware"));
    cfg.advanced = s.value(QStringLiteral("advanced"), true).toBool();
    cfg.loader = s.value(QStringLiteral("loader")).toString();
    cfg.parameter = s.value(QStringLiteral("parameter")).toString();
    cfg.firmware = s.value(QStringLiteral("firmware")).toString();
    cfg.storage = s.value(QStringLiteral("storage"), QStringLiteral("EMMC")).toString();
    cfg.mode = s.value(QStringLiteral("mode"), QStringLiteral("write")).toString();
    cfg.misc = s.value(QStringLiteral("misc")).toString();
    cfg.boot = s.value(QStringLiteral("boot")).toString();
    cfg.kernel = s.value(QStringLiteral("kernel")).toString();
    cfg.system = s.value(QStringLiteral("system")).toString();
    cfg.recovery = s.value(QStringLiteral("recovery")).toString();
    cfg.uboot = s.value(QStringLiteral("uboot")).toString();
    cfg.trust = s.value(QStringLiteral("trust")).toString();
    cfg.forceByAddress = s.value(QStringLiteral("forceByAddress"), false).toBool();
    s.endGroup();
    return cfg;
}

void Paths::saveConfig(const Config &cfg)
{
    QSettings s(configFile(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("firmware"));
    s.setValue(QStringLiteral("advanced"), cfg.advanced);
    s.setValue(QStringLiteral("loader"), cfg.loader);
    s.setValue(QStringLiteral("parameter"), cfg.parameter);
    s.setValue(QStringLiteral("firmware"), cfg.firmware);
    s.setValue(QStringLiteral("storage"), cfg.storage);
    s.setValue(QStringLiteral("mode"), cfg.mode);
    s.setValue(QStringLiteral("misc"), cfg.misc);
    s.setValue(QStringLiteral("boot"), cfg.boot);
    s.setValue(QStringLiteral("kernel"), cfg.kernel);
    s.setValue(QStringLiteral("system"), cfg.system);
    s.setValue(QStringLiteral("recovery"), cfg.recovery);
    s.setValue(QStringLiteral("uboot"), cfg.uboot);
    s.setValue(QStringLiteral("trust"), cfg.trust);
    s.setValue(QStringLiteral("forceByAddress"), cfg.forceByAddress);
    s.endGroup();
    s.sync();
}
