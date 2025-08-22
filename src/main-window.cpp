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

        open_file(name);
    });

    _ui.horizontalSlider->setTracking(false);
}

void MainWindow::connect_module_signals()
{
    UpseModule& module = *_module;
    QObject::connect(_ui.horizontalSlider, &QSlider::valueChanged, &module, &UpseModule::seek);
    QObject::connect(_ui.playButton, &QPushButton::clicked, &module, &UpseModule::toggle_pause);
}

void MainWindow::open_file(QString const& file_name)
{
    shutdown_module();

    _module.emplace(file_name.toStdString());
    _module->moveToThread(&(*_module));

    if (!*_module) {
        QMessageBox::critical(this, "Cannot open file", "Cannot open file");
        return;
    }

    connect_module_signals();
    _module->start();
}

void MainWindow::shutdown_module()
{
    if (_module) {
        QMetaObject::invokeMethod(&(*_module), &UpseModule::shutdown);
        _module->wait();
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

MainWindow::~MainWindow() { shutdown_module(); }
