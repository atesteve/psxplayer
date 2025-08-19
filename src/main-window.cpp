#include "main-window.h"

#include <fmt/format.h>
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

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
