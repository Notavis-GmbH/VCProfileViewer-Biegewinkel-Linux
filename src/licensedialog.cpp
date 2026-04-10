// =============================================================================
// licensedialog.cpp
// Qt-Dialog für Lizenzaktivierung. Unterstützt kommerzielle Aktivierung
// und Trial-Start mit Fortschrittsanzeige und aussagekräftigen Fehlermeldungen.
// =============================================================================

#include "licensedialog.h"
#include <QMessageBox>
#include <QApplication>
#include <QFont>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QFrame>

// ---------------------------------------------------------------------------
// Konstruktor
// ---------------------------------------------------------------------------
LicenseDialog::LicenseDialog(LicenseManager* manager, QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(tr("Lizenzaktivierung"));
    setMinimumWidth(480);
    setModal(true);
    // Schließen-Button in der Titelleiste deaktivieren
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    setupUi();
}

// ---------------------------------------------------------------------------
// setupUi() — Layout und Widgets erstellen
// ---------------------------------------------------------------------------
void LicenseDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(32, 32, 32, 32);

    // --- Titel ---
    m_titleLabel = new QLabel(tr("Willkommen!"), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);

    // --- Untertitel ---
    m_subtitleLabel = new QLabel(
        tr("Bitte gib deinen Lizenzschlüssel ein oder starte eine 69-tägige Testversion."),
        this
    );
    m_subtitleLabel->setWordWrap(true);
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_subtitleLabel);

    // --- Trennlinie ---
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator);

    // --- Lizenzschlüssel-Eingabe ---
    auto* inputLabel = new QLabel(tr("Lizenzschlüssel:"), this);
    mainLayout->addWidget(inputLabel);

    m_licenseKeyEdit = new QLineEdit(this);
    m_licenseKeyEdit->setPlaceholderText(tr("XXXXXX-XXXXXX-XXXXXX-XXXXXX-XXXXXX-XX"));
    m_licenseKeyEdit->setMinimumHeight(36);
    // Eingabe-Validierung: nur erlaubte Zeichen zulassen
    m_licenseKeyEdit->setInputMethodHints(Qt::ImhLatinOnly);
    mainLayout->addWidget(m_licenseKeyEdit);

    // --- Aktivieren-Button ---
    m_activateButton = new QPushButton(tr("Lizenz aktivieren"), this);
    m_activateButton->setMinimumHeight(40);
    m_activateButton->setDefault(true);
    // Stil für den Primär-Button
    m_activateButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #0078d4;"
        "  color: white;"
        "  border-radius: 4px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #106ebe; }"
        "QPushButton:pressed { background-color: #005a9e; }"
        "QPushButton:disabled { background-color: #cccccc; }"
    );
    mainLayout->addWidget(m_activateButton);

    // --- Oder-Trenner ---
    auto* orLayout = new QHBoxLayout();
    auto* leftLine  = new QFrame(this);
    auto* rightLine = new QFrame(this);
    leftLine->setFrameShape(QFrame::HLine);
    rightLine->setFrameShape(QFrame::HLine);
    auto* orLabel = new QLabel(tr("  oder  "), this);
    orLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    orLayout->addWidget(leftLine);
    orLayout->addWidget(orLabel);
    orLayout->addWidget(rightLine);
    mainLayout->addLayout(orLayout);

    // --- Trial-Button ---
    m_trialButton = new QPushButton(tr("69-Tage-Testversion starten"), this);
    m_trialButton->setMinimumHeight(40);
    m_trialButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #f3f3f3;"
        "  color: #333333;"
        "  border: 1px solid #cccccc;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #e8e8e8; }"
        "QPushButton:pressed { background-color: #d0d0d0; }"
        "QPushButton:disabled { color: #aaaaaa; }"
    );
    mainLayout->addWidget(m_trialButton);

    // --- Fortschrittsbalken (versteckt während Inaktivität) ---
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0); // Unbestimmter Fortschritt
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumHeight(6);
    m_progressBar->setTextVisible(false);
    mainLayout->addWidget(m_progressBar);

    // --- Statusmeldung ---
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setVisible(false);
    mainLayout->addWidget(m_statusLabel);

    // --- Verbindungen ---
    connect(m_activateButton, &QPushButton::clicked,
            this, &LicenseDialog::onActivateClicked);
    connect(m_trialButton, &QPushButton::clicked,
            this, &LicenseDialog::onStartTrialClicked);
    // Enter-Taste im Eingabefeld → Aktivierung auslösen
    connect(m_licenseKeyEdit, &QLineEdit::returnPressed,
            m_activateButton, &QPushButton::click);
}

// ---------------------------------------------------------------------------
// onActivateClicked()
// ---------------------------------------------------------------------------
void LicenseDialog::onActivateClicked()
{
    const QString key = m_licenseKeyEdit->text().trimmed();
    if (key.isEmpty()) {
        showError(tr("Bitte gib einen Lizenzschlüssel ein."));
        return;
    }

    setUiBusy(true);
    showSuccess(tr("Lizenz wird aktiviert..."));
    QApplication::processEvents(); // UI aktualisieren

    const bool success = m_manager->activateCommercialLicense(key);

    setUiBusy(false);

    if (success) {
        showSuccess(tr("Lizenz erfolgreich aktiviert! Die Anwendung wird gestartet."));
        QApplication::processEvents();
        accept(); // Dialog schließen und App starten
    } else {
        showError(tr("Aktivierung fehlgeschlagen: %1")
                  .arg(m_manager->lastErrorMessage()));
    }
}

// ---------------------------------------------------------------------------
// onStartTrialClicked()
// ---------------------------------------------------------------------------
void LicenseDialog::onStartTrialClicked()
{
    // Bestätigung einholen
    const auto result = QMessageBox::question(
        this,
        tr("Testversion starten"),
        tr("Möchtest du eine kostenlose 69-Tage-Testversion starten?\n\n"
           "Hinweis: Die Testversion ist an diese Hardware gebunden "
           "und kann nicht auf anderen Geräten verwendet werden."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes
    );

    if (result != QMessageBox::Yes) {
        return;
    }

    setUiBusy(true);
    showSuccess(tr("Testversion wird eingerichtet..."));
    QApplication::processEvents();

    const bool success = m_manager->startTrial();

    setUiBusy(false);

    if (success) {
        const int daysLeft = m_manager->trialDaysRemaining();
        showSuccess(tr("Testversion gestartet! %1 Tage verbleibend. Die Anwendung wird gestartet.")
                    .arg(daysLeft));
        QApplication::processEvents();
        accept();
    } else {
        showError(tr("Testversion konnte nicht gestartet werden: %1")
                  .arg(m_manager->lastErrorMessage()));
    }
}

// ---------------------------------------------------------------------------
// Hilfsmethoden für UI-Zustand
// ---------------------------------------------------------------------------

void LicenseDialog::setUiBusy(bool busy)
{
    m_activateButton->setEnabled(!busy);
    m_trialButton->setEnabled(!busy);
    m_licenseKeyEdit->setEnabled(!busy);
    m_progressBar->setVisible(busy);
}

void LicenseDialog::showError(const QString& message)
{
    m_statusLabel->setVisible(true);
    m_statusLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    m_statusLabel->setText(message);
}

void LicenseDialog::showSuccess(const QString& message)
{
    m_statusLabel->setVisible(true);
    m_statusLabel->setStyleSheet("color: #388e3c; font-weight: bold;");
    m_statusLabel->setText(message);
}

QString LicenseDialog::enteredLicenseKey() const
{
    return m_licenseKeyEdit->text().trimmed();
}