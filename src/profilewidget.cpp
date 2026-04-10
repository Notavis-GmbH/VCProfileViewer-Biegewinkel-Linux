/****************************************************************************
** profilewidget.cpp
****************************************************************************/
#include "profilewidget.h"
#include "licensemanager.h"
#include "keygen_config.h"
#include <QSettings>
#include <QtSvg/QSvgRenderer>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QFontMetrics>
#include <QWheelEvent>
#include <algorithm>
#include <limits>

// ========================================================================
// ProfileChartView
// ========================================================================

ProfileChartView::ProfileChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    // Disable Qt Charts built-in rubber-band zoom – we handle wheel ourselves
    setRubberBand(QChartView::NoRubberBand);
    m_rois[0] = m_rois[1] = RoiRect{};
}

void ProfileChartView::setDrawingRoi(RoiId id)
{
    m_drawingRoi = id;
    if (id != ROI_NONE)
        setCursor(Qt::CrossCursor);
    else
        setCursor(Qt::ArrowCursor);
}

void ProfileChartView::setRoi(RoiId id, const RoiRect &r)
{
    if (id >= 0 && id < 2) {
        m_rois[id] = r;
        viewport()->update();
    }
}

QPointF ProfileChartView::widgetToChart(const QPoint &p) const
{
    QRectF plotArea = chart()->plotArea();
    auto   axes     = chart()->axes();
    if (axes.size() < 2) return {};

    QValueAxis *axX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
    QValueAxis *axZ = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical).first());
    if (!axX || !axZ) return {};

    double xRatio = (p.x() - plotArea.left()) / plotArea.width();
    double zRatio = (p.y() - plotArea.top())  / plotArea.height();

    double x = axX->min() + xRatio * (axX->max() - axX->min());
    double z = axZ->max() - zRatio * (axZ->max() - axZ->min());

    return QPointF(x, z);
}

void ProfileChartView::mousePressEvent(QMouseEvent *e)
{
    // Right mouse button = Pan
    if (e->button() == Qt::RightButton) {
        QValueAxis *axX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
        QValueAxis *axZ = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical).first());
        if (axX && axZ) {
            m_panning  = true;
            m_panStart = e->pos();
            m_panX0min = axX->min(); m_panX0max = axX->max();
            m_panZ0min = axZ->min(); m_panZ0max = axZ->max();
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
    }
    // Left mouse button = ROI draw
    if (m_drawingRoi != ROI_NONE && e->button() == Qt::LeftButton) {
        m_dragging    = true;
        m_dragStart   = e->pos();
        m_dragCurrent = e->pos();
        e->accept();
        return;
    }
    QChartView::mousePressEvent(e);
}

void ProfileChartView::mouseMoveEvent(QMouseEvent *e)
{
    // Pan
    if (m_panning) {
        QValueAxis *axX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
        QValueAxis *axZ = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical).first());
        if (axX && axZ) {
            QRectF plotArea = chart()->plotArea();
            double dxPx = e->pos().x() - m_panStart.x();
            double dzPx = e->pos().y() - m_panStart.y();
            double xRange = m_panX0max - m_panX0min;
            double zRange = m_panZ0max - m_panZ0min;
            double dx = -dxPx / plotArea.width()  * xRange;
            double dz =  dzPx / plotArea.height() * zRange;
            axX->setRange(m_panX0min + dx, m_panX0max + dx);
            axZ->setRange(m_panZ0min + dz, m_panZ0max + dz);
            chart()->update();
            scene()->update();
        }
        e->accept();
        return;
    }
    // ROI rubber-band
    if (m_dragging) {
        m_dragCurrent = e->pos();
        viewport()->update();
        e->accept();
        return;
    }
    QChartView::mouseMoveEvent(e);
}

void ProfileChartView::mouseReleaseEvent(QMouseEvent *e)
{
    // End pan
    if (m_panning && e->button() == Qt::RightButton) {
        m_panning = false;
        setCursor(m_drawingRoi != ROI_NONE ? Qt::CrossCursor : Qt::ArrowCursor);
        e->accept();
        return;
    }
    // End ROI draw
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        QPointF p0 = widgetToChart(m_dragStart);
        QPointF p1 = widgetToChart(e->pos());

        RoiRect r;
        r.xMin  = std::min(p0.x(), p1.x());
        r.xMax  = std::max(p0.x(), p1.x());
        r.zMin  = std::min(p0.y(), p1.y());
        r.zMax  = std::max(p0.y(), p1.y());
        r.valid = (r.xMax - r.xMin > 0.1) && (r.zMax - r.zMin > 0.1);

        if (r.valid) {
            m_rois[m_drawingRoi] = r;
            emit roiChanged(m_drawingRoi, r);
        }
        setDrawingRoi(ROI_NONE);
        viewport()->update();
        e->accept();
        return;
    }
    QChartView::mouseReleaseEvent(e);
}

void ProfileChartView::setHeatmapData(
        const std::vector<std::pair<double,double>> &res1,
        const std::vector<std::pair<double,double>> &res2,
        const FitLine &fl1, const FitLine &fl2)
{
    m_hmRes1 = res1;  m_hmLine1 = fl1;
    m_hmRes2 = res2;  m_hmLine2 = fl2;
    viewport()->update();
}

void ProfileChartView::mouseDoubleClickEvent(QMouseEvent *e)
{
    // Double-click = emit signal so ProfileWidget can reset zoom
    emit resetZoomRequested();
    QChartView::mouseDoubleClickEvent(e);
}

void ProfileChartView::wheelEvent(QWheelEvent *e)
{
    QValueAxis *axX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
    QValueAxis *axZ = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical).first());
    if (!axX || !axZ) { QChartView::wheelEvent(e); return; }

    // Zoom factor: scroll up = zoom in, scroll down = zoom out
    double factor = (e->angleDelta().y() > 0) ? 0.85 : 1.0 / 0.85;

    // Zoom around the mouse cursor position in chart coordinates
    QPointF pivot = widgetToChart(e->position().toPoint());

    double newXMin = pivot.x() + (axX->min() - pivot.x()) * factor;
    double newXMax = pivot.x() + (axX->max() - pivot.x()) * factor;
    double newZMin = pivot.y() + (axZ->min() - pivot.y()) * factor;
    double newZMax = pivot.y() + (axZ->max() - pivot.y()) * factor;

    axX->setRange(newXMin, newXMax);
    axZ->setRange(newZMin, newZMax);

    // Force immediate repaint – Qt Charts defers redraws otherwise
    chart()->update();
    scene()->update();
    e->accept();
}

// Chart-pixel coordinates of a world point
QPoint ProfileChartView::chartToWidget(double x, double z) const
{
    QRectF plotArea = chart()->plotArea();
    QValueAxis *axX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
    QValueAxis *axZ = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical).first());
    if (!axX || !axZ) return {};

    double xRatio = (x - axX->min()) / (axX->max() - axX->min());
    double zRatio = (axZ->max() - z)  / (axZ->max() - axZ->min());

    int px = static_cast<int>(plotArea.left() + xRatio * plotArea.width());
    int py = static_cast<int>(plotArea.top()  + zRatio * plotArea.height());
    return QPoint(px, py);
}

void ProfileChartView::drawRoiOverlay(QPainter &painter, RoiId id, QColor color)
{
    RoiRect &r = m_rois[id];
    if (!r.valid) return;

    QPoint tl = chartToWidget(r.xMin, r.zMax);
    QPoint br = chartToWidget(r.xMax, r.zMin);
    QRect  rect(tl, br);

    // Fill
    QColor fill = color;
    fill.setAlpha(40);
    painter.fillRect(rect, fill);

    // Border
    QPen pen(color, 2, Qt::SolidLine);
    painter.setPen(pen);
    painter.drawRect(rect);

    // Label
    QString label = QString("ROI %1").arg(id + 1);
    QFont f = painter.font();
    f.setBold(true);
    f.setPointSize(9);
    painter.setFont(f);
    painter.setPen(color);
    painter.drawText(tl + QPoint(4, 14), label);
}

// Helper: colour for a residual value relative to the max in this ROI
static QColor heatColor(double residual, double maxRes)
{
    if (maxRes < 1e-9) return QColor::fromHsv(120, 200, 255, 210);
    double t   = std::min(residual / maxRes, 1.0);   // 0 (good) … 1 (bad)
    int    hue = static_cast<int>((1.0 - t) * 120.0); // green=120 → red=0
    return QColor::fromHsv(hue, 230, 255, 210);
}

void ProfileChartView::drawHeatmap(
        QPainter &painter,
        const std::vector<std::pair<double,double>> &residuals,
        const FitLine &fl)
{
    if (!fl.valid || residuals.empty()) return;

    painter.setPen(Qt::NoPen);
    const int hs = 4;   // half-size of each heat square in pixels
    for (auto &r : residuals) {
        double x   = r.first;
        double res = r.second;
        double z   = fl.slope * x + fl.intercept;  // z on fit line
        QPoint px  = chartToWidget(x, z);
        QColor col = heatColor(res, fl.maxResidual);
        painter.fillRect(px.x() - hs, px.y() - hs, hs * 2, hs * 2, col);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  setFitLabels / setDocOverlayVisible
// ─────────────────────────────────────────────────────────────────────────────
void ProfileChartView::setFitLabels(const QString &m1Label, const QString &m2Label,
                                    double bendingAngleDeg)
{
    m_methodLabel1    = m1Label;
    m_methodLabel2    = m2Label;
    m_bendingAngle    = bendingAngleDeg;
    m_hasBendingAngle = !m1Label.isEmpty() && !m2Label.isEmpty();
    viewport()->update();
}

void ProfileChartView::setDocOverlayVisible(bool v)
{
    m_docOverlayVisible = v;
    viewport()->update();
}

void ProfileChartView::setAngleQuadrant(AngleQuadrant q)
{
    m_angleQuadrant = q;
    viewport()->update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: draw a semi-transparent rounded-rect background for text
// ─────────────────────────────────────────────────────────────────────────────
static void drawBubble(QPainter &p, const QRect &r, QColor bg)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, 5, 5);
    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawFitLineLabels – draw Phi + Method label at the RIGHT end of each fit line
// ─────────────────────────────────────────────────────────────────────────────
void ProfileChartView::drawFitLineLabels(QPainter &painter)
{
    // ROI 1 (blue) and ROI 2 (orange)
    struct LineInfo {
        const FitLine *fl;
        const QString *label;
        QColor         color;
        int            side;   // -1 = label left of end, +1 = label right of start
    };

    LineInfo lines[] = {
        { &m_hmLine1, &m_methodLabel1, QColor(0, 180, 255),  +1 },
        { &m_hmLine2, &m_methodLabel2, QColor(255, 140,  0), -1 },
    };

    QFont font = painter.font();
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);

    for (auto &li : lines) {
        if (!li.fl->valid || li.label->isEmpty()) continue;

        // Anchor point: right end of fit line (xMax)
        double anchorX = (li.side > 0) ? li.fl->xMax : li.fl->xMin;
        double anchorZ = li.fl->slope * anchorX + li.fl->intercept;
        QPoint anchor = chartToWidget(anchorX, anchorZ);

        // Build label string
        QString text = QString("%1  φ=%2°  RMS=%3μm")
                       .arg(*li.label)
                       .arg(li.fl->phi,          0, 'f', 2)
                       .arg(li.fl->rmsResidual * 1000.0, 0, 'f', 1);

        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(text);
        textRect.adjust(-6, -4, 6, 4);   // padding

        // Position: above the anchor, shifted left/right
        int tx = (li.side > 0)
                 ? anchor.x() - textRect.width() - 4
                 : anchor.x() + 4;
        int ty = anchor.y() - textRect.height() / 2 - 2;

        // Clamp to viewport
        tx = std::max(4, std::min(tx, viewport()->width()  - textRect.width()  - 4));
        ty = std::max(4, std::min(ty, viewport()->height() - textRect.height() - 4));

        textRect.moveTopLeft(QPoint(tx, ty));

        // Draw bubble + text
        QColor bg = li.color; bg.setAlpha(160);
        drawBubble(painter, textRect, QColor(20, 20, 30, 200));

        painter.setPen(QPen(li.color, 1));
        painter.drawRoundedRect(textRect, 5, 5);

        painter.setPen(li.color);
        painter.drawText(textRect, Qt::AlignCenter, text);

        // Draw a small circle at the anchor point
        painter.setPen(Qt::NoPen);
        painter.setBrush(li.color);
        painter.drawEllipse(anchor, 4, 4);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
//  drawInfoPanel – large bending angle (top-right) + small ROI subtitles
// ─────────────────────────────────────────────────────────────────────────────
void ProfileChartView::drawInfoPanel(QPainter &painter)
{
    if (!m_hasBendingAngle && !m_hmLine1.valid && !m_hmLine2.valid) return;

    QRectF pa = chart()->plotArea();

    // ── Large bending angle ────────────────────────────────────────────────
    if (m_hasBendingAngle) {
        QFont bigFont = painter.font();
        bigFont.setPointSize(32);
        bigFont.setBold(true);
        QFontMetrics bigFm(bigFont);
        const QString angleText = QString("%1°").arg(std::abs(m_bendingAngle), 0, 'f', 2);
        int tw = bigFm.horizontalAdvance(angleText) + 24;
        int th = bigFm.height() + 16;
        int px = static_cast<int>(pa.right()) - tw - 8;
        int py = static_cast<int>(pa.top())  +  8;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 10, 20, 220));
        painter.drawRoundedRect(QRect(px, py, tw, th), 8, 8);
        painter.setPen(QPen(QColor(0, 230, 118), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRect(px, py, tw, th), 8, 8);
        painter.setFont(bigFont);
        painter.setPen(QColor(0, 230, 118));
        painter.drawText(QRect(px, py, tw, th), Qt::AlignCenter, angleText);
    }

    // ROI subtitle panel removed per user request
}


// ─────────────────────────────────────────────────────────────────────────────
//  drawDocOverlay – full documentation panel (toggle via ? button)
// ─────────────────────────────────────────────────────────────────────────────
void ProfileChartView::drawDocOverlay(QPainter &painter)
{
    QFont titleFont = painter.font();
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    QFont sectionFont = painter.font();
    sectionFont.setPointSize(11);
    sectionFont.setBold(true);
    QFont bodyFont = painter.font();
    bodyFont.setPointSize(10);
    bodyFont.setBold(false);

    // ── Measure content height first so the box fits exactly ─────────────────
    QFontMetrics fmTitle(titleFont);
    QFontMetrics fmSection(sectionFont);
    QFontMetrics fmBody(bodyFont);

    const int titleLineH   = fmTitle.height()   + 4;
    const int sectionLineH = fmSection.height() + 6;
    const int bodyLineH    = fmBody.height()    + 4;
    const int versionLineH = fmBody.height() + 2;
    const int sectionGap   = 6;   // extra space before each section
    const int closeHintH   = bodyLineH + 8;
    const int paddingV     = 20;  // top + bottom padding
    const int paddingH     = 20;  // left + right padding
    const int panelW       = 640;
    const int keyW         = 150;

    // Count total height
    int contentH = titleLineH + 6 /*divider*/;
    // Steuerung: 1 section + 5 rows
    contentH += sectionGap + sectionLineH + 5 * bodyLineH;
    // Linienfinder: 1 section + 4 rows
    contentH += sectionGap + sectionLineH + 4 * bodyLineH;
    // Auto-Heuristik: 1 section + 5 rows
    contentH += sectionGap + sectionLineH + 5 * bodyLineH;
    // Visualisierung: 1 section + 4 rows
    contentH += sectionGap + sectionLineH + 4 * bodyLineH;
    // Lizenz: 1 section + 3 rows + Reset-Hinweis
    contentH += sectionGap + sectionLineH + 3 * bodyLineH + bodyLineH;
    contentH += closeHintH;
    const int panelH = contentH + paddingV * 2;

    // ── Position: top-left of plot area ──────────────────────────────────────
    QRectF pa = chart()->plotArea();
    int ox = static_cast<int>(pa.left()) + 20;
    int oy = static_cast<int>(pa.top())  + 20;
    // Clamp so it never exceeds the plot area
    int availH = static_cast<int>(pa.height()) - 40;
    int availW = static_cast<int>(pa.width())  - 40;
    QRect overlay(ox, oy,
                  std::min(panelW, availW),
                  panelH);

    // ── Background ───────────────────────────────────────────────────────────
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 10, 20, 238));
    painter.drawRoundedRect(overlay, 10, 10);
    painter.setPen(QPen(QColor(100, 100, 140), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(overlay, 10, 10);

    int x = overlay.x() + paddingH;
    int y = overlay.y() + paddingV;
    int w = overlay.width() - paddingH * 2;

    // ─── Title ───────────────────────────────────────────────────────────────
    // ── Logo (left) + Title + Version (right) ─────────────────────────────────
    // Draw logo on the left of the title bar
    {
        const int logoW = 90, logoH = 32;
        QSvgRenderer logoRenderer(QString(":/images/logo_notavis.svg"));
        if (!logoRenderer.isValid())
            logoRenderer.load(QCoreApplication::applicationDirPath() + "/resources/logo_notavis.svg");
        if (logoRenderer.isValid()) {
            painter.save();
            painter.setOpacity(0.85);
            logoRenderer.render(&painter, QRectF(x, y, logoW, logoH));
            painter.restore();
        }

        // Title centred
        painter.setFont(titleFont);
        painter.setPen(QColor(0, 200, 255));
        painter.drawText(QRect(x + logoW + 8, y, w - logoW - 8, titleLineH),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         "VC 3D Profile Viewer  –  Hilfe");

        // Version bottom-right of title row
        y += qMax(titleLineH, logoH) + 2;

#ifndef BUILD_TIMESTAMP
#  define BUILD_TIMESTAMP "dev"
#endif
        // Versionszeile – eigene Zeile unter dem Titel
        QFont vf = bodyFont; vf.setPointSize(8); vf.setBold(false);
        painter.setFont(vf);
        painter.setPen(QColor(120, 120, 150));
        painter.drawText(QRect(x, y, w, versionLineH),
                         static_cast<int>(Qt::AlignRight | Qt::AlignVCenter),
                         QString("v2.2-%1").arg(BUILD_TIMESTAMP));

        y += versionLineH + 2;
    }
    painter.setPen(QColor(70, 70, 100));
    painter.drawLine(x, y, x + w, y);
    y += 6;

    // ─── Section / row helpers ────────────────────────────────────────────────
    auto section = [&](const QString &title) {
        y += sectionGap;
        painter.setFont(sectionFont);
        painter.setPen(QColor(160, 200, 255));
        painter.drawText(QRect(x, y, w, sectionLineH), Qt::AlignLeft | Qt::AlignVCenter, title);
        y += sectionLineH;
    };
    auto row = [&](const QString &key, const QString &desc, QColor kc = QColor(210, 210, 210)) {
        painter.setFont(bodyFont);
        painter.setPen(kc);
        painter.drawText(QRect(x + 4, y, keyW, bodyLineH),
                         Qt::AlignLeft | Qt::AlignVCenter, key);
        painter.setPen(QColor(185, 185, 185));
        painter.drawText(QRect(x + 4 + keyW, y, w - keyW - 8, bodyLineH),
                         Qt::AlignLeft | Qt::AlignVCenter, desc);
        y += bodyLineH;
    };

    // ─── Steuerung ───────────────────────────────────────────────────────────
    section("▶ Steuerung");
    row("Mausrad",          "Zoom um Cursorposition");
    row("Rechte Maustaste", "Pan (Bild verschieben)");
    row("Doppelklick",      "Zoom auf gesamte Messdaten (Fit)");
    row("Linkes Drag",      "ROI aufziehen (wenn ROI-Modus aktiv)");
    row("⌫  Fit-Button",    "Zoom auf gesamte Messdaten zurücksetzen");

    // ─── Linienfinder ────────────────────────────────────────────────────────
    section("▶ Linienfinder (pro ROI wählbar)");
    row("OLS",    "Kleinste Quadrate – schnell, optimal für saubere Profile",
        QColor(180, 220, 255));
    row("RANSAC", "Random Sample Consensus – robust gegen Ausreißer & Reflexionen",
        QColor(255, 200, 100));
    row("Hough",  "Hough-Transformation – robust bei Lücken & Artefakten",
        QColor(100, 255, 180));
    row("Auto",   "Automatisch: wählt OLS/RANSAC/Hough je nach Inlier-Ratio+RMS",
        QColor(200, 160, 255));

    // ─── Auto-Modus Heuristik ────────────────────────────────────────────────
    section("▶ Auto-Modus Heuristik");
    row("≥ 90 % Inlier",  "OLS (sauberes Profil)");
    row("60–90 % Inlier", "RANSAC (Ausreißer erkannt)");
    row("< 60 % Inlier",  "Hough (fragmentiertes Profil)");
    row("Inlier-Band",    "0,5 mm Abstand vom OLS-Fit");
    row("RMS-Fallback",   "RANSAC-RMS > 1,5 × OLS-RMS  →  Hough");

    // ─── Visualisierung ──────────────────────────────────────────────────────
    section("▶ Visualisierung");
    row("Blaue Linie",   "Erkannte Gerade ROI 1 (inkl. φ und RMS)");
    row("Orange Linie",  "Erkannte Gerade ROI 2 (inkl. φ und RMS)");
    row("Heatmap",       "Residuen je Messpunkt: grün=klein → rot=groß");
    row("Info-Panel",    "Zusammenfassung oben rechts im Chart");

    // ─── Lizenz ──────────────────────────────────────────────────────────────
    {
        QSettings lic;
        const QString licType  = lic.value(KeygenConfig::SETTINGS_LICENSE_TYPE).toString();
        const QString licKey   = lic.value(KeygenConfig::SETTINGS_LICENSE_KEY).toString();

        section(QStringLiteral("\u25B6 Lizenz"));

        QString typeStr, statusStr;
        QColor  statusColor = QColor(100, 255, 150);

        if (licType == QLatin1String("trial")) {
            LicenseManager tmpMgr;
            const int days = tmpMgr.trialDaysRemaining();
            typeStr   = QStringLiteral("Testversion (69 Tage)");
            if (days > 0) {
                statusStr  = QStringLiteral("Aktiv – noch %1 Tag%2").arg(days).arg(days == 1 ? "" : "e");
            } else {
                statusStr  = QStringLiteral("Abgelaufen");
                statusColor = QColor(255, 100, 100);
            }
        } else if (licType == QLatin1String("commercial")) {
            typeStr   = QStringLiteral("Kommerzielle Lizenz");
            statusStr = QStringLiteral("Aktiv");
        } else {
            typeStr   = QStringLiteral("–");
            statusStr = QStringLiteral("Nicht aktiviert");
            statusColor = QColor(255, 180, 50);
        }

        row("Typ",    typeStr);
        row("Status", statusStr, statusColor);

        // Schlüssel – nur erste/letzte 4 Zeichen zeigen
        QString keyDisplay;
        if (licKey.length() > 8)
            keyDisplay = licKey.left(4) + QStringLiteral("–…–") + licKey.right(4);
        else if (!licKey.isEmpty())
            keyDisplay = licKey;
        else
            keyDisplay = QStringLiteral("–");
        row("Schlüssel", keyDisplay, QColor(160, 160, 160));

        // Reset-Hinweis
        y += 2;
        painter.setFont(bodyFont);
        painter.setPen(QColor(100, 120, 160));
        painter.drawText(QRect(x + 4, y, w - 8, bodyLineH),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Zum Zurücksetzen: Strg+Shift+R"));
        y += bodyLineH;
    }

    // ─── Close hint ──────────────────────────────────────────────────────────
    y += 4;
    painter.setFont(bodyFont);
    painter.setPen(QColor(120, 120, 150));
    painter.drawText(QRect(x, y, w, bodyLineH),
                     Qt::AlignRight | Qt::AlignVCenter, "[ ? ] zum Schließen");
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawWatermark – NOTAVIS logo, semi-transparent, bottom-right of plot area
// ─────────────────────────────────────────────────────────────────────────────
void ProfileChartView::drawWatermark(QPainter &painter)
{
    QRectF pa = chart()->plotArea();
    if (pa.width() < 50 || pa.height() < 50) return;

    // Try Qt resource first, then filesystem fallback next to EXE
    static QSvgRenderer s_renderer;
    static bool s_loaded = false;
    if (!s_loaded) {
        s_loaded = true;
        if (!s_renderer.load(QString(":/images/logo_notavis.svg"))) {
            // Fallback: look next to executable
            const QString fsPath = QCoreApplication::applicationDirPath()
                                   + "/resources/logo_notavis.svg";
            s_renderer.load(fsPath);
            if (s_renderer.isValid())
                qInfo() << "[Watermark] Loaded from filesystem:" << fsPath;
            else
                qWarning() << "[Watermark] SVG not found via resource or filesystem";
        } else {
            qInfo() << "[Watermark] Loaded from Qt resource";
        }
    }
    if (!s_renderer.isValid()) return;

    // Target size: width = 20% of plot width, aspect ratio 1000:360
    const double aspect = 1000.0 / 360.0;
    int w = static_cast<int>(pa.width() * 0.20);
    int h = static_cast<int>(w / aspect);
    int x = static_cast<int>(pa.right())  - w - 16;
    int y = static_cast<int>(pa.bottom()) - h - 16;

    painter.save();
    painter.setOpacity(0.18);  // subtle but visible
    s_renderer.render(&painter, QRectF(x, y, w, h));
    painter.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  drawAngleArc – arc at the intersection of the two fit lines
// ─────────────────────────────────────────────────────────────────────────────
void ProfileChartView::drawAngleArc(QPainter &painter)
{
    if (!m_hmLine1.valid || !m_hmLine2.valid) return;

    // Guard: axes must be valid before calling chartToWidget
    if (chart()->axes(Qt::Horizontal).isEmpty() ||
        chart()->axes(Qt::Vertical).isEmpty()) return;
    {
        QValueAxis *axX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first());
        QValueAxis *axZ = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical).first());
        if (!axX || !axZ) return;
        if ((axX->max() - axX->min()) < 1e-9) return;
        if ((axZ->max() - axZ->min()) < 1e-9) return;
    }

    // Also guard plotArea size (can be zero during resize/fullscreen transition)
    QRectF pa2 = chart()->plotArea();
    if (pa2.width() < 10 || pa2.height() < 10) return;

    double s1 = m_hmLine1.slope, b1 = m_hmLine1.intercept;
    double s2 = m_hmLine2.slope, b2 = m_hmLine2.intercept;
    if (std::abs(s2 - s1) < 1e-9) return;

    // Intersection in chart coordinates
    double xi = (b2 - b1) / (s1 - s2);
    double zi = s1 * xi + b1;
    QPoint ip = chartToWidget(xi, zi);

    // Two lines through the intersection create 4 sectors.
    // Each line has a "rightward" direction (toward larger X in chart) with
    // screen angle:  atan2(-slope, 1)  (Z inverted: up in chart = up in screen = smaller Y)
    // and a "leftward" direction (toward smaller X):  atan2(slope, -1)
    //
    // The 4 sectors are bounded by these 4 ray directions:
    //   L1_right, L2_right, L1_left (=L1_right+180), L2_left (=L2_right+180)
    //
    // Sector index (matching AngleQuadrant enum):
    //   0 TopLeft:     between L1_left  and L2_left   (both lines going left)
    //   1 TopRight:    between L1_right and L2_left   (L1 right, L2 left)
    //   2 BottomLeft:  between L1_left  and L2_right  (L1 left,  L2 right)
    //   3 BottomRight: between L1_right and L2_right  (both lines going right)

    // L1 and L2 each have two rays from the intersection.
    // ang_r = rightward (chart +X direction), ang_l = leftward (chart -X direction)
    // In screen space: chart +X = screen +X, chart +Z = screen -Y (Z inverted)
    double ang1r = std::atan2(-s1, 1.0) * 180.0 / M_PI;
    double ang2r = std::atan2(-s2, 1.0) * 180.0 / M_PI;
    // Normalise both to [0, 360)
    while (ang1r < 0) ang1r += 360.0;
    while (ang2r < 0) ang2r += 360.0;
    double ang1l = ang1r + 180.0;  if (ang1l >= 360.0) ang1l -= 360.0;
    double ang2l = ang2r + 180.0;  if (ang2l >= 360.0) ang2l -= 360.0;
    // All four sector boundaries, normalised to [0,360)
    // Sectors go CCW in Qt convention; we always sweep the SHORT arc (<180 deg)
    // between the two bounding rays of the chosen quadrant.
    //
    // The 4 rays divide the full circle into 4 sectors.
    // Sort them to find the 4 sector start/end pairs:
    double rays[4] = { ang1r, ang2r, ang1l, ang2l };
    // Sort
    for (int i=0;i<3;i++) for (int j=i+1;j<4;j++) if (rays[j]<rays[i]) std::swap(rays[i],rays[j]);
    // The 4 sectors (CCW) between consecutive sorted rays:
    // sector[k] = rays[k] to rays[(k+1)%4], span = rays[(k+1)%4] - rays[k]  (mod 360 for last)
    //
    // Each sector belongs to one quadrant. Identify which:
    // A sector's midpoint angle tells us which "half" of each line it's in.
    // Mid-angle m: line is in "right" half if the angle to that line's right ray < 90 deg.
    //
    // Quadrant mapping:
    //   L1_right + L2_right -> BottomRight (both right halves)
    //   L1_left  + L2_right -> BottomLeft
    //   L1_right + L2_left  -> TopRight
    //   L1_left  + L2_left  -> TopLeft

    double startAngle = ang1r, spanAngle = ang2r - ang1r;  // fallback

    for (int k = 0; k < 4; k++) {
        double a = rays[k];
        double b = (k == 3) ? rays[0] + 360.0 : rays[k+1];
        double span = b - a;
        if (span <= 0) span += 360.0;  // should not happen after sort, but guard
        double mid  = a + span * 0.5;
        while (mid >= 360.0) mid -= 360.0;
        while (mid <    0.0) mid += 360.0;

        // Determine which half of L1 and L2 the mid-angle falls in
        auto angDiff = [](double a, double b) -> double {
            double d = std::fmod(std::abs(a - b), 360.0);
            return d > 180.0 ? 360.0 - d : d;
        };
        bool l1Right = (angDiff(mid, ang1r) < angDiff(mid, ang1l));
        bool l2Right = (angDiff(mid, ang2r) < angDiff(mid, ang2l));

        AngleQuadrant sectorQ;
        if      ( l1Right &&  l2Right) sectorQ = AngleQuadrant::BottomRight;
        else if (!l1Right &&  l2Right) sectorQ = AngleQuadrant::BottomLeft;
        else if ( l1Right && !l2Right) sectorQ = AngleQuadrant::TopRight;
        else                           sectorQ = AngleQuadrant::TopLeft;

        if (sectorQ == m_angleQuadrant) {
            startAngle = a;
            while (startAngle >= 360.0) startAngle -= 360.0;
            while (startAngle <    0.0) startAngle += 360.0;
            spanAngle  = span;
            break;
        }
    }

    // Qt drawPie: CCW positive from +X, 1/16 deg; screen Y down so negate
    const int R = 44;
    int qtStart = static_cast<int>(-startAngle * 16.0);
    int qtSpan  = static_cast<int>(-spanAngle  * 16.0);

    painter.setPen(QPen(QColor(255, 220, 0), 2));
    painter.setBrush(QBrush(QColor(255, 220, 0, 50)));
    painter.drawPie(QRect(ip.x()-R, ip.y()-R, 2*R, 2*R), qtStart, qtSpan);

    // Label
    static const char* kNames[] = {"Oben-Links","Oben-Rechts","Unten-Links","Unten-Rechts"};
    const int qi = static_cast<int>(m_angleQuadrant);
    QString label = kNames[qi];
    QFont lf = painter.font(); lf.setPointSize(9); lf.setBold(true);
    painter.setFont(lf);
    QFontMetrics lfm(lf);
    int lw = lfm.horizontalAdvance(label) + 10, lh = lfm.height() + 4;
    int lx = (m_angleQuadrant == AngleQuadrant::TopLeft ||
              m_angleQuadrant == AngleQuadrant::BottomLeft)
             ? ip.x() - R - lw - 4
             : ip.x() + R + 8;
    int ly = ip.y() - lh / 2;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 10, 20, 180));
    painter.drawRoundedRect(QRect(lx, ly, lw, lh), 3, 3);
    painter.setPen(QColor(255, 220, 0));
    painter.setBrush(Qt::NoBrush);
    painter.drawText(lx + 4, ly + lfm.ascent() + 2, label);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 220, 0, 200));
    painter.drawEllipse(ip, 4, 4);
}

void ProfileChartView::paintEvent(QPaintEvent *e)
{
    QChartView::paintEvent(e);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. Heatmap (drawn first, behind ROI borders and lines)
    drawHeatmap(painter, m_hmRes1, m_hmLine1);
    drawHeatmap(painter, m_hmRes2, m_hmLine2);

    // 2. ROI overlay borders
    drawRoiOverlay(painter, ROI_1, QColor(0, 180, 255));
    drawRoiOverlay(painter, ROI_2, QColor(255, 140, 0));

    // 3. Fit-line labels (phi, method, RMS) at line ends
    drawFitLineLabels(painter);

    // 4. Info panel (top-right summary box)
    drawInfoPanel(painter);
    drawAngleArc(painter);

    // 5. ROI rubber-band while drawing
    if (m_dragging) {
        QRect dragRect = QRect(m_dragStart, m_dragCurrent).normalized();
        QColor col = (m_drawingRoi == ROI_1) ? QColor(0, 180, 255, 60)
                                              : QColor(255, 140, 0, 60);
        painter.fillRect(dragRect, col);
        QColor border = (m_drawingRoi == ROI_1) ? QColor(0, 180, 255)
                                                : QColor(255, 140, 0);
        painter.setPen(QPen(border, 2, Qt::DashLine));
        painter.drawRect(dragRect);
    }

    // 6. Documentation overlay (toggled by ? button)
    if (m_docOverlayVisible)
        drawDocOverlay(painter);
}

// ========================================================================
// ProfileWidget
// ========================================================================

ProfileWidget::ProfileWidget(QWidget *parent) : QWidget(parent)
{
    m_chart = new QChart();
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->legend()->hide();
    m_chart->setMargins(QMargins(0, 0, 0, 0));
    m_chart->setBackgroundBrush(QBrush(QColor(30, 30, 30)));

    // Profile series (green)
    m_profileSeries = new QLineSeries();
    QPen pen(QColor(0, 200, 100));
    pen.setWidth(2);
    m_profileSeries->setPen(pen);
    m_chart->addSeries(m_profileSeries);

    // Fit line series – ROI 1 (blue) and ROI 2 (orange), drawn on top
    m_fitSeries1 = new QLineSeries();
    QPen fitPen1(QColor(0, 180, 255));
    fitPen1.setWidth(3);
    fitPen1.setStyle(Qt::SolidLine);
    m_fitSeries1->setPen(fitPen1);
    m_chart->addSeries(m_fitSeries1);

    m_fitSeries2 = new QLineSeries();
    QPen fitPen2(QColor(255, 140, 0));
    fitPen2.setWidth(3);
    fitPen2.setStyle(Qt::SolidLine);
    m_fitSeries2->setPen(fitPen2);
    m_chart->addSeries(m_fitSeries2);

    m_axisX = new QValueAxis();
    m_axisX->setTitleText("X [mm]");
    m_axisX->setLabelFormat("%.1f");
    m_axisX->setGridLineColor(QColor(70, 70, 70));
    m_axisX->setTitleBrush(QBrush(Qt::white));
    m_axisX->setLabelsBrush(QBrush(Qt::white));
    m_axisX->setRange(0, 150);

    m_axisZ = new QValueAxis();
    m_axisZ->setTitleText("Z [mm]");
    m_axisZ->setLabelFormat("%.1f");
    m_axisZ->setGridLineColor(QColor(70, 70, 70));
    m_axisZ->setTitleBrush(QBrush(Qt::white));
    m_axisZ->setLabelsBrush(QBrush(Qt::white));
    m_axisZ->setRange(0, 50);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisZ, Qt::AlignLeft);
    m_profileSeries->attachAxis(m_axisX);
    m_profileSeries->attachAxis(m_axisZ);
    m_fitSeries1->attachAxis(m_axisX);
    m_fitSeries1->attachAxis(m_axisZ);
    m_fitSeries2->attachAxis(m_axisX);
    m_fitSeries2->attachAxis(m_axisZ);

    m_chartView = new ProfileChartView(m_chart, this);
    m_chartView->setMinimumHeight(400);

    connect(m_chartView, &ProfileChartView::roiChanged,
            this,        &ProfileWidget::roiChanged);
    connect(m_chartView, &ProfileChartView::resetZoomRequested,
            this,        &ProfileWidget::resetZoom);

    // ── Button toolbar at the bottom of the chart ────────────────────────
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 2, 4, 2);
    toolbar->setSpacing(6);

    QPushButton *btnDoc = new QPushButton("?  Hilfe");
    btnDoc->setToolTip("Dokumentation und Steuerungshinweise einblenden");
    btnDoc->setStyleSheet(
        "QPushButton { background:#1a1a3a; color:#aac8ff; "
        "border:1px solid #334; border-radius:4px; padding:3px 10px; font-size:11px; }"
        "QPushButton:checked { background:#003366; color:#00ccff; border:1px solid #00aaff; }"
        "QPushButton:hover   { background:#1e2a50; }");
    btnDoc->setCheckable(true);
    connect(btnDoc, &QPushButton::toggled, this, &ProfileWidget::onToggleDocOverlay);

    // Color legend
    QLabel *lbl1 = new QLabel("▬ ROI 1");
    lbl1->setStyleSheet("color:#00b4ff; font-size:11px; font-weight:bold;");
    QLabel *lbl2 = new QLabel("▬ ROI 2");
    lbl2->setStyleSheet("color:#ff8c00; font-size:11px; font-weight:bold;");
    QLabel *lblHeat = new QLabel("■ Heatmap: Residuen");
    lblHeat->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
        "stop:0 #00c800, stop:0.5 #c8c800, stop:1 #c80000); "
        "color:white; font-size:10px; padding:1px 6px; border-radius:3px;");

    toolbar->addWidget(lbl1);
    toolbar->addWidget(lbl2);
    toolbar->addSpacing(8);
    toolbar->addWidget(lblHeat);
    toolbar->addStretch();
    toolbar->addWidget(btnDoc);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_chartView);
    layout->addLayout(toolbar);
    setLayout(layout);
}

void ProfileWidget::updateProfile(const std::vector<ProfilePoint> &points)
{
    if (points.empty()) { clearProfile(); return; }

    // Compute data bounds
    float minX =  std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minZ =  std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();

    QList<QPointF> pts;
    pts.reserve(static_cast<int>(points.size()));
    for (auto &p : points) {
        pts.append(QPointF(p.x_mm, p.z_mm));
        minX = std::min(minX, p.x_mm);
        maxX = std::max(maxX, p.x_mm);
        minZ = std::min(minZ, p.z_mm);
        maxZ = std::max(maxZ, p.z_mm);
    }

    // Use replace() – but guard against Qt Charts not painting:
    // always call setRange afterwards so the chart marks itself dirty.
    m_profileSeries->replace(pts);

    float marginX = (maxX - minX) * 0.05f + 1.0f;
    float marginZ = (maxZ - minZ) * 0.10f + 1.0f;

    if (m_autoScale || m_firstFrame) {
        // Fit axes to actual data – fixes invisible profile when
        // data range (e.g. Z: 108..251 mm) differs from initial defaults.
        m_axisX->setRange(minX - marginX, maxX + marginX);
        m_axisZ->setRange(minZ - marginZ, maxZ + marginZ);
        m_firstFrame = false;
    } else {
        // Force Qt Charts to repaint even though axes did not change
        double xMin = m_axisX->min(), xMax = m_axisX->max();
        m_axisX->setRange(xMin, xMax + 1e-9);  // tiny nudge
        m_axisX->setRange(xMin, xMax);
    }
    m_chartView->viewport()->update();
}

void ProfileWidget::clearProfile()
{
    m_profileSeries->clear();
    m_fitSeries1->clear();
    m_fitSeries2->clear();
    m_firstFrame = true;  // next data will re-fit axes
}

void ProfileWidget::setRoi(int roiId, const RoiRect &r)
{
    if (roiId == 0 || roiId == 1)
        m_chartView->setRoi(static_cast<ProfileChartView::RoiId>(roiId), r);
}

RoiRect ProfileWidget::roi(int id) const
{
    if (id == 0 || id == 1)
        return m_chartView->roi(static_cast<ProfileChartView::RoiId>(id));
    return {};
}

void ProfileWidget::onDrawRoi1()
{
    m_chartView->setDrawingRoi(ProfileChartView::ROI_1);
}

void ProfileWidget::onDrawRoi2()
{
    m_chartView->setDrawingRoi(ProfileChartView::ROI_2);
}

void ProfileWidget::resetZoom()
{
    // Re-fit axes to the current series data
    const auto &pts = m_profileSeries->points();
    if (pts.isEmpty()) return;

    double minX = pts[0].x(), maxX = pts[0].x();
    double minZ = pts[0].y(), maxZ = pts[0].y();
    for (const auto &p : pts) {
        minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
        minZ = std::min(minZ, p.y()); maxZ = std::max(maxZ, p.y());
    }
    double mX = (maxX - minX) * 0.05 + 1.0;
    double mZ = (maxZ - minZ) * 0.10 + 1.0;
    m_axisX->setRange(minX - mX, maxX + mX);
    m_axisZ->setRange(minZ - mZ, maxZ + mZ);
    m_chartView->viewport()->update();
}

void ProfileWidget::updateFitLines(
        const FitLine &line1, const FitLine &line2,
        const std::vector<std::pair<double,double>> &residuals1,
        const std::vector<std::pair<double,double>> &residuals2)
{
    // Update fit-line Qt series (drawn by Qt Charts engine)
    auto fillLine = [](QLineSeries *s, const FitLine &fl) {
        s->clear();
        if (!fl.valid) return;
        s->append(fl.xMin, fl.slope * fl.xMin + fl.intercept);
        s->append(fl.xMax, fl.slope * fl.xMax + fl.intercept);
    };
    fillLine(m_fitSeries1, line1);
    fillLine(m_fitSeries2, line2);

    m_fitLine1 = line1;
    m_fitLine2 = line2;
    m_residuals1 = residuals1;
    m_residuals2 = residuals2;

    // Forward heatmap data to the chart view for per-point paintEvent rendering
    m_chartView->setHeatmapData(residuals1, residuals2, line1, line2);

    m_chartView->viewport()->update();
}

void ProfileWidget::setFitLabels(const QString &m1Label, const QString &m2Label,
                                  double bendingAngleDeg)
{
    m_chartView->setFitLabels(m1Label, m2Label, bendingAngleDeg);
}

void ProfileWidget::setAngleQuadrant(AngleQuadrant q)
{
    m_chartView->setAngleQuadrant(q);
}

void ProfileWidget::onToggleDocOverlay()
{
    // Called by the checkable ? button – just relay current state to the chart view
    // We look up the sender's checked state to avoid storing a member pointer
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    bool checked = btn ? btn->isChecked() : false;
    m_chartView->setDocOverlayVisible(checked);
}

void ProfileWidget::updateProductResult(const QString &/*resultText*/)
{
    // Handled in MainWindow overlay
}
