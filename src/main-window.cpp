#include "main-window.h"

#include <fmt/format.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QToolTip>
#include <QWindowStateChangeEvent>
#include <QStyle>

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
    _ui.playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    _ui.stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    for (int i = 0; i < 24; ++i) {
        auto* const widget = new ChannelWidget(_ui.channelsCollapsableContainer);
        widget->ui.title->setText(QString::asprintf("Ch %d", i));
        widget->ui.soundMeterBar->setOrientation(Qt::Orientation::Vertical);
        widget->setFixedWidth(75);
        _ui.channelsLayout->addWidget(widget, i / 8, i % 8);
        _channelWidgets.push_back(widget);

        QObject::connect(
            widget->ui.volumeBar, &QSlider::valueChanged, this, [this, ch = i](int value) {
                QMetaObject::invokeMethod(
                    &_module, &UpseModule::set_channel_vol, ch, value / 100.f);
            });

        QObject::connect(
            widget->ui.muteButton, &QPushButton::toggled, this, [this, ch = i](bool checked) {
                QMetaObject::invokeMethod(&_module, &UpseModule::mute_channel, ch, checked);
                _channelWidgets[ch]->ui.volumeBar->setDisabled(checked);
                _channelWidgets[ch]->ui.title->setDisabled(checked);
            });

        QObject::connect(
            widget->ui.soloButton, &QPushButton::toggled, this, [this, ch = i](bool checked) {
                for (int i = 0; i < 24; ++i) {
                    if (i == ch) {
                        if (checked) {
                            _channelWidgets[i]->ui.muteButton->setChecked(false);
                        }
                        continue;
                    }
                    _channelWidgets[i]->ui.muteButton->setChecked(checked);
                    _channelWidgets[i]->ui.soloButton->blockSignals(true);
                    _channelWidgets[i]->ui.soloButton->setChecked(false);
                    _channelWidgets[i]->ui.soloButton->blockSignals(false);
                }
            });
    }

    this->adjustSize();

    QObject::connect(
        _ui.channelsCollapsableContainer, &KCollapsibleGroupBox::sizeChanged, this, [this] {
            if (!(this->windowState() & Qt::WindowState::WindowMaximized)) {
                auto const expanding = _ui.channelsCollapsableContainer->isExpanded();
                this->resize(QSize{this->size().width(),
                                   expanding
                                       ? std::max(this->sizeHint().height(), this->size().height())
                                       : this->sizeHint().height()});
            }
        });

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
    QObject::connect(_ui.stopButton, &QPushButton::clicked, &_module, &UpseModule::stop);

    QObject::connect(_ui.speedSlider, &QSlider::valueChanged, [this](int position) {
        position *= 5;
        position = std::max(5, position);
        _ui.speedLabel->setText(QString::asprintf("%d%%", position));

        float const multiplier = position / 100.f;
        _ui.seekSlider->setSingleStep(5000 * multiplier);

        QMetaObject::invokeMethod(&_module, &UpseModule::set_speed, multiplier);
    });

    QObject::connect(
        &_module, &UpseModule::total_time_changed, this, &MainWindow::total_time_changed);
    QObject::connect(&_module, &UpseModule::seek_changed, this, &MainWindow::seek_changed);

    static void (*set_all_disabled)(QObject*, bool) = [](QObject* o, bool disabled) {
        if (auto* widget = dynamic_cast<QWidget*>(o)) {
            widget->setDisabled(disabled);
        }
        for (auto* child : o->children()) {
            set_all_disabled(child, disabled);
        }
    };

    using State = UpseModule::State;
    QObject::connect(&_module, &UpseModule::state_changed, this, [this](State new_state) {
        if (new_state == State::Unloaded) {
            set_all_disabled(_ui.seekContainer, true);
            set_all_disabled(_ui.controlsContainer, true);
            return;
        }
        set_all_disabled(_ui.seekContainer, false);
        set_all_disabled(_ui.controlsContainer, false);

        if (new_state == State::Seeking) {
            _cursorTimer.disconnect();
            QObject::connect(&_cursorTimer, &QTimer::timeout, this, [this] {
                this->setCursor(Qt::BusyCursor);
                _ui.remainingTime->setEnabled(false);
            });
            _cursorTimer.setInterval(500);
            _cursorTimer.setSingleShot(true);
            _cursorTimer.start();
            return;
        }

        _cursorTimer.stop();
        this->setCursor(Qt::ArrowCursor);
        _ui.remainingTime->setEnabled(true);

        switch (new_state) {
        case State::Paused:
        case State::Stopped:
            _ui.playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            break;
        case State::Playing:
            _ui.playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
            break;
        case State::Seeking:
        case State::Unloaded:; // Do nothing.
        }
    });

    QObject::connect(
        &_module, &UpseModule::sound_level_changed, _ui.soundMeterBar, &SoundMeterBar::set_level);

    QObject::connect(
        &_module, &UpseModule::channel_sound_level_changed, [this](int channel, float l, float r) {
            QMetaObject::invokeMethod(
                _channelWidgets[channel]->ui.soundMeterBar, &SoundMeterBar::set_level, l, r);
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

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    auto const key = event->key();
    if (key == Qt::Key_Space) {
        event->setAccepted(true);
        emit _ui.playButton->clicked();
    } else if (key == Qt::Key_Left) {
        event->setAccepted(true);
        _ui.seekSlider->triggerAction(QSlider::SliderSingleStepSub);
    } else if (key == Qt::Key_Right) {
        event->setAccepted(true);
        _ui.seekSlider->triggerAction(QSlider::SliderSingleStepAdd);
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange) {
        if (this->windowState() & Qt::WindowState::WindowMaximized) {
            _ui.channelsCollapsableContainer->setExpanded(true);
        } else {
            this->resize(QSize{this->size().width(), this->sizeHint().height()});
        }
    }
}

MainWindow::~MainWindow()
{
    QMetaObject::invokeMethod(&_module, &UpseModule::shutdown);
    _module.wait();
}
