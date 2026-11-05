#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>

namespace aiws {

/**
 * @brief Receives Remote Add requests on its own std::thread.
 *
 * The handler runs on the server thread and must not wait for the GUI thread.
 * No Qt types appear here, so the class can be tested without a window.
 */
class IpcServer {
public:
    using AddHandler = std::function<void(const std::string& path)>;

    IpcServer(std::string socket_path, AddHandler on_add);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /** @brief Binds the socket and starts the thread. False, with error set, on failure. */
    bool start(std::string& error);

    /** @brief Stops serving, joins the thread, and removes the socket file. Idempotent. */
    void stop();

private:
    void serve();
    void handle_client(int client_fd);
    bool read_exact(int fd, unsigned char* buffer, std::size_t count);

    std::string socket_path_;
    AddHandler on_add_;
    int listen_fd_{-1};  // Set by start() before the thread exists; closed after join.
    std::atomic<bool> stop_requested_{false};
    std::thread thread_;
};

}  // namespace aiws
