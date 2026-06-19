#include "gui/main_window.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("change-of-system");
    app.setApplicationDisplayName("change-of-system");

    MainWindow window;
    window.show();

    return app.exec();
}
