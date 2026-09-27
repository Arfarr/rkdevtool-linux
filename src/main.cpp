#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QFont>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QTranslator>

#include "mainwindow.h"

namespace {

// 界面统一使用的中文字体候选，按优先级排列
// 本机 fontconfig 的默认 sans/mono 都是 DejaVu LGC Sans，不含任何中文字形，
// 所以必须显式指定含 CJK 的字体，并把整张列表交给 Qt 做逐级回退。
// kUiFonts / uiFont 统一由 mainwindow.h 声明，避免重复定义

// 运行时绘制图标，避免额外二进制资源文件
QIcon makeAppIcon()
{
    QPixmap pm(128, 128);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, 128);
    g.setColorAt(0, QColor(0x35, 0x7a, 0xd8));
    g.setColorAt(1, QColor(0x14, 0x46, 0x82));
    p.setBrush(g);
    p.setPen(QPen(QColor(0xff, 0xff, 0xff), 4));
    p.drawRoundedRect(6, 6, 116, 116, 16, 16);
    p.setPen(Qt::white);
    p.setFont(uiFont(26, true));
    p.drawText(pm.rect().adjusted(0, -6, 0, 0), Qt::AlignCenter, QStringLiteral("RK"));
    p.setFont(uiFont(13, true));
    p.drawText(QRect(0, 74, 128, 30), Qt::AlignCenter, QStringLiteral("DevTool"));
    return QIcon(pm);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RKDevTool"));
    app.setOrganizationName(QStringLiteral("Rockchip"));
    app.setApplicationVersion(QStringLiteral(RKDEVTOOL_VERSION));
    app.setWindowIcon(makeAppIcon());

    // 与 Windows 版一致的紧凑控件观感
    QFont f = uiFont(9);
    app.setFont(f);
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QStringLiteral(R"(
QGroupBox {
    margin-top: 8px;
    border: 1px solid #b8b8b8;
    border-radius: 2px;
    background: #f7f7f7;
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 6px;
    padding: 0 3px;
    color: #202020;
}
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fdfdfd, stop:1 #e6e6e6);
    border: 1px solid #a8a8a8;
    border-radius: 2px;
    padding: 1px 10px;
}
QPushButton:hover   { border-color: #7a7a7a; }
QPushButton:pressed { background: #dcdcdc; }
QPushButton:disabled{ color: #9a9a9a; background: #f0f0f0; }
QLineEdit, QComboBox {
    background: #ffffff;
    border: 1px solid #a8a8a8;
    border-radius: 2px;
    selection-background-color: #316ac5;
}
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
    background: #ffffff;
    selection-background-color: #316ac5;
    selection-color: #ffffff;
}
QProgressBar {
    border: 1px solid #a8a8a8;
    border-radius: 2px;
    background: #ffffff;
    text-align: center;
    font-size: 9px;
}
QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7fc4ff, stop:1 #2f7fd6); }
QStatusBar { border-top: 1px solid #c8c8c8; }
QSplitter::handle { background: #d0d0d0; width: 3px; }
QMenuBar, QMenu { background: #f5f5f5; }
QMenu::item:selected { background: #316ac5; color: #ffffff; }
)"));

    for (const QString &a : app.arguments()) {
        if (a.startsWith(QStringLiteral("--export-icon="))) {
            const QString out = a.mid(14);
            QPixmap pm = makeAppIcon().pixmap(128, 128);
            QDir().mkpath(QFileInfo(out).absolutePath());
            const bool okImg = pm.save(out, "PNG");
            qInfo("export icon %s -> %s", qPrintable(out), okImg ? "ok" : "fail");
            return 0;
        }
    }

    // --page=download|upgrade|advanced 指定启动时所在页签
    QString pageArg;
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QStringLiteral("--page=")))
            pageArg = a.mid(7);
    }

    MainWindow w;
    w.show();
    if (!pageArg.isEmpty() && !w.showPage(pageArg))
        qWarning("未知页签名：%s", qPrintable(pageArg));

    if (app.arguments().contains(QStringLiteral("--selftest"))) {
        QTimer::singleShot(2500, [&w]() {
            w.dumpSelfTest();
            QCoreApplication::quit();
        });
    }
    for (const QString &a : app.arguments()) {
        if (a.startsWith(QStringLiteral("--test-cmd=")))
            QTimer::singleShot(1800, [&w, a]() { w.runTestCommand(a.mid(11)); });
    }
    return app.exec();
}
