#pragma once

// Wire format shared by the application and tools/aiws_send.cpp.
//
// Request: 4-byte big-endian N (1..65535), then N bytes: absolute path, UTF-8.
// Reply:   1 status byte (0 accepted, 1 rejected), 4-byte big-endian M
//          (0..4096), then M bytes: UTF-8 reason. M is 0 when accepted.

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>

namespace aiws::ipc {

constexpr std::uint32_t kMaxPathBytes = 65535;
constexpr std::uint32_t kMaxReasonBytes = 4096;
constexpr unsigned char kStatusAccepted = 0;
constexpr unsigned char kStatusRejected = 1;

inline void append_u32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

inline std::uint32_t read_u32(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

inline std::string default_socket_path() {
    const char* user = std::getenv("USER");
    if (user != nullptr && user[0] != '\0') {
        return "/tmp/aiws-" + std::string(user) + ".sock";
    }
    return "/tmp/aiws-" + std::to_string(::getuid()) + ".sock";
}

}  // namespace aiws::ipc
