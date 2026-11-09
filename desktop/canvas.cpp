#include "editor.h"
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>
#include <QTextEdit>
#include <algorithm>
#include <cmath>

namespace lc {
Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName("이미지 편집 영역");
    setMinimumSize(360, 250);
}

QRectF Canvas::imageBox() const {
    if (image.isNull()) return {};
    const QSizeF available(std::max(1, width() - 48), std::max(1, height() - 48));
    const auto scale = std::min({1.0, available.width() / image.width(), available.height() / image.height()});
    const QSizeF size = QSizeF(image.size()) * scale;
    return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
}

QPoint Canvas::imagePoint(QPointF position) const {
    const auto box = imageBox();
    const auto ratio = image.width() / box.width();
    return {std::clamp(static_cast<int>((position.x() - box.x()) * ratio), 0, image.width() - 1),
            std::clamp(static_cast<int>((position.y() - box.y()) * ratio), 0, image.height() - 1)};
}

void Canvas::load(QImage next) {
    commitText(false);
    image = next.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(1);
    undoStack.clear();
    undoBytes = 0;
    selected = {};
    patch = {};
    dragging = false;
    update();
}

void Canvas::setTool(Tool next) {
    commitText();
    tool = next;
    selected = {};
    patch = {};
    dragging = false;
    update();
}

void Canvas::apply(QImage next, QRect keepSelection) {
    if (next.isNull()) return;
    constexpr qsizetype budget = 64 * 1024 * 1024;
    undoBytes += image.sizeInBytes();
    undoStack.push_back(image);
    while (undoStack.size() > 1 && (undoStack.size() > 30 || undoBytes > budget)) {
        undoBytes -= undoStack.front().sizeInBytes();
        undoStack.pop_front();
    }
    image = std::move(next);
    image.setDevicePixelRatio(1);
    selected = keepSelection;
    update();
    emit edited();
}

void Canvas::undo() {
    if (text && text->hasFocus()) { text->undo(); return; }
    if (undoStack.empty()) return;
    image = std::move(undoStack.back());
    undoBytes -= image.sizeInBytes();
    undoStack.pop_back();
    selected = {};
    patch = {};
    dragging = false;
    update();
    emit edited();
}

QImage Canvas::copyImage() const {
    return tool == Tool::Select && selected.isValid() ? image.copy(selected) : image;
}

void Canvas::copyToClipboard() {
    if (text && text->hasFocus()) { text->copy(); return; }
    const auto copy = copyImage();
    if (!copy.isNull()) QApplication::clipboard()->setImage(copy);
}

void Canvas::pasteFromClipboard() {
    if (text && text->hasFocus()) { text->paste(); return; }
    paste(QApplication::clipboard()->image());
}

void Canvas::deleteRegion() {
    if (tool != Tool::Select || !selected.isValid() || dragging) return;
    QImage next = image.copy();
    {
        QPainter painter(&next);
        painter.fillRect(selected, Qt::white);
    }
    apply(std::move(next));
}

void Canvas::extend(QSize size, QPoint inset) {
    if (image.isNull() || size.width() < image.width() + inset.x() ||
        size.height() < image.height() + inset.y() || inset.x() < 0 || inset.y() < 0 ||
        qint64(size.width()) * size.height() > 64000000 || std::max(size.width(), size.height()) > 12000)
        return;
    commitText();
    QImage next(size, QImage::Format_ARGB32_Premultiplied);
    if (next.isNull()) return;
    next.fill(Qt::white);
    {
        QPainter painter(&next);
        painter.drawImage(inset, image);
    }
    apply(std::move(next));
}

void Canvas::paste(QImage incoming) {
    if (incoming.isNull() || image.isNull()) return;
    commitText();
    incoming.setDevicePixelRatio(1);
    const QSize newSize = image.size().expandedTo(incoming.size() + QSize(64, 64));
    if (qint64(newSize.width()) * newSize.height() > 64000000 ||
        std::max(newSize.width(), newSize.height()) > 12000) return;
    QImage next(newSize, QImage::Format_ARGB32_Premultiplied);
    if (next.isNull()) return;
    next.fill(Qt::white);
    {
        QPainter painter(&next);
        painter.drawImage(0, 0, image);
        painter.drawImage(32, 32, incoming);
    }
    apply(std::move(next), tool == Tool::Select ? QRect(QPoint(32, 32), incoming.size()) : QRect{});
}

void Canvas::drawMark(QPainter &painter) const {
    painter.setRenderHint(QPainter::Antialiasing);
    auto ink = color;
    if (tool == Tool::Highlight) ink.setAlpha(95);
    if (tool == Tool::Erase) ink = Qt::white;
    painter.setPen(QPen(ink, tool == Tool::Highlight ? stroke * 3 : stroke,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(filled ? QBrush(ink) : Qt::NoBrush);
    const QRectF area(QRect(start, end).normalized());
    switch (tool) {
    case Tool::Pen: case Tool::Highlight: case Tool::Erase:
        if (points.size() == 1) painter.drawPoint(points.front());
        else painter.drawPolyline(QPolygon(points));
        break;
    case Tool::Line: painter.drawLine(start, end); break;
    case Tool::Arrow: {
        painter.drawLine(start, end);
        const auto angle = std::atan2(end.y() - start.y(), end.x() - start.x());
        const auto length = std::max(12, stroke * 4);
        for (const auto offset : {-0.55, 0.55})
            painter.drawLine(QPointF(end), QPointF(end) - QPointF(std::cos(angle + offset),
                                                                 std::sin(angle + offset)) * length);
        break;
    }
    case Tool::Box: painter.drawRect(area); break;
    case Tool::Oval: painter.drawEllipse(area); break;
    default: break;
    }
}

void Canvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#edf1f6"));
    if (image.isNull()) {
        painter.setPen(QColor("#526176"));
        painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter,
                         "영역을 캡처하거나 이미지를 여세요.");
        return;
    }
    const auto box = imageBox();
    painter.setPen(QColor("#dce3ed"));
    painter.drawRect(box.adjusted(-1, -1, 1, 1));
    painter.translate(box.topLeft());
    painter.scale(box.width() / image.width(), box.height() / image.height());
    painter.setClipRect(image.rect());
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(0, 0, image);
    if (dragging) {
        if (patch.isNull()) drawMark(painter);
        else {
            painter.fillRect(beforeDrag, Qt::white);
            painter.drawImage(selected, patch);
        }
    }
    auto outline = selected;
    if (dragging && (tool == Tool::Crop || tool == Tool::Mosaic || tool == Tool::Text))
        outline = QRect(start, end).normalized();
    if (text) outline = textArea;
    if (outline.isValid()) {
        QPen border(QColor("#1761d7"), 1, Qt::DashLine);
        border.setCosmetic(true);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(outline));
    }
}

int Canvas::selectionHit(QPoint point) const {
    if (!selected.isValid()) return 0;
    const int tolerance = std::max(1, qRound(6 * image.width() / imageBox().width()));
    if (!selected.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point)) return 0;
    int sides = 0;
    const auto left = std::abs(point.x() - selected.left()), right = std::abs(point.x() - selected.right());
    const auto top = std::abs(point.y() - selected.top()), bottom = std::abs(point.y() - selected.bottom());
    if (std::min(left, right) <= tolerance) sides |= left <= right ? 1 : 2;
    if (std::min(top, bottom) <= tolerance) sides |= top <= bottom ? 4 : 8;
    return sides ? sides : selected.contains(point) ? 16 : 0;
}

void Canvas::moveSelection(QPoint point) {
    auto delta = point - start;
    selected = beforeDrag;
    if (resizeEdges == 16) {
        selected.translate(std::clamp(delta.x(), -selected.left(), image.width() - 1 - selected.right()),
                           std::clamp(delta.y(), -selected.top(), image.height() - 1 - selected.bottom()));
        return;
    }
    if (resizeEdges & 1) selected.setLeft(std::clamp(beforeDrag.left() + delta.x(), 0, beforeDrag.right()));
    if (resizeEdges & 2) selected.setRight(std::clamp(beforeDrag.right() + delta.x(), beforeDrag.left(), image.width() - 1));
    if (resizeEdges & 4) selected.setTop(std::clamp(beforeDrag.top() + delta.y(), 0, beforeDrag.bottom()));
    if (resizeEdges & 8) selected.setBottom(std::clamp(beforeDrag.bottom() + delta.y(), beforeDrag.top(), image.height() - 1));
}

void Canvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || image.isNull()) return;
    commitText();
    setFocus(Qt::MouseFocusReason);
    if (!imageBox().contains(event->position())) {
        selected = {};
        update();
        return;
    }
    start = end = imagePoint(event->position());
    points = {start};
    resizeEdges = tool == Tool::Select ? selectionHit(start) : 0;
    if (resizeEdges) {
        beforeDrag = selected;
        patch = image.copy(selected);
    } else {
        selected = {};
        patch = {};
    }
    dragging = true;
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
    if (image.isNull()) return;
    const auto position = imagePoint(event->position());
    if (!dragging) {
        auto cursor = Qt::CrossCursor;
        if (!imageBox().contains(event->position())) cursor = Qt::ArrowCursor;
        else if (tool == Tool::Select) {
            const int hit = selectionHit(position);
            if (hit == 16) cursor = Qt::SizeAllCursor;
            else if (hit == 1 || hit == 2) cursor = Qt::SizeHorCursor;
            else if (hit == 4 || hit == 8) cursor = Qt::SizeVerCursor;
            else if (hit == 5 || hit == 10) cursor = Qt::SizeFDiagCursor;
            else if (hit) cursor = Qt::SizeBDiagCursor;
        } else if (tool == Tool::Text) cursor = Qt::IBeamCursor;
        setCursor(cursor);
        return;
    }
    end = position;
    if (tool == Tool::Select) {
        if (patch.isNull()) selected = QRect(start, end).normalized();
        else moveSelection(end);
    } else if (tool == Tool::Pen || tool == Tool::Highlight || tool == Tool::Erase) {
        if (points.back() != end) points.append(end);
    }
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !dragging) return;
    mouseMoveEvent(event);
    dragging = false;
    auto area = QRect(start, end).normalized();
    const bool areaReady = area.width() >= 2 && area.height() >= 2;
    if (tool == Tool::Select) {
        if (!patch.isNull() && beforeDrag != selected) {
            auto next = image.copy();
            {
                QPainter painter(&next);
                painter.fillRect(beforeDrag, Qt::white);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(selected, patch);
            }
            apply(std::move(next), selected);
        } else if (patch.isNull() && !areaReady) selected = {};
        patch = {};
    } else if (tool == Tool::Text && areaReady) {
        textArea = area;
        text = new QTextEdit(this);
        text->setAcceptRichText(false);
        text->setPlaceholderText("Ctrl+Enter로 적용 · Esc로 취소");
        text->setStyleSheet("QTextEdit {background: rgba(255,255,255,212); border: 1px solid #1761d7;}");
        text->setTextColor(color);
        text->installEventFilter(this);
        positionText();
        text->show();
        text->setFocus();
    } else if (tool == Tool::Crop && areaReady) {
        apply(image.copy(area));
    } else if (tool == Tool::Mosaic && areaReady) {
        auto next = image.copy();
        auto block = image.copy(area).scaled(std::max(1, area.width() / 12), std::max(1, area.height() / 12),
                                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        {
            QPainter painter(&next);
            painter.drawImage(area, block);
        }
        apply(std::move(next));
    } else if (tool != Tool::Text && tool != Tool::Crop && tool != Tool::Mosaic) {
        auto next = image;
        {
            QPainter painter(&next);
            drawMark(painter);
        }
        apply(std::move(next));
    }
    update();
}

void Canvas::positionText() {
    if (!text || image.isNull()) return;
    const auto box = imageBox();
    const auto factor = box.width() / image.width();
    auto frame = QRectF(box.topLeft() + QPointF(textArea.topLeft()) * factor, QSizeF(textArea.size()) * factor);
    text->setGeometry(frame.toAlignedRect());
    QFont font = text->font();
    font.setPixelSize(std::max(8, qRound(24 * factor)));
    text->setFont(font);
}

void Canvas::commitText(bool accept) {
    if (!text) return;
    const auto value = text->toPlainText();
    text->hide();
    text->deleteLater();
    text = nullptr;
    if (accept && !value.isEmpty()) {
        auto next = image.copy();
        QTextDocument doc;
        QFont font = this->font();
        font.setPixelSize(24);
        doc.setDefaultFont(font);
        doc.setDefaultStyleSheet("body { color: " + color.name() + "; }");
        doc.setPlainText(value);
        doc.setTextWidth(textArea.width());
        {
            QPainter painter(&next);
            painter.translate(textArea.topLeft());
            doc.drawContents(&painter, QRect(QPoint(), textArea.size()));
        }
        apply(std::move(next));
    }
    update();
}

bool Canvas::eventFilter(QObject *object, QEvent *event) {
    if (object == text && event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape ||
            ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
             (key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))) {
            commitText(key->key() != Qt::Key_Escape);
            setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void Canvas::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    positionText();
}

void Canvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) deleteRegion();
    else if (event->key() == Qt::Key_Escape) {
        selected = {};
        dragging = false;
        patch = {};
        update();
    } else QWidget::keyPressEvent(event);
}
}
