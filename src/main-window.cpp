#include "main-window.h"

#include <fmt/format.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

extern "C" {
float multiplier = 1;
}

static bool toggle{true};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow{parent}
{
    _ui.setupUi(this);

    QObject::connect(_ui.actionOpen, &QAction::triggered, this, [this] {
        auto const name = QFileDialog::getOpenFileName(this);

        if (name.isEmpty()) {
            return;
        }

        open_file(name);
    });

    QObject::connect(_ui.playButton, &QPushButton::clicked, this, [this] {
        if (!_module) {
            return;
        }

        if (toggle) {
            _module->pause();
        } else {
            _module->play();
        }

        toggle = !toggle;
    });

    _ui.horizontalSlider->setTracking(false);
    QObject::connect(_ui.horizontalSlider, &QSlider::valueChanged, this, [](int pos) {
        multiplier = pos / 100.0;
    });
}

void MainWindow::open_file(QString const& file_name)
{
    _module.emplace(file_name.toStdString());

    if (!*_module) {
        QMessageBox::critical(this, "Cannot open file", "Cannot open file");
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    auto const list = event->mimeData()->urls();
    if (list.empty()) {
        return;
    }
    open_file(list[0].toLocalFile());
}
