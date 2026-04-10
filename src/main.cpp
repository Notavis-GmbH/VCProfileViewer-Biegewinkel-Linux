/****************************************************************************
** main.cpp
****************************************************************************/
#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QtGlobal>
#include "mainwindow.h"
#include "licensemanager.h"
#include "licensedialog.h"
#include "licensemanager.h"
#include "licensedialog.h"

// ──────────────────────────────────────────────────────────────────────────────
//  Application-level message handler – writes qDebug/qWarning/qCritical
//  to  Logs/AppLog_yyyyMMdd_HHmmss.txt  next to the EXE.
//  Format:  2026-04-08 13:14:22.345 [W] sensorworker.cpp:42 - message
// ──────────────────────────────────────────────────────────────────────────────
static QFile        g_logFile;
static QTextStream  g_logStream;
static QMutex       g_logMutex;

static void appMessageHandler(QtMsgType type,
                               const QMessageLogContext &ctx,
                               const QString &msg)
{
    QMutexLocker locker(&g_logMutex);

    const char level = [type]() -> char {
        switch (type) {
            case QtDebugMsg:    return 'D';
            case QtInfoMsg:     return 'I';
            case QtWarningMsg:  return 'W';
            case QtCriticalMsg: return 'C';
            case QtFatalMsg:    return 'F';
            default:            return '?';
        }
    }();

    // Short source file name (strip path)
    QString file = ctx.file ? QString(ctx.file) : QString();
    int slash = qMax(file.lastIndexOf('/'), file.lastIndexOf('\\'));
    if (slash >= 0) file = file.mid(slash + 1);

    const QString line = QString("%1 [%2] %3:%4 - %5")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
        .arg(level)
        .arg(file)
        .arg(ctx.line)
        .arg(msg);

    if (g_logFile.isOpen())
        g_logStream << line << '\n';

    // Also print to stderr for IDE/CI visibility
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());

    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("VC3DProfileViewer_Biegewinkel");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("NOTAVIS");

    // ── Open application log ──────────────────────────────────────────────
    {
        QDir appDir(QApplication::applicationDirPath());
        appDir.mkpath("Logs");
        const QString logPath = appDir.filePath(
            QString("Logs/AppLog_%1.txt")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
        g_logFile.setFileName(logPath);
        if (g_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            g_logStream.setDevice(&g_logFile);
            g_logStream.setEncoding(QStringConverter::Utf8);
            g_logStream << "VC 3D Profile Viewer – Application Log\n";
            g_logStream << "Started: "
                        << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
            g_logStream << "Build:   " << __DATE__ << " " << __TIME__ << "\n";
            g_logStream << QString(60, '-') << "\n";
        }
    }
    qInstallMessageHandler(appMessageHandler);

    // ── Dark style ────────────────────────────────────────────────────────
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window,          QColor(40,  40,  42));
    darkPalette.setColor(QPalette::WindowText,       Qt::white);
    darkPalette.setColor(QPalette::Base,             QColor(28,  28,  30));
    darkPalette.setColor(QPalette::AlternateBase,    QColor(50,  50,  52));
    darkPalette.setColor(QPalette::ToolTipBase,      Qt::white);
    darkPalette.setColor(QPalette::ToolTipText,      Qt::white);
    darkPalette.setColor(QPalette::Text,             Qt::white);
    darkPalette.setColor(QPalette::Button,           QColor(55,  55,  58));
    darkPalette.setColor(QPalette::ButtonText,       Qt::white);
    darkPalette.setColor(QPalette::BrightText,       Qt::red);
    darkPalette.setColor(QPalette::Link,             QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight,        QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText,  Qt::black);
    app.setPalette(darkPalette);

    app.setStyleSheet(
        "QGroupBox { border: 1px solid #555; border-radius: 4px; margin-top: 6px; "
        "            font-weight: bold; padding-top: 4px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; "
        "                   padding: 0 4px 0 4px; color: #aaa; } "
        "QLineEdit, QDoubleSpinBox { background: #222; border: 1px solid #555; "
        "                            color: white; padding: 2px 4px; border-radius: 3px; } "
        "QLabel { color: #ddd; } "
        "QStatusBar { background: #222; color: #888; }"
    );

    // ── Lizenzprüfung ──────────────────────────────────────────────────
    LicenseManager licenseManager;
    bool licenseOk = licenseManager.validateOnStartup();
    if (!licenseOk) {
        LicenseDialog dlg(&licenseManager);
        if (dlg.exec() != QDialog::Accepted) {
            return 0; // Kein Lizenzschlüssel eingegeben — App beenden
        }
    }

    MainWindow w;
    w.show();
    int ret = app.exec();

    // Flush & close log on clean exit
    qInstallMessageHandler(nullptr);
    if (g_logFile.isOpen()) {
        g_logStream << QString(60, '-') << "\n";
        g_logStream << "Exited: "
                    << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
        g_logStream.flush();
        g_logFile.close();
    }
    return ret;
}
