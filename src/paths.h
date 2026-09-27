#pragma once

#include <QString>
#include <QStringList>

// 负责定位 Rockchip 官方 upgrade_tool 可执行文件、读写用户配置。
namespace Paths {

// upgrade_tool 所在目录（可用 RKDEVTOOL_UPGRADE_TOOL 环境变量覆盖）
QString toolDir();

// upgrade_tool 可执行文件完整路径
QString upgradeTool();

// 配置文件（INI）路径
QString configFile();

// 读取升级配置
struct Config {
    bool advanced = true;                       // 高级模式
    QString loader;
    QString parameter;
    QString firmware;
    QString storage = QStringLiteral("EMMC"); // EMMC / SPINOR / SPINAND / FLASH
    QString mode = QStringLiteral("write");     // write / loader / partition
    QString misc;
    QString boot;
    QString kernel;
    QString system;
    QString recovery;
    QString uboot;
    QString trust;
    bool forceByAddress = false;
};

Config loadConfig();
void saveConfig(const Config &cfg);

} // namespace Paths
