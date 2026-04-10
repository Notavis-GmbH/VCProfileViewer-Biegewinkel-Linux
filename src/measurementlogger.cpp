/****************************************************************************
** measurementlogger.cpp
****************************************************************************/
#include "measurementlogger.h"
#include <QDir>

MeasurementLogger::MeasurementLogger(QObject *parent)
    : QObject(parent)
{}

MeasurementLogger::~MeasurementLogger()
{
    close();
}

bool MeasurementLogger::open(const QString &filePath, QString *errOut)
{
    close();   // close any existing file first

    // Ensure parent directory exists
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errOut)
            *errOut = m_file.errorString();
        return false;
    }

    m_stream.setDevice(&m_file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_stream.setEncoding(QStringConverter::Utf8);
#else
    m_stream.setCodec("UTF-8");
#endif
    m_rowCount = 0;
    writeHeader();

    emit logOpened(filePath);
    return true;
}

void MeasurementLogger::close()
{
    if (!m_file.isOpen()) return;

    m_stream.flush();
    QString path = m_file.fileName();
    int rows = m_rowCount;
    m_file.close();
    m_rowCount = 0;

    emit logClosed(path, rows);
}

void MeasurementLogger::writeHeader()
{
    // BOM for Excel auto-detection of UTF-8
    m_stream << "\xEF\xBB\xBF";

    const char S = SEP;
    m_stream
        << "Timestamp"          << S
        << "Phi1_deg"           << S
        << "Phi2_deg"           << S
        << "BendingAngle_deg"   << S
        << "RMS1_um"            << S
        << "RMS2_um"            << S
        << "MaxRes1_um"         << S
        << "MaxRes2_um"         << S
        << "Method1"            << S
        << "Method2"            << S
        << "Inlier1"            << S   // fraction 0..1, empty if non-Auto
        << "Inlier2"            << S
        << "ROI1_Start_mm"      << S
        << "ROI1_End_mm"        << S
        << "ROI2_Start_mm"      << S
        << "ROI2_End_mm"
        << "\n";
    m_stream.flush();
}

void MeasurementLogger::append(const LogEntry &e)
{
    if (!m_file.isOpen()) return;
    if (!e.valid1 && !e.valid2) return;   // nothing meaningful to log

    const char S = SEP;
    const QString na = QString();   // empty string for N/A fields

    // ISO 8601 timestamp with milliseconds
    QString ts = e.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz");

    auto dbl = [](double v, int prec = 4) -> QString {
        return QString::number(v, 'f', prec);
    };

    m_stream
        << ts                                                           << S
        << (e.valid1 ? dbl(e.phi1_deg)           : na)                 << S
        << (e.valid2 ? dbl(e.phi2_deg)           : na)                 << S
        << (e.hasBending ? dbl(e.bendingAngle_deg) : na)               << S
        << (e.valid1 ? dbl(e.rms1_um,    2)      : na)                 << S
        << (e.valid2 ? dbl(e.rms2_um,    2)      : na)                 << S
        << (e.valid1 ? dbl(e.maxRes1_um, 2)      : na)                 << S
        << (e.valid2 ? dbl(e.maxRes2_um, 2)      : na)                 << S
        << (e.valid1 ? e.method1                  : na)                << S
        << (e.valid2 ? e.method2                  : na)                << S
        << (e.valid1 && e.inlier1 >= 0 ? dbl(e.inlier1, 3) : na)      << S
        << (e.valid2 && e.inlier2 >= 0 ? dbl(e.inlier2, 3) : na)      << S
        << dbl(e.roi1Start_mm, 2) << S
        << dbl(e.roi1End_mm,   2) << S
        << dbl(e.roi2Start_mm, 2) << S
        << dbl(e.roi2End_mm,   2)
        << "\n";

    m_stream.flush();
    ++m_rowCount;

    emit rowWritten(m_rowCount);
}
