// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui_main-window.h"
#include "upse.h"
#include "channel-widget.h"

#include <QTimer>
#include <QWindow>
#include <QShortcut>

#include <vector>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

public slots:
    void create_channels(int number_of_channels);

private slots:
    void total_time_changed(std::chrono::milliseconds ms);
    void seek_changed(std::chrono::milliseconds ms);

private:
    void load_file(QString const& file_name);
    void connect_module_signals();

    // void keyPressEvent(QKeyEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void changeEvent(QEvent* event) override;

    Ui::MainWindow _ui;
    UpseModule _module;
    QTimer _cursorTimer;
    bool _movingSlider{};
    std::vector<std::unique_ptr<ChannelWidget>> _channelWidgets;
    std::vector<std::unique_ptr<QShortcut>> _shortcuts;
};
