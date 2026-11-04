#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>

namespace {

void confirm_exit(QWidget* parent) {
    auto answer = QMessageBox::question(
        parent,
        QObject::tr("Exit"),
        QObject::tr("Are you sure you want to exit AI Workspace?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;
    window.set_exit_callback(confirm_exit);
    window.show();

    return app.exec();
}
