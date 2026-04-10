#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSlider>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QThread>
#include <QCloseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSpinBox>
#include <QFont>
#include <QFrame>
#include <QScrollArea>
#include <QShortcut>
#include <QKeySequence>
#include "licensemanager.h"
#include "licensedialog.h"
#include "keygen_config.h"
#include <QStatusBar>
#include <QSizePolicy>
#include <cmath>
#include <numeric>
#include <utility>
#include <random>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
//  Local helper: least-squares line fit through profile points inside a ROI
//  Returns FitLine with z = slope*x + intercept
// ──────────────────────────────────────────────────────────────────────────────
static FitLine fitLineInRoi(const std::vector<ProfilePoint> &pts,
                             const RoiRect &roi,
                             std::vector<std::pair<double,double>> *residualsOut = nullptr)
{
    FitLine fl;
    if (!roi.valid) return fl;

    // Collect points inside the ROI x-range
    std::vector<double> xs, zs;
    for (const auto &p : pts) {
        if (p.x_mm >= roi.xMin && p.x_mm <= roi.xMax)
        { xs.push_back(p.x_mm); zs.push_back(p.z_mm); }
    }
    if (xs.size() < 2) return fl;

    // Ordinary least squares: z = slope*x + intercept
    double n    = static_cast<double>(xs.size());
    double sumX = 0, sumZ = 0, sumXX = 0, sumXZ = 0;
    for (size_t i = 0; i < xs.size(); ++i) {
        sumX  += xs[i];
        sumZ  += zs[i];
        sumXX += xs[i] * xs[i];
        sumXZ += xs[i] * zs[i];
    }
    double denom = n * sumXX - sumX * sumX;
    if (std::abs(denom) < 1e-12) return fl;  // vertical / degenerate

    fl.slope     = (n * sumXZ - sumX * sumZ) / denom;
    fl.intercept = (sumZ - fl.slope * sumX) / n;
    fl.xMin      = roi.xMin;
    fl.xMax      = roi.xMax;
    fl.phi       = std::atan(fl.slope) * 180.0 / M_PI;

    // Compute residuals
    double sumSq = 0.0;
    if (residualsOut) residualsOut->clear();
    for (size_t i = 0; i < xs.size(); ++i) {
        double res = std::abs(zs[i] - (fl.slope * xs[i] + fl.intercept));
        sumSq += res * res;
        fl.maxResidual = std::max(fl.maxResidual, res);
        if (residualsOut)
            residualsOut->emplace_back(xs[i], res);
    }
    fl.rmsResidual = std::sqrt(sumSq / n);
    fl.valid       = true;
    return fl;
}

// ──────────────────────────────────────────────────────────────────────────────
//  Shared: fill residuals + rms/max from a known slope/intercept
// ──────────────────────────────────────────────────────────────────────────────
static void computeResiduals(FitLine &fl,
                              const std::vector<double> &xs,
                              const std::vector<double> &zs,
                              std::vector<std::pair<double,double>> *out)
{
    if (out) out->clear();
    double sumSq = 0.0;
    for (size_t i = 0; i < xs.size(); ++i) {
        double res = std::abs(zs[i] - (fl.slope * xs[i] + fl.intercept));
        sumSq += res * res;
        fl.maxResidual = std::max(fl.maxResidual, res);
        if (out) out->emplace_back(xs[i], res);
    }
    fl.rmsResidual = xs.empty() ? 0.0 : std::sqrt(sumSq / xs.size());
}

// ──────────────────────────────────────────────────────────────────────────────
//  RANSAC line fit
//  Iteratively picks 2 random points, counts inliers within threshold,
//  then refines the best consensus set with OLS.
//  threshold_mm: inlier distance from line [mm]  (default 0.5 mm)
//  iterations:   number of random trials         (default 200)
// ──────────────────────────────────────────────────────────────────────────────
static FitLine ransacLineInRoi(const std::vector<ProfilePoint> &pts,
                                const RoiRect &roi,
                                std::vector<std::pair<double,double>> *residualsOut = nullptr,
                                double threshMm = 0.5,
                                int    iterations = 200)
{
    FitLine fl;
    if (!roi.valid) return fl;

    std::vector<double> xs, zs;
    for (const auto &p : pts)
        if (p.x_mm >= roi.xMin && p.x_mm <= roi.xMax)
        { xs.push_back(p.x_mm); zs.push_back(p.z_mm); }

    const int N = static_cast<int>(xs.size());
    if (N < 4) return fl;   // need at least 4 for meaningful result

    std::mt19937 rng(42);   // deterministic seed – same result every frame
    std::uniform_int_distribution<int> dist(0, N - 1);

    int    bestCount  = 0;
    double bestSlope  = 0.0, bestIntercept = 0.0;

    for (int iter = 0; iter < iterations; ++iter) {
        // Sample 2 distinct points
        int a = dist(rng), b = dist(rng);
        while (b == a) b = dist(rng);

        double dx = xs[b] - xs[a];
        if (std::abs(dx) < 1e-9) continue;   // vertical sample – skip

        double s  = (zs[b] - zs[a]) / dx;
        double ic = zs[a] - s * xs[a];

        // Count inliers
        int count = 0;
        for (int i = 0; i < N; ++i) {
            double res = std::abs(zs[i] - (s * xs[i] + ic));
            if (res <= threshMm) ++count;
        }
        if (count > bestCount) {
            bestCount = count;  bestSlope = s;  bestIntercept = ic;
        }
    }

    if (bestCount < 2) return fl;

    // Refine with OLS on the inlier set
    std::vector<double> ixs, izs;
    ixs.reserve(bestCount);  izs.reserve(bestCount);
    for (int i = 0; i < N; ++i) {
        double res = std::abs(zs[i] - (bestSlope * xs[i] + bestIntercept));
        if (res <= threshMm) { ixs.push_back(xs[i]); izs.push_back(zs[i]); }
    }

    double n = static_cast<double>(ixs.size());
    double sumX = 0, sumZ = 0, sumXX = 0, sumXZ = 0;
    for (size_t i = 0; i < ixs.size(); ++i) {
        sumX += ixs[i]; sumZ += izs[i];
        sumXX += ixs[i]*ixs[i]; sumXZ += ixs[i]*izs[i];
    }
    double denom = n*sumXX - sumX*sumX;
    if (std::abs(denom) < 1e-12) return fl;

    fl.slope     = (n*sumXZ - sumX*sumZ) / denom;
    fl.intercept = (sumZ - fl.slope * sumX) / n;
    fl.xMin      = roi.xMin;  fl.xMax = roi.xMax;
    fl.phi       = std::atan(fl.slope) * 180.0 / M_PI;
    fl.method    = FitMethod::RANSAC;
    computeResiduals(fl, xs, zs, residualsOut);
    fl.valid = true;
    return fl;
}

// ──────────────────────────────────────────────────────────────────────────────
//  Hough Transform line fit
//  Discretises (slope, intercept) space via the normal parametrisation
//  (rho, theta). Finds the peak bin, then refines with OLS on all points
//  within threshMm of the Hough line.
//
//  thetaBins: angular resolution of accumulator (default 360 = 0.5° steps)
//  rhoBins:   rho resolution (default 400 bins over the data range)
//  threshMm:  inlier band for OLS refinement [mm]
// ──────────────────────────────────────────────────────────────────────────────
static FitLine houghLineInRoi(const std::vector<ProfilePoint> &pts,
                               const RoiRect &roi,
                               std::vector<std::pair<double,double>> *residualsOut = nullptr,
                               int    thetaBins = 360,
                               int    rhoBins   = 400,
                               double threshMm  = 0.5)
{
    FitLine fl;
    if (!roi.valid) return fl;

    std::vector<double> xs, zs;
    for (const auto &p : pts)
        if (p.x_mm >= roi.xMin && p.x_mm <= roi.xMax)
        { xs.push_back(p.x_mm); zs.push_back(p.z_mm); }

    const int N = static_cast<int>(xs.size());
    if (N < 4) return fl;

    // Normalise coordinates to [-1, 1] for numerical stability
    double xc = 0.0, zc = 0.0;
    for (int i = 0; i < N; ++i) { xc += xs[i]; zc += zs[i]; }
    xc /= N;  zc /= N;

    double xScale = 1.0, zScale = 1.0;
    for (int i = 0; i < N; ++i) {
        xScale = std::max(xScale, std::abs(xs[i] - xc));
        zScale = std::max(zScale, std::abs(zs[i] - zc));
    }

    // rho range in normalised coords
    const double rhoMax = std::sqrt(2.0);   // max |rho| for unit circle
    const double rhoStep  = 2.0 * rhoMax / rhoBins;
    const double thetaStep = M_PI / thetaBins;

    // Accumulator (flat vector)
    std::vector<int> acc(static_cast<size_t>(thetaBins * rhoBins), 0);

    for (int i = 0; i < N; ++i) {
        double xn = (xs[i] - xc) / xScale;
        double zn = (zs[i] - zc) / zScale;
        for (int t = 0; t < thetaBins; ++t) {
            double theta = t * thetaStep;
            double rho   = xn * std::cos(theta) + zn * std::sin(theta);
            int    rBin  = static_cast<int>((rho + rhoMax) / rhoStep);
            if (rBin >= 0 && rBin < rhoBins)
                ++acc[static_cast<size_t>(t * rhoBins + rBin)];
        }
    }

    // Find peak bin
    auto peakIt = std::max_element(acc.begin(), acc.end());
    int  peakIdx  = static_cast<int>(peakIt - acc.begin());
    int  peakT    = peakIdx / rhoBins;
    int  peakR    = peakIdx % rhoBins;

    double theta = peakT * thetaStep;
    double rho   = (peakR * rhoStep + rhoStep * 0.5) - rhoMax;

    // Convert (rho, theta) in normalised space back to (slope, intercept) in mm
    // Line: xn*cos(t) + zn*sin(t) = rho
    // If sin(theta) != 0:  zn = (rho - xn*cos(t)) / sin(t)
    //   => z*zScale+zc = [(rho - (x-xc)/xScale * cos(t)) / sin(t)] * zScale + zc
    double sinT = std::sin(theta);
    double cosT = std::cos(theta);

    double slope_n, intercept_n;  // in normalised coords
    if (std::abs(sinT) > 1e-6) {
        slope_n     = -cosT / sinT;
        intercept_n =  rho  / sinT;
    } else {
        // Near-vertical: line is almost x = const – fallback to OLS
        return fl;
    }

    // De-normalise: zn = slope_n * xn + intercept_n
    //               (z-zc)/zScale = slope_n * (x-xc)/xScale + intercept_n
    //               z = slope_n*(zScale/xScale)*x
    //                 + intercept_n*zScale - slope_n*(zScale/xScale)*xc + zc
    fl.slope     = slope_n * (zScale / xScale);
    fl.intercept = intercept_n * zScale - fl.slope * xc + zc;
    fl.xMin      = roi.xMin;  fl.xMax = roi.xMax;
    fl.phi       = std::atan(fl.slope) * 180.0 / M_PI;
    fl.method    = FitMethod::Hough;

    // OLS refinement on Hough inliers
    std::vector<double> ixs, izs;
    for (int i = 0; i < N; ++i) {
        double res = std::abs(zs[i] - (fl.slope*xs[i] + fl.intercept));
        if (res <= threshMm) { ixs.push_back(xs[i]); izs.push_back(zs[i]); }
    }
    if (ixs.size() >= 2) {
        double n = static_cast<double>(ixs.size());
        double sX=0,sZ=0,sXX=0,sXZ=0;
        for (size_t i=0;i<ixs.size();++i){
            sX+=ixs[i]; sZ+=izs[i]; sXX+=ixs[i]*ixs[i]; sXZ+=ixs[i]*izs[i];
        }
        double d = n*sXX - sX*sX;
        if (std::abs(d) > 1e-12) {
            fl.slope     = (n*sXZ - sX*sZ) / d;
            fl.intercept = (sZ - fl.slope*sX) / n;
            fl.phi       = std::atan(fl.slope) * 180.0 / M_PI;
        }
    }

    computeResiduals(fl, xs, zs, residualsOut);
    fl.valid = true;
    return fl;
}

// ──────────────────────────────────────────────────────────────────────────────
//  Auto-select: run all three methods, choose based on inlier ratio + RMS
//
//  Decision thresholds (all configurable via constants below):
//    inlierRatio ≥ AUTO_INLIER_OLS     → OLS   (clean data)
//    inlierRatio ≥ AUTO_INLIER_RANSAC  → RANSAC (some outliers)
//    inlierRatio <  AUTO_INLIER_RANSAC → Hough  (fragmented / many gaps)
//  Additional override: if RANSAC RMS > OLS RMS * AUTO_RMS_FACTOR → Hough
// ──────────────────────────────────────────────────────────────────────────────
static constexpr double AUTO_INLIER_OLS    = 0.90;  // ≥90% inliers → OLS
static constexpr double AUTO_INLIER_RANSAC = 0.60;  // ≥60% inliers → RANSAC, else Hough
static constexpr double AUTO_THRESH_MM     = 0.50;  // inlier band [mm]
static constexpr double AUTO_RMS_FACTOR    = 1.50;  // RANSAC/OLS RMS ratio for Hough fallback

static FitLine autoFitLine(const std::vector<ProfilePoint> &pts,
                           const RoiRect &roi,
                           std::vector<std::pair<double,double>> *residualsOut = nullptr)
{
    if (!roi.valid) return FitLine{};

    // Collect ROI points once
    std::vector<double> xs, zs;
    for (const auto &p : pts)
        if (p.x_mm >= roi.xMin && p.x_mm <= roi.xMax)
        { xs.push_back(p.x_mm); zs.push_back(p.z_mm); }

    const int N = static_cast<int>(xs.size());
    if (N < 4) return FitLine{};

    // ── Step 1: OLS ───────────────────────────────────────────────────────────
    FitLine olsFl = fitLineInRoi(pts, roi, nullptr);
    if (!olsFl.valid) return FitLine{};

    // Count OLS inliers
    int olsInliers = 0;
    for (int i = 0; i < N; ++i) {
        double res = std::abs(zs[i] - (olsFl.slope * xs[i] + olsFl.intercept));
        if (res <= AUTO_THRESH_MM) ++olsInliers;
    }
    double inlierRatioOls = static_cast<double>(olsInliers) / N;

    AutoSelectInfo info;
    info.olsRms = olsFl.rmsResidual;
    info.inlierRatioRansac = inlierRatioOls;  // reused below

    // ── Step 2: decide ───────────────────────────────────────────────────────
    if (inlierRatioOls >= AUTO_INLIER_OLS) {
        // Clean data – OLS is optimal
        info.chosen = FitMethod::OLS;
        std::snprintf(info.reason, sizeof(info.reason),
            "OLS: inlier=%.2f ≥ %.2f (sauberes Profil)",
            inlierRatioOls, AUTO_INLIER_OLS);
        FitLine fl = fitLineInRoi(pts, roi, residualsOut);
        fl.method   = FitMethod::OLS;
        fl.autoInfo = info;
        return fl;
    }

    // Need RANSAC or Hough – run RANSAC to get its RMS for comparison
    FitLine ransacFl = ransacLineInRoi(pts, roi, nullptr);
    info.ransacRms = ransacFl.valid ? ransacFl.rmsResidual : 1e9;

    // Count RANSAC inliers with the refined RANSAC line
    int ransacInliers = 0;
    if (ransacFl.valid) {
        for (int i = 0; i < N; ++i) {
            double res = std::abs(zs[i] - (ransacFl.slope * xs[i] + ransacFl.intercept));
            if (res <= AUTO_THRESH_MM) ++ransacInliers;
        }
    }
    info.inlierRatioRansac = static_cast<double>(ransacInliers) / N;

    bool useHough = (inlierRatioOls < AUTO_INLIER_RANSAC);
    // Also fall back to Hough if RANSAC didn’t improve over OLS significantly
    if (!useHough && ransacFl.valid &&
        ransacFl.rmsResidual > olsFl.rmsResidual * AUTO_RMS_FACTOR)
        useHough = true;

    if (!useHough) {
        // RANSAC
        info.chosen = FitMethod::RANSAC;
        std::snprintf(info.reason, sizeof(info.reason),
            "RANSAC: inlier(OLS)=%.2f < %.2f, inlier(RANSAC)=%.2f",
            inlierRatioOls, AUTO_INLIER_OLS, info.inlierRatioRansac);
        FitLine fl = ransacLineInRoi(pts, roi, residualsOut);
        fl.method   = FitMethod::RANSAC;
        fl.autoInfo = info;
        return fl;
    }

    // Hough
    FitLine houghFl = houghLineInRoi(pts, roi, nullptr);
    info.houghRms = houghFl.valid ? houghFl.rmsResidual : 1e9;

    int houghInliers = 0;
    if (houghFl.valid) {
        for (int i = 0; i < N; ++i) {
            double res = std::abs(zs[i] - (houghFl.slope * xs[i] + houghFl.intercept));
            if (res <= AUTO_THRESH_MM) ++houghInliers;
        }
    }
    info.inlierRatioHough = static_cast<double>(houghInliers) / N;
    info.chosen = FitMethod::Hough;
    std::snprintf(info.reason, sizeof(info.reason),
        "Hough: inlier(OLS)=%.2f < %.2f (fragmentiert)",
        inlierRatioOls, AUTO_INLIER_RANSAC);

    FitLine fl = houghLineInRoi(pts, roi, residualsOut);
    fl.method   = FitMethod::Hough;
    fl.autoInfo = info;
    return fl;
}

// ──────────────────────────────────────────────────────────────────────────────
//  Dispatch: call the right algorithm based on FitMethod
// ──────────────────────────────────────────────────────────────────────────────
static FitLine fitLine(const std::vector<ProfilePoint> &pts,
                       const RoiRect &roi,
                       FitMethod method,
                       std::vector<std::pair<double,double>> *residualsOut = nullptr)
{
    switch (method) {
        case FitMethod::RANSAC: return ransacLineInRoi(pts, roi, residualsOut);
        case FitMethod::Hough:  return houghLineInRoi (pts, roi, residualsOut);
        case FitMethod::Auto:   return autoFitLine    (pts, roi, residualsOut);
        default:                return fitLineInRoi   (pts, roi, residualsOut);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Construction
// ──────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
#ifndef BUILD_TIMESTAMP
#  define BUILD_TIMESTAMP "dev"
#endif
    setWindowTitle(QString("VC 3D Profile Viewer  v2.2-%1")
                   .arg(QString(BUILD_TIMESTAMP).replace(" ","-").replace(":","")));
    setMinimumSize(1100, 700);

    // ── Strg+Shift+R: Lizenz zurücksetzen ────────────────────────────────────
    auto *resetShortcut = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);
    connect(resetShortcut, &QShortcut::activated, this, [this]() {
        const auto btn = QMessageBox::question(
            this,
            tr("Lizenz zurücksetzen"),
            tr("Möchtest du die aktuelle Lizenz wirklich entfernen?\n\n"
               "Die Anwendung wird danach neu gestartet und du musst\n"
               "einen neuen Lizenzschlüssel eingeben oder eine neue\n"
               "Testversion starten."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (btn != QMessageBox::Yes) return;

        // Lizenz aus QSettings löschen
        QSettings settings;
        settings.remove(KeygenConfig::SETTINGS_LICENSE_KEY);
        settings.remove(KeygenConfig::SETTINGS_LICENSE_ID);
        settings.remove(KeygenConfig::SETTINGS_LICENSE_TYPE);
        settings.remove(KeygenConfig::SETTINGS_TRIAL_EXPIRY);
        settings.sync();

        // Lizenzdialog zeigen
        LicenseManager mgr;
        LicenseDialog dlg(&mgr, this);
        if (dlg.exec() != QDialog::Accepted) {
            // Kein gültiger Key → App beenden
            qApp->quit();
        }
    });

    // JSON player (no thread needed – all Qt slots, no blocking I/O)
    m_jsonPlayer = new JsonPlayer(this);

    // Measurement logger
    m_logger = new MeasurementLogger(this);
    m_fitRateTimer.start();
    connect(m_logger, &MeasurementLogger::rowWritten,  this, &MainWindow::onLogRowWritten);
    connect(m_logger, &MeasurementLogger::logOpened,   this, [this]{ m_lblLogStatus->setText("<b style='color:#00e676'>● Aufzeichnung: 0 Zeilen</b>"); });
    connect(m_logger, &MeasurementLogger::logClosed,   this, [this]{ m_lblLogStatus->setText("Bereit"); m_btnLogToggle->setChecked(false); m_btnLogToggle->setText("● Aufzeichnung starten"); m_btnLogToggle->setStyleSheet("background:#1a5c2e; color:white; padding:5px; border-radius:4px;"); });

    buildUi();
    buildStatusBar();
    loadSettings();
    applySourceMode();
}

MainWindow::~MainWindow()
{
    if (m_sensorWorker && m_sensorWorker->isRunning()) {
        m_sensorWorker->stopCapture();
        m_sensorWorker->wait(1500);  // destructor: brief blocking wait is acceptable
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  UI construction
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::buildUi()
{
    // ── Central split: left panel + profile chart ─────────────────────────
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(splitter);

    // ── LEFT PANEL ────────────────────────────────────────────────────────
    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(260);
    scroll->setMaximumWidth(400);  // wider to fit all controls
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget     *leftPanel  = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(10);

    // Source selector
    QGroupBox *sourceGroup = new QGroupBox("Datenquelle");
    QHBoxLayout *srcLayout = new QHBoxLayout(sourceGroup);
    m_rbLive     = new QRadioButton("Live Sensor");
    m_rbPlayback = new QRadioButton("JSON Wiedergabe");
    m_rbLive->setChecked(true);
    srcLayout->addWidget(m_rbLive);
    srcLayout->addWidget(m_rbPlayback);

    QButtonGroup *srcBtnGrp = new QButtonGroup(this);
    srcBtnGrp->addButton(m_rbLive,     0);
    srcBtnGrp->addButton(m_rbPlayback, 1);

    connect(m_rbLive,     &QRadioButton::toggled, this, &MainWindow::onSourceModeChanged);
    connect(m_rbPlayback, &QRadioButton::toggled, this, &MainWindow::onSourceModeChanged);

    leftLayout->addWidget(sourceGroup);

    // Preset group (Kopf)
    m_presetGroup = new QGroupBox("Typverwaltung");
    buildPresetGroup(leftPanel, leftLayout);

    // Sensor group
    m_sensorGroup = new QGroupBox("Sensor (Live)");
    buildSensorGroup(leftPanel, leftLayout);

    // ROI group
    m_roiGroup = new QGroupBox("ROI Einstellungen");
    buildRoiGroup(leftPanel, leftLayout);

    // Playback group
    m_playbackGroup = new QGroupBox("JSON Wiedergabe");
    buildPlaybackGroup(leftPanel, leftLayout);

    // Result display
    QGroupBox *resultGroup = new QGroupBox("Messergebnis");
    QVBoxLayout *resultLayout = new QVBoxLayout(resultGroup);

    m_lblAngle = new QLabel("—");
    m_lblAngle->setAlignment(Qt::AlignCenter);
    QFont angleFont = m_lblAngle->font();
    angleFont.setPointSize(28);
    angleFont.setBold(true);
    m_lblAngle->setFont(angleFont);
    m_lblAngle->setStyleSheet("color: #00e676; background: #1a1a2e; border-radius: 6px; padding: 6px;");

    QLabel *lblAngleCaption = new QLabel("Biegewinkel");
    lblAngleCaption->setAlignment(Qt::AlignCenter);
    lblAngleCaption->setStyleSheet("color: #888; font-size: 11px;");

    // ── Quadrant-Auswahl ──────────────────────────────────────────────
    QLabel *lblQuadCaption = new QLabel("Winkel-Quadrant (Messrichtung)");
    lblQuadCaption->setAlignment(Qt::AlignCenter);
    lblQuadCaption->setStyleSheet("color: #888; font-size: 10px;");

    auto makeQuadBtn = [](const QString &label, const QString &tip) {
        QPushButton *btn = new QPushButton(label);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setMinimumHeight(32);
        btn->setStyleSheet(
            "QPushButton { background:#2a2a3a; color:#ccc; border:1px solid #444; "
            "              border-radius:4px; font-size:16px; }"
            "QPushButton:checked { background:#1a3c5c; color:white; border:1px solid #4af; }"
            "QPushButton:hover   { background:#3a3a4a; }");
        return btn;
    };

    m_btnQuadTL = makeQuadBtn("◤", "Oben-Links (Innenwinkel oben)");
    m_btnQuadTR = makeQuadBtn("◥", "Oben-Rechts");
    m_btnQuadBL = makeQuadBtn("◣", "Unten-Links");
    m_btnQuadBR = makeQuadBtn("◢", "Unten-Rechts (Aussenwinkel unten)");
    m_btnQuadTL->setChecked(true);  // default

    QGridLayout *quadGrid = new QGridLayout;
    quadGrid->setSpacing(4);
    quadGrid->addWidget(m_btnQuadTL, 0, 0);
    quadGrid->addWidget(m_btnQuadTR, 0, 1);
    quadGrid->addWidget(m_btnQuadBL, 1, 0);
    quadGrid->addWidget(m_btnQuadBR, 1, 1);

    resultLayout->addWidget(lblAngleCaption);
    resultLayout->addWidget(m_lblAngle);
    resultLayout->addWidget(lblQuadCaption);
    resultLayout->addLayout(quadGrid);
    leftLayout->addWidget(resultGroup);

    // Connect quadrant buttons
    connect(m_btnQuadTL, &QPushButton::clicked, this, [this]{ onQuadrantSelected(AngleQuadrant::TopLeft);     });
    connect(m_btnQuadTR, &QPushButton::clicked, this, [this]{ onQuadrantSelected(AngleQuadrant::TopRight);    });
    connect(m_btnQuadBL, &QPushButton::clicked, this, [this]{ onQuadrantSelected(AngleQuadrant::BottomLeft);  });
    connect(m_btnQuadBR, &QPushButton::clicked, this, [this]{ onQuadrantSelected(AngleQuadrant::BottomRight); });

    // Log group
    buildLogGroup(leftPanel, leftLayout);

    // Recorder group
    m_recorderGroup = new QGroupBox("Profil-Aufnahme (JSON)");
    buildRecorderGroup(leftPanel, leftLayout);

    leftLayout->addStretch();

    scroll->setWidget(leftPanel);
    splitter->addWidget(scroll);

    // ── PROFILE CHART ─────────────────────────────────────────────────────
    m_profileWidget = new ProfileWidget(this);
    splitter->addWidget(m_profileWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({340, 900});   // initial left panel width
    splitter->setCollapsible(0, false); // prevent left panel from collapsing to 0

    // Connect ROI draw from chart to spinboxes
    connect(m_profileWidget, &ProfileWidget::roiChanged,
            this,            &MainWindow::onRoiDrawn);
}

void MainWindow::buildSensorGroup(QWidget * /*parent*/, QVBoxLayout *layout)
{
    QFormLayout *form = new QFormLayout(m_sensorGroup);
    form->setLabelAlignment(Qt::AlignRight);

    m_editIp   = new QLineEdit("192.168.3.15");
    m_editPort = new QLineEdit("1096");
    m_editPort->setMaximumWidth(80);

    form->addRow("IP-Adresse:", m_editIp);
    form->addRow("Port:",       m_editPort);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_btnConnect    = new QPushButton("▶  Verbinden");
    m_btnDisconnect = new QPushButton("■  Trennen");
    m_btnDisconnect->setEnabled(false);
    m_btnConnect->setStyleSheet("background:#1a5c2e; color:white; padding:5px 10px; border-radius:4px;");
    m_btnDisconnect->setStyleSheet("background:#5c1a1a; color:white; padding:5px 10px; border-radius:4px;");
    btnLayout->addWidget(m_btnConnect);
    btnLayout->addWidget(m_btnDisconnect);
    form->addRow(btnLayout);

    m_lblSensorStatus = new QLabel("Getrennt");
    m_lblSensorStatus->setStyleSheet("color:#ff5555; font-weight:bold;");
    form->addRow("Status:", m_lblSensorStatus);

    layout->addWidget(m_sensorGroup);

    connect(m_btnConnect,    &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
}

void MainWindow::buildRoiGroup(QWidget * /*parent*/, QVBoxLayout *layout)
{
    QFormLayout *form = new QFormLayout(m_roiGroup);
    form->setLabelAlignment(Qt::AlignRight);

    // ROI 1
    QLabel *lbl1 = new QLabel("<b><span style='color:#00b4ff'>ROI 1</span></b>");
    lbl1->setTextFormat(Qt::RichText);
    form->addRow(lbl1);

    m_roi1Start = new QDoubleSpinBox;
    m_roi1End   = new QDoubleSpinBox;
    for (auto *sb : {m_roi1Start, m_roi1End}) {
        sb->setRange(-200.0, 200.0);
        sb->setSuffix(" mm");
        sb->setDecimals(1);
        sb->setSingleStep(1.0);
    }
    m_roi1Start->setValue(-100.0);
    m_roi1End->setValue(0.0);
    form->addRow("Start:", m_roi1Start);
    form->addRow("Ende:",  m_roi1End);

    m_roi1Method = new QComboBox;
    m_roi1Method->addItem("OLS");
    m_roi1Method->addItem("RANSAC");
    m_roi1Method->addItem("Hough");
    m_roi1Method->addItem("Auto");
    m_roi1Method->setToolTip("Linienfinder für ROI 1:\nOLS – Kleinste Quadrate (schnell)\nRANSAC – Ausreißer-robust\nHough – Lücken/Artefakt-robust\nAuto – Automatische Wahl je nach Inlier-Ratio+RMS");
    form->addRow("Methode:", m_roi1Method);

    m_lblMethod1 = new QLabel("");
    m_lblMethod1->setStyleSheet("color:#aaa; font-size:10px; font-style:italic;");
    m_lblMethod1->setAlignment(Qt::AlignRight);
    form->addRow(m_lblMethod1);

    // Separator
    QFrame *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color:#444;");
    form->addRow(sep);

    // ROI 2
    QLabel *lbl2 = new QLabel("<b><span style='color:#ff8c00'>ROI 2</span></b>");
    lbl2->setTextFormat(Qt::RichText);
    form->addRow(lbl2);

    m_roi2Start = new QDoubleSpinBox;
    m_roi2End   = new QDoubleSpinBox;
    for (auto *sb : {m_roi2Start, m_roi2End}) {
        sb->setRange(-200.0, 200.0);
        sb->setSuffix(" mm");
        sb->setDecimals(1);
        sb->setSingleStep(1.0);
    }
    m_roi2Start->setValue(0.0);
    m_roi2End->setValue(100.0);
    form->addRow("Start:", m_roi2Start);
    form->addRow("Ende:",  m_roi2End);

    m_roi2Method = new QComboBox;
    m_roi2Method->addItem("OLS");
    m_roi2Method->addItem("RANSAC");
    m_roi2Method->addItem("Hough");
    m_roi2Method->addItem("Auto");
    m_roi2Method->setToolTip("Linienfinder für ROI 2:\nOLS – Kleinste Quadrate (schnell)\nRANSAC – Ausreißer-robust\nHough – Lücken/Artefakt-robust\nAuto – Automatische Wahl je nach Inlier-Ratio+RMS");
    form->addRow("Methode:", m_roi2Method);

    m_lblMethod2 = new QLabel("");
    m_lblMethod2->setStyleSheet("color:#aaa; font-size:10px; font-style:italic;");
    m_lblMethod2->setAlignment(Qt::AlignRight);
    form->addRow(m_lblMethod2);

    QLabel *lblHint = new QLabel("Tipp: ROI auch per Maus-Drag im Profil ziehen");
    lblHint->setStyleSheet("color:#666; font-size:10px;");
    lblHint->setWordWrap(true);
    form->addRow(lblHint);

    layout->addWidget(m_roiGroup);

    connect(m_roi1Start, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRoi1Changed);
    connect(m_roi1End,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRoi1Changed);
    connect(m_roi2Start, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRoi2Changed);
    connect(m_roi2End,   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onRoi2Changed);
}

void MainWindow::buildLogGroup(QWidget * /*parent*/, QVBoxLayout *layout)
{
    m_logGroup = new QGroupBox("Messwert-Log (CSV)");
    QVBoxLayout *vbl = new QVBoxLayout(m_logGroup);

    // Path row
    QHBoxLayout *pathRow = new QHBoxLayout;
    m_editLogPath = new QLineEdit;
    m_editLogPath->setPlaceholderText("Log-Pfad wählen…");
    m_editLogPath->setReadOnly(false);
    m_btnLogBrowse = new QPushButton("…");
    m_btnLogBrowse->setMaximumWidth(32);
    m_btnLogBrowse->setToolTip("Log-Datei wählen");
    pathRow->addWidget(m_editLogPath);
    pathRow->addWidget(m_btnLogBrowse);
    vbl->addLayout(pathRow);

    // Toggle button
    m_btnLogToggle = new QPushButton("● Aufzeichnung starten");
    m_btnLogToggle->setCheckable(true);
    m_btnLogToggle->setStyleSheet("background:#1a5c2e; color:white; padding:5px; border-radius:4px;");
    vbl->addWidget(m_btnLogToggle);

    // Status label
    m_lblLogStatus = new QLabel("Bereit");
    m_lblLogStatus->setStyleSheet("color:#888; font-size:10px;");
    vbl->addWidget(m_lblLogStatus);

    layout->addWidget(m_logGroup);

    connect(m_btnLogBrowse, &QPushButton::clicked, this, &MainWindow::onLogBrowse);
    connect(m_btnLogToggle, &QPushButton::toggled, this, &MainWindow::onLogToggle);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Typverwaltung (Presets)
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::buildPresetGroup(QWidget * /*parent*/, QVBoxLayout *layout)
{
    QVBoxLayout *vbl = new QVBoxLayout(m_presetGroup);
    vbl->setSpacing(6);

    // ComboBox row
    QHBoxLayout *comboRow = new QHBoxLayout;
    m_cmbPresets = new QComboBox;
    m_cmbPresets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_cmbPresets->setToolTip("Gespeicherten Typ laden");
    comboRow->addWidget(m_cmbPresets);
    vbl->addLayout(comboRow);

    // Button row
    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    m_btnPresetSave = new QPushButton("Speichern");
    m_btnPresetSave->setToolTip("Aktuelle Einstellungen als neuen Typ speichern");
    m_btnPresetSave->setStyleSheet("background:#1a3c5c; color:white; padding:4px 6px; border-radius:4px;");

    m_btnPresetRen = new QPushButton("Umbenennen");
    m_btnPresetRen->setToolTip("Ausgewählten Typ umbenennen");
    m_btnPresetRen->setStyleSheet("background:#2a2a2a; color:#ccc; padding:4px 6px; border-radius:4px;");

    m_btnPresetDel = new QPushButton("Löschen");
    m_btnPresetDel->setToolTip("Ausgewählten Typ löschen");
    m_btnPresetDel->setStyleSheet("background:#3c1a1a; color:#ccc; padding:4px 6px; border-radius:4px;");

    btnRow->addWidget(m_btnPresetSave);
    btnRow->addWidget(m_btnPresetRen);
    btnRow->addWidget(m_btnPresetDel);
    vbl->addLayout(btnRow);

    layout->addWidget(m_presetGroup);

    // Lock button
    m_btnLock = new QPushButton("🔓  Einstellungen sperren");
    m_btnLock->setCheckable(true);
    m_btnLock->setStyleSheet(
        "QPushButton { background:#2a2a2a; color:#ccc; padding:5px; border-radius:4px; }"
        "QPushButton:checked { background:#5c3a00; color:#ffd600; border:1px solid #ffd600; }");
    m_btnLock->setToolTip("Alle Einstellungen sperren – verhindert versehentliche Änderungen");
    vbl->addWidget(m_btnLock);

    connect(m_cmbPresets,    QOverload<int>::of(&QComboBox::activated),
            this, &MainWindow::onPresetSelected);
    connect(m_btnPresetSave, &QPushButton::clicked, this, &MainWindow::onPresetSave);
    connect(m_btnPresetRen,  &QPushButton::clicked, this, &MainWindow::onPresetRename);
    connect(m_btnPresetDel,  &QPushButton::clicked, this, &MainWindow::onPresetDelete);
    connect(m_btnLock,       &QPushButton::toggled, this, &MainWindow::onLockToggle);
}

// ─────────────────────────────────────────────────────────────────────────────
//  buildRecorderGroup – JSON Profile Recorder UI
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::buildRecorderGroup(QWidget * /*parent*/, QVBoxLayout *layout)
{
    QVBoxLayout *vbl = new QVBoxLayout(m_recorderGroup);
    vbl->setSpacing(5);

    // Folder row
    QHBoxLayout *folderRow = new QHBoxLayout;
    m_editRecordFolder = new QLineEdit;
    m_editRecordFolder->setPlaceholderText("Aufnahme-Ordner wählen…");
    m_editRecordFolder->setReadOnly(false);
    m_btnRecordBrowse = new QPushButton("…");
    m_btnRecordBrowse->setMaximumWidth(32);
    m_btnRecordBrowse->setToolTip("Zielordner für JSON-Dateien wählen");
    folderRow->addWidget(m_editRecordFolder);
    folderRow->addWidget(m_btnRecordBrowse);
    vbl->addLayout(folderRow);

    // Max frames row
    QHBoxLayout *maxRow = new QHBoxLayout;
    QLabel *lblMax = new QLabel("Max. Frames (0 = unbegrenzt):");
    lblMax->setStyleSheet("color:#aaa; font-size:10px;");
    m_spinMaxFrames = new QSpinBox;
    m_spinMaxFrames->setRange(0, 100000);
    m_spinMaxFrames->setValue(0);
    m_spinMaxFrames->setMaximumWidth(80);
    m_spinMaxFrames->setToolTip("0 = keine Begrenzung");
    m_spinMaxFrames->setStyleSheet(
        "background:#222; color:white; border:1px solid #555; border-radius:3px; padding:2px 4px;");
    maxRow->addWidget(lblMax);
    maxRow->addStretch();
    maxRow->addWidget(m_spinMaxFrames);
    vbl->addLayout(maxRow);

    // Toggle button
    m_btnRecordToggle = new QPushButton("⏺  Aufnahme starten");
    m_btnRecordToggle->setCheckable(true);
    m_btnRecordToggle->setStyleSheet(
        "background:#1a5c2e; color:white; padding:5px; border-radius:4px;");
    vbl->addWidget(m_btnRecordToggle);

    // Status label
    m_lblRecordStatus = new QLabel("Bereit – nur im Live-Sensor-Modus");
    m_lblRecordStatus->setStyleSheet("color:#888; font-size:10px;");
    vbl->addWidget(m_lblRecordStatus);

    layout->addWidget(m_recorderGroup);

    // Default folder
    m_editRecordFolder->setText(
        QDir(QApplication::applicationDirPath()).filePath("Recordings"));

    connect(m_btnRecordBrowse, &QPushButton::clicked, this, &MainWindow::onRecordBrowse);
    connect(m_btnRecordToggle, &QPushButton::toggled, this, &MainWindow::onRecordToggle);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Recorder Slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onRecordBrowse()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Aufnahme-Ordner wählen",
        m_editRecordFolder->text().isEmpty()
            ? QDir(QApplication::applicationDirPath()).filePath("Recordings")
            : m_editRecordFolder->text());
    if (!dir.isEmpty())
        m_editRecordFolder->setText(dir);
}

void MainWindow::onRecordToggle(bool checked)
{
    if (checked) {
        // Only allow recording in live sensor mode
        if (m_sourceMode != SourceMode::LiveSensor) {
            QMessageBox::information(this, "Profil-Aufnahme",
                "Die Aufnahme ist nur im Live-Sensor-Modus verfügbar.");
            m_btnRecordToggle->setChecked(false);
            return;
        }
        QString folder = m_editRecordFolder->text().trimmed();
        if (folder.isEmpty())
            folder = QDir(QApplication::applicationDirPath()).filePath("Recordings");
        QDir().mkpath(folder);
        m_recordFolder = folder;
        m_recordCount  = 0;
        m_recording    = true;
        m_btnRecordToggle->setText("⏹  Aufnahme stoppen");
        m_btnRecordToggle->setStyleSheet(
            "background:#5c1a1a; color:white; padding:5px; border-radius:4px;");
        m_lblRecordStatus->setText("<b style='color:#ff5555'>⏺  Aufnahme läuft…</b>");
        qInfo().noquote() << "[Recorder] Aufnahme gestartet:" << folder;
    } else {
        m_recording = false;
        m_btnRecordToggle->setText("⏺  Aufnahme starten");
        m_btnRecordToggle->setStyleSheet(
            "background:#1a5c2e; color:white; padding:5px; border-radius:4px;");
        m_lblRecordStatus->setText(
            QString("Gespeichert: %1 Frame%2  →  %3")
                .arg(m_recordCount)
                .arg(m_recordCount == 1 ? "" : "s")
                .arg(QDir::toNativeSeparators(m_recordFolder)));
        qInfo().noquote() << QString("[Recorder] Aufnahme gestoppt: %1 Frames → %2")
            .arg(m_recordCount).arg(m_recordFolder);
    }
}

void MainWindow::recordFrame(const std::vector<ProfilePoint> &pts)
{
    if (!m_recording || pts.empty()) return;

    // Auto-stop at max frames
    const int maxF = m_spinMaxFrames->value();
    if (maxF > 0 && m_recordCount >= maxF) {
        m_btnRecordToggle->setChecked(false);  // triggers onRecordToggle(false)
        return;
    }

    // Build filename: LaserLineData_YYYYMMDD_HHmmsszzzzzz.json
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmsszzz");
    const QString fileName = QString("LaserLineData_%1.json").arg(ts);
    const QString filePath = QDir(m_recordFolder).filePath(fileName);

    // Serialize as JSON array [{"x":…,"y":…}, …]
    QJsonArray arr;
    for (const auto &pt : pts) {
        QJsonObject obj;
        obj["x"] = static_cast<double>(pt.x_mm);
        obj["y"] = static_cast<double>(pt.z_mm);  // y in JSON = Z height
        arr.append(obj);
    }
    QJsonDocument doc(arr);

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning().noquote() << "[Recorder] Kann Datei nicht schreiben:" << filePath;
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();

    ++m_recordCount;
    m_lblRecordStatus->setText(
        QString("<b style='color:#ff5555'>⏺  Aufnahme: %1 Frame%2</b>")
            .arg(m_recordCount)
            .arg(m_recordCount == 1 ? "" : "s"));
}

void MainWindow::buildPlaybackGroup(QWidget * /*parent*/, QVBoxLayout *layout)
{
    QVBoxLayout *vbl = new QVBoxLayout(m_playbackGroup);

    // Folder picker
    QHBoxLayout *folderRow = new QHBoxLayout;
    m_editFolder = new QLineEdit;
    m_editFolder->setPlaceholderText("JSON-Ordner wählen…");
    m_editFolder->setReadOnly(true);
    m_btnBrowse = new QPushButton("…");
    m_btnBrowse->setFixedWidth(32);
    folderRow->addWidget(m_editFolder);
    folderRow->addWidget(m_btnBrowse);
    vbl->addLayout(folderRow);

    // Frame info
    m_lblFrameInfo = new QLabel("Kein Ordner geladen");
    m_lblFrameInfo->setStyleSheet("color:#aaa; font-size:11px;");
    m_lblFrameInfo->setAlignment(Qt::AlignCenter);
    vbl->addWidget(m_lblFrameInfo);

    // Playback buttons
    QHBoxLayout *btnRow = new QHBoxLayout;
    m_btnStepBack = new QPushButton("◀◀");
    m_btnPlay     = new QPushButton("▶  Play");
    m_btnStop     = new QPushButton("■  Stop");
    m_btnStepFwd  = new QPushButton("▶▶");

    m_btnPlay->setStyleSheet("background:#1a5c2e; color:white; padding:4px 8px; border-radius:4px;");
    m_btnStop->setStyleSheet("background:#5c3a1a; color:white; padding:4px 8px; border-radius:4px;");

    for (auto *b : {m_btnStepBack, m_btnPlay, m_btnStop, m_btnStepFwd})
        btnRow->addWidget(b);
    vbl->addLayout(btnRow);

    // Speed slider
    QHBoxLayout *speedRow = new QHBoxLayout;
    QLabel *lblSpeedCaption = new QLabel("Geschwindigkeit:");
    m_speedSlider = new QSlider(Qt::Horizontal);
    m_speedSlider->setRange(1, 10);   // 1 = slow (2000ms), 10 = fast (100ms)
    m_speedSlider->setValue(5);       // default ~500ms
    m_lblSpeed = new QLabel("1.0×");
    m_lblSpeed->setMinimumWidth(35);
    speedRow->addWidget(lblSpeedCaption);
    speedRow->addWidget(m_speedSlider);
    speedRow->addWidget(m_lblSpeed);
    vbl->addLayout(speedRow);

    layout->addWidget(m_playbackGroup);

    // Connections
    connect(m_btnBrowse,  &QPushButton::clicked, this, &MainWindow::onBrowseJsonFolder);
    connect(m_btnPlay,    &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(m_btnStop,    &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(m_btnStepBack,&QPushButton::clicked, this, &MainWindow::onStepBackClicked);
    connect(m_btnStepFwd, &QPushButton::clicked, this, &MainWindow::onStepForwardClicked);
    connect(m_speedSlider,&QSlider::valueChanged, this, &MainWindow::onSpeedSliderChanged);

    // JsonPlayer signals – profileReady goes through MainWindow so we can
    // compute fit lines before forwarding to ProfileWidget
    connect(m_jsonPlayer, &JsonPlayer::profileReady,    this, &MainWindow::onJsonProfileReady);
    connect(m_jsonPlayer, &JsonPlayer::frameChanged,    this, &MainWindow::onJsonFrameChanged);
    connect(m_jsonPlayer, &JsonPlayer::folderLoaded,    this, &MainWindow::onJsonFolderLoaded);
    connect(m_jsonPlayer, &JsonPlayer::playbackStarted, this, &MainWindow::onJsonPlaybackStarted);
    connect(m_jsonPlayer, &JsonPlayer::playbackStopped, this, &MainWindow::onJsonPlaybackStopped);
}

void MainWindow::buildStatusBar()
{
    // "Fit" and zoom-hint button in the status bar area
    QPushButton *btnFit = new QPushButton("⊡  Fit");
    btnFit->setToolTip("Ansicht an Profildaten anpassen (Doppelklick im Plot hat den gleichen Effekt)");
    btnFit->setStyleSheet("padding: 2px 10px; font-size: 11px;");
    connect(btnFit, &QPushButton::clicked, m_profileWidget, &ProfileWidget::resetZoom);
    statusBar()->addPermanentWidget(btnFit);

    QLabel *lblHint = new QLabel("  Mausrad: Zoom  |  Rechte Maustaste: Pan  |  Linkes Drag im Plot: ROI");
    lblHint->setStyleSheet("color:#888; font-size:10px;");
    statusBar()->addPermanentWidget(lblHint);

    statusBar()->showMessage("Bereit – Quelle wählen und verbinden");
}

// ──────────────────────────────────────────────────────────────────────────────
//  Source mode
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::applySourceMode()
{
    bool isLive = (m_sourceMode == SourceMode::LiveSensor);

    m_sensorGroup->setEnabled(isLive);
    m_playbackGroup->setEnabled(!isLive);

    if (!isLive) {
        // Stop live sensor if running
        if (m_sensorWorker && m_sensorWorker->isRunning()) {
            m_sensorWorker->stopCapture();
            // Non-blocking: onSensorDisconnected() handles UI update
        }
        statusBar()->showMessage("JSON Wiedergabe – Ordner laden und Play drücken");
    } else {
        m_jsonPlayer->stop();
        updatePlayButtons(false);
        statusBar()->showMessage("Live Sensor – IP/Port eingeben und Verbinden drücken");
    }
}

void MainWindow::onSourceModeChanged()
{
    m_sourceMode = m_rbLive->isChecked() ? SourceMode::LiveSensor : SourceMode::JsonPlayback;
    applySourceMode();
}

// ──────────────────────────────────────────────────────────────────────────────
//  Sensor slots
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onConnectClicked()
{
    if (m_sensorWorker && m_sensorWorker->isRunning()) return;

    QString ip   = m_editIp->text().trimmed();
    int     port = m_editPort->text().trimmed().toInt();

    RoiRect roi1, roi2;
    roi1.xMin = m_roi1Start->value(); roi1.xMax = m_roi1End->value(); roi1.valid = (roi1.xMax > roi1.xMin);
    roi2.xMin = m_roi2Start->value(); roi2.xMax = m_roi2End->value(); roi2.valid = (roi2.xMax > roi2.xMin);

    m_sensorWorker = new SensorWorker(this);
    connect(m_sensorWorker, &SensorWorker::profileReady,    this, &MainWindow::onSensorData);
    connect(m_sensorWorker, &SensorWorker::angleReady,      this, &MainWindow::onAngleReady);
    connect(m_sensorWorker, &SensorWorker::errorOccurred,   this, &MainWindow::onSensorError);
    connect(m_sensorWorker, &SensorWorker::connected,       this, &MainWindow::onSensorConnected);
    connect(m_sensorWorker, &SensorWorker::disconnected,    this, &MainWindow::onSensorDisconnected);

    connect(m_sensorWorker, &QThread::finished, m_sensorWorker, &QObject::deleteLater);

    m_sensorWorker->startCapture(ip, m_editPort->text().toUShort(), roi1, roi2);
    qInfo().noquote() << QString("[Sensor] Verbindungsversuch: %1:%2  ROI1=[%3..%4]  ROI2=[%5..%6]")
        .arg(ip).arg(port)
        .arg(QString::number(roi1.xMin, 'f', 1))
        .arg(QString::number(roi1.xMax, 'f', 1))
        .arg(QString::number(roi2.xMin, 'f', 1))
        .arg(QString::number(roi2.xMax, 'f', 1));
    statusBar()->showMessage("Verbinde mit " + ip + ":" + QString::number(port) + " …");
}

void MainWindow::onDisconnectClicked()
{
    if (!m_sensorWorker || !m_sensorWorker->isRunning()) return;
    // Non-blocking: just set stop flag.
    // onSensorDisconnected() updates the UI once the thread exits.
    m_sensorWorker->stopCapture();
}

void MainWindow::onSensorData(const std::vector<ProfilePoint> &points)
{
    m_lastProfilePts = points;
    m_profileWidget->updateProfile(points);
    // Record frame to JSON if recording is active
    if (m_recording) recordFrame(points);
    // Throttle fit computation to ~30 Hz to keep UI responsive
    if (m_fitRateTimer.elapsed() >= 33) {
        m_fitRateTimer.restart();
        computeAndDisplayFitLines(points);
    }
}

void MainWindow::onAngleReady(AngleResult /*result*/)
{
    // Angle display is now handled by updateAngleDisplay() via onSensorData
    // (local regression is used for both Live and JSON modes)
}

void MainWindow::onSensorError(const QString &msg)
{
    qWarning().noquote() << "[Sensor] Fehler:" << msg;
    statusBar()->showMessage("Fehler: " + msg);
    m_lblSensorStatus->setText("Fehler");
    m_lblSensorStatus->setStyleSheet("color:#ff5555; font-weight:bold;");
}

void MainWindow::onSensorConnected()
{
    qInfo() << "[Sensor] Verbunden:" << m_editIp->text() << "Port" << m_editPort->text();
    updateConnectButtons(true);
    m_lblSensorStatus->setText("Verbunden");
    m_lblSensorStatus->setStyleSheet("color:#00e676; font-weight:bold;");
    statusBar()->showMessage("Sensor verbunden – Profildaten werden empfangen");
}

void MainWindow::onSensorDisconnected()
{
    qWarning() << "[Sensor] Verbindung getrennt";
    updateConnectButtons(false);
    m_lblSensorStatus->setText("Getrennt");
    m_lblSensorStatus->setStyleSheet("color:#ff5555; font-weight:bold;");
}

void MainWindow::updateConnectButtons(bool connected)
{
    m_btnConnect->setEnabled(!connected);
    m_btnDisconnect->setEnabled(connected);
}

// ──────────────────────────────────────────────────────────────────────────────
//  ROI slots
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onRoi1Changed()
{
    RoiRect roi1; roi1.xMin = m_roi1Start->value(); roi1.xMax = m_roi1End->value();
    roi1.valid = (roi1.xMax > roi1.xMin);
    RoiRect roi2; roi2.xMin = m_roi2Start->value(); roi2.xMax = m_roi2End->value();
    roi2.valid = (roi2.xMax > roi2.xMin);
    m_profileWidget->setRoi(0, roi1);
    if (m_sensorWorker && m_sourceMode == SourceMode::LiveSensor)
        m_sensorWorker->updateRois(roi1, roi2);
    // Recompute fit with cached frame so result updates immediately
    if (!m_lastProfilePts.empty()) {
        m_fitRateTimer.restart();
        computeAndDisplayFitLines(m_lastProfilePts);
    }
}

void MainWindow::onRoi2Changed()
{
    RoiRect roi1; roi1.xMin = m_roi1Start->value(); roi1.xMax = m_roi1End->value();
    roi1.valid = (roi1.xMax > roi1.xMin);
    RoiRect roi2; roi2.xMin = m_roi2Start->value(); roi2.xMax = m_roi2End->value();
    roi2.valid = (roi2.xMax > roi2.xMin);
    m_profileWidget->setRoi(1, roi2);
    if (m_sensorWorker && m_sourceMode == SourceMode::LiveSensor)
        m_sensorWorker->updateRois(roi1, roi2);
    if (!m_lastProfilePts.empty()) {
        m_fitRateTimer.restart();
        computeAndDisplayFitLines(m_lastProfilePts);
    }
}

void MainWindow::onRoiDrawn(int roiIndex, RoiRect r)
{
    float xStart = static_cast<float>(r.xMin);
    float xEnd   = static_cast<float>(r.xMax);
    if (roiIndex == 0) {
        QSignalBlocker b1(m_roi1Start), b2(m_roi1End);
        m_roi1Start->setValue(xStart);
        m_roi1End->setValue(xEnd);
        if (m_sensorWorker && m_sourceMode == SourceMode::LiveSensor) {
            RoiRect r2; r2.xMin = m_roi2Start->value(); r2.xMax = m_roi2End->value();
            m_sensorWorker->updateRois(r, r2);
        }
    } else {
        QSignalBlocker b1(m_roi2Start), b2(m_roi2End);
        m_roi2Start->setValue(xStart);
        m_roi2End->setValue(xEnd);
        if (m_sensorWorker && m_sourceMode == SourceMode::LiveSensor) {
            RoiRect r1; r1.xMin = m_roi1Start->value(); r1.xMax = m_roi1End->value();
            m_sensorWorker->updateRois(r1, r);
        }
    }
    statusBar()->showMessage(QString("ROI %1 gesetzt: %2 … %3 mm")
                                 .arg(roiIndex + 1)
                                 .arg(xStart, 0, 'f', 1)
                                 .arg(xEnd,   0, 'f', 1));
}

// ──────────────────────────────────────────────────────────────────────────────
//  JSON Playback slots
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onBrowseJsonFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "JSON-Ordner wählen",
        m_editFolder->text().isEmpty() ?
            QDir(QApplication::applicationDirPath()).filePath("Data") :
            m_editFolder->text());

    if (dir.isEmpty()) return;

    m_editFolder->setText(dir);
    m_jsonPlayer->loadFolder(dir);
}

void MainWindow::onPlayClicked()
{
    if (m_jsonPlayer->frameCount() == 0) {
        statusBar()->showMessage("Kein Ordner geladen – bitte zuerst JSON-Ordner wählen");
        return;
    }
    m_jsonPlayer->play();
}

void MainWindow::onStopClicked()
{
    m_jsonPlayer->stop();
}

void MainWindow::onStepForwardClicked()
{
    m_jsonPlayer->stepForward();
}

void MainWindow::onStepBackClicked()
{
    m_jsonPlayer->stepBack();
}

void MainWindow::onSpeedSliderChanged(int value)
{
    // value 1..10 → interval 2000ms..100ms (inverse)
    // speed multiplier: 1=0.5x, 5=1x, 10=2x displayed nicely
    int intervalMs = 2100 - value * 200;  // 1→1900, 5→1100... let's do a nicer curve
    // Simple mapping: value 1=2000ms, 3=1000ms, 5=500ms, 7=250ms, 10=100ms
    static const int intervals[] = {2000, 1500, 1000, 750, 500, 350, 250, 200, 150, 100};
    intervalMs = intervals[value - 1];

    m_jsonPlayer->setIntervalMs(intervalMs);

    float hz = 1000.0f / intervalMs;
    m_lblSpeed->setText(QString("%1 Hz").arg(hz, 0, 'f', 1));
}

void MainWindow::onJsonFrameChanged(int index, int total)
{
    m_lblFrameInfo->setText(QString("Frame %1 / %2").arg(index + 1).arg(total));
}

void MainWindow::onJsonFolderLoaded(int frameCount)
{
    qInfo().noquote() << QString("[JSON] Ordner geladen: %1 Frames  Pfad: %2")
        .arg(frameCount).arg(m_editFolder->text());
    m_lblFrameInfo->setText(QString("%1 Dateien geladen").arg(frameCount));
    statusBar()->showMessage(QString("JSON-Ordner geladen: %1 Frames – Play drücken zum Abspielen").arg(frameCount));
    m_btnPlay->setEnabled(frameCount > 0);
    m_btnStepBack->setEnabled(frameCount > 0);
    m_btnStepFwd->setEnabled(frameCount > 0);
}

void MainWindow::onJsonPlaybackStarted()
{
    updatePlayButtons(true);
    statusBar()->showMessage("JSON Wiedergabe läuft…");
}

void MainWindow::onJsonPlaybackStopped()
{
    updatePlayButtons(false);
    statusBar()->showMessage("JSON Wiedergabe gestoppt");
}

void MainWindow::onJsonProfileReady(const std::vector<ProfilePoint> &points)
{
    m_lastProfilePts = points;
    m_profileWidget->updateProfile(points);
    if (m_fitRateTimer.elapsed() >= 33) {
        m_fitRateTimer.restart();
        computeAndDisplayFitLines(points);
    }
}

void MainWindow::computeAndDisplayFitLines(const std::vector<ProfilePoint> &pts)
{
    RoiRect roi1, roi2;
    roi1.xMin = m_roi1Start->value(); roi1.xMax = m_roi1End->value();
    roi1.valid = (roi1.xMax > roi1.xMin);
    roi2.xMin = m_roi2Start->value(); roi2.xMax = m_roi2End->value();
    roi2.valid = (roi2.xMax > roi2.xMin);

    auto m1 = static_cast<FitMethod>(m_roi1Method->currentIndex());
    auto m2 = static_cast<FitMethod>(m_roi2Method->currentIndex());

    std::vector<std::pair<double,double>> res1, res2;
    FitLine fl1 = fitLine(pts, roi1, m1, &res1);
    FitLine fl2 = fitLine(pts, roi2, m2, &res2);

    m_profileWidget->updateFitLines(fl1, fl2, res1, res2);
    updateAngleDisplay(fl1, fl2);

    // Compute bending angle for overlay
    double bendingAngle = 0.0;
    if (fl1.valid && fl2.valid) {
        bendingAngle = fl2.phi - fl1.phi;
        while (bendingAngle >  180.0) bendingAngle -= 360.0;
        while (bendingAngle < -180.0) bendingAngle += 360.0;
    }

    // Forward method labels + bending angle to the chart overlay
    // (must call AFTER methodName lambda is defined below – so we forward-compute here)
    // We pass "" if a fit line is not valid so the overlay stays clean.
    // Note: methodName is defined after this block, so we inline the logic:
    auto inlineMethodName = [](FitMethod requested, const FitLine &fl) -> QString {
        if (requested == FitMethod::Auto && fl.valid) {
            if      (fl.method == FitMethod::RANSAC) return QStringLiteral("Auto\u2192RANSAC");
            else if (fl.method == FitMethod::Hough)  return QStringLiteral("Auto\u2192Hough");
            else                                      return QStringLiteral("Auto\u2192OLS");
        }
        switch (requested) {
            case FitMethod::RANSAC: return QStringLiteral("RANSAC");
            case FitMethod::Hough:  return QStringLiteral("Hough");
            case FitMethod::Auto:   return QStringLiteral("Auto");
            default:                return QStringLiteral("OLS");
        }
    };
    // Apply quadrant to get the display angle (same value as left panel)
    double displayBending = std::abs(bendingAngle);
    if (fl1.valid && fl2.valid) {
        if (m_angleQuadrant == AngleQuadrant::TopRight ||
            m_angleQuadrant == AngleQuadrant::BottomLeft)
            displayBending = 180.0 - displayBending;
    }

    m_profileWidget->setFitLabels(
        fl1.valid ? inlineMethodName(m1, fl1) : QString(),
        fl2.valid ? inlineMethodName(m2, fl2) : QString(),
        displayBending);

    // Update Auto-mode labels (show which method was actually chosen)
    auto updateMethodLabel = [](QLabel *lbl, FitMethod requested, const FitLine &fl) {
        if (requested == FitMethod::Auto && fl.valid) {
            const char *chosen = "OLS";
            if      (fl.method == FitMethod::RANSAC) chosen = "RANSAC";
            else if (fl.method == FitMethod::Hough)  chosen = "Hough";
            lbl->setText(QString("→ %1  |  inlier=%2")
                             .arg(chosen)
                             .arg(fl.autoInfo.inlierRatioRansac, 0, 'f', 2));
            lbl->setToolTip(QString::fromUtf8(fl.autoInfo.reason));
            lbl->setVisible(true);
        } else {
            lbl->clear();
            lbl->setVisible(false);
        }
    };
    updateMethodLabel(m_lblMethod1, m1, fl1);
    updateMethodLabel(m_lblMethod2, m2, fl2);

    // ── Messwert-Log ──────────────────────────────────────────────────────
    if (m_logger->isOpen()) {
        LogEntry entry;
        entry.timestamp      = QDateTime::currentDateTime();
        entry.valid1         = fl1.valid;
        entry.phi1_deg       = fl1.valid ? fl1.phi : 0.0;
        entry.rms1_um        = fl1.valid ? fl1.rmsResidual * 1000.0 : 0.0;
        entry.maxRes1_um     = fl1.valid ? fl1.maxResidual * 1000.0 : 0.0;
        entry.method1        = fl1.valid ? inlineMethodName(m1, fl1) : QString();
        entry.inlier1        = (fl1.valid && m1 == FitMethod::Auto) ? fl1.autoInfo.inlierRatioRansac : -1.0;
        entry.roi1Start_mm   = m_roi1Start->value();
        entry.roi1End_mm     = m_roi1End->value();
        entry.valid2         = fl2.valid;
        entry.phi2_deg       = fl2.valid ? fl2.phi : 0.0;
        entry.rms2_um        = fl2.valid ? fl2.rmsResidual * 1000.0 : 0.0;
        entry.maxRes2_um     = fl2.valid ? fl2.maxResidual * 1000.0 : 0.0;
        entry.method2        = fl2.valid ? inlineMethodName(m2, fl2) : QString();
        entry.inlier2        = (fl2.valid && m2 == FitMethod::Auto) ? fl2.autoInfo.inlierRatioRansac : -1.0;
        entry.roi2Start_mm   = m_roi2Start->value();
        entry.roi2End_mm     = m_roi2End->value();
        entry.bendingAngle_deg = displayBending;  // quadrant-corrected value
        entry.hasBending     = fl1.valid && fl2.valid;
        m_logger->append(entry);
    }

    // Method name helper (for Auto mode shows "Auto→Hough" etc.)
    auto methodName = [](FitMethod requested, const FitLine &fl) -> QString {
        if (requested == FitMethod::Auto && fl.valid) {
            QString chosen;
            if      (fl.method == FitMethod::RANSAC) chosen = "RANSAC";
            else if (fl.method == FitMethod::Hough)  chosen = "Hough";
            else                                      chosen = "OLS";
            return QStringLiteral("Auto→") + chosen;
        }
        switch (requested) {
            case FitMethod::RANSAC: return QStringLiteral("RANSAC");
            case FitMethod::Hough:  return QStringLiteral("Hough");
            case FitMethod::Auto:   return QStringLiteral("Auto");
            default:                return QStringLiteral("OLS");
        }
    };

    // Show RMS residuals + method in status bar
    if (fl1.valid || fl2.valid) {
        QString msg;
        if (fl1.valid)
            msg += QString("ROI1[%1]: φ=%2°  RMS=%3μm")
                   .arg(methodName(m1, fl1))
                   .arg(fl1.phi, 0,'f',2)
                   .arg(fl1.rmsResidual * 1000.0, 0,'f',1);
        if (fl2.valid)
            msg += QString("   ROI2[%1]: φ=%2°  RMS=%3μm")
                   .arg(methodName(m2, fl2))
                   .arg(fl2.phi, 0,'f',2)
                   .arg(fl2.rmsResidual * 1000.0, 0,'f',1);
        statusBar()->showMessage(msg);
    }
}

void MainWindow::updateAngleDisplay(const FitLine &fl1, const FitLine &fl2)
{
    if (fl1.valid && fl2.valid) {
        double delta = fl2.phi - fl1.phi;
        while (delta >  180.0) delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        // The raw delta is the angle between the two lines (always 0..180).
        // Depending on which quadrant the user wants to measure:
        //   Acute side  (the smaller angle): use |delta| directly
        //   Obtuse side (the larger angle):  use 180 - |delta|
        // Which quadrant is "acute" depends on the actual geometry,
        // but as a convention matching the arc display:
        //   TopLeft + BottomRight = same sector (one of the two)
        //   TopRight + BottomLeft = opposite sector
        // We simply show |delta| for TL/BR and (180-|delta|) for TR/BL.
        // The arc in the chart always shows the correct visual sector.
        double displayAngle = std::abs(delta);
        if (m_angleQuadrant == AngleQuadrant::TopRight ||
            m_angleQuadrant == AngleQuadrant::BottomLeft) {
            displayAngle = 180.0 - displayAngle;
        }
        m_lblAngle->setText(QString("%1°").arg(displayAngle, 0, 'f', 2));
        m_lblAngle->setStyleSheet("color: #00e676; background: #1a1a2e; border-radius: 6px; padding: 6px;");
    } else if (fl1.valid) {
        m_lblAngle->setText(QString("%1°").arg(fl1.phi, 0, 'f', 2));
    } else {
        m_lblAngle->setText("—");
    }
}

void MainWindow::updatePlayButtons(bool playing)
{
    m_btnPlay->setEnabled(!playing);
    m_btnStop->setEnabled(playing);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Preset management
// ──────────────────────────────────────────────────────────────────────────────
QString MainWindow::presetsPath() const
{
    return QDir(QApplication::applicationDirPath()).filePath("Devices/Presets.ini");
}

void MainWindow::refreshPresetCombo()
{
    QSignalBlocker blocker(m_cmbPresets);
    const QString current = m_cmbPresets->currentIndex() > 0
                            ? m_cmbPresets->currentText() : QString();
    m_cmbPresets->clear();
    m_cmbPresets->addItem("– Typ wählen –");

    QSettings ps(presetsPath(), QSettings::IniFormat);
    const QStringList groups = ps.childGroups();
    for (const QString &g : groups) {
        m_cmbPresets->addItem(g);

        // Build tooltip: IP + ROI-Bereiche + Methoden
        ps.beginGroup(g);
        const QString ip    = ps.value("Sensor_IP",   "?").toString();
        const QString port  = ps.value("Sensor_Port", "?").toString();
        const double r1s    = ps.value("ROI1_Start",   0.0).toDouble();
        const double r1e    = ps.value("ROI1_End",     0.0).toDouble();
        const double r2s    = ps.value("ROI2_Start",   0.0).toDouble();
        const double r2e    = ps.value("ROI2_End",     0.0).toDouble();
        const int    m1idx  = ps.value("ROI1_Method",  0).toInt();
        const int    m2idx  = ps.value("ROI2_Method",  0).toInt();
        const int    mode   = ps.value("Source_Mode",  0).toInt();
        const QString folder= ps.value("Playback_Folder", "").toString();
        ps.endGroup();

        static const char* kMethods[] = {"OLS", "RANSAC", "Hough", "Auto"};
        const QString m1str = (m1idx >= 0 && m1idx <= 3) ? kMethods[m1idx] : "?";
        const QString m2str = (m2idx >= 0 && m2idx <= 3) ? kMethods[m2idx] : "?";
        const QString modeStr = (mode == 1) ? "JSON Wiedergabe" : "Live Sensor";

        QString tip;
        tip += QString("Sensor:  %1 : %2\n").arg(ip, port);
        tip += QString("ROI 1:   %1 … %2 mm  [%3]\n")
               .arg(r1s, 0,'f',1).arg(r1e, 0,'f',1).arg(m1str);
        tip += QString("ROI 2:   %1 … %2 mm  [%3]\n")
               .arg(r2s, 0,'f',1).arg(r2e, 0,'f',1).arg(m2str);
        tip += QString("Quelle:  %1").arg(modeStr);
        if (!folder.isEmpty())
            tip += QString("\nOrdner:  %1").arg(QDir::toNativeSeparators(folder));

        m_cmbPresets->setItemData(m_cmbPresets->count() - 1, tip, Qt::ToolTipRole);
    }

    // Restore selection
    if (!current.isEmpty()) {
        int idx = m_cmbPresets->findText(current);
        if (idx >= 0) m_cmbPresets->setCurrentIndex(idx);
    }

    m_btnPresetRen->setEnabled(m_cmbPresets->count() > 1);
    m_btnPresetDel->setEnabled(m_cmbPresets->count() > 1);
}

void MainWindow::writePreset(const QString &name)
{
    QSettings ps(presetsPath(), QSettings::IniFormat);
    ps.beginGroup(name);
    ps.setValue("Sensor_IP",       m_editIp->text());
    ps.setValue("Sensor_Port",     m_editPort->text());
    ps.setValue("ROI1_Start",      m_roi1Start->value());
    ps.setValue("ROI1_End",        m_roi1End->value());
    ps.setValue("ROI1_Method",     m_roi1Method->currentIndex());
    ps.setValue("ROI2_Start",      m_roi2Start->value());
    ps.setValue("ROI2_End",        m_roi2End->value());
    ps.setValue("ROI2_Method",     m_roi2Method->currentIndex());
    ps.setValue("Playback_Folder", m_editFolder->text());
    ps.setValue("Playback_Speed",  m_speedSlider->value());
    ps.setValue("Source_Mode",     static_cast<int>(m_sourceMode));
    ps.setValue("Log_Path",        m_editLogPath->text());
    ps.endGroup();
    ps.sync();
}

void MainWindow::applyPreset(const QString &name)
{
    QSettings ps(presetsPath(), QSettings::IniFormat);
    if (!ps.childGroups().contains(name)) {
        qWarning().noquote() << "[Preset] Typ nicht gefunden:" << name;
        return;
    }
    qInfo().noquote() << "[Preset] Lade Typ:" << name;
    m_presetLoading = true;

    ps.beginGroup(name);
    m_editIp->setText(         ps.value("Sensor_IP",       "192.168.3.15").toString());
    m_editPort->setText(       ps.value("Sensor_Port",     "1096").toString());
    m_roi1Start->setValue(     ps.value("ROI1_Start",      -100.0).toDouble());
    m_roi1End->setValue(       ps.value("ROI1_End",          0.0).toDouble());
    m_roi1Method->setCurrentIndex(ps.value("ROI1_Method", 0).toInt());
    m_roi2Start->setValue(     ps.value("ROI2_Start",        0.0).toDouble());
    m_roi2End->setValue(       ps.value("ROI2_End",         100.0).toDouble());
    m_roi2Method->setCurrentIndex(ps.value("ROI2_Method", 0).toInt());
    // Expand relative folder paths (e.g. "TestData") to absolute path next to EXE
    QString folder = ps.value("Playback_Folder", "").toString();
    if (!folder.isEmpty()) {
        if (QDir(folder).isRelative())
            folder = QDir(QApplication::applicationDirPath()).filePath(folder);
        m_editFolder->setText(folder);
    }
    m_speedSlider->setValue(   ps.value("Playback_Speed",   5).toInt());
    onSpeedSliderChanged(m_speedSlider->value());
    // Store the source mode from preset but DON'T auto-switch or auto-start.
    // User stays in their current mode until they manually switch.
    const int presetMode = ps.value("Source_Mode", 0).toInt();
    // Only update the folder path (for reference), not the active mode
    const QString logPath = ps.value("Log_Path", "").toString();
    if (!logPath.isEmpty()) m_editLogPath->setText(logPath);
    ps.endGroup();

    // Sync ROIs to chart
    { RoiRect r; r.xMin = m_roi1Start->value(); r.xMax = m_roi1End->value(); r.valid = (r.xMax > r.xMin); m_profileWidget->setRoi(0, r); }
    { RoiRect r; r.xMin = m_roi2Start->value(); r.xMax = m_roi2End->value(); r.valid = (r.xMax > r.xMin); m_profileWidget->setRoi(1, r); }

    // Load JSON folder into player (updates file list + frame counter)
    // but do NOT auto-play – user still has to press Play manually.
    // Also: don't force source mode switch, user stays in current mode.
    if (!folder.isEmpty()) {
        // Expand relative paths (e.g. "TestData") to absolute
        QString absFolder = folder;
        if (QDir(absFolder).isRelative())
            absFolder = QDir(QApplication::applicationDirPath()).filePath(absFolder);
        m_editFolder->setText(absFolder);
        if (QDir(absFolder).exists()) {
            qInfo().noquote() << "[Preset] Lade JSON-Ordner (kein Auto-Play):" << absFolder;
            m_jsonPlayer->loadFolder(absFolder);
        }
    }

    m_presetLoading = false;

    statusBar()->showMessage(
        QString("Typ geladen: %1  (Quelle nicht automatisch gestartet)").arg(name), 4000);
}

void MainWindow::onPresetSelected(int index)
{
    if (m_presetLoading || index <= 0) return;  // 0 = "– Typ wählen –" placeholder
    const QString name = m_cmbPresets->itemText(index);
    if (!name.isEmpty())
        applyPreset(name);
}

void MainWindow::onPresetSave()
{
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "Typ speichern",
        "Name für diesen Typ:",
        QLineEdit::Normal,
        m_cmbPresets->currentIndex() > 0 ? m_cmbPresets->currentText() : QString(),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    // Overwrite-Warnung nur wenn neuer Name
    QSettings ps(presetsPath(), QSettings::IniFormat);
    if (ps.childGroups().contains(name)) {
        auto btn = QMessageBox::question(this, "Überschreiben?",
            QString("Typ \"<b>%1</b>\" existiert bereits.<br>Überschreiben?").arg(name),
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
    }

    writePreset(name);
    refreshPresetCombo();

    // Select the just-saved preset
    int idx = m_cmbPresets->findText(name);
    if (idx >= 0) { QSignalBlocker b(m_cmbPresets); m_cmbPresets->setCurrentIndex(idx); }
    statusBar()->showMessage(QString("Typ gespeichert: %1").arg(name), 3000);
}

void MainWindow::onPresetRename()
{
    int idx = m_cmbPresets->currentIndex();
    if (idx <= 0) return;
    const QString oldName = m_cmbPresets->currentText();

    bool ok = false;
    QString newName = QInputDialog::getText(
        this, "Typ umbenennen",
        "Neuer Name:",
        QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName) return;
    newName = newName.trimmed();

    // Copy settings to new group, remove old
    QSettings ps(presetsPath(), QSettings::IniFormat);
    ps.beginGroup(oldName);
    QStringList keys = ps.allKeys();
    QVariantMap vals;
    for (const QString &k : keys) vals[k] = ps.value(k);
    ps.endGroup();
    ps.remove(oldName);
    ps.beginGroup(newName);
    for (auto it = vals.cbegin(); it != vals.cend(); ++it)
        ps.setValue(it.key(), it.value());
    ps.endGroup();
    ps.sync();

    refreshPresetCombo();
    int newIdx = m_cmbPresets->findText(newName);
    if (newIdx >= 0) { QSignalBlocker b(m_cmbPresets); m_cmbPresets->setCurrentIndex(newIdx); }
    statusBar()->showMessage(QString("Typ umbenannt: %1 → %2").arg(oldName, newName), 3000);
}

void MainWindow::onPresetDelete()
{
    int idx = m_cmbPresets->currentIndex();
    if (idx <= 0) return;
    const QString name = m_cmbPresets->currentText();

    auto btn = QMessageBox::question(this, "Typ löschen",
        QString("Typ \"<b>%1</b>\" wirklich löschen?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    QSettings ps(presetsPath(), QSettings::IniFormat);
    ps.remove(name);
    ps.sync();

    refreshPresetCombo();
    m_cmbPresets->setCurrentIndex(0);
    statusBar()->showMessage(QString("Typ gelöscht: %1").arg(name), 3000);
}

// ──────────────────────────────────────────────────────────────────────────────
//  Settings (QSettings → Devices/default.ini)
// ──────────────────────────────────────────────────────────────────────────────
QString MainWindow::settingsPath() const
{
    return QDir(QApplication::applicationDirPath()).filePath("Devices/default.ini");
}

void MainWindow::saveSettings()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue("Sensor/IP",      m_editIp->text());
    s.setValue("Sensor/Port",    m_editPort->text());
    s.setValue("ROI1/Start",     m_roi1Start->value());
    s.setValue("ROI1/End",       m_roi1End->value());
    s.setValue("ROI2/Start",     m_roi2Start->value());
    s.setValue("ROI2/End",       m_roi2End->value());
    s.setValue("ROI1/Method",    m_roi1Method->currentIndex());
    s.setValue("ROI2/Method",    m_roi2Method->currentIndex());
    s.setValue("Playback/Folder",m_editFolder->text());
    s.setValue("Playback/Speed", m_speedSlider->value());
    s.setValue("Source/Mode",    static_cast<int>(m_sourceMode));
    s.setValue("Log/Path",       m_editLogPath->text());
}

void MainWindow::loadSettings()
{
    QString path = settingsPath();

    // Create Devices/Data/Logs folders if they don't exist
    QDir dir(QApplication::applicationDirPath());
    dir.mkpath("Devices");
    dir.mkpath("Data");
    dir.mkpath("Logs");

    // Load main settings if file exists (skip widget-fill if not, but still seed presets)
    if (QFile::exists(path)) {
    QSettings s(path, QSettings::IniFormat);
    m_editIp->setText(s.value("Sensor/IP",   "192.168.3.15").toString());
    m_editPort->setText(s.value("Sensor/Port","1096").toString());
    m_roi1Start->setValue(s.value("ROI1/Start", -100.0).toDouble());
    m_roi1End->setValue(s.value("ROI1/End",       0.0).toDouble());
    m_roi2Start->setValue(s.value("ROI2/Start",   0.0).toDouble());
    m_roi2End->setValue(s.value("ROI2/End",      100.0).toDouble());
    m_roi1Method->setCurrentIndex(s.value("ROI1/Method", 0).toInt());
    m_roi2Method->setCurrentIndex(s.value("ROI2/Method", 0).toInt());

    QString folder = s.value("Playback/Folder", "").toString();
    if (!folder.isEmpty()) m_editFolder->setText(folder);

    m_speedSlider->setValue(s.value("Playback/Speed", 5).toInt());
    onSpeedSliderChanged(m_speedSlider->value());  // refresh label

    int mode = s.value("Source/Mode", 0).toInt();
    if (mode == 1) {
        m_rbPlayback->setChecked(true);
        m_sourceMode = SourceMode::JsonPlayback;
    }

    // Log path
    QString logPath = s.value("Log/Path", "").toString();
    if (logPath.isEmpty()) {
        logPath = QDir(QApplication::applicationDirPath()).filePath(
            QString("Logs/MeasLog_%1.csv")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    }
    m_editLogPath->setText(logPath);

    // Sync ROIs to chart – valid=true so they are drawn immediately
    { RoiRect r; r.xMin = m_roi1Start->value(); r.xMax = m_roi1End->value(); r.valid = (r.xMax > r.xMin); m_profileWidget->setRoi(0, r); }
    { RoiRect r; r.xMin = m_roi2Start->value(); r.xMax = m_roi2End->value(); r.valid = (r.xMax > r.xMin); m_profileWidget->setRoi(1, r); }
    } // end if(QFile::exists)
    else {
        // No settings file yet – set default log path
        m_editLogPath->setText(QDir(QApplication::applicationDirPath()).filePath(
            QString("Logs/MeasLog_%1.csv")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))));
    }

    // Seed presets (only TestTyp_1 – TestData-Setup removed)
    {
        QSettings ps(presetsPath(), QSettings::IniFormat);
        // Remove legacy TestData-Setup if it exists
        if (ps.childGroups().contains("TestData-Setup"))
            ps.remove("TestData-Setup");
        // Seed "TestTyp_1" preset with actual recording settings
        if (!ps.childGroups().contains("TestTyp_1")) {
            const QString recPath =
                QDir(QApplication::applicationDirPath()).filePath("TestData");
            ps.beginGroup("TestTyp_1");
            ps.setValue("Sensor_IP",       "192.168.3.15");
            ps.setValue("Sensor_Port",     "1096");
            ps.setValue("ROI1_Start",      -38.0);
            ps.setValue("ROI1_End",          0.0);
            ps.setValue("ROI1_Method",       2);    // Hough
            ps.setValue("ROI2_Start",        0.0);
            ps.setValue("ROI2_End",        106.0);
            ps.setValue("ROI2_Method",       2);    // Hough
            ps.setValue("Playback_Folder", recPath);
            ps.setValue("Playback_Speed",    5);    // 1.0x
            ps.setValue("Source_Mode",       1);    // JSON Wiedergabe
            ps.setValue("Log_Path",          QString());
            ps.endGroup();
        }
        ps.sync();
    }

    // Populate preset combo
    refreshPresetCombo();
}

void MainWindow::onQuadrantSelected(AngleQuadrant q)
{
    m_angleQuadrant = q;
    // Update button checked states (exclusive)
    m_btnQuadTL->setChecked(q == AngleQuadrant::TopLeft);
    m_btnQuadTR->setChecked(q == AngleQuadrant::TopRight);
    m_btnQuadBL->setChecked(q == AngleQuadrant::BottomLeft);
    m_btnQuadBR->setChecked(q == AngleQuadrant::BottomRight);
    // Update chart arc
    m_profileWidget->setAngleQuadrant(q);
    // Recompute with cached last frame so angle updates immediately
    if (!m_lastProfilePts.empty())
        computeAndDisplayFitLines(m_lastProfilePts);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lock / Unlock – sperrt alle Einstellungs-Widgets
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onLockToggle(bool locked)
{
    m_locked = locked;
    m_btnLock->setText(locked ? "🔒  Gesperrt – zum Entsperren klicken"
                               : "🔓  Einstellungen sperren");

    // All widgets to lock/unlock
    const QList<QWidget*> lockable = {
        m_editIp, m_editPort,
        m_roi1Start, m_roi1End, m_roi1Method,
        m_roi2Start, m_roi2End, m_roi2Method,
        m_editFolder, m_speedSlider,
        m_rbLive, m_rbPlayback,
        m_editLogPath, m_btnLogBrowse, m_btnLogToggle,
        m_editRecordFolder, m_btnRecordBrowse, m_btnRecordToggle,
        m_cmbPresets, m_btnPresetSave, m_btnPresetRen, m_btnPresetDel,
        m_spinMaxFrames
    };
    for (QWidget *w : lockable)
        if (w) w->setEnabled(!locked);

    statusBar()->showMessage(
        locked ? "🔒 Einstellungen gesperrt" : "🔓 Einstellungen entsperrt", 3000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    if (m_logger->isOpen()) m_logger->close();
    if (m_sensorWorker && m_sensorWorker->isRunning()) {
        m_sensorWorker->stopCapture();
        // Process events while waiting so close doesn't freeze
        for (int i = 0; i < 15 && m_sensorWorker->isRunning(); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            m_sensorWorker->wait(100);
        }
    }
    m_jsonPlayer->stop();
    event->accept();
}

// ──────────────────────────────────────────────────────────────────────────────
//  Messwert-Log Slots
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onLogBrowse()
{
    QString suggested = m_editLogPath->text();
    if (suggested.isEmpty()) {
        suggested = QDir(QApplication::applicationDirPath()).filePath(
            QString("Logs/MeasLog_%1.csv")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    }
    QString fn = QFileDialog::getSaveFileName(
        this, "Log-Datei wählen", suggested,
        "CSV-Dateien (*.csv);;Alle Dateien (*)");
    if (!fn.isEmpty())
        m_editLogPath->setText(fn);
}

void MainWindow::onLogToggle(bool checked)
{
    if (checked) {
        QString p = m_editLogPath->text().trimmed();
        qInfo().noquote() << "[MeasLog] Aufzeichnung starten:" << p;
        if (p.isEmpty()) {
            p = QDir(QApplication::applicationDirPath()).filePath(
                QString("Logs/MeasLog_%1.csv")
                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
            m_editLogPath->setText(p);
        }
        // Ensure parent directory exists
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (!m_logger->open(p)) {
            QMessageBox::warning(this, "Log-Fehler",
                QString("Konnte Log-Datei nicht öffnen:\n%1").arg(p));
            m_btnLogToggle->setChecked(false);
            return;
        }
        m_btnLogToggle->setText("■ Aufzeichnung stoppen");
        m_btnLogToggle->setStyleSheet("background:#5c1a1a; color:white; padding:5px; border-radius:4px;");
    } else {
        m_logger->close();
        m_btnLogToggle->setText("● Aufzeichnung starten");
        m_btnLogToggle->setStyleSheet("background:#1a5c2e; color:white; padding:5px; border-radius:4px;");
    }
}

void MainWindow::onLogRowWritten(int row)
{
    m_lblLogStatus->setText(
        QString("<b style='color:#00e676'>● Aufzeichnung: %1 Zeile%2</b>")
            .arg(row)
            .arg(row == 1 ? "" : "n"));
}
