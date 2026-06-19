#include "gui/main_window.h"

#include "core/event.h"
#include "core/monitor_engine.h"
#include "platform/platform_detection.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    engine_ = std::make_unique<changeos::MonitorEngine>();
    setup_ui();
}

MainWindow::~MainWindow() {
    if (engine_ && engine_->is_running()) engine_->stop_all();
}

void MainWindow::setup_ui() {
    setWindowTitle("change-of-system — Realtime System Change Monitor");
    resize(1100, 700);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* main_layout = new QVBoxLayout(central);

    auto* header = new QLabel(this);
    header->setText(QString::fromStdString(
        "Platform: " + changeos::platform::name() + " ("
        + changeos::platform::architecture() + ")   |   Host: "
        + changeos::platform::hostname() + "   |   User: "
        + changeos::platform::username()));
    header->setStyleSheet("padding: 6px; background: #222; color: #eee;");
    main_layout->addWidget(header);

    auto* button_row = new QHBoxLayout();
    toggle_button_ = new QPushButton("Start Monitoring", this);
    clear_button_ = new QPushButton("Clear", this);
    button_row->addWidget(toggle_button_);
    button_row->addWidget(clear_button_);
    button_row->addStretch(1);
    main_layout->addLayout(button_row);

    event_list_ = new QListWidget(this);
    event_list_->setSelectionMode(QAbstractItemView::NoSelection);
    main_layout->addWidget(event_list_, 1);

    status_label_ = new QLabel("Idle", this);
    status_label_->setStyleSheet("padding: 4px; color: #555;");
    main_layout->addWidget(status_label_);

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(750);

    connect(toggle_button_, &QPushButton::clicked,
            this, &MainWindow::toggle_monitoring);
    connect(clear_button_, &QPushButton::clicked,
            this, &MainWindow::clear_events);
    connect(refresh_timer_, &QTimer::timeout,
            this, &MainWindow::refresh_events);
}

void MainWindow::toggle_monitoring() {
    if (!monitoring_) {
        if (!engine_->initialize({})) {
            QMessageBox::warning(this, "Error",
                                 "Failed to initialize engine.");
            return;
        }
        engine_->start_all();
        toggle_button_->setText("Stop Monitoring");
        refresh_timer_->start();
        monitoring_ = true;
    } else {
        engine_->stop_all();
        refresh_timer_->stop();
        toggle_button_->setText("Start Monitoring");
        monitoring_ = false;
    }
    update_status();
}

void MainWindow::refresh_events() {
    auto events = engine_->recent_events(500);
    int displayed = event_list_->count();

    for (int i = displayed; i < static_cast<int>(events.size()); ++i) {
        const auto& e = events[static_cast<std::size_t>(i)];
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            e.timestamp.time_since_epoch()).count();
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(ms));
        QString line = QString("[%1] [%2] [%3] %4")
            .arg(dt.toString("yyyy-MM-dd hh:mm:ss"))
            .arg(QString::fromStdString(changeos::category_name(e.category)))
            .arg(QString::fromStdString(changeos::type_name(e.type)))
            .arg(QString::fromStdString(e.summary));
        event_list_->addItem(line);
    }

    while (event_list_->count() > 1000) {
        delete event_list_->takeItem(0);
    }
    event_list_->scrollToBottom();
    update_status();
}

void MainWindow::clear_events() {
    event_list_->clear();
}

void MainWindow::update_status() {
    QString status = monitoring_
        ? QString("Monitoring — %1 events displayed").arg(event_list_->count())
        : QString("Idle — %1 events displayed").arg(event_list_->count());
    status_label_->setText(status);
}
