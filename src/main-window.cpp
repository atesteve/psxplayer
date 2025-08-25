#include "main-window.h"

#include <fmt/format.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QToolTip>

#include <filesystem>

namespace {

QString ms_to_string(std::chrono::milliseconds ms)
{
    using namespace std::chrono;

    int min = duration_cast<minutes>(ms).count();
    int secs = duration_cast<seconds>(ms).count() % 60;

    return QString{fmt::format("{:02}:{:02}", min, secs).c_str()};
}

} // namespace

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

    QObject::connect(
        _ui.seekSlider, &QSlider::sliderPressed, this, [this] { _movingSlider = true; });
    QObject::connect(
        _ui.seekSlider, &QSlider::sliderReleased, this, [this] { _movingSlider = false; });

    QObject::connect(_ui.seekSlider, &QSlider::sliderMoved, [](int position) {
        QToolTip::showText(
            QCursor::pos(), ms_to_string(std::chrono::milliseconds{position}), nullptr);
    });
    QObject::connect(_ui.seekSlider, &QSliderMouseEvent::mouseMoved, [](int position) {
        QToolTip::showText(
            QCursor::pos(), ms_to_string(std::chrono::milliseconds{position}), nullptr);
    });

    QObject::connect(_ui.speedSlider, &QSlider::sliderMoved, [](int position) {
        QToolTip::showText(QCursor::pos(), QString::asprintf("%d%%", position * 5), nullptr);
    });
    QObject::connect(_ui.speedSlider, &QSliderMouseEvent::mouseMoved, [](int position) {
        QToolTip::showText(QCursor::pos(), QString::asprintf("%d%%", position * 5), nullptr);
    });

    connect_module_signals();
    _module.start();
}

void MainWindow::connect_module_signals()
{
    QObject::connect(_ui.seekSlider, &QSlider::valueChanged, &_module, &UpseModule::seek);
    QObject::connect(_ui.playButton, &QPushButton::clicked, &_module, &UpseModule::toggle_pause);

    QObject::connect(
        &_module, &UpseModule::total_time_changed, this, &MainWindow::total_time_changed);
    QObject::connect(&_module, &UpseModule::seek_changed, this, &MainWindow::seek_changed);

    QObject::connect(_ui.speedSlider, &QSlider::valueChanged, [this](int position) {
        position *= 5;
        position = std::max(5, position);
        _ui.speedLabel->setText(QString::asprintf("%d%%", position));

        float const multiplier = position / 100.f;
        _ui.seekSlider->setSingleStep(5000 * multiplier);

        QMetaObject::invokeMethod(&_module, &UpseModule::set_speed, multiplier);
    });
}

void MainWindow::load_file(QString const& file_name)
{
    this->setWindowTitle(QString::fromStdString(
        std::filesystem::path{file_name.toStdString()}.filename().string() + " - PSXPlayer"));

    QMetaObject::invokeMethod(
        &_module, &UpseModule::load_file, Qt::ConnectionType::BlockingQueuedConnection, file_name);

    if (!_module) {
        QMessageBox::critical(this, "Cannot open file", "Cannot open file");
        return;
    }
}

void MainWindow::total_time_changed(std::chrono::milliseconds ms)
{
    _ui.seekSlider->blockSignals(true);
    _ui.seekSlider->setSingleStep(5000);
    _ui.seekSlider->setPageStep(10000);
    _ui.seekSlider->setMaximum(ms.count());
    _ui.seekSlider->setTickInterval(ms.count() / 4);
    _ui.seekSlider->blockSignals(false);

    _ui.totalTime->setText(ms_to_string(ms));
}

void MainWindow::seek_changed(std::chrono::milliseconds ms)
{
    if (_movingSlider) {
        return;
    }

    _ui.seekSlider->blockSignals(true);
    _ui.seekSlider->setValue(ms.count());
    _ui.seekSlider->blockSignals(false);
    _ui.remainingTime->setText(ms_to_string(ms));
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
