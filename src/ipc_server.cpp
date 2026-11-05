#include "aiws/ipc_server.hpp"

#include "aiws/ipc_protocol.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

namespace aiws {

namespace {

constexpr int kPollSliceMs = 100;                      // Bounds stop latency (E4).
constexpr std::chrono::seconds kClientIdleTimeout{3};  // Bounds a silent client.

bool fill_address(const std::string& path, sockaddr_un& address) {
    if (path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

// True if a live server already answers on this socket.
bool socket_is_live(const sockaddr_un& address) {
    int probe = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (probe < 0) {
        return false;
    }
    bool live = ::connect(probe, reinterpret_cast<const sockaddr*>(&address),
                          sizeof(address)) == 0;
    ::close(probe);
    return live;
}

// Empty means readable. Never blocks: O_NONBLOCK guards against a FIFO.
std::string why_unreadable(const std::string& path) {
    if (path.empty() || path.front() != '/' ||
        path.find('\0') != std::string::npos) {
        return "invalid path";
    }
    int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return std::strerror(errno);
    }
    struct stat info {};
    std::string reason;
    if (::fstat(fd, &info) != 0) {
        reason = std::strerror(errno);
    } else if (!S_ISREG(info.st_mode)) {
        reason = "not a regular file";
    }
    ::close(fd);
    return reason;
}

void send_reply(int fd, unsigned char status, std::string reason) {
    if (reason.size() > ipc::kMaxReasonBytes) {
        reason.resize(ipc::kMaxReasonBytes);
    }
    std::string reply;
    reply.push_back(static_cast<char>(status));
    ipc::append_u32(reply, static_cast<std::uint32_t>(reason.size()));
    reply += reason;

    // The reply fits in the socket buffer. MSG_NOSIGNAL: a vanished client
    // must not kill the application with SIGPIPE.
    std::size_t sent = 0;
    while (sent < reply.size()) {
        ssize_t n = ::send(fd, reply.data() + sent, reply.size() - sent,
                           MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(n);
    }
}

}  // namespace

IpcServer::IpcServer(std::string socket_path, AddHandler on_add)
    : socket_path_(std::move(socket_path)), on_add_(std::move(on_add)) {}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start(std::string& error) {
    sockaddr_un address{};
    if (!fill_address(socket_path_, address)) {
        error = "socket path is too long";
        return false;
    }

    if (socket_is_live(address)) {
        error = "another application is serving " + socket_path_;
        return false;
    }

    // A leftover socket from a crashed run is safe to remove. Anything else
    // at this path is not ours, so bind() will fail and report it.
    struct stat info {};
    if (::lstat(socket_path_.c_str(), &info) == 0 && S_ISSOCK(info.st_mode)) {
        ::unlink(socket_path_.c_str());
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(listen_fd_, 16) != 0) {
        error = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    ::chmod(socket_path_.c_str(), 0600);

    thread_ = std::thread(&IpcServer::serve, this);
    return true;
}

void IpcServer::stop() {
    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(socket_path_.c_str());
    }
}

void IpcServer::serve() {
    while (!stop_requested_.load()) {
        pollfd waiting{listen_fd_, POLLIN, 0};
        int ready = ::poll(&waiting, 1, kPollSliceMs);
        if (ready <= 0) {
            continue;  // Timeout or EINTR: re-check the stop flag.
        }
        int client = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client >= 0) {
            handle_client(client);
            ::close(client);
        }
    }
}

// Waits only in short slices, so stop() is honored even mid-request.
// Reassembles data that arrives in pieces (E3).
bool IpcServer::read_exact(int fd, unsigned char* buffer, std::size_t count) {
    auto last_data = std::chrono::steady_clock::now();
    std::size_t received = 0;

    while (received < count) {
        if (stop_requested_.load() ||
            std::chrono::steady_clock::now() - last_data > kClientIdleTimeout) {
            return false;
        }
        pollfd waiting{fd, POLLIN, 0};
        if (::poll(&waiting, 1, kPollSliceMs) <= 0) {
            continue;
        }
        ssize_t n = ::recv(fd, buffer + received, count - received, 0);
        if (n == 0) {
            return false;  // Client closed early.
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        received += static_cast<std::size_t>(n);
        last_data = std::chrono::steady_clock::now();
    }
    return true;
}

void IpcServer::handle_client(int client_fd) {
    unsigned char header[4];
    if (!read_exact(client_fd, header, sizeof(header))) {
        return;
    }

    const std::uint32_t length = ipc::read_u32(header);
    if (length == 0 || length > ipc::kMaxPathBytes) {
        send_reply(client_fd, ipc::kStatusRejected, "invalid path length");
        return;
    }

    std::vector<unsigned char> bytes(length);
    if (!read_exact(client_fd, bytes.data(), bytes.size())) {
        return;
    }
    const std::string path(bytes.begin(), bytes.end());

    const std::string reason = why_unreadable(path);
    if (!reason.empty()) {
        send_reply(client_fd, ipc::kStatusRejected, reason);
        return;
    }

    on_add_(path);  // Queues the add for the GUI thread; does not wait for it.
    send_reply(client_fd, ipc::kStatusAccepted, "");
}

}  // namespace aiws
