#pragma once

#include <QMainWindow>

#include <memory>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace changeos {
class MonitorEngine;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggle_monitoring();
    void refresh_events();
    void clear_events();

private:
    void setup_ui();
    void update_status();

    std::unique_ptr<changeos::MonitorEngine> engine_;

    QPushButton* toggle_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;
    QListWidget* event_list_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* refresh_timer_ = nullptr;

    bool monitoring_ = false;
};
