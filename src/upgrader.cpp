#include "upgrader.h"
#include "paths.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

// 去掉终端 ANSI 颜色/控制序列
QString stripAnsi(const QString &in)
{
    static const QRegularExpression re(QStringLiteral("\x1b\\[[0-9;?]*[a-zA-Z]"));
    QString s = in;
    s.remove(re);
    s.remove(QRegularExpression(QStringLiteral("\x1b\\][^\x07]*\x07")));
    s.remove(QChar(0x07));
    s.remove(QChar(0x1b));
    return s;
}

QString compact(const QString &in)
{
    return stripAnsi(in).simplified();
}

} // namespace

Upgrader::Upgrader(QObject *parent) : QObject(parent)
{
    m_tool = Paths::upgradeTool();
}

Upgrader::~Upgrader()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
}

void Upgrader::setToolPath(const QString &path)
{
    m_tool = path;
}

bool Upgrader::toolAvailable() const
{
    return QFileInfo(m_tool).isFile() && QFileInfo(m_tool).isExecutable();
}

void Upgrader::reset()
{
    m_buf.clear();
    m_percent = 0;
    m_stageIndex = 0;
    m_stageTotal = 1;
    m_failed = false;
    m_stagePercent.clear();
    m_lastLine.clear();
    setPercent(0);
}

void Upgrader::setPercent(int p)
{
    p = qBound(0, p, 100);
    if (p == m_percent)
        return;
    m_percent = p;
    emit percentChanged(m_percent);
}

bool Upgrader::run(Job job, const QStringList &args, const QString &desc)
{
    if (busy())
        return false;
    if (!toolAvailable()) {
        emit logLine(QStringLiteral("[错误] 找不到 upgrade_tool 可执行文件：%1").arg(m_tool));
        emit jobFinished(false, QStringLiteral("缺少 upgrade_tool"));
        return false;
    }

    reset();
    m_job = job;
    m_desc = desc;

    QStringList fullArgs;
    QString program;
    if (m_privileged) {
        program = QStringLiteral("pkexec");
        fullArgs << m_tool << args;
        emit logLine(QStringLiteral("[提权] 通过 pkexec 执行：upgrade_tool %1").arg(args.join(' ')));
    } else {
        program = m_tool;
        fullArgs << args;
    }

    emit logLine(QStringLiteral("--- 执行：upgrade_tool %1").arg(args.join(' ')));
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(Paths::toolDir());
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &Upgrader::onReadyRead);
    connect(m_proc, &QProcess::readyReadStandardError, this, &Upgrader::onReadyRead);
    connect(m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &Upgrader::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, &Upgrader::onErrorOccurred);
    emit busyChanged(true);
    m_proc->start(program, fullArgs);
    // 关闭标准输入：让子进程立即收到 EOF，避免交互式命令永久等待输入
    if (m_proc->waitForStarted(5000)) {
        m_proc->closeWriteChannel();
    } else {
        m_proc->kill();
        m_proc->waitForFinished(1000);
        m_proc->deleteLater();
        m_proc = nullptr;
        emit busyChanged(false);
        emit logLine(QStringLiteral("[错误] 启动超时：%1").arg(program));
        emit jobFinished(false, QStringLiteral("进程启动超时"));
        return false;
    }
    return true;
}

void Upgrader::abort()
{
    if (!m_proc)
        return;
    emit logLine(QStringLiteral("[中止] 用户中断操作"));
    m_proc->terminate();
    if (!m_proc->waitForFinished(1500))
        m_proc->kill();
}

void Upgrader::onReadyRead()
{
    if (!m_proc)
        return;
    m_buf += QString::fromLocal8Bit(m_proc->readAll());
    int nl;
    while ((nl = m_buf.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        handleLine(line);
    }
}

void Upgrader::handleLine(const QString &raw)
{
    const QString line = stripAnsi(raw).trimmed();
    if (line.isEmpty())
        return;
    if (m_lastLine == line)
        return;
    m_lastLine = line;

    emit logLine(line);

    // 错误判定
    static const QStringList errKeys{
        QStringLiteral("No found any rockusb device"),
        QStringLiteral("please plug device in"),
        QStringLiteral("No found rockusb"),
        QStringLiteral("Fail"),
        QStringLiteral("failed"),
        QStringLiteral("ERROR"),
        QStringLiteral("error"),
        QStringLiteral("Timeout"),
        QStringLiteral("timeout"),
        QStringLiteral("No space"),
        QStringLiteral("not match"),
        QStringLiteral("Invalid"),
        QStringLiteral("invalid"),
        QStringLiteral("Can't "),
        QStringLiteral("denied"),
    };
    for (const QString &k : errKeys) {
        if (line.contains(k)) {
            m_failed = true;
            break;
        }
    }

    // 进度：xx% 或 当前/总数
    static const QRegularExpression pct(QStringLiteral("(\\d{1,3})\\s*%"));
    static const QRegularExpression frac(QStringLiteral("(\\d+)\\s*/\\s*(\\d+)"));
    const QRegularExpressionMatch mp = pct.match(line);
    if (mp.hasMatch()) {
        int v = mp.captured(1).toInt();
        if (v <= 100) {
            setPercent(v);
            return;
        }
    }
    const QRegularExpressionMatch mf = frac.match(line);
    if (mf.hasMatch()) {
        const int a = mf.captured(1).toInt();
        const int b = mf.captured(2).toInt();
        if (b > 0)
            setPercent(a * 100 / b);
    }
}

void Upgrader::onErrorOccurred(QProcess::ProcessError err)
{
    if (!m_proc)
        return;
    if (err == QProcess::FailedToStart) {
        emit logLine(QStringLiteral("[错误] 无法启动进程：%1").arg(m_proc->errorString()));
        m_failed = true;
        m_proc->deleteLater();
        m_proc = nullptr;
        setPercent(0);
        emit busyChanged(false);
        emit jobFinished(false, QStringLiteral("进程启动失败"));
    }
}

void Upgrader::onFinished(int code, QProcess::ExitStatus status)
{
    if (!m_buf.contains(QLatin1Char('\n')) && !m_buf.trimmed().isEmpty())
        handleLine(m_buf);
    m_buf.clear();

    const bool ok = (status == QProcess::NormalExit) && code == 0 && !m_failed;
    if (ok && m_job == Job::ListDevices)
        setPercent(100);

    QString summary;
    if (ok) {
        summary = QStringLiteral("%1 完成").arg(m_desc);
        setPercent(100);
    } else {
        summary = m_failed ? QStringLiteral("%1 失败").arg(m_desc)
                           : QStringLiteral("%1 失败（退出码 %2）").arg(m_desc).arg(code);
    }

    if (m_proc) {
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_job = Job::None;
    emit busyChanged(false);
    emit jobFinished(ok, summary);
}
