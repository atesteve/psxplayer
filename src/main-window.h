#pragma once

#include "ui_main-window.h"
#include "upse.h"
#include "channel-widget.h"

#include <QTimer>
#include <QWindow>

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

    void keyPressEvent(QKeyEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void changeEvent(QEvent* event) override;

    Ui::MainWindow _ui;
    UpseModule _module;
    QTimer _cursorTimer;
    bool _movingSlider{};
    std::vector<ChannelWidget*> _channelWidgets;
};
