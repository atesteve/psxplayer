#include "main-window.h"

#include <fmt/format.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

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

    QObject::connect(_ui.horizontalSlider, &QSlider::sliderReleased, this, [this] {
        float const pos =
            static_cast<float>(_ui.horizontalSlider->value()) / _ui.horizontalSlider->maximum();
        _module->seek(pos);
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
