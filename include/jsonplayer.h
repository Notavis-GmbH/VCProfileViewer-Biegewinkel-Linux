#pragma once

#include <QObject>
#include <QTimer>
#include <QStringList>
#include <vector>
#include "types.h"  // ProfilePoint (no Windows headers)

class JsonPlayer : public QObject
{
    Q_OBJECT

public:
    explicit JsonPlayer(QObject *parent = nullptr);
    ~JsonPlayer();

    // Load all JSON files from a folder (sorted by filename = timestamp)
    bool loadFolder(const QString &folderPath);

    int  frameCount() const { return m_files.size(); }
    int  currentFrame() const { return m_currentIndex; }
    bool isPlaying() const { return m_timer.isActive(); }
    QString folderPath() const { return m_folderPath; }

public slots:
    void play();
    void stop();
    void stepForward();
    void stepBack();
    void setFrame(int index);
    void setIntervalMs(int ms);   // playback speed

signals:
    void profileReady(std::vector<ProfilePoint> points);
    void frameChanged(int index, int total);  // for progress label
    void playbackStarted();
    void playbackStopped();
    void folderLoaded(int frameCount);

private slots:
    void onTimer();

private:
    std::vector<ProfilePoint> loadFrame(int index);
    std::vector<ProfilePoint> parseJsonFile(const QString &filePath);

    QStringList m_files;
    QString     m_folderPath;
    int         m_currentIndex = 0;
    QTimer      m_timer;
};
