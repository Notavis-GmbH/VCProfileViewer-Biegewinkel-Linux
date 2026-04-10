/****************************************************************************
** sensorworker.cpp
****************************************************************************/
#include "sensorworker.h"
#include "vcprotocol.h"  // must come before Qt headers to get winsock2 first
#include <QMutexLocker>
#include <QString>
#include <QRegularExpression>
#include <cstring>

SensorWorker::SensorWorker(QObject *parent)
    : QThread(parent)
{}

SensorWorker::~SensorWorker()
{
    stopCapture();
    wait(3000);
}

void SensorWorker::startCapture(const QString &ip, uint16_t port,
                                 const RoiRect &roi1, const RoiRect &roi2)
{
    if (m_running.loadAcquire()) return;
    m_ip   = ip;
    m_port = port;
    {
        QMutexLocker lk(&m_roiMutex);
        m_roi1 = roi1;
        m_roi2 = roi2;
    }
    m_stopRequested.storeRelease(0);
    start();
}

void SensorWorker::stopCapture()
{
    m_stopRequested.storeRelease(1);
}

void SensorWorker::updateRois(const RoiRect &roi1, const RoiRect &roi2)
{
    QMutexLocker lk(&m_roiMutex);
    m_roi1 = roi1;
    m_roi2 = roi2;
}

// -----------------------------------------------------------------------
// Build and send initialization parameters
// -----------------------------------------------------------------------
void SensorWorker::sendInit(VcProtocol &proto, const RoiRect &roi1, const RoiRect &roi2)
{
    // ---- STOP first ----
    proto.sendCommand(CMD_STOP, "");
    QThread::msleep(100);

    // ---- Sensor parameters ----
    QString sensorParams =
        "LaserMode=1\n"
        "ShutterTime=50\n"
        "GainVal=0\n"
        "ExposureMode=1\n"          // AUTO_EXPOSURE
        "AutoShutterVal=128\n"
        "AutoShutterMin=1\n"
        "AutoShutterMax=5000\n"
        "RlcThresh=10\n"
        "LaserSelect=0\n"
        "OptWidth=8\n"
        "MinWidth=2\n"
        "MaxWidth=30\n"
        "SpeckleFilterDx=5\n"
        "SpeckleFilterDy=3\n"
        "MedianFilter=7\n"
        "ReflexionFilter=-1\n"
        "LaserMaskFilter=31\n"
        "EthernetSendNoWait=1\n"
        "EthernetPackNr=1\n"
        "AutoTriggerFPS=50.0\n"
        "DataMode=12\n"             // DataMode=4(profile mm) + DataMode=8(product string)
        "NbrLines=-1\n";            // continuous

    proto.sendCommand(CMD_RECEIVE_SENSOR_DATA,
                      sensorParams.toStdString(), 1);

    std::string resp;
    bool sensorRespOk = proto.readStringResponse(CMD_RECEIVE_SENSOR_DATA, 1, resp, 2000);
    qInfo().noquote() << QString("[Sensor] SensorParam response: ok=%1  body=%2")
        .arg(sensorRespOk ? "true" : "false")
        .arg(QString::fromStdString(resp).left(120));

    // ---- Product parameters ----
    // J00: ROI 1 line detection, J01: ROI 2 line detection
    QString prodParams = "AdjustSensorRoi=2\n";

    // ROI 1
    prodParams += "J00_ProductType=1\n";
    prodParams += "J00_LineAlgoMode=2\n";
    prodParams += "J00_BestLineFilter=100\n";
    prodParams += "J00_HistoLineFilter=1\n";
    prodParams += "J00_PhiMin=0.0\n";

    if (roi1.valid) {
        prodParams += QString("J00_PolygonNr=2\n");
        prodParams += QString("J00_PolygonPoint_X00_MM=%1\n").arg(roi1.xMin, 0, 'f', 2);
        prodParams += QString("J00_PolygonPoint_Z00_MM=%1\n").arg(roi1.zMin, 0, 'f', 2);
        prodParams += QString("J00_PolygonPoint_X01_MM=%1\n").arg(roi1.xMax, 0, 'f', 2);
        prodParams += QString("J00_PolygonPoint_Z01_MM=%1\n").arg(roi1.zMax, 0, 'f', 2);
    } else {
        // Default: full width
        prodParams += "J00_PolygonNr=2\n";
        prodParams += "J00_PolygonPoint_X00_MM=-500.0\n";
        prodParams += "J00_PolygonPoint_Z00_MM=0.0\n";
        prodParams += "J00_PolygonPoint_X01_MM=500.0\n";
        prodParams += "J00_PolygonPoint_Z01_MM=500.0\n";
    }

    // ROI 2
    prodParams += "J01_ProductType=1\n";
    prodParams += "J01_LineAlgoMode=2\n";
    prodParams += "J01_BestLineFilter=100\n";
    prodParams += "J01_HistoLineFilter=1\n";
    prodParams += "J01_PhiMin=0.0\n";

    if (roi2.valid) {
        prodParams += "J01_PolygonNr=2\n";
        prodParams += QString("J01_PolygonPoint_X00_MM=%1\n").arg(roi2.xMin, 0, 'f', 2);
        prodParams += QString("J01_PolygonPoint_Z00_MM=%1\n").arg(roi2.zMin, 0, 'f', 2);
        prodParams += QString("J01_PolygonPoint_X01_MM=%1\n").arg(roi2.xMax, 0, 'f', 2);
        prodParams += QString("J01_PolygonPoint_Z01_MM=%1\n").arg(roi2.zMax, 0, 'f', 2);
    } else {
        prodParams += "J01_PolygonNr=2\n";
        prodParams += "J01_PolygonPoint_X00_MM=-500.0\n";
        prodParams += "J01_PolygonPoint_Z00_MM=0.0\n";
        prodParams += "J01_PolygonPoint_X01_MM=500.0\n";
        prodParams += "J01_PolygonPoint_Z01_MM=500.0\n";
    }

    proto.sendCommand(CMD_RECEIVE_PRODUCT_DATA,
                      prodParams.toStdString(), 2);
    bool prodRespOk = proto.readStringResponse(CMD_RECEIVE_PRODUCT_DATA, 2, resp, 2000);
    qInfo().noquote() << QString("[Sensor] ProductParam response: ok=%1  body=%2")
        .arg(prodRespOk ? "true" : "false")
        .arg(QString::fromStdString(resp).left(120));

    emit statusMessage("Sensor initialized, capturing...");
}

// -----------------------------------------------------------------------
// Parse product result string
// e.g.: JOB 0.0001: Phi=156.36 Cx=00.4011 Cy=-0.9161 B=-0104.19 ...
//       JOB 1.0001: Phi=156.36 ...
// -----------------------------------------------------------------------
AngleResult SensorWorker::parseProductString(const QString &s)
{
    AngleResult r;
    r.rawString = s;

    QRegularExpression rePhi(R"(Phi=([\-\d\.]+))");
    QRegularExpression rePoints(R"(Points=(\d+))");
    QRegularExpression reDetLen(R"(DetLen=([\d\.]+))");
    QRegularExpression redPhi(R"(dPhi=([\d\.]+))");
    QRegularExpression reError(R"(Error=([\-\d]+))");
    QRegularExpression reCx(R"(Cx=([\-\d\.]+))");
    QRegularExpression reCy(R"(Cy=([\-\d\.]+))");
    QRegularExpression reB(R"(B=([\-\d\.]+))");

    auto m = rePhi.match(s);
    if (m.hasMatch()) { r.phi = m.captured(1).toDouble(); r.valid = true; }

    m = rePoints.match(s);
    if (m.hasMatch()) r.points = m.captured(1).toInt();

    m = reDetLen.match(s);
    if (m.hasMatch()) r.detLen = m.captured(1).toDouble();

    m = redPhi.match(s);
    if (m.hasMatch()) r.phiAccuracy = m.captured(1).toDouble();

    m = reError.match(s);
    if (m.hasMatch()) r.error = m.captured(1).toInt();

    m = reCx.match(s);
    if (m.hasMatch()) r.cx = m.captured(1).toDouble();

    m = reCy.match(s);
    if (m.hasMatch()) r.cy = m.captured(1).toDouble();

    m = reB.match(s);
    if (m.hasMatch()) r.lineB = m.captured(1).toDouble();

    return r;
}

// -----------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------
void SensorWorker::run()
{
    m_running.storeRelease(1);

    VcProtocol proto;
    emit statusMessage(QString("Connecting to %1:%2 ...").arg(m_ip).arg(m_port));

    if (!proto.connect(m_ip.toStdString(), m_port)) {
        emit errorOccurred(QString("Cannot connect to %1:%2").arg(m_ip).arg(m_port));
        m_running.storeRelease(0);
        return;
    }

    emit connected();
    emit statusMessage("Connected.");

    RoiRect roi1, roi2;
    {
        QMutexLocker lk(&m_roiMutex);
        roi1 = m_roi1;
        roi2 = m_roi2;
    }

    sendInit(proto, roi1, roi2);

    bool reinit = false;

    while (!m_stopRequested.loadAcquire()) {

        // Check if ROIs changed
        {
            QMutexLocker lk(&m_roiMutex);
            if (m_roi1.valid != roi1.valid ||
                m_roi1.xMin != roi1.xMin   || m_roi1.xMax != roi1.xMax ||
                m_roi1.zMin != roi1.zMin   || m_roi1.zMax != roi1.zMax ||
                m_roi2.valid != roi2.valid ||
                m_roi2.xMin != roi2.xMin   || m_roi2.xMax != roi2.xMax ||
                m_roi2.zMin != roi2.zMin   || m_roi2.zMax != roi2.zMax)
            {
                roi1 = m_roi1;
                roi2 = m_roi2;
                reinit = true;
            }
        }

        if (reinit) {
            emit statusMessage("Updating ROIs...");
            sendInit(proto, roi1, roi2);
            reinit = false;
        }

        // Receive data frame
        int dataMode = 0, resultCnt = 0;
        uint64_t ts = 0;
        std::vector<uint8_t> payload;

        bool ok = proto.readDataFrame(dataMode, resultCnt, ts, payload, 600);
        if (!ok) continue;

        qDebug().noquote() << QString("[Sensor] Frame: dataMode=%1  resultCnt=%2  bytes=%3")
            .arg(dataMode).arg(resultCnt).arg(payload.size());

        // DataMode=9 observed (profile+product combined).
        // resultCnt = pointCount from frame header offset 20.
        // Payload = resultCnt float pairs (x,z), then optional product string.

        // ── Profile data ─────────────────────────────────────────────────────
        if (resultCnt > 0 &&
            payload.size() >= static_cast<size_t>(resultCnt) * 8)
        {
            std::vector<ProfilePoint> pts;
            pts.reserve(resultCnt);
            for (int i = 0; i < resultCnt; ++i) {
                float x, z;
                memcpy(&x, payload.data() + i * 8,     4);
                memcpy(&z, payload.data() + i * 8 + 4, 4);
                if (x > -9999.f && z > -9999.f)
                    pts.push_back({x, z});
            }
            qDebug().noquote() << QString("[Sensor] Profile: %1 valid points (cnt=%2)")
                .arg(pts.size()).arg(resultCnt);
            if (!pts.empty()) emit profileReady(pts);

            // ── Product string (appended after profile floats) ───────────────
            const size_t profileBytes = static_cast<size_t>(resultCnt) * 8;
            if (payload.size() > profileBytes) {
                const QString text = QString::fromLatin1(
                    reinterpret_cast<const char*>(payload.data() + profileBytes),
                    static_cast<int>(payload.size() - profileBytes));
                const QStringList lines2 = text.split('\n', Qt::SkipEmptyParts);
                for (const auto &line : lines2) {
                    if (line.startsWith("JOB")) {
                        qDebug().noquote() << "[Sensor] Product:" << line.left(80);
                        AngleResult ar = parseProductString(line);
                        emit angleReady(ar);
                    }
                }
            }
        }
    }  // while (!m_stopRequested)

    // Stop sensor
    proto.sendCommand(CMD_STOP, "");
    QThread::msleep(100);
    proto.disconnect();

    m_running.storeRelease(0);
    emit disconnected();
    emit statusMessage("Disconnected.");
}
