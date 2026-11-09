#pragma once
#include <QImage>
#include <QString>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace lc {
struct GifProfile {
    const char *name;
    int fps;
    int edge;
};
inline constexpr GifProfile gifProfiles[] = {
    {"표준 · 12fps · 최대 1280px", 12, 1280},
    {"선명 · 15fps · 최대 1600px", 15, 1600},
    {"작게 · 10fps · 최대 960px", 10, 960}};
inline constexpr int gifTimeLimit = 120000;
inline constexpr qint64 gifByteLimit = 80ll * 1024 * 1024;

class GifRecorder {
    struct Sample { QImage image; qint64 time; };
    QString path;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Sample> samples;
    bool stopping = false;
    bool cancelled = false;
    qint64 stoppedAt = 0;
    bool completed = false;
    QString failure;
    qint64 bytesWritten = 0;
    std::thread worker;
    void encode();

public:
    explicit GifRecorder(QString file);
    ~GifRecorder();
    bool readyForSample();
    void submit(QImage image, qint64 time);
    void stop(qint64 time, bool cancel = false);
    bool done();
    QString error();
    qint64 bytes();
};

bool saveRecording(const QString &source, const QString &target);
}
