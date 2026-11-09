#pragma once
#include <QImage>
#include <QListWidget>
#include <QMainWindow>
#include <QTimer>
#include <deque>

class QCheckBox;
class QLabel;
class QPushButton;
class QTextEdit;

namespace lc {
enum class Tool { Select, Pen, Line, Arrow, Box, Oval, Highlight, Text, Mosaic, Crop, Erase };

class Canvas final : public QWidget {
    Q_OBJECT
    QImage image, patch;
    std::deque<QImage> undoStack;
    qsizetype undoBytes = 0;
    Tool tool = Tool::Pen;
    QColor color = QColor("#172033");
    int stroke = 5, resizeEdges = 0;
    bool filled = false, dragging = false;
    QPoint start, end;
    QVector<QPoint> points;
    QRect selected, beforeDrag, textArea;
    QTextEdit *text = nullptr;

    QRectF imageBox() const;
    QPoint imagePoint(QPointF position) const;
    void drawMark(QPainter &painter) const;
    void apply(QImage next, QRect keepSelection = {});
    void positionText();
    int selectionHit(QPoint point) const;
    void moveSelection(QPoint point);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *, QEvent *) override;

public:
    explicit Canvas(QWidget *parent = nullptr);
    const QImage &pixels() const { return image; }
    QImage copyImage() const;
    void copyToClipboard();
    void pasteFromClipboard();
    void load(QImage next);
    void setTool(Tool next);
    void setInk(QColor next) { color = next; }
    void setStroke(int width) { stroke = width; }
    void setFill(bool fill) { filled = fill; }
    void commitText(bool accept = true);
    void paste(QImage incoming);
    void extend(QSize size, QPoint inset);
    void deleteRegion();
    void undo();

signals:
    void edited();
};

class HistoryList final : public QListWidget {
    int anchor = -1;
protected:
    void mousePressEvent(QMouseEvent *event) override;
public:
    using QListWidget::QListWidget;
    void resetAnchor() { anchor = -1; }
};

class Editor final : public QMainWindow {
    Q_OBJECT
    QString dataPath, activePath, saveFolder;
    Canvas *canvas = nullptr;
    HistoryList *history = nullptr;
    QPushButton *removeChecked = nullptr, *saveChecked = nullptr;
    QCheckBox *allChecked = nullptr;
    QLabel *checkCount = nullptr;
    QWidget *penRail = nullptr;
    QTimer persistTimer;
    bool dirty = false, selecting = false, trayAvailable = false;

    void activate(QListWidgetItem *item);
    void refreshChecks();
    void removeSelected();
    void exportChecked();
    void exportCurrent();
    void canvasSizeDialog();
    void addThumbnail(const QString &path, const QImage &image, bool newest);
    void loadHistory();

protected:
    void closeEvent(QCloseEvent *event) override;

public:
    explicit Editor(QString path);
    Canvas *editorCanvas() { return canvas; }
    void setTrayAvailable(bool available) { trayAvailable = available; }
    bool addImage(QImage image);
    bool persist();
    void showEditor();
    void notice(const QString &text);
    void quit();
    void setCaptureHint(const QString &hint);

signals:
    void captureRequested(bool gif);
    void shortcutRequested();
};
}
