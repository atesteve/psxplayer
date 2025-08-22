#pragma once

#include "ui_main-window.h"
#include "upse.h"

#include <QMainWindow>

#include <optional>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    void open_file(QString const& file_name);
    void connect_module_signals();
    void shutdown_module();

    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;

    Ui::MainWindow _ui;
    std::optional<UpseModule> _module;
    int _clips{};
};
