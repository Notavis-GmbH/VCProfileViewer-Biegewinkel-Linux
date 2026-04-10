#pragma once
#include <vector>

#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QSlider>
#include <QCheckBox>
#include <QToolButton>
#include <QComboBox>
#include <QSettings>
#include <memory>

#include "types.h"      // ProfilePoint, RoiRect
#include "sensorworker.h"
#include "profilewidget.h"
#include "jsonplayer.h"
#include "measurementlogger.h"

// ──────────────────────────────────────────────
//  Source mode
// ──────────────────────────────────────────────
enum class SourceMode { LiveSensor, JsonPlayback };

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Live sensor
    void onConnectClicked();
    void onDisconnectClicked();
    void onSensorData(const std::vector<ProfilePoint> &points);
    void onAngleReady(AngleResult result);
    void onSensorError(const QString &msg);
    void onSensorConnected();
    void onSensorDisconnected();

    // ROI
    void onRoi1Changed();
    void onRoi2Changed();
    void onRoiDrawn(int roiIndex, RoiRect r);

    // JSON Playback
    void onSourceModeChanged();
    void onBrowseJsonFolder();
    void onPlayClicked();
    void onStopClicked();
    void onStepForwardClicked();
    void onStepBackClicked();
    void onSpeedSliderChanged(int value);
    void onJsonFrameChanged(int index, int total);
    void onJsonFolderLoaded(int frameCount);
    void onJsonPlaybackStarted();
    void onJsonPlaybackStopped();
    void onJsonProfileReady(const std::vector<ProfilePoint> &points);

    // Measurement log
    void onLogToggle(bool checked);

    // Quadrant selection
    void onQuadrantSelected(AngleQuadrant q);

    // JSON Profile Recorder
    void onRecordToggle(bool checked);
    void onRecordBrowse();
    void onLogBrowse();
    void onLogRowWritten(int row);

    // Preset management
    void onPresetSelected(int index);
    void onPresetSave();
    void onPresetRename();
    void onPresetDelete();
    void onLockToggle(bool checked);

private:
    void buildUi();
    void buildSensorGroup(QWidget *parent, class QVBoxLayout *layout);
    void buildRoiGroup(QWidget *parent, class QVBoxLayout *layout);
    void buildPlaybackGroup(QWidget *parent, class QVBoxLayout *layout);
    void buildLogGroup(QWidget *parent, class QVBoxLayout *layout);
    void buildPresetGroup(QWidget *parent, class QVBoxLayout *layout);
    void buildRecorderGroup(QWidget *parent, class QVBoxLayout *layout);
    void recordFrame(const std::vector<ProfilePoint> &pts);
    void buildStatusBar();

    // Preset helpers
    QString presetsPath() const;
    void    refreshPresetCombo();
    void    applyPreset(const QString &name);
    void    writePreset(const QString &name);

    void applySourceMode();
    void updateConnectButtons(bool connected);
    void updatePlayButtons(bool playing);
    void computeAndDisplayFitLines(const std::vector<ProfilePoint> &pts);
    void updateAngleDisplay(const FitLine &fl1, const FitLine &fl2);
    void saveSettings();
    void loadSettings();
    QString settingsPath() const;

    // ── Widgets ───────────────────────────────
    ProfileWidget  *m_profileWidget = nullptr;

    // Source selector
    QRadioButton   *m_rbLive       = nullptr;
    QRadioButton   *m_rbPlayback   = nullptr;

    // Sensor group
    QGroupBox      *m_sensorGroup  = nullptr;
    QLineEdit      *m_editIp       = nullptr;
    QLineEdit      *m_editPort     = nullptr;
    QPushButton    *m_btnConnect   = nullptr;
    QPushButton    *m_btnDisconnect= nullptr;
    QLabel         *m_lblSensorStatus = nullptr;

    // ROI group
    QGroupBox      *m_roiGroup     = nullptr;
    QDoubleSpinBox *m_roi1Start    = nullptr;
    QDoubleSpinBox *m_roi1End      = nullptr;
    QComboBox      *m_roi1Method   = nullptr;   // OLS / RANSAC / Hough / Auto
    QLabel         *m_lblMethod1   = nullptr;   // shows chosen method in Auto mode
    QDoubleSpinBox *m_roi2Start    = nullptr;
    QDoubleSpinBox *m_roi2End      = nullptr;
    QComboBox      *m_roi2Method   = nullptr;
    QLabel         *m_lblMethod2   = nullptr;   // shows chosen method in Auto mode

    // Result display
    QLabel         *m_lblAngle     = nullptr;

    // Quadrant selection buttons (replace Phi1/Phi2)
    QPushButton    *m_btnQuadTL    = nullptr;  // top-left
    QPushButton    *m_btnQuadTR    = nullptr;  // top-right
    QPushButton    *m_btnQuadBL    = nullptr;  // bottom-left
    QPushButton    *m_btnQuadBR    = nullptr;  // bottom-right
    AngleQuadrant   m_angleQuadrant = AngleQuadrant::TopLeft;
    std::vector<ProfilePoint> m_lastProfilePts;  // cached for quadrant re-computation
    QElapsedTimer   m_fitRateTimer;  // throttle fit computation to ~30 Hz
    bool            m_fitPending = false;

    // JSON playback group
    QGroupBox      *m_playbackGroup= nullptr;
    QLineEdit      *m_editFolder   = nullptr;
    QPushButton    *m_btnBrowse    = nullptr;
    QPushButton    *m_btnPlay      = nullptr;
    QPushButton    *m_btnStop      = nullptr;
    QPushButton    *m_btnStepBack  = nullptr;
    QPushButton    *m_btnStepFwd   = nullptr;
    QSlider        *m_speedSlider  = nullptr;
    QLabel         *m_lblSpeed     = nullptr;
    QLabel         *m_lblFrameInfo = nullptr;

    // Log group
    QGroupBox      *m_logGroup      = nullptr;
    QLineEdit      *m_editLogPath   = nullptr;
    QPushButton    *m_btnLogBrowse  = nullptr;
    QPushButton    *m_btnLogToggle  = nullptr;
    QLabel         *m_lblLogStatus  = nullptr;

    // Preset group
    QGroupBox      *m_presetGroup   = nullptr;
    QPushButton    *m_btnLock       = nullptr;  // settings lock
    bool            m_locked        = false;
    QComboBox      *m_cmbPresets    = nullptr;
    QPushButton    *m_btnPresetSave = nullptr;
    QPushButton    *m_btnPresetRen  = nullptr;
    QPushButton    *m_btnPresetDel  = nullptr;
    bool            m_presetLoading = false;  // suppress recursive signals during applyPreset

    // JSON Profile Recorder
    QGroupBox      *m_recorderGroup    = nullptr;
    QLineEdit      *m_editRecordFolder = nullptr;
    QPushButton    *m_btnRecordBrowse  = nullptr;
    QPushButton    *m_btnRecordToggle  = nullptr;
    QLabel         *m_lblRecordStatus  = nullptr;
    QSpinBox       *m_spinMaxFrames    = nullptr;
    bool            m_recording        = false;
    int             m_recordCount      = 0;
    QString         m_recordFolder;

    // ── Backend objects ───────────────────────
    SensorWorker        *m_sensorWorker = nullptr;
    JsonPlayer          *m_jsonPlayer   = nullptr;
    MeasurementLogger   *m_logger       = nullptr;

    SourceMode      m_sourceMode   = SourceMode::LiveSensor;
};
