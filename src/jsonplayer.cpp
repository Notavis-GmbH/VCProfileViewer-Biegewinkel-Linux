#include "jsonplayer.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <algorithm>

JsonPlayer::JsonPlayer(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(750);  // default ~1.3 Hz, matches recorded data
    connect(&m_timer, &QTimer::timeout, this, &JsonPlayer::onTimer);
}

JsonPlayer::~JsonPlayer()
{
    m_timer.stop();
}

bool JsonPlayer::loadFolder(const QString &folderPath)
{
    m_timer.stop();
    m_files.clear();
    m_currentIndex = 0;
    m_folderPath = folderPath;

    QDir dir(folderPath);
    if (!dir.exists()) {
        qWarning() << "JsonPlayer: Folder does not exist:" << folderPath;
        return false;
    }

    QStringList filters;
    filters << "*.json";
    QStringList entries = dir.entryList(filters, QDir::Files, QDir::Name);

    if (entries.isEmpty()) {
        qWarning() << "JsonPlayer: No JSON files found in" << folderPath;
        return false;
    }

    // Store absolute paths, sorted by name (timestamp-based filename ensures correct order)
    for (const QString &entry : entries) {
        m_files.append(dir.absoluteFilePath(entry));
    }

    qDebug() << "JsonPlayer: Loaded" << m_files.size() << "files from" << folderPath;
    emit folderLoaded(static_cast<int>(m_files.size()));
    emit frameChanged(0, static_cast<int>(m_files.size()));

    // Show first frame immediately
    if (!m_files.isEmpty()) {
        auto pts = loadFrame(0);
        if (!pts.empty()) {
            emit profileReady(pts);
        }
    }

    return true;
}

void JsonPlayer::play()
{
    if (m_files.isEmpty()) return;
    m_timer.start();
    emit playbackStarted();
}

void JsonPlayer::stop()
{
    m_timer.stop();
    emit playbackStopped();
}

void JsonPlayer::stepForward()
{
    if (m_files.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % static_cast<int>(m_files.size());
    auto pts = loadFrame(m_currentIndex);
    if (!pts.empty()) emit profileReady(pts);
    emit frameChanged(m_currentIndex, static_cast<int>(m_files.size()));
}

void JsonPlayer::stepBack()
{
    if (m_files.isEmpty()) return;
    m_currentIndex--;
    if (m_currentIndex < 0) m_currentIndex = static_cast<int>(m_files.size()) - 1;
    auto pts = loadFrame(m_currentIndex);
    if (!pts.empty()) emit profileReady(pts);
    emit frameChanged(m_currentIndex, static_cast<int>(m_files.size()));
}

void JsonPlayer::setFrame(int index)
{
    if (m_files.isEmpty()) return;
    if (index < 0 || index >= static_cast<int>(m_files.size())) return;
    m_currentIndex = index;
    auto pts = loadFrame(m_currentIndex);
    if (!pts.empty()) emit profileReady(pts);
    emit frameChanged(m_currentIndex, static_cast<int>(m_files.size()));
}

void JsonPlayer::setIntervalMs(int ms)
{
    m_timer.setInterval(ms);
}

void JsonPlayer::onTimer()
{
    if (m_files.isEmpty()) {
        m_timer.stop();
        return;
    }

    auto pts = loadFrame(m_currentIndex);
    if (!pts.empty()) emit profileReady(pts);
    emit frameChanged(m_currentIndex, static_cast<int>(m_files.size()));

    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_files.size())) {
        m_currentIndex = 0;  // loop
    }
}

std::vector<ProfilePoint> JsonPlayer::loadFrame(int index)
{
    if (index < 0 || index >= static_cast<int>(m_files.size()))
        return {};
    return parseJsonFile(m_files[index]);
}

std::vector<ProfilePoint> JsonPlayer::parseJsonFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "JsonPlayer: Cannot open file:" << filePath;
        return {};
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "JsonPlayer: JSON parse error in" << filePath << ":" << err.errorString();
        return {};
    }

    if (!doc.isArray()) {
        qWarning() << "JsonPlayer: Expected JSON array in" << filePath;
        return {};
    }

    QJsonArray arr = doc.array();
    std::vector<ProfilePoint> points;
    points.reserve(static_cast<size_t>(arr.size()));

    for (const QJsonValue &val : arr) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();
        ProfilePoint pt;
        pt.x_mm = static_cast<float>(obj.value("x").toDouble(0.0));
        pt.z_mm = static_cast<float>(obj.value("y").toDouble(0.0));  // JSON "y" = Z height
        points.push_back(pt);
    }

    return points;
}
