#include "gif.h"

#include <QFile>
#include <QFileInfo>
#include <vector>
#include <QHash>
#include <QSaveFile>
#include <gif_lib.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace lc {
namespace {
class GifFile {
    QFile file;
    GifFileType *gif = nullptr;

    static int write(GifFileType *handle, const GifByteType *data, int length) {
        auto output = static_cast<QFile *>(handle->UserData);
        return static_cast<int>(output->write(reinterpret_cast<const char *>(data), length));
    }

    static void require(bool ok) {
        if (!ok) throw std::runtime_error("GIF 파일에 기록하지 못했습니다.");
    }

public:
    explicit GifFile(const QString &path) : file(path) {
        require(file.open(QIODevice::WriteOnly));
        int error = 0;
        gif = EGifOpen(&file, write, &error);
        require(gif != nullptr);
        EGifSetGifVersion(gif, true);
    }
    ~GifFile() {
        if (gif) EGifCloseFile(gif, nullptr);
    }

    void start(QSize size) {
        require(EGifPutScreenDesc(gif, size.width(), size.height(), 8, 0, nullptr) == GIF_OK);
        const unsigned char loop[] = {1, 0, 0};
        require(EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) == GIF_OK &&
                EGifPutExtensionBlock(gif, 11, "NETSCAPE2.0") == GIF_OK &&
                EGifPutExtensionBlock(gif, 3, loop) == GIF_OK &&
                EGifPutExtensionTrailer(gif) == GIF_OK);
    }

    void frame(const QImage &image, const QRect &area, int delay) {
        const int count = area.width() * area.height();
        std::vector<GifPixelType> indexed(count);
        GifColorType colors[256]{};
        QHash<QRgb, int> exact;
        bool overflow = false;
        int index = 0;
        for (int y = area.top(); y <= area.bottom() && !overflow; ++y) {
            const auto row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = area.left(); x <= area.right(); ++x) {
                QRgb color = row[x];
                auto found = exact.constFind(color);
                if (found != exact.constEnd()) indexed[index++] = found.value();
                else if (exact.size() == 256) { overflow = true; break; }
                else {
                    int entry = exact.size();
                    exact.insert(color, entry);
                    colors[entry] = {static_cast<GifByteType>(qRed(color)),
                                     static_cast<GifByteType>(qGreen(color)),
                                     static_cast<GifByteType>(qBlue(color))};
                    indexed[index++] = entry;
                }
            }
        }
        int colorCount = exact.size();
        if (overflow) {
            const auto reduced = image.copy(area).convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither);
            const auto table = reduced.colorTable();
            colorCount = table.size();
            require(!reduced.isNull() && colorCount > 0 && colorCount <= 256);
            for (int i = 0; i < colorCount; ++i) {
                colors[i] = {static_cast<GifByteType>(qRed(table[i])),
                             static_cast<GifByteType>(qGreen(table[i])),
                             static_cast<GifByteType>(qBlue(table[i]))};
            }
            for (int y = 0; y < area.height(); ++y)
                std::memcpy(indexed.data() + y * area.width(), reduced.constScanLine(y), area.width());
        }
        int paletteSize = 2;
        while (paletteSize < colorCount) paletteSize *= 2;
        std::unique_ptr<ColorMapObject, decltype(&GifFreeMapObject)> palette(
            GifMakeMapObject(paletteSize, colors), GifFreeMapObject);
        require(palette != nullptr);
        const unsigned char control[] = {4, static_cast<unsigned char>(delay & 255),
                                         static_cast<unsigned char>(delay >> 8), 0};
        require(EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, control) == GIF_OK &&
                EGifPutImageDesc(gif, area.x(), area.y(), area.width(), area.height(), false, palette.get()) == GIF_OK);
        for (int y = 0; y < area.height(); ++y)
            require(EGifPutLine(gif, indexed.data() + y * area.width(), area.width()) == GIF_OK);
    }

    qint64 size() const { return file.pos(); }

    void finish() {
        auto handle = gif;
        gif = nullptr;
        require(EGifCloseFile(handle, nullptr) == GIF_OK && file.flush());
    }
};

QRect difference(const QImage &old, const QImage &next) {
    if (old.isNull()) return next.rect();
    int left = next.width(), top = next.height(), right = -1, bottom = -1;
    for (int y = 0; y < next.height(); ++y) {
        const auto a = reinterpret_cast<const QRgb *>(old.constScanLine(y));
        const auto b = reinterpret_cast<const QRgb *>(next.constScanLine(y));
        if (!std::memcmp(a, b, static_cast<size_t>(next.width()) * sizeof(QRgb))) continue;
        top = std::min(top, y);
        bottom = y;
        for (int x = 0; x < next.width(); ++x)
            if (a[x] != b[x]) { left = std::min(left, x); right = std::max(right, x); }
    }
    return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}
}

GifRecorder::GifRecorder(QString file) : path(std::move(file)), worker([this] { encode(); }) {}

GifRecorder::~GifRecorder() {
    stop(0, true);
    worker.join();
}

bool GifRecorder::readyForSample() {
    std::lock_guard<std::mutex> lock(mutex);
    return !stopping && !completed && samples.size() < 3;
}

void GifRecorder::submit(QImage image, qint64 time) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping || completed || image.isNull() || samples.size() >= 3) return;
    samples.push_back({std::move(image), time});
    changed.notify_one();
}

void GifRecorder::stop(qint64 time, bool cancel) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!stopping) stoppedAt = time;
    stopping = true;
    cancelled = cancelled || cancel;
    changed.notify_one();
}

bool GifRecorder::done() {
    std::lock_guard<std::mutex> lock(mutex);
    return completed;
}

QString GifRecorder::error() {
    std::lock_guard<std::mutex> lock(mutex);
    return failure;
}

qint64 GifRecorder::bytes() {
    std::lock_guard<std::mutex> lock(mutex);
    return bytesWritten;
}

void GifRecorder::encode() {
    QString error;
    try {
        GifFile output(path);
        QImage pending;
        QRect pendingArea;
        qint64 origin = 0, pendingTime = 0, writtenTicks = 0;
        auto flush = [&](qint64 time) {
            const auto end = std::max(writtenTicks + 2, (time - origin + 5) / 10);
            output.frame(pending, pendingArea, static_cast<int>(std::min<qint64>(end - writtenTicks, 65535)));
            writtenTicks = end;
            std::lock_guard<std::mutex> lock(mutex);
            bytesWritten = output.size();
        };
        for (;;) {
            Sample sample;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || !samples.empty(); });
                if (cancelled) break;
                if (samples.empty()) {
                    const auto end = stoppedAt;
                    lock.unlock();
                    if (pending.isNull()) throw std::runtime_error("저장할 녹화 화면이 없습니다.");
                    flush(std::max(end, pendingTime + 20));
                    output.finish();
                    lock.lock();
                    bytesWritten = QFileInfo(path).size();
                    break;
                }
                sample = std::move(samples.front());
                samples.pop_front();
            }
            QImage next = sample.image.convertToFormat(QImage::Format_RGB32);
            if (pending.isNull()) {
                output.start(next.size());
                origin = sample.time;
            } else if (pending.size() != next.size()) {
                throw std::runtime_error("녹화 중 화면 크기가 바뀌었습니다.");
            }
            auto area = difference(pending, next);
            if (!area.isValid()) continue;
            if (!pending.isNull()) flush(sample.time);
            pending = std::move(next);
            pendingArea = area;
            pendingTime = sample.time;
        }
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
    }
    std::lock_guard<std::mutex> lock(mutex);
    failure = std::move(error);
    completed = true;
}

bool saveRecording(const QString &source, const QString &target) {
    QFile input(source);
    QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) {
        const auto block = input.read(65536);
        if (block.isEmpty() || output.write(block) != block.size()) return false;
    }
    return output.commit();
}
}
