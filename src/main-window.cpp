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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow{parent}
{
    _ui.setupUi(this);

    QObject::connect(_ui.actionOpen, &QAction::triggered, this, [this] {
        auto const name = QFileDialog::getOpenFileName(this);

        if (name.isEmpty()) {
            return;
        }

        load_file(name);
    });

    _ui.horizontalSlider->setTracking(false);

    connect_module_signals();
    _module.start();
}

void MainWindow::connect_module_signals()
{
    QObject::connect(_ui.horizontalSlider, &QSlider::valueChanged, &_module, &UpseModule::seek);
    QObject::connect(_ui.playButton, &QPushButton::clicked, &_module, &UpseModule::toggle_pause);
}

void MainWindow::load_file(QString const& file_name)
{
    QMetaObject::invokeMethod(
        &_module, &UpseModule::load_file, Qt::ConnectionType::BlockingQueuedConnection, file_name);

    if (!_module) {
        QMessageBox::critical(this, "Cannot open file", "Cannot open file");
        return;
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
    load_file(list[0].toLocalFile());
}

MainWindow::~MainWindow()
{
    QMetaObject::invokeMethod(&_module, &UpseModule::shutdown);
    _module.wait();
}
