// aiws_send [--socket PATH] add FILE
// Exit: 0 accepted and readable, 1 rejected, 2 no application or communication failure.

#include "aiws/ipc_protocol.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr int kExitOk = 0;
constexpr int kExitRejected = 1;
constexpr int kExitNoApplication = 2;

int remaining_ms(Clock::time_point deadline) {
    auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
    return left.count() > 0 ? static_cast<int>(left.count()) : 0;
}

// Retries so a just-started application still counts; stays inside the 2 s limit.
int connect_with_deadline(const std::string& path, Clock::time_point deadline) {
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path)) {
        return -1;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

    while (true) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) {
            return -1;
        }
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&address),
                      sizeof(address)) == 0) {
            return fd;
        }
        ::close(fd);
        if (remaining_ms(deadline) == 0) {
            return -1;
        }
        std::this_thread::sleep_for(milliseconds(50));
    }
}

bool wait_for(int fd, short events, Clock::time_point deadline) {
    while (true) {
        pollfd waiting{fd, events, 0};
        int ready = ::poll(&waiting, 1, remaining_ms(deadline));
        if (ready > 0) {
            return true;
        }
        if (ready == 0 || errno != EINTR) {
            return false;
        }
    }
}

bool send_all(int fd, const std::string& data, Clock::time_point deadline) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (!wait_for(fd, POLLOUT, deadline)) {
            return false;
        }
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_exact(int fd, unsigned char* buffer, std::size_t count,
                Clock::time_point deadline) {
    std::size_t received = 0;
    while (received < count) {
        if (!wait_for(fd, POLLIN, deadline)) {
            return false;
        }
        ssize_t n = ::recv(fd, buffer + received, count - received, 0);
        if (n == 0) {
            return false;
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        }
        received += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = aiws::ipc::default_socket_path();
    int next = 1;
    if (next + 1 < argc && std::string(argv[next]) == "--socket") {
        socket_path = argv[next + 1];
        next += 2;
    }
    if (argc - next != 2 || std::string(argv[next]) != "add" ||
        argv[next + 1][0] == '\0') {
        std::cerr << "usage: aiws_send [--socket PATH] add FILE\n";
        return kExitNoApplication;
    }

    // A relative FILE means relative to aiws_send's directory, which the
    // application cannot know, so it is resolved here (E1).
    std::string path = argv[next + 1];
    if (path.front() != '/') {
        std::vector<char> cwd(65536);
        if (::getcwd(cwd.data(), cwd.size()) == nullptr) {
            std::cerr << "aiws_send: cannot read working directory\n";
            return kExitNoApplication;
        }
        path = std::string(cwd.data()) + "/" + path;
    }
    if (path.size() > aiws::ipc::kMaxPathBytes) {
        std::cerr << "aiws_send: path is too long\n";
        return kExitRejected;
    }

    // CR-1 limits: 2 s to reach the application, 5 s overall. Margins cover exit.
    const auto start = Clock::now();
    int fd = connect_with_deadline(socket_path, start + milliseconds(1800));
    if (fd < 0) {
        std::cerr << "aiws_send: no application at " << socket_path << "\n";
        return kExitNoApplication;
    }

    const auto deadline = start + milliseconds(4800);
    std::string request;
    aiws::ipc::append_u32(request, static_cast<std::uint32_t>(path.size()));
    request += path;

    unsigned char head[5];
    std::string reason;
    bool ok = send_all(fd, request, deadline) &&
              recv_exact(fd, head, sizeof(head), deadline);
    if (ok) {
        const std::uint32_t length = aiws::ipc::read_u32(head + 1);
        if (length > aiws::ipc::kMaxReasonBytes) {
            ok = false;
        } else {
            std::vector<unsigned char> bytes(length);
            ok = length == 0 || recv_exact(fd, bytes.data(), bytes.size(), deadline);
            reason.assign(bytes.begin(), bytes.end());
        }
    }
    ::close(fd);

    if (!ok) {
        std::cerr << "aiws_send: communication failed\n";
        return kExitNoApplication;
    }
    if (head[0] != aiws::ipc::kStatusAccepted) {
        std::cerr << "aiws_send: rejected: " << reason << "\n";
        return kExitRejected;
    }
    return kExitOk;
}
