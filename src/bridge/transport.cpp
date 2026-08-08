#include "bridge/transport.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace bridge {
namespace {

constexpr std::size_t kReadChunk = 4096;

// A safety case can be large, and a single `get_argument_tree` reply carries a
// lot of JSON. This is not a protocol limit so much as a refusal to grow a
// buffer without bound on a peer that never sends a newline.
//
// Written in the destination type. `64u * 1024u * 1024u` multiplied in
// `unsigned int` and widened only on assignment, and unsigned overflow wraps
// silently rather than being diagnosed: raising the 64 to 8192 would have made
// this constant exactly 0, and a 0-byte limit rejects every message. The
// product fits today. Spelling the type is what stops that being something to
// re-check on each edit.
constexpr std::size_t kMaxMessageBytes = std::size_t{64} * 1024 * 1024;

} // namespace

// ---------------------------------------------------------------------------
// Windows: named pipes.
// ---------------------------------------------------------------------------
#ifdef _WIN32

namespace {

// The listening handle is created overlapped so a blocked `ConnectNamedPipe`
// can be woken by an event when the application shuts down. An overlapped
// handle stays overlapped once connected, so every read and write on it must
// pass an OVERLAPPED too -- hence the client opens overlapped as well, and both
// ends share one I/O path rather than two that drift.
bool OverlappedIo(HANDLE handle, HANDLE io_event, void* buffer, DWORD size, DWORD& transferred, bool writing) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = io_event;
    ResetEvent(io_event);

    const BOOL started = writing ? WriteFile(handle, buffer, size, nullptr, &overlapped)
                                 : ReadFile(handle, buffer, size, nullptr, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    return GetOverlappedResult(handle, &overlapped, &transferred, TRUE) != FALSE;
}

std::string LastErrorText(const char* what) {
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ").";
}

} // namespace

struct Connection::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE io_event = nullptr;
    std::string pending;

    ~Impl() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
        if (io_event != nullptr) {
            CloseHandle(io_event);
        }
    }
};

struct Listener::Impl {
    std::string address;
    HANDLE stop_event = nullptr;
    // The instance currently waiting for a client. One always exists between
    // `Start` and the connection that consumes it, so "the listener started"
    // and "a client can connect" mean the same thing. Creating it lazily inside
    // `Accept` instead left a window in which a client connecting immediately
    // after startup got ERROR_FILE_NOT_FOUND.
    HANDLE pending = INVALID_HANDLE_VALUE;
    std::atomic<bool> stopping{false};

    ~Impl() {
        if (pending != INVALID_HANDLE_VALUE) {
            CloseHandle(pending);
        }
        if (stop_event != nullptr) {
            CloseHandle(stop_event);
        }
    }
};

namespace {

// Security: a NULL descriptor gives the pipe the creating token's default DACL,
// which grants the creating user and SYSTEM and denies other interactive users.
// That is the access control; the per-connection token in `bridge/protocol.h` is
// the second gate, against another process running as the same user.
HANDLE CreatePipeInstance(const std::string& address) {
    return CreateNamedPipeA(address.c_str(),
                            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                            PIPE_UNLIMITED_INSTANCES,
                            static_cast<DWORD>(kReadChunk),
                            static_cast<DWORD>(kReadChunk),
                            0,
                            nullptr);
}

} // namespace

std::unique_ptr<Connection> Connection::Connect(const std::string& address, std::string& error) {
    error.clear();

    // A listener always has one instance waiting, but it replaces that instance
    // *after* a connection is handed off, so a second client arriving in that
    // instant sees no instance at all. Both outcomes are momentary and worth a
    // short retry; neither is worth hanging on, because "no application running"
    // is the ordinary case for the adapter and must stay fast.
    constexpr int kAttempts = 5;
    constexpr DWORD kBackoffMs = 20;

    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        handle = CreateFileA(
            address.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }

        const DWORD status = GetLastError();
        if (status == ERROR_PIPE_BUSY) {
            // Every instance is serving someone else. This is the one case worth
            // waiting on properly.
            if (!WaitNamedPipeA(address.c_str(), 2000)) {
                error = "Assurance Forge is listening but did not accept a connection in time.";
                return nullptr;
            }
            continue;
        }
        if (status != ERROR_FILE_NOT_FOUND || attempt + 1 == kAttempts) {
            error = "No Assurance Forge is listening on " + address + ".";
            return nullptr;
        }
        Sleep(kBackoffMs);
    }
    if (handle == INVALID_HANDLE_VALUE) {
        error = "No Assurance Forge is listening on " + address + ".";
        return nullptr;
    }

    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->handle = handle;
    impl->io_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (impl->io_event == nullptr) {
        error = LastErrorText("CreateEvent");
        return nullptr;
    }
    return std::unique_ptr<Connection>(new Connection(std::move(impl)));
}

std::unique_ptr<Listener> Listener::Start(const std::string& address, std::string& error) {
    error.clear();

    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->address = address;
    impl->stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (impl->stop_event == nullptr) {
        error = LastErrorText("CreateEvent");
        return nullptr;
    }
    impl->pending = CreatePipeInstance(address);
    if (impl->pending == INVALID_HANDLE_VALUE) {
        error = LastErrorText("CreateNamedPipe");
        return nullptr;
    }
    return std::unique_ptr<Listener>(new Listener(std::move(impl)));
}

std::unique_ptr<Connection> Listener::Accept(std::string& error) {
    error.clear();
    if (impl_->stopping.load()) {
        return nullptr;
    }

    // Consume the waiting instance and put a fresh one in its place before
    // returning, so the address never stops being connectable.
    const HANDLE pipe = impl_->pending;
    impl_->pending = INVALID_HANDLE_VALUE;
    if (pipe == INVALID_HANDLE_VALUE) {
        error = "The bridge listener has no pipe instance.";
        return nullptr;
    }

    const HANDLE connect_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (connect_event == nullptr) {
        error = LastErrorText("CreateEvent");
        CloseHandle(pipe);
        return nullptr;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = connect_event;

    bool connected = ConnectNamedPipe(pipe, &overlapped) != FALSE;
    if (!connected) {
        const DWORD status = GetLastError();
        if (status == ERROR_PIPE_CONNECTED) {
            // A client won the race between CreateNamedPipe and ConnectNamedPipe.
            connected = true;
        } else if (status == ERROR_IO_PENDING) {
            const HANDLE waits[] = {connect_event, impl_->stop_event};
            const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (which == WAIT_OBJECT_0) {
                DWORD ignored = 0;
                connected = GetOverlappedResult(pipe, &overlapped, &ignored, FALSE) != FALSE;
            } else {
                // Stopped, or the wait itself failed. Either way this listener is
                // finished; an empty `error` marks the clean case.
                CancelIo(pipe);
                CloseHandle(connect_event);
                CloseHandle(pipe);
                if (which != WAIT_OBJECT_0 + 1) {
                    error = LastErrorText("WaitForMultipleObjects");
                }
                return nullptr;
            }
        } else {
            error = LastErrorText("ConnectNamedPipe");
            CloseHandle(connect_event);
            CloseHandle(pipe);
            return nullptr;
        }
    }

    CloseHandle(connect_event);
    if (!connected) {
        CloseHandle(pipe);
        error = LastErrorText("ConnectNamedPipe");
        return nullptr;
    }

    impl_->pending = CreatePipeInstance(impl_->address);

    std::unique_ptr<Connection::Impl> impl = std::make_unique<Connection::Impl>();
    impl->handle = pipe;
    impl->io_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (impl->io_event == nullptr) {
        error = LastErrorText("CreateEvent");
        return nullptr;
    }
    return std::unique_ptr<Connection>(new Connection(std::move(impl)));
}

void Listener::Stop() {
    impl_->stopping.store(true);
    if (impl_->stop_event != nullptr) {
        SetEvent(impl_->stop_event);
    }
}

bool Connection::ReadMessage(std::string& out_message) {
    out_message.clear();
    while (true) {
        const std::size_t newline = impl_->pending.find('\n');
        if (newline != std::string::npos) {
            out_message = impl_->pending.substr(0, newline);
            impl_->pending.erase(0, newline + 1);
            if (!out_message.empty() && out_message.back() == '\r') {
                out_message.pop_back();
            }
            return true;
        }
        if (impl_->pending.size() > kMaxMessageBytes) {
            return false;
        }
        if (impl_->handle == INVALID_HANDLE_VALUE) {
            return false;
        }

        char buffer[kReadChunk];
        DWORD read = 0;
        if (!OverlappedIo(impl_->handle, impl_->io_event, buffer, static_cast<DWORD>(kReadChunk), read, false) ||
            read == 0) {
            return false;
        }
        impl_->pending.append(buffer, read);
    }
}

bool Connection::WriteMessage(const std::string& message) {
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::string framed = message;
    framed.push_back('\n');

    std::size_t offset = 0;
    while (offset < framed.size()) {
        DWORD written = 0;
        if (!OverlappedIo(impl_->handle,
                          impl_->io_event,
                          framed.data() + offset,
                          static_cast<DWORD>(framed.size() - offset),
                          written,
                          true) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

void Connection::Close() {
    if (impl_->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
}

// ---------------------------------------------------------------------------
// POSIX: AF_UNIX stream sockets.
// ---------------------------------------------------------------------------
#else

namespace {

bool FillSocketAddress(const std::string& address, sockaddr_un& out, std::string& error) {
    std::memset(&out, 0, sizeof(out));
    out.sun_family = AF_UNIX;
    if (address.size() >= sizeof(out.sun_path)) {
        error = "The bridge socket path is too long for this platform: " + address;
        return false;
    }
    std::memcpy(out.sun_path, address.c_str(), address.size());
    return true;
}

std::string ErrnoText(const char* what) {
    return std::string(what) + " failed: " + std::strerror(errno);
}

} // namespace

struct Connection::Impl {
    int fd = -1;
    std::string pending;

    ~Impl() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

struct Listener::Impl {
    int fd = -1;
    int stop_read = -1;
    int stop_write = -1;
    std::string address;
    std::atomic<bool> stopping{false};

    ~Impl() {
        if (fd >= 0) {
            ::close(fd);
        }
        if (stop_read >= 0) {
            ::close(stop_read);
        }
        if (stop_write >= 0) {
            ::close(stop_write);
        }
        if (!address.empty()) {
            ::unlink(address.c_str());
        }
    }
};

std::unique_ptr<Connection> Connection::Connect(const std::string& address, std::string& error) {
    error.clear();

    sockaddr_un target{};
    if (!FillSocketAddress(address, target, error)) {
        return nullptr;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = ErrnoText("socket");
        return nullptr;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0) {
        ::close(fd);
        error = "No Assurance Forge is listening on " + address + ".";
        return nullptr;
    }

    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->fd = fd;
    return std::unique_ptr<Connection>(new Connection(std::move(impl)));
}

std::unique_ptr<Listener> Listener::Start(const std::string& address, std::string& error) {
    error.clear();

    sockaddr_un bound{};
    if (!FillSocketAddress(address, bound, error)) {
        return nullptr;
    }

    // The runtime directory is created lazily on first use, and `bind` will not
    // create it. A Windows pipe name has no such parent, hence the platform
    // asymmetry.
    const std::filesystem::path parent = std::filesystem::path(address).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    // A socket file left by a crashed run would make bind fail with EADDRINUSE
    // forever. Nothing is listening on it -- the process is gone -- so removing
    // it is safe and is what every unix daemon does here.
    ::unlink(address.c_str());

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = ErrnoText("socket");
        return nullptr;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)) != 0) {
        error = ErrnoText("bind");
        ::close(fd);
        return nullptr;
    }
    // Owner only. The containing directory is already user-private, and this
    // says so on the socket itself.
    ::chmod(address.c_str(), S_IRUSR | S_IWUSR);
    if (::listen(fd, 4) != 0) {
        error = ErrnoText("listen");
        ::close(fd);
        ::unlink(address.c_str());
        return nullptr;
    }

    int stop_pipe[2] = {-1, -1};
    if (::pipe(stop_pipe) != 0) {
        error = ErrnoText("pipe");
        ::close(fd);
        ::unlink(address.c_str());
        return nullptr;
    }

    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->fd = fd;
    impl->stop_read = stop_pipe[0];
    impl->stop_write = stop_pipe[1];
    impl->address = address;
    return std::unique_ptr<Listener>(new Listener(std::move(impl)));
}

std::unique_ptr<Connection> Listener::Accept(std::string& error) {
    error.clear();
    while (!impl_->stopping.load()) {
        // `accept` cannot be interrupted portably, so the wait happens in `poll`
        // over both the listening socket and a self-pipe `Stop` writes to.
        pollfd waits[2]{};
        waits[0].fd = impl_->fd;
        waits[0].events = POLLIN;
        waits[1].fd = impl_->stop_read;
        waits[1].events = POLLIN;

        const int ready = ::poll(waits, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = ErrnoText("poll");
            return nullptr;
        }
        if ((waits[1].revents & POLLIN) != 0) {
            return nullptr;
        }
        if ((waits[0].revents & POLLIN) == 0) {
            continue;
        }

        const int accepted = ::accept(impl_->fd, nullptr, nullptr);
        if (accepted < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            error = ErrnoText("accept");
            return nullptr;
        }

        std::unique_ptr<Connection::Impl> impl = std::make_unique<Connection::Impl>();
        impl->fd = accepted;
        return std::unique_ptr<Connection>(new Connection(std::move(impl)));
    }
    return nullptr;
}

void Listener::Stop() {
    impl_->stopping.store(true);
    if (impl_->stop_write >= 0) {
        const char wake = 1;
        // Best effort: a full self-pipe already carries an unread wake-up.
        [[maybe_unused]] const ssize_t ignored = ::write(impl_->stop_write, &wake, 1);
    }
}

bool Connection::ReadMessage(std::string& out_message) {
    out_message.clear();
    while (true) {
        const std::size_t newline = impl_->pending.find('\n');
        if (newline != std::string::npos) {
            out_message = impl_->pending.substr(0, newline);
            impl_->pending.erase(0, newline + 1);
            if (!out_message.empty() && out_message.back() == '\r') {
                out_message.pop_back();
            }
            return true;
        }
        if (impl_->pending.size() > kMaxMessageBytes) {
            return false;
        }
        if (impl_->fd < 0) {
            return false;
        }

        char buffer[kReadChunk];
        const ssize_t read = ::read(impl_->fd, buffer, kReadChunk);
        if (read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (read == 0) {
            return false;
        }
        impl_->pending.append(buffer, static_cast<std::size_t>(read));
    }
}

bool Connection::WriteMessage(const std::string& message) {
    if (impl_->fd < 0) {
        return false;
    }
    std::string framed = message;
    framed.push_back('\n');

    std::size_t offset = 0;
    while (offset < framed.size()) {
        const ssize_t written = ::write(impl_->fd, framed.data() + offset, framed.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

void Connection::Close() {
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
}

#endif

// ---------------------------------------------------------------------------
// Shared.
// ---------------------------------------------------------------------------

Connection::Connection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Connection::~Connection() = default;

Listener::Listener(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Listener::~Listener() {
    Stop();
}

const std::string& Listener::address() const {
    return impl_->address;
}

} // namespace bridge
