#pragma once

#include "ui_main-window.h"
#include "upse.h"
#include "channel-widget.h"

#include <QMainWindow>

#include <vector>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void total_time_changed(std::chrono::milliseconds ms);
    void seek_changed(std::chrono::milliseconds ms);

private:
    void load_file(QString const& file_name);
    void connect_module_signals();

    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;

    Ui::MainWindow _ui;
    UpseModule _module;
    bool _movingSlider{};
    std::vector<ChannelWidget*> _channelWidgets;
};
