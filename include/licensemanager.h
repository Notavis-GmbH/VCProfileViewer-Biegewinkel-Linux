#pragma once

// =============================================================================
// licensemanager.h
// Verwaltet die gesamte Lizenzlogik: Fingerabdruck, Validierung, Aktivierung,
// Trial-Start. Alle Netzwerkaufrufe erfolgen synchron über einen lokalen
// QEventLoop (geeignet für den App-Start; für UI-Threads ggf. async umstellen).
// =============================================================================

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ---------------------------------------------------------------------------
// Mögliche Lizenzzustände der Anwendung
// ---------------------------------------------------------------------------
enum class LicenseStatus {
    VALID,           // Kommerzielle Lizenz aktiv und gültig
    EXPIRED,         // Lizenz abgelaufen (kommerzielle oder Trial)
    TRIAL_ACTIVE,    // Testlizenz aktiv, Tage verbleibend > 0
    TRIAL_EXPIRED,   // Testlizenz abgelaufen
    NOT_ACTIVATED,   // Lizenz vorhanden, aber diese Maschine noch nicht aktiviert
    ERROR            // Netzwerkfehler oder unbekannter Fehler
};

class LicenseManager : public QObject
{
    Q_OBJECT

public:
    explicit LicenseManager(QObject* parent = nullptr);

    // -----------------------------------------------------------------------
    // Hardware-Fingerabdruck: MAC-Adresse + CPU-Modell + Hostname → SHA-256
    // -----------------------------------------------------------------------
    QString generateFingerprint();

    // -----------------------------------------------------------------------
    // Prüft die gespeicherte Lizenz gegen Keygen.sh und gibt den Status zurück
    // -----------------------------------------------------------------------
    LicenseStatus checkLicense();

    // -----------------------------------------------------------------------
    // Aktiviert eine kommerzielle Lizenz auf dieser Maschine
    // POST /licenses/actions/validate-key → Lizenz-ID holen
    // POST /machines → Maschine registrieren
    // -----------------------------------------------------------------------
    bool activateCommercialLicense(const QString& licenseKey);

    // -----------------------------------------------------------------------
    // Erstellt eine neue Trial-Lizenz und aktiviert sofort diese Maschine
    // POST /licenses (mit Trial-Policy)
    // POST /machines
    // -----------------------------------------------------------------------
    bool startTrial();

    // -----------------------------------------------------------------------
    // Gibt die verbleibenden Testtage zurück (0 wenn abgelaufen oder kein Trial)
    // -----------------------------------------------------------------------
    int trialDaysRemaining();

    // -----------------------------------------------------------------------
    // Wird beim App-Start aufgerufen; behandelt alle Lizenzzustände
    // Gibt true zurück wenn die App gestartet werden darf
    // -----------------------------------------------------------------------
    bool validateOnStartup();

    // Getter für die zuletzt abgerufene Lizenznachricht (für UI-Anzeige)
    QString lastErrorMessage() const { return m_lastError; }

signals:
    // Wird ausgesendet wenn validateOnStartup() den LicenseDialog benötigt
    void licenseDialogRequired();

private:
    // -----------------------------------------------------------------------
    // Hilfsmethode: Synchroner HTTP POST mit JSON-Body
    // Gibt QJsonDocument zurück, httpStatus wird per Referenz befüllt
    // -----------------------------------------------------------------------
    QJsonDocument postJson(const QString& endpoint,
                           const QJsonObject& payload,
                           const QString& authToken,
                           int& httpStatus);

    // -----------------------------------------------------------------------
    // Hilfsmethode: Synchroner HTTP GET
    // -----------------------------------------------------------------------
    QJsonDocument getJson(const QString& endpoint,
                          const QString& authToken,
                          int& httpStatus);

    // -----------------------------------------------------------------------
    // Lizenz-Key über validate-key-Endpoint validieren und Lizenz-ID ermitteln
    // meta.valid = true/false, gibt Lizenz-ID zurück oder leeren String bei Fehler
    // -----------------------------------------------------------------------
    QString validateLicenseKey(const QString& licenseKey,
                               const QString& fingerprint,
                               bool& isValid,
                               QString& validationCode);

    QNetworkAccessManager* m_networkManager;
    QString                m_lastError;
    QString                m_cachedFingerprint; // Zwischenspeicher für Fingerabdruck
};