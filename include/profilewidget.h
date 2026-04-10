/****************************************************************************
** profilewidget.h
** Interactive 2D profile display with ROI drawing via mouse drag
****************************************************************************/
#pragma once

#include <QWidget>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QScatterSeries>
#include <QValueAxis>
#include <QRubberBand>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QRectF>
#include <QString>
#include <vector>
#include <utility>
#include <cmath>
#include "types.h"  // ProfilePoint, RoiRect, FitLine (no Windows headers)

// RoiRect is defined in types.h

class ProfileChartView : public QChartView
{
    Q_OBJECT
public:
    explicit ProfileChartView(QChart *chart, QWidget *parent = nullptr);

    // ROI editing mode
    enum RoiId { ROI_NONE = -1, ROI_1 = 0, ROI_2 = 1 };
    void setDrawingRoi(RoiId id);
    RoiId drawingRoi() const { return m_drawingRoi; }

    void setRoi(RoiId id, const RoiRect &r);
    RoiRect roi(RoiId id) const { return m_rois[id]; }

    // Heatmap: per-point (x_mm, |residual_mm|) + max residual per ROI
    void setHeatmapData(const std::vector<std::pair<double,double>> &res1,
                        const std::vector<std::pair<double,double>> &res2,
                        const FitLine &fl1, const FitLine &fl2);

    // Overlay text labels shown on the fit lines and in the info panel
    // methodLabel: "OLS" / "RANSAC" / "Hough" / "Auto→Hough" etc.
    void setFitLabels(const QString &m1Label, const QString &m2Label,
                      double bendingAngleDeg);

    // Toggle documentation overlay on/off
    void setDocOverlayVisible(bool v);

    // Which quadrant angle to display at the intersection
    void setAngleQuadrant(AngleQuadrant q);
    AngleQuadrant angleQuadrant() const { return m_angleQuadrant; }

signals:
    void roiChanged(int roiId, RoiRect r);
    void resetZoomRequested();

protected:
    void mousePressEvent(QMouseEvent *e)   override;
    void mouseMoveEvent(QMouseEvent *e)    override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e)              override;
    void mouseDoubleClickEvent(QMouseEvent *e)   override;
    void paintEvent(QPaintEvent *e)              override;

private:
    QPoint  chartToWidget(double x, double z) const;
    QPointF widgetToChart(const QPoint &p)    const;
    void    drawRoiOverlay(QPainter &painter, RoiId id, QColor color);
    void    drawHeatmap(QPainter &painter,
                        const std::vector<std::pair<double,double>> &residuals,
                        const FitLine &fl);
    void    drawFitLineLabels(QPainter &painter);
    void    drawInfoPanel(QPainter &painter);
    void    drawDocOverlay(QPainter &painter);

    RoiId   m_drawingRoi = ROI_NONE;

    // ROI rubber-band
    bool    m_dragging   = false;
    QPoint  m_dragStart;
    QPoint  m_dragCurrent;
    RoiRect m_rois[2];

    // Pan state (right mouse button)
    bool    m_panning    = false;
    QPoint  m_panStart;
    double  m_panX0min = 0, m_panX0max = 0, m_panZ0min = 0, m_panZ0max = 0;

    // Heatmap data (painted in paintEvent per-point)
    std::vector<std::pair<double,double>> m_hmRes1, m_hmRes2;
    FitLine  m_hmLine1, m_hmLine2;

    // Fit-line overlay labels
    QString  m_methodLabel1, m_methodLabel2;
    double   m_bendingAngle = 0.0;
    bool     m_hasBendingAngle = false;

    // Documentation overlay
    bool          m_docOverlayVisible = false;

    // Which quadrant to measure
    AngleQuadrant m_angleQuadrant     = AngleQuadrant::TopLeft;

    // Helper: draw angle arc at intersection
    void    drawAngleArc(QPainter &painter);
    void    drawWatermark(QPainter &painter);
};

// -----------------------------------------------------------------------

class ProfileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileWidget(QWidget *parent = nullptr);

    void updateProfile(const std::vector<ProfilePoint> &points);
    void updateProductResult(const QString &resultText);
    // residuals1/2: per-point (x, residual_mm) pairs for heatmap
    void updateFitLines(const FitLine &line1, const FitLine &line2,
                        const std::vector<std::pair<double,double>> &residuals1,
                        const std::vector<std::pair<double,double>> &residuals2);
    // Pass method labels + bending angle for overlay display
    void setFitLabels(const QString &m1Label, const QString &m2Label,
                      double bendingAngleDeg);
    void setAngleQuadrant(AngleQuadrant q);
    void clearProfile();

    void setRoi(int roiId, const RoiRect &r);
    RoiRect roi(int id) const;

public slots:
    void onDrawRoi1();
    void onDrawRoi2();
    void resetZoom();   // fit axes to current series data
    void onToggleDocOverlay();

signals:
    void roiChanged(int roiId, RoiRect r);

private:
    ProfileChartView *m_chartView;
    QChart           *m_chart;
    QLineSeries      *m_profileSeries;
    QLineSeries      *m_fitSeries1;    // ROI 1 fit line (blue)
    QLineSeries      *m_fitSeries2;    // ROI 2 fit line (orange)
    // Heatmap data stored for per-point painting in ProfileChartView::paintEvent
    std::vector<std::pair<double,double>> m_residuals1;  // (x_mm, |residual|)
    std::vector<std::pair<double,double>> m_residuals2;
    FitLine  m_fitLine1;
    FitLine  m_fitLine2;
    QValueAxis       *m_axisX;
    QValueAxis       *m_axisZ;
    bool              m_autoScale = true;
    bool              m_firstFrame = true;  // fit axes on very first data frame
};
