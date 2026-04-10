#pragma once

// =============================================================================
// licensedialog.h
// Einfacher Qt-Dialog für die Lizenzaktivierung beim ersten Start.
// Zeigt ein Eingabefeld für den Lizenzschlüssel, einen "Aktivieren"-Button
// und einen "69-Tage-Trial starten"-Button.
// =============================================================================

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>

#include "licensemanager.h"

class LicenseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseDialog(LicenseManager* manager, QWidget* parent = nullptr);

    // Gibt den eingegebenen Lizenzschlüssel zurück (nach Accept)
    QString enteredLicenseKey() const;

private slots:
    // Wird aufgerufen wenn "Aktivieren" geklickt wird
    void onActivateClicked();

    // Wird aufgerufen wenn "Trial starten" geklickt wird
    void onStartTrialClicked();

private:
    void setupUi();
    void setUiBusy(bool busy);
    void showError(const QString& message);
    void showSuccess(const QString& message);

    LicenseManager* m_manager;

    // UI-Elemente
    QLabel*      m_titleLabel;
    QLabel*      m_subtitleLabel;
    QLineEdit*   m_licenseKeyEdit;
    QPushButton* m_activateButton;
    QPushButton* m_trialButton;
    QLabel*      m_statusLabel;
    QProgressBar* m_progressBar;
};