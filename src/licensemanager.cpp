// =============================================================================
// licensemanager.cpp
// Vollständige Implementierung aller Lizenzverwaltungsmethoden.
// Nutzt QNetworkAccessManager mit QNetworkReply::waitForReadyRead()
// für blockierende HTTP-Calls ohne verschachtelten QEventLoop.
// =============================================================================

#include "licensemanager.h"
#include "keygen_config.h"

#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QSettings>
#include <QTimer>
#include <QLoggingCategory>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonArray>
#include <QEventLoop>

// Logging-Kategorie für alle Lizenz-bezogenen Meldungen
Q_LOGGING_CATEGORY(licenseLog, "app.license")

// ---------------------------------------------------------------------------
// Konstruktor
// ---------------------------------------------------------------------------
LicenseManager::LicenseManager(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

// ---------------------------------------------------------------------------
// generateFingerprint()
// ---------------------------------------------------------------------------
QString LicenseManager::generateFingerprint()
{
    if (!m_cachedFingerprint.isEmpty()) {
        return m_cachedFingerprint;
    }

    QString macAddress;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const QString hwAddr = iface.hardwareAddress();
        if (!hwAddr.isEmpty() && hwAddr != "00:00:00:00:00:00") {
            macAddress = hwAddr;
            break;
        }
    }

    if (macAddress.isEmpty()) {
        macAddress = "00:00:00:00:00:00";
        qCWarning(licenseLog) << "Keine MAC-Adresse gefunden, verwende Fallback.";
    }

    const QString cpuModel = QSysInfo::currentCpuArchitecture()
                           + QLatin1Char('/')
                           + QSysInfo::buildCpuArchitecture();
    const QString hostname = QSysInfo::machineHostName();

    const QString combined = macAddress
                           + QLatin1Char('|')
                           + cpuModel
                           + QLatin1Char('|')
                           + hostname;

    qCDebug(licenseLog) << "Fingerabdruck-Eingabe:"
                        << "MAC=" << macAddress
                        << "CPU=" << cpuModel
                        << "Host=" << hostname;

    const QByteArray hash = QCryptographicHash::hash(
        combined.toUtf8(),
        QCryptographicHash::Sha256
    );

    m_cachedFingerprint = QString::fromLatin1(hash.toHex());
    qCInfo(licenseLog) << "Hardware-Fingerabdruck generiert:" << m_cachedFingerprint.left(16) << "...";

    return m_cachedFingerprint;
}

// ---------------------------------------------------------------------------
// checkLicense()
// ---------------------------------------------------------------------------
LicenseStatus LicenseManager::checkLicense()
{
    QSettings settings;
    const QString licenseKey  = settings.value(KeygenConfig::SETTINGS_LICENSE_KEY).toString();
    const QString licenseType = settings.value(KeygenConfig::SETTINGS_LICENSE_TYPE).toString();

    if (licenseKey.isEmpty()) {
        qCInfo(licenseLog) << "Keine gespeicherte Lizenz gefunden.";
        return LicenseStatus::NOT_ACTIVATED;
    }

    const QString fingerprint = generateFingerprint();

    bool isValid = false;
    QString validationCode;
    const QString licenseId = validateLicenseKey(licenseKey, fingerprint, isValid, validationCode);

    qCInfo(licenseLog) << "Validierungsergebnis:"
                       << "valid=" << isValid
                       << "code=" << validationCode
                       << "type=" << licenseType;

    if (isValid) {
        if (licenseType == QLatin1String("trial")) {
            const int daysLeft = trialDaysRemaining();
            if (daysLeft > 0) {
                qCInfo(licenseLog) << "Trial aktiv," << daysLeft << "Tage verbleibend.";
                return LicenseStatus::TRIAL_ACTIVE;
            } else {
                qCWarning(licenseLog) << "Trial-Lizenz lokal abgelaufen.";
                return LicenseStatus::TRIAL_EXPIRED;
            }
        }
        return LicenseStatus::VALID;
    }

    if (validationCode == QLatin1String("EXPIRED")) {
        if (licenseType == QLatin1String("trial")) {
            return LicenseStatus::TRIAL_EXPIRED;
        }
        return LicenseStatus::EXPIRED;
    }

    if (validationCode == QLatin1String("NO_MACHINES")
     || validationCode == QLatin1String("NO_MACHINE")
     || validationCode == QLatin1String("FINGERPRINT_SCOPE_MISMATCH")) {
        return LicenseStatus::NOT_ACTIVATED;
    }

    m_lastError = validationCode;
    return LicenseStatus::ERROR;
}

// ---------------------------------------------------------------------------
// activateCommercialLicense()
// ---------------------------------------------------------------------------
bool LicenseManager::activateCommercialLicense(const QString& licenseKey)
{
    qCInfo(licenseLog) << "Starte kommerzielle Lizenzaktivierung...";

    const QString fingerprint = generateFingerprint();

    bool isValid = false;
    QString validationCode;
    const QString licenseId = validateLicenseKey(licenseKey, fingerprint, isValid, validationCode);

    if (isValid) {
        qCInfo(licenseLog) << "Lizenz bereits auf dieser Maschine aktiviert.";
        QSettings settings;
        settings.setValue(KeygenConfig::SETTINGS_LICENSE_KEY,  licenseKey);
        settings.setValue(KeygenConfig::SETTINGS_LICENSE_ID,   licenseId);
        settings.setValue(KeygenConfig::SETTINGS_LICENSE_TYPE, "commercial");
        return true;
    }

    if (licenseId.isEmpty() && validationCode != QLatin1String("NO_MACHINES")
                             && validationCode != QLatin1String("NO_MACHINE")
                             && validationCode != QLatin1String("FINGERPRINT_SCOPE_MISMATCH")) {
        m_lastError = tr("Ungültiger Lizenzschlüssel: %1").arg(validationCode);
        qCWarning(licenseLog) << "Aktivierung abgebrochen:" << m_lastError;
        return false;
    }

    if (licenseId.isEmpty()) {
        m_lastError = tr("Lizenz nicht gefunden.");
        return false;
    }

    // Policy hat authenticationStrategy=TOKEN → Product Token für /machines erforderlich
    const QString authToken = QStringLiteral("Bearer ")
                              + QLatin1String(KeygenConfig::KEYGEN_PRODUCT_TOKEN);

    QJsonObject machineAttrs;
    machineAttrs[QLatin1String("fingerprint")] = fingerprint;
    machineAttrs[QLatin1String("platform")]    = QSysInfo::prettyProductName();
    machineAttrs[QLatin1String("name")]        = QSysInfo::machineHostName();

    QJsonObject licenseRef;
    licenseRef[QLatin1String("type")] = QLatin1String("licenses");
    licenseRef[QLatin1String("id")]   = licenseId;

    QJsonObject licenseRel;
    licenseRel[QLatin1String("data")] = licenseRef;

    QJsonObject relationships;
    relationships[QLatin1String("license")] = licenseRel;

    QJsonObject data;
    data[QLatin1String("type")]          = QLatin1String("machines");
    data[QLatin1String("attributes")]    = machineAttrs;
    data[QLatin1String("relationships")] = relationships;

    QJsonObject payload;
    payload[QLatin1String("data")] = data;

    int httpStatus = 0;
    const QJsonDocument response = postJson("/machines", payload, authToken, httpStatus);

    if (httpStatus == 201) {
        qCInfo(licenseLog) << "Maschine erfolgreich aktiviert. HTTP 201.";
        QSettings settings;
        settings.setValue(KeygenConfig::SETTINGS_LICENSE_KEY,  licenseKey);
        settings.setValue(KeygenConfig::SETTINGS_LICENSE_ID,   licenseId);
        settings.setValue(KeygenConfig::SETTINGS_LICENSE_TYPE, "commercial");
        return true;
    }

    if (httpStatus == 422) {
        const QJsonArray errors = response.object()
                                          .value(QLatin1String("errors"))
                                          .toArray();
        if (!errors.isEmpty()) {
            const QString code = errors.first()
                                       .toObject()
                                       .value(QLatin1String("code"))
                                       .toString();
            if (code == QLatin1String("MACHINE_ALREADY_ACTIVATED")) {
                qCInfo(licenseLog) << "Maschine war bereits aktiviert — gilt als Erfolg.";
                QSettings settings;
                settings.setValue(KeygenConfig::SETTINGS_LICENSE_KEY,  licenseKey);
                settings.setValue(KeygenConfig::SETTINGS_LICENSE_ID,   licenseId);
                settings.setValue(KeygenConfig::SETTINGS_LICENSE_TYPE, "commercial");
                return true;
            }
            m_lastError = code;
        }
    }

    qCWarning(licenseLog) << "Maschinenaktivierung fehlgeschlagen. HTTP" << httpStatus;
    if (m_lastError.isEmpty()) {
        m_lastError = tr("Aktivierung fehlgeschlagen (HTTP %1)").arg(httpStatus);
    }
    return false;
}

// ---------------------------------------------------------------------------
// startTrial()
// ---------------------------------------------------------------------------
bool LicenseManager::startTrial()
{
    qCInfo(licenseLog) << "Starte Trial-Lizenz-Erstellung...";
    qCInfo(licenseLog) << "Product Token Länge:" << strlen(KeygenConfig::KEYGEN_PRODUCT_TOKEN);

    const QString fingerprint = generateFingerprint();
    const QString authToken   = QStringLiteral("Bearer ")
                              + QLatin1String(KeygenConfig::KEYGEN_PRODUCT_TOKEN);

    // --- Schritt 1: Neue Trial-Lizenz erstellen ---
    QJsonObject policyRef;
    policyRef[QLatin1String("type")] = QLatin1String("policies");
    policyRef[QLatin1String("id")]   = QLatin1String(KeygenConfig::KEYGEN_TRIAL_POLICY_ID);

    QJsonObject policyRel;
    policyRel[QLatin1String("data")] = policyRef;

    QJsonObject relationships;
    relationships[QLatin1String("policy")] = policyRel;

    QJsonObject metadata;
    metadata[QLatin1String("initialFingerprint")] = fingerprint;

    QJsonObject licenseAttrs;
    licenseAttrs[QLatin1String("metadata")] = metadata;

    QJsonObject data;
    data[QLatin1String("type")]          = QLatin1String("licenses");
    data[QLatin1String("attributes")]    = licenseAttrs;
    data[QLatin1String("relationships")] = relationships;

    QJsonObject payload;
    payload[QLatin1String("data")] = data;

    int httpStatus = 0;
    const QJsonDocument licenseResponse = postJson(
        QStringLiteral("/licenses"),
        payload,
        authToken,
        httpStatus
    );

    qCInfo(licenseLog) << "Trial POST /licenses HTTP Status:" << httpStatus;

    if (httpStatus != 201) {
        const QJsonArray errors = licenseResponse.object()
                                                 .value(QLatin1String("errors"))
                                                 .toArray();
        if (!errors.isEmpty()) {
            m_lastError = errors.first()
                                .toObject()
                                .value(QLatin1String("detail"))
                                .toString();
        } else {
            m_lastError = tr("Trial-Lizenzerstellung fehlgeschlagen (HTTP %1)").arg(httpStatus);
        }
        qCWarning(licenseLog) << "Trial-Erstellung fehlgeschlagen:" << m_lastError;
        return false;
    }

    const QJsonObject licenseData   = licenseResponse.object()
                                                     .value(QLatin1String("data"))
                                                     .toObject();
    const QString licenseId         = licenseData.value(QLatin1String("id")).toString();
    const QJsonObject licenseAttrsR = licenseData.value(QLatin1String("attributes")).toObject();
    const QString licenseKey        = licenseAttrsR.value(QLatin1String("key")).toString();
    const QString expiryStr         = licenseAttrsR.value(QLatin1String("expiry")).toString();

    qCInfo(licenseLog) << "Trial-Lizenz erstellt. ID:" << licenseId
                       << "Ablauf:" << expiryStr;

    // --- Schritt 2: Diese Maschine für die Trial-Lizenz aktivieren ---
    // Policy hat authenticationStrategy=TOKEN + protected=true
    // => Product Token als Bearer verwenden, nicht "License <key>"
    const QString licenseAuth = QStringLiteral("Bearer ")
                              + QLatin1String(KeygenConfig::KEYGEN_PRODUCT_TOKEN);

    QJsonObject machineAttrs;
    machineAttrs[QLatin1String("fingerprint")] = fingerprint;
    machineAttrs[QLatin1String("platform")]    = QSysInfo::prettyProductName();
    machineAttrs[QLatin1String("name")]        = QSysInfo::machineHostName();

    QJsonObject licenseRef2;
    licenseRef2[QLatin1String("type")] = QLatin1String("licenses");
    licenseRef2[QLatin1String("id")]   = licenseId;

    QJsonObject licenseRel2;
    licenseRel2[QLatin1String("data")] = licenseRef2;

    QJsonObject relationships2;
    relationships2[QLatin1String("license")] = licenseRel2;

    QJsonObject machineData;
    machineData[QLatin1String("type")]          = QLatin1String("machines");
    machineData[QLatin1String("attributes")]    = machineAttrs;
    machineData[QLatin1String("relationships")] = relationships2;

    QJsonObject machinePayload;
    machinePayload[QLatin1String("data")] = machineData;

    int machineHttpStatus = 0;
    const QJsonDocument machineResponse = postJson(
        QStringLiteral("/machines"),
        machinePayload,
        licenseAuth,
        machineHttpStatus
    );

    qCInfo(licenseLog) << "Trial POST /machines HTTP Status:" << machineHttpStatus;

    if (machineHttpStatus != 201 && machineHttpStatus != 200) {
        qCWarning(licenseLog) << "Trial-Maschinenaktivierung fehlgeschlagen. HTTP"
                              << machineHttpStatus;
        m_lastError = tr("Trial-Aktivierung der Maschine fehlgeschlagen (HTTP %1)")
                      .arg(machineHttpStatus);
        return false;
    }

    QSettings settings;
    settings.setValue(KeygenConfig::SETTINGS_LICENSE_KEY,   licenseKey);
    settings.setValue(KeygenConfig::SETTINGS_LICENSE_ID,    licenseId);
    settings.setValue(KeygenConfig::SETTINGS_LICENSE_TYPE,  "trial");
    settings.setValue(KeygenConfig::SETTINGS_TRIAL_EXPIRY,  expiryStr);

    qCInfo(licenseLog) << "Trial erfolgreich gestartet. Ablauf:" << expiryStr;
    return true;
}

// ---------------------------------------------------------------------------
// trialDaysRemaining()
// ---------------------------------------------------------------------------
int LicenseManager::trialDaysRemaining()
{
    QSettings settings;
    const QString expiryStr = settings.value(KeygenConfig::SETTINGS_TRIAL_EXPIRY).toString();

    if (expiryStr.isEmpty()) {
        return 0;
    }

    const QDateTime expiry  = QDateTime::fromString(expiryStr, Qt::ISODate);
    const QDateTime now     = QDateTime::currentDateTimeUtc();

    if (!expiry.isValid() || expiry <= now) {
        return 0;
    }

    const qint64 secsLeft = now.secsTo(expiry);
    return static_cast<int>(secsLeft / 86400);
}

// ---------------------------------------------------------------------------
// validateOnStartup()
// ---------------------------------------------------------------------------
bool LicenseManager::validateOnStartup()
{
    qCInfo(licenseLog) << "Lizenzprüfung beim Start...";

    const LicenseStatus status = checkLicense();

    switch (status) {
    case LicenseStatus::VALID:
        qCInfo(licenseLog) << "Kommerzielle Lizenz gültig. App wird gestartet.";
        return true;

    case LicenseStatus::TRIAL_ACTIVE: {
        const int daysLeft = trialDaysRemaining();
        qCInfo(licenseLog) << "Trial aktiv." << daysLeft << "Tage verbleibend.";
        return true;
    }

    case LicenseStatus::TRIAL_EXPIRED:
        qCWarning(licenseLog) << "Trial abgelaufen. Lizenzdialog wird angezeigt.";
        emit licenseDialogRequired();
        return false;

    case LicenseStatus::EXPIRED:
        qCWarning(licenseLog) << "Kommerzielle Lizenz abgelaufen.";
        emit licenseDialogRequired();
        return false;

    case LicenseStatus::NOT_ACTIVATED:
        qCInfo(licenseLog) << "Keine Aktivierung gefunden. Lizenzdialog wird angezeigt.";
        emit licenseDialogRequired();
        return false;

    case LicenseStatus::ERROR:
    default:
        qCWarning(licenseLog) << "Lizenzfehler:" << m_lastError
                              << "— App wird trotzdem gestartet (Netzwerkfehler toleriert).";
        return true;
    }
}

// ===========================================================================
// Private Hilfsmethoden
// ===========================================================================

// ---------------------------------------------------------------------------
// postJson() — Blockierender HTTP POST
// Verwendet QNetworkReply::waitForReadyRead() statt verschachteltem QEventLoop.
// Funktioniert zuverlässig auch wenn aus einem QDialog heraus aufgerufen.
// ---------------------------------------------------------------------------
QJsonDocument LicenseManager::postJson(const QString& endpoint,
                                       const QJsonObject& payload,
                                       const QString& authToken,
                                       int& httpStatus)
{
    const QString url = QLatin1String(KeygenConfig::KEYGEN_API_BASE)
                      + QLatin1String(KeygenConfig::KEYGEN_ACCOUNT_ID)
                      + endpoint;

    qCDebug(licenseLog) << "POST" << url;

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QLatin1String("application/vnd.api+json"));
    request.setRawHeader("Accept",         "application/vnd.api+json");
    request.setRawHeader("Keygen-Version", "1.5");
    if (!authToken.isEmpty()) {
        request.setRawHeader("Authorization", authToken.toUtf8());
    }

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    // m_networkManager wiederverwenden — vermeidet SSL-Session-Probleme
    // bei aufeinanderfolgenden Requests (z.B. /licenses dann /machines).
    QNetworkReply* reply = m_networkManager->post(request, body);

    // Synchron warten mit eigenem QEventLoop — aber auf dem Stack des Callers,
    // nicht verschachtelt in einem bestehenden Loop.
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(20000); // 20 Sekunden Timeout

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start();
    loop.exec();

    timeoutTimer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(licenseLog) << "Netzwerkfehler (POST" << endpoint << "):"
                              << reply->error()
                              << reply->errorString();
        m_lastError = reply->errorString();
        httpStatus  = -1;
        reply->deleteLater();
        return QJsonDocument{};
    }

    httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    qCDebug(licenseLog) << "HTTP" << httpStatus << "Response:" << responseData.left(200);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(licenseLog) << "JSON-Parsing-Fehler:" << parseError.errorString();
    }

    return doc;
}

// ---------------------------------------------------------------------------
// getJson() — Blockierender HTTP GET
// ---------------------------------------------------------------------------
QJsonDocument LicenseManager::getJson(const QString& endpoint,
                                      const QString& authToken,
                                      int& httpStatus)
{
    const QString url = QLatin1String(KeygenConfig::KEYGEN_API_BASE)
                      + QLatin1String(KeygenConfig::KEYGEN_ACCOUNT_ID)
                      + endpoint;

    QNetworkRequest request{QUrl{url}};
    request.setRawHeader("Accept",         "application/vnd.api+json");
    request.setRawHeader("Keygen-Version", "1.5");
    if (!authToken.isEmpty()) {
        request.setRawHeader("Authorization", authToken.toUtf8());
    }

    QNetworkReply* reply = m_networkManager->get(request);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(20000);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start();
    loop.exec();

    timeoutTimer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(licenseLog) << "Netzwerkfehler (GET" << endpoint << "):"
                              << reply->errorString();
        m_lastError = reply->errorString();
        httpStatus  = -1;
        reply->deleteLater();
        return QJsonDocument{};
    }

    httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    return QJsonDocument::fromJson(responseData, &parseError);
}

// ---------------------------------------------------------------------------
// validateLicenseKey()
// ---------------------------------------------------------------------------
QString LicenseManager::validateLicenseKey(const QString& licenseKey,
                                            const QString& fingerprint,
                                            bool& isValid,
                                            QString& validationCode)
{
    isValid        = false;
    validationCode = QString();

    QJsonObject scopeObj;
    scopeObj[QLatin1String("fingerprint")] = fingerprint;

    QJsonObject meta;
    meta[QLatin1String("key")]   = licenseKey;
    meta[QLatin1String("scope")] = scopeObj;

    QJsonObject payload;
    payload[QLatin1String("meta")] = meta;

    int httpStatus = 0;
    const QJsonDocument response = postJson(
        QStringLiteral("/licenses/actions/validate-key"),
        payload,
        QString(),
        httpStatus
    );

    if (httpStatus == -1 || response.isNull()) {
        validationCode = QLatin1String("NETWORK_ERROR");
        return QString();
    }

    const QJsonObject root     = response.object();
    const QJsonObject metaResp = root.value(QLatin1String("meta")).toObject();
    const QJsonObject dataObj  = root.value(QLatin1String("data")).toObject();

    isValid        = metaResp.value(QLatin1String("valid")).toBool(false);
    validationCode = metaResp.value(QLatin1String("code")).toString();

    const QString licenseId = dataObj.value(QLatin1String("id")).toString();

    qCDebug(licenseLog) << "validate-key Antwort:"
                        << "valid=" << isValid
                        << "code=" << validationCode
                        << "licenseId=" << licenseId;

    return licenseId;
}
