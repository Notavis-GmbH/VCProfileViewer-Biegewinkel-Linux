#pragma once

// Shared data types – no Windows headers included here
// so this file is safe to include from Qt widget headers

// Line-fitting algorithm selection (per ROI)
// Which of the 4 angles at the intersection of the two fit lines to display:
//   TopLeft=0  TopRight=1  BottomLeft=2  BottomRight=3
//   (relative to the intersection point, in chart coordinates)
enum class AngleQuadrant {
    TopLeft     = 0,   // Winkel oben-links    (Innenwinkel bei aufgebogenem Teil)
    TopRight    = 1,   // Winkel oben-rechts
    BottomLeft  = 2,   // Winkel unten-links
    BottomRight = 3,   // Winkel unten-rechts  (Aussenwinkel bei aufgebogenem Teil)
};

enum class FitMethod {
    OLS    = 0,  // Ordinary Least Squares  – fast, optimal for clean data
    RANSAC = 1,  // Random Sample Consensus – robust against outliers / gaps
    Hough  = 2,  // Hough Transform         – robust, handles fragmented lines
    Auto   = 3   // Automatic selection based on inlier ratio + RMS heuristic
};

// Result of the automatic method selection heuristic
struct AutoSelectInfo {
    FitMethod chosen    = FitMethod::OLS;
    double    olsRms    = 0.0;   // OLS  RMS residual [mm]
    double    ransacRms = 0.0;   // RANSAC RMS on ALL points [mm]
    double    houghRms  = 0.0;   // Hough  RMS on ALL points [mm]
    double    inlierRatioRansac = 0.0;  // fraction of points within RANSAC threshold
    double    inlierRatioHough  = 0.0;  // fraction of points within Hough threshold
    // Human-readable reason string (shown in tooltip / log)
    // e.g.  "RANSAC: inlier=0.72 < 0.85  (outliers detected)"
    char reason[128] = {};
};

struct ProfilePoint {
    float x_mm;
    float z_mm;
};

// Region of interest (in sensor world coordinates, mm)
struct RoiRect {
    double xMin = 0.0;
    double xMax = 0.0;
    double zMin = -9999.0;  // unused for line-fit ROI
    double zMax =  9999.0;
    bool valid = false;
};

// Result of a linear regression fit within one ROI
// Represents the line  z = slope * x + intercept
struct FitLine {
    double slope       = 0.0;
    double intercept   = 0.0;
    double xMin        = 0.0;   // draw range (= ROI x-bounds)
    double xMax        = 0.0;
    double phi         = 0.0;   // angle in degrees (atan(slope))
    double    rmsResidual = 0.0;   // root-mean-square residual [mm]
    double    maxResidual = 0.0;   // maximum absolute residual [mm]
    FitMethod    method         = FitMethod::OLS;
    AutoSelectInfo autoInfo;         // filled only when method==Auto was requested
    bool           valid          = false;
};
