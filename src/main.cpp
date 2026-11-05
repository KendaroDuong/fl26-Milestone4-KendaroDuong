#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QObject>
#include <QString>

#include <csignal>
#include <iostream>

namespace {

volatile std::sig_atomic_t termination_requested = 0;

void request_termination(int) {
    termination_requested = 1;
}

// SIGTERM/SIGINT quit through the normal path, so the socket file is removed.
class TerminationWatcher final : public QObject {
public:
    TerminationWatcher() { startTimer(100); }

protected:
    void timerEvent(QTimerEvent*) override {
        if (termination_requested != 0) {
            QApplication::quit();
        }
    }
};

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

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption delay_option(
        "build-delay-ms", "Extra build time per document, in ms.", "N", "0");
    QCommandLineOption socket_option(
        "ipc-socket", "Socket for Remote Add requests.", "PATH",
        QString::fromStdString(aiws::ipc::default_socket_path()));
    parser.addOption(delay_option);
    parser.addOption(socket_option);
    parser.process(app);

    bool delay_ok = false;
    const int delay_ms = parser.value(delay_option).toInt(&delay_ok);
    if (!delay_ok || delay_ms < 0) {
        std::cerr << "--build-delay-ms needs a non-negative integer\n";
        return 2;
    }

    std::signal(SIGTERM, request_termination);
    std::signal(SIGINT, request_termination);
    TerminationWatcher watcher;

    aiws::gui::MainWindow window;
    window.set_exit_callback(confirm_exit);
    window.set_build_delay_ms(delay_ms);

    QString ipc_error;
    if (!window.start_ipc_server(parser.value(socket_option), &ipc_error)) {
        std::cerr << "Remote Add is unavailable: "
                  << ipc_error.toStdString() << "\n";
    }

    window.show();
    return app.exec();
}
