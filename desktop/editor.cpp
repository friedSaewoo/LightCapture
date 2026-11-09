#include "editor.h"
#include "platform.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QUuid>
#include <algorithm>

namespace lc {
void HistoryList::mousePressEvent(QMouseEvent *event) {
    auto entry = itemAt(event->pos());
    if (event->button() != Qt::LeftButton || !entry) {
        QListWidget::mousePressEvent(event);
        return;
    }
    const int index = row(entry);
    const auto state = entry->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
    setCurrentItem(entry);
    if (currentItem() != entry) return;
    if (event->modifiers().testFlag(Qt::ShiftModifier) && anchor >= 0 && anchor < count()) {
        for (int i = std::min(index, anchor); i <= std::max(index, anchor); ++i)
            item(i)->setCheckState(state);
    } else {
        entry->setCheckState(state);
        anchor = index;
    }
    setFocus(Qt::MouseFocusReason);
}

static QIcon toolIcon(Tool tool) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#25354a"), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (tool) {
    case Tool::Select:
        p.setPen(QPen(QColor("#25354a"), 1.5, Qt::DashLine));
        p.drawRect(QRectF(4, 5, 16, 14));
        break;
    case Tool::Pen:
        p.drawPolygon(QPolygonF{{5, 19}, {7, 13}, {16, 4}, {20, 8}, {11, 17}});
        p.drawLine(14, 6, 18, 10);
        break;
    case Tool::Line: p.drawLine(5, 19, 19, 5); break;
    case Tool::Arrow:
        p.drawLine(5, 19, 19, 5);
        p.drawPolyline(QPolygonF{{11, 5}, {19, 5}, {19, 13}});
        break;
    case Tool::Box: p.drawRect(QRectF(4, 5, 16, 14)); break;
    case Tool::Oval: p.drawEllipse(QRectF(4, 5, 16, 14)); break;
    case Tool::Highlight:
        p.drawPolygon(QPolygonF{{5, 15}, {15, 5}, {20, 10}, {10, 20}});
        p.drawLine(4, 20, 12, 20);
        break;
    case Tool::Text:
        p.drawLine(5, 5, 19, 5); p.drawLine(12, 5, 12, 19);
        break;
    case Tool::Mosaic:
        for (const QPoint &point : {QPoint(5, 5), {13, 5}, {5, 13}, {13, 13}})
            p.drawRect(QRect(point, QSize(6, 6)));
        break;
    case Tool::Crop:
        p.drawPolyline(QPolygonF{{7, 3}, {7, 17}, {21, 17}});
        p.drawPolyline(QPolygonF{{3, 7}, {17, 7}, {17, 21}});
        break;
    case Tool::Erase:
        p.drawPolygon(QPolygonF{{4, 14}, {13, 5}, {20, 12}, {13, 19}, {9, 19}});
        p.drawLine(8, 10, 16, 18);
        break;
    }
    return QIcon(pixmap);
}

Editor::Editor(QString path) : dataPath(std::move(path)) {
    QDir().mkpath(dataPath);
    setWindowTitle("LightCapture · macOS / Linux 미리보기");
    setMinimumSize(780, 510);
    resize(1120, 740);
    auto body = new QWidget;
    setCentralWidget(body);
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto bar = new QWidget;
    bar->setObjectName("toolbar");
    auto barLayout = new QVBoxLayout(bar);
    barLayout->setContentsMargins(14, 10, 14, 9);
    barLayout->setSpacing(7);
    auto actions = new QHBoxLayout;
    auto button = [&](QString title, auto callback) {
        auto control = new QPushButton(title);
        actions->addWidget(control);
        connect(control, &QPushButton::clicked, this, callback);
        return control;
    };
    auto capture = button("영역 캡처", [this] { emit captureRequested(false); });
    capture->setObjectName("captureButton");
    auto gif = button("GIF 녹화", [this] { emit captureRequested(true); });
    gif->setEnabled(!usesPortal());
    if (usesPortal()) gif->setToolTip("GIF 녹화는 X11 세션에서 사용할 수 있습니다.");
    button("그림판", [this] {
        QImage blank(1280, 720, QImage::Format_RGB32);
        blank.fill(Qt::white);
        addImage(blank);
    });
    button("열기", [this] {
        auto path = QFileDialog::getOpenFileName(this, "이미지 열기", {}, "이미지 (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty()) return;
        auto image = loadImage(path);
        if (image.isNull()) notice("이미지 파일을 읽지 못했습니다.");
        else addImage(std::move(image));
    });
    button("현재 저장", [this] { exportCurrent(); });
    button("캔버스 크기", [this] { canvasSizeDialog(); });
    actions->addStretch();
    button("단축키", [this] { emit shortcutRequested(); });
    barLayout->addLayout(actions);
    auto tools = new QHBoxLayout;
    auto group = new QButtonGroup(this);
    const char *names[] = {"사각형 선택 · 이동 · 크기 조절", "펜", "직선", "화살표", "사각형", "타원",
                           "형광펜", "텍스트", "모자이크", "자르기", "지우개"};
    for (int i = 0; i < 11; ++i) {
        auto tool = static_cast<Tool>(i);
        auto control = new QToolButton;
        control->setAccessibleName(names[i]);
        control->setToolTip(names[i]);
        control->setCheckable(true);
        control->setChecked(tool == Tool::Pen);
        control->setIcon(toolIcon(tool));
        control->setIconSize(QSize(22, 22));
        control->setFixedSize(31, 31);
        tools->addWidget(control);
        group->addButton(control, i);
    }
    const char *colors[] = {"#172033", "#6b7280", "#ffffff", "#e5484d", "#f97316",
                            "#facc15", "#16a34a", "#06b6d4", "#2563eb", "#9333ea"};
    auto palette = new QButtonGroup(this);
    for (const auto color : colors) {
        auto chip = new QToolButton;
        chip->setAccessibleName(QString("색상 %1").arg(color));
        chip->setToolTip(color);
        chip->setFixedSize(20, 24);
        chip->setCheckable(true);
        chip->setChecked(color == colors[0]);
        chip->setStyleSheet(QString("QToolButton {background:%1; border:1px solid #b6c2d2;}"
                                    "QToolButton:checked {border:2px solid #1761d7;}").arg(color));
        palette->addButton(chip);
        tools->addWidget(chip);
        connect(chip, &QToolButton::clicked, this, [this, color] { canvas->setInk(QColor(color)); });
    }
    auto fill = new QCheckBox("채움");
    tools->addWidget(fill);
    tools->addStretch();
    barLayout->addLayout(tools);
    layout->addWidget(bar);

    auto row = new QHBoxLayout;
    row->setSpacing(0);
    penRail = new QWidget;
    auto railLayout = new QVBoxLayout(penRail);
    railLayout->setContentsMargins(8, 12, 8, 0);
    auto widthText = new QLabel("5 px");
    widthText->setAlignment(Qt::AlignHCenter);
    auto stroke = new QSlider(Qt::Vertical);
    stroke->setRange(1, 80);
    stroke->setValue(5);
    stroke->setFixedHeight(200);
    stroke->setAccessibleName("펜 굵기");
    railLayout->addWidget(widthText);
    railLayout->addWidget(stroke, 0, Qt::AlignHCenter);
    railLayout->addStretch();
    row->addWidget(penRail);
    canvas = new Canvas;
    row->addWidget(canvas, 1);
    auto side = new QWidget;
    side->setObjectName("historyPanel");
    side->setFixedWidth(210);
    auto sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(10, 12, 10, 8);
    auto label = new QLabel("캡처 목록");
    label->setObjectName("historyTitle");
    sideLayout->addWidget(label);
    auto historyActions = new QHBoxLayout;
    removeChecked = new QPushButton("삭제");
    saveChecked = new QPushButton("저장");
    saveChecked->setObjectName("primary");
    historyActions->addWidget(removeChecked);
    historyActions->addWidget(saveChecked);
    sideLayout->addLayout(historyActions);
    auto checks = new QHBoxLayout;
    allChecked = new QCheckBox("전체 체크");
    checkCount = new QLabel;
    checks->addWidget(allChecked);
    checks->addStretch();
    checks->addWidget(checkCount);
    sideLayout->addLayout(checks);
    history = new HistoryList;
    history->setAccessibleName("캡처 목록");
    history->setIconSize(QSize(153, 106));
    history->setUniformItemSizes(true);
    history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideLayout->addWidget(history, 1);
    row->addWidget(side);
    layout->addLayout(row, 1);
    setStyleSheet(
        "QMainWindow, #toolbar {background:#fff; color:#172033;}"
        "#toolbar {border-bottom:1px solid #dce3ed;}"
        "#historyPanel {background:#f8fafd; border-left:1px solid #dce3ed;}"
        "#historyTitle {font-weight:600; padding:2px 1px 5px;}"
        "QPushButton, QToolButton {background:#fff; color:#172033; border:1px solid #dce3ed; border-radius:5px;}"
        "QPushButton {padding:6px 10px; min-height:20px;}"
        "QPushButton:hover, QToolButton:hover, QToolButton:checked, #captureButton {background:#eaf2ff;}"
        "QPushButton:focus, QToolButton:focus {border-color:#1761d7;}"
        "#primary {background:#1761d7; color:white; border-color:#1761d7;}"
        "QPushButton:disabled, #primary:disabled {background:#eef2f6; color:#5b6778; border-color:#dce3ed;}"
        "QListWidget {background:#fff; color:#172033; border:0; outline:0;}"
        "QListWidget::item {border:1px solid #e2e8f0; border-radius:6px; margin-bottom:5px; padding:3px;}"
        "QListWidget::item:selected {background:#eaf2ff; border:1px solid #1761d7; color:#172033;}");
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        canvas->setTool(static_cast<Tool>(id));
        penRail->setVisible(id == static_cast<int>(Tool::Pen));
        canvas->setFocus();
    });
    connect(fill, &QCheckBox::toggled, canvas, &Canvas::setFill);
    connect(stroke, &QSlider::valueChanged, this, [this, widthText](int value) {
        widthText->setText(QString::number(value) + " px");
        canvas->setStroke(value);
    });
    connect(removeChecked, &QPushButton::clicked, this, &Editor::removeSelected);
    connect(saveChecked, &QPushButton::clicked, this, &Editor::exportChecked);
    connect(history, &QListWidget::itemChanged, this, &Editor::refreshChecks);
    connect(history, &QListWidget::currentItemChanged, this, [this](auto item, auto) { activate(item); });
    connect(allChecked, &QCheckBox::clicked, this, [this](bool checked) {
        const QSignalBlocker block(history);
        for (int i = 0; i < history->count(); ++i)
            history->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        refreshChecks();
    });
    persistTimer.setSingleShot(true);
    persistTimer.setInterval(400);
    connect(&persistTimer, &QTimer::timeout, this, [this] { persist(); });
    connect(canvas, &Canvas::edited, this, [this] { dirty = true; persistTimer.start(); });
    auto shortcut = new QAction(this);
    shortcut->setShortcut(QKeySequence::Save);
    addAction(shortcut);
    connect(shortcut, &QAction::triggered, this, &Editor::exportCurrent);
    auto editShortcut = [this](QKeySequence::StandardKey key, auto callback) {
        auto action = new QAction(this);
        action->setShortcut(key);
        addAction(action);
        connect(action, &QAction::triggered, this, callback);
    };
    editShortcut(QKeySequence::Copy, [this] { canvas->copyToClipboard(); });
    editShortcut(QKeySequence::Paste, [this] {
        if (canvas->pixels().isNull()) {
            auto image = QApplication::clipboard()->image();
            if (!image.isNull()) addImage(std::move(image));
        } else canvas->pasteFromClipboard();
    });
    editShortcut(QKeySequence::Undo, [this] { canvas->undo(); });
    auto quitAction = new QAction(this);
    quitAction->setShortcut(QKeySequence::Quit);
    addAction(quitAction);
    connect(quitAction, &QAction::triggered, this, &Editor::quit);
    loadHistory();
    refreshChecks();
    notice("영역을 캡처하고 편집한 뒤 저장하세요.");
}

void Editor::notice(const QString &text) { statusBar()->showMessage(text); }

void Editor::setCaptureHint(const QString &hint) {
    findChild<QPushButton *>("captureButton")->setToolTip(hint);
    notice(hint);
}

void Editor::showEditor() {
    showNormal();
    raise();
    activateWindow();
    canvas->setFocus();
}

void Editor::addThumbnail(const QString &path, const QImage &image, bool newest) {
    auto item = new QListWidgetItem;
    item->setData(Qt::UserRole, path);
    item->setData(Qt::AccessibleTextRole, "캡처 이미지");
    item->setIcon(QIcon(QPixmap::fromImage(image.scaled(153, 106, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
    item->setCheckState(Qt::Unchecked);
    item->setSizeHint(QSize(176, 119));
    if (newest) history->insertItem(0, item);
    else history->addItem(item);
}

void Editor::loadHistory() {
    QDir dir(dataPath);
    const auto entries = dir.entryInfoList({"capture-*.png"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const auto &entry : entries) {
        QImageReader reader(entry.absoluteFilePath());
        const auto size = reader.size();
        if (size.isEmpty() || qint64(size.width()) * size.height() > 64000000) continue;
        reader.setScaledSize(size.scaled(153, 106, Qt::KeepAspectRatio));
        const auto thumbnail = reader.read();
        if (!thumbnail.isNull()) addThumbnail(entry.absoluteFilePath(), thumbnail, false);
    }
    if (history->count()) history->setCurrentRow(0);
}

bool Editor::persist() {
    persistTimer.stop();
    if (!dirty || activePath.isEmpty()) return true;
    if (!saveImage(canvas->pixels(), activePath)) {
        notice("편집 내용을 보관하지 못했습니다. 저장 공간을 확인하세요.");
        return false;
    }
    dirty = false;
    for (int i = 0; i < history->count(); ++i) {
        auto item = history->item(i);
        if (item->data(Qt::UserRole).toString() != activePath) continue;
        item->setIcon(QIcon(QPixmap::fromImage(canvas->pixels().scaled(
            153, 106, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
        break;
    }
    return true;
}

void Editor::activate(QListWidgetItem *item) {
    if (selecting || !item) return;
    const auto path = item->data(Qt::UserRole).toString();
    if (path == activePath) return;
    canvas->commitText();
    const auto next = persist() ? loadImage(path) : QImage{};
    if (next.isNull()) {
        const QSignalBlocker block(history);
        for (int i = 0; i < history->count(); ++i)
            if (history->item(i)->data(Qt::UserRole).toString() == activePath)
                history->setCurrentRow(i);
        notice("캡처를 열지 못했습니다. 파일과 저장 공간을 확인하세요.");
        return;
    }
    activePath = path;
    canvas->load(next);
}

bool Editor::addImage(QImage image) {
    if (image.isNull() || qint64(image.width()) * image.height() > 64000000 ||
        std::max(image.width(), image.height()) > 12000) {
        notice("이미지 크기가 편집 한도를 초과했습니다.");
        return false;
    }
    canvas->commitText();
    if (!persist()) return false;
    const auto name = QString("capture-%1-%2.png").arg(QDateTime::currentMSecsSinceEpoch())
                           .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
    const auto path = QDir(dataPath).filePath(name);
    if (!saveImage(image, path)) {
        notice("캡처를 보관하지 못했습니다. 저장 공간을 확인하세요.");
        return false;
    }
    selecting = true;
    addThumbnail(path, image, true);
    history->resetAnchor();
    history->setCurrentRow(0);
    selecting = false;
    activePath = path;
    canvas->load(std::move(image));
    refreshChecks();
    canvas->setFocus();
    notice("이미지를 편집하거나 저장하세요.");
    return true;
}

void Editor::refreshChecks() {
    int count = 0;
    for (int i = 0; i < history->count(); ++i)
        count += history->item(i)->checkState() == Qt::Checked;
    checkCount->setText(QString("%1개").arg(count));
    const QSignalBlocker block(allChecked);
    allChecked->setChecked(count && count == history->count());
    allChecked->setEnabled(history->count());
    saveChecked->setEnabled(count);
    removeChecked->setEnabled(count);
}

void Editor::removeSelected() {
    int checked = 0;
    for (int i = 0; i < history->count(); ++i)
        checked += history->item(i)->checkState() == Qt::Checked;
    if (!checked || QMessageBox::question(this, "캡처 삭제",
        QString("선택한 캡처 %1개를 보관함에서 삭제할까요?").arg(checked)) != QMessageBox::Yes) return;
    canvas->commitText();
    if (!persist()) return;
    int failed = 0;
    selecting = true;
    for (int i = history->count() - 1; i >= 0; --i) {
        auto item = history->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const auto path = item->data(Qt::UserRole).toString();
        if (!QFile::remove(path) && QFile::exists(path)) { ++failed; continue; }
        if (path == activePath) {
            activePath.clear();
            canvas->load({});
        }
        delete history->takeItem(i);
    }
    selecting = false;
    history->resetAnchor();
    if (history->count() && activePath.isEmpty()) {
        history->setCurrentRow(0);
        activate(history->currentItem());
    }
    refreshChecks();
    notice(failed ? QString("%1개를 삭제하지 못했습니다.").arg(failed) : QString("선택한 캡처를 삭제했습니다."));
}

void Editor::exportCurrent() {
    canvas->commitText();
    if (canvas->pixels().isNull()) return;
    auto path = QFileDialog::getSaveFileName(this, "현재 이미지 저장",
        QDir(saveFolder.isEmpty() ? QDir::homePath() : saveFolder).filePath("LightCapture.png"), "PNG 이미지 (*.png)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (saveImage(canvas->pixels(), path)) {
        saveFolder = QFileInfo(path).absolutePath();
        notice("현재 이미지를 저장했습니다.");
    } else notice("이미지를 저장하지 못했습니다.");
}

void Editor::exportChecked() {
    canvas->commitText();
    if (!persist()) return;
    const auto folder = QFileDialog::getExistingDirectory(this, "선택한 이미지 저장 폴더", saveFolder);
    if (folder.isEmpty()) return;
    saveFolder = folder;
    int saved = 0, failed = 0;
    for (int i = 0; i < history->count(); ++i) {
        auto item = history->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const auto source = item->data(Qt::UserRole).toString();
        auto target = QDir(folder).filePath(QFileInfo(source).fileName());
        for (int index = 1; QFile::exists(target); ++index)
            target = QDir(folder).filePath(QFileInfo(source).completeBaseName() + QString("-%1.png").arg(index));
        if (QFile::copy(source, target)) ++saved;
        else ++failed;
    }
    notice(QString("%1개를 저장했습니다. 실패 %2개").arg(saved).arg(failed));
}

void Editor::canvasSizeDialog() {
    const auto size = canvas->pixels().size();
    if (size.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle("캔버스 확장");
    QFormLayout form(&dialog);
    QSpinBox left, top, right, bottom;
    for (auto field : {&left, &top, &right, &bottom}) field->setRange(0, 4096);
    form.addRow("왼쪽 여백 (px)", &left);
    form.addRow("위쪽 여백 (px)", &top);
    form.addRow("오른쪽 여백 (px)", &right);
    form.addRow("아래쪽 여백 (px)", &bottom);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    const QSize extra(left.value() + right.value(), top.value() + bottom.value());
    if (extra.width() || extra.height()) canvas->extend(size + extra, QPoint(left.value(), top.value()));
}

void Editor::closeEvent(QCloseEvent *event) {
    canvas->commitText();
    if (!persist()) { event->ignore(); return; }
    if (trayAvailable) {
        hide();
        event->ignore();
    } else {
        event->accept();
        QApplication::quit();
    }
}

void Editor::quit() {
    canvas->commitText();
    if (persist()) QApplication::quit();
}
}
