#pragma once
/****************************************************************************
** MeasurementLogger
**
** Writes one CSV row per measurement frame:
**
**  Timestamp,Phi1_deg,Phi2_deg,BendingAngle_deg,
**  RMS1_um,RMS2_um,MaxRes1_um,MaxRes2_um,
**  Method1,Method2,Inlier1,Inlier2,
**  ROI1_Start_mm,ROI1_End_mm,ROI2_Start_mm,ROI2_End_mm
**
** Thread-safety: append() is NOT thread-safe – call only from the GUI thread
** (all profile callbacks already run on the GUI thread via Qt::QueuedConnection).
****************************************************************************/

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QDateTime>

struct LogEntry {
    QDateTime  timestamp;

    // ROI 1
    double phi1_deg     = 0.0;
    double rms1_um      = 0.0;   // in µm
    double maxRes1_um   = 0.0;
    QString method1;              // "OLS" / "RANSAC" / "Hough" / "Auto→RANSAC" …
    double inlier1      = -1.0;  // -1 = not applicable (non-Auto mode)
    double roi1Start_mm = 0.0;
    double roi1End_mm   = 0.0;
    bool   valid1       = false;

    // ROI 2
    double phi2_deg     = 0.0;
    double rms2_um      = 0.0;
    double maxRes2_um   = 0.0;
    QString method2;
    double inlier2      = -1.0;
    double roi2Start_mm = 0.0;
    double roi2End_mm   = 0.0;
    bool   valid2       = false;

    // Combined
    double bendingAngle_deg = 0.0;
    bool   hasBending       = false;
};

class MeasurementLogger : public QObject
{
    Q_OBJECT
public:
    explicit MeasurementLogger(QObject *parent = nullptr);
    ~MeasurementLogger();

    // Open a new log file (creates/overwrites).
    // Returns true on success; error message in *errOut on failure.
    bool open(const QString &filePath, QString *errOut = nullptr);

    // Close the current log file (flushes and closes).
    void close();

    bool isOpen() const { return m_file.isOpen(); }
    QString filePath() const { return m_file.fileName(); }

    // Append one measurement row. No-op if file not open or entry not valid.
    void append(const LogEntry &entry);

    // Total rows written since last open()
    int rowCount() const { return m_rowCount; }

signals:
    // Emitted after every successful write (row number, 1-based)
    void rowWritten(int rowNumber);

    // Emitted when the file is opened or closed
    void logOpened(const QString &path);
    void logClosed(const QString &path, int totalRows);

private:
    void writeHeader();

    QFile        m_file;
    QTextStream  m_stream;
    int          m_rowCount = 0;

    static constexpr char SEP = ';';   // semicolon – safe for European Excel locale
};
