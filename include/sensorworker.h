/****************************************************************************
** sensorworker.h
** QThread worker for non-blocking sensor communication
****************************************************************************/
#pragma once

#include <QThread>
#include <QMutex>
#include <QAtomicInt>
#include <QString>
#include <vector>
#include "types.h"     // ProfilePoint, RoiRect (no Windows headers)
// NOTE: vcprotocol.h includes winsock2.h which pollutes the namespace.
// Forward-declare VcProtocol here; the real include goes in sensorworker.cpp only.
class VcProtocol;

struct AngleResult {
    bool    valid = false;
    double  phi = 0.0;
    double  phiAccuracy = 0.0;
    int     points = 0;
    double  detLen = 0.0;
    int     error = 0;
    double  cx = 0.0, cy = 0.0, lineB = 0.0;
    QString rawString;
};

class SensorWorker : public QThread
{
    Q_OBJECT
public:
    explicit SensorWorker(QObject *parent = nullptr);
    ~SensorWorker() override;

    void startCapture(const QString &ip, uint16_t port,
                      const RoiRect &roi1, const RoiRect &roi2);
    void stopCapture();

    bool isRunning() const { return m_running.loadAcquire(); }

signals:
    void profileReady(std::vector<ProfilePoint> points);
    void angleReady(AngleResult result);
    void connected();
    void disconnected();
    void errorOccurred(QString message);
    void statusMessage(QString msg);

protected:
    void run() override;

private:
    void sendInit(VcProtocol &proto, const RoiRect &roi1, const RoiRect &roi2);
    AngleResult parseProductString(const QString &s);

    QAtomicInt  m_running{0};
    QAtomicInt  m_stopRequested{0};

    QString     m_ip;
    uint16_t    m_port = 1096;
    RoiRect     m_roi1, m_roi2;
    QMutex      m_roiMutex;

public:
    void updateRois(const RoiRect &roi1, const RoiRect &roi2);
};
