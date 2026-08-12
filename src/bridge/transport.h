#pragma once

// A local, user-private, message-framed connection between two processes on one
// machine. Windows named pipe; POSIX AF_UNIX stream socket.
//
// No TCP, on purpose. A loopback port is reachable by every process on the
// machine, shows up in `netstat`, and in a regulated environment invites the
// question of what a safety-case tool is doing listening on a socket. A named
// pipe and a unix socket are both access-controlled by the operating system to
// the user who created them.
//
// Framing is newline-delimited, matching `bridge/protocol.h`. A message may not
// contain a newline -- `nlohmann::json::dump()` without indentation never emits
// one, which is why the encoders in that header do not pretty-print.

#include <memory>
#include <string>

namespace bridge {

// One end of an established connection. Both the application and the adapter
// use this class; only how they obtain it differs.
class Connection {
public:
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Connects to a listener at `address`. Fails rather than blocking forever
    // when nothing is listening -- an absent application is the normal case for
    // the adapter, not an error worth hanging on.
    static std::unique_ptr<Connection> Connect(const std::string& address, std::string& error);

    // False at end of stream or on a transport failure, which the caller should
    // treat identically: the peer is gone.
    bool ReadMessage(std::string& out_message);
    bool WriteMessage(const std::string& message);

    // Safe to call from another thread while ReadMessage or WriteMessage is
    // blocked: both unblock and return false. The descriptor is reclaimed on
    // destruction, not here, so no blocked thread can race a reused handle.
    void Close();

private:
    friend class Listener;
    struct Impl;

    explicit Connection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The application's side. Accepts connections until stopped.
class Listener {
public:
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    static std::unique_ptr<Listener> Start(const std::string& address, std::string& error);

    // Blocks until a client connects, `Stop` is called, or the transport fails.
    // Returns null in the latter two cases; `error` is empty for a clean stop,
    // so a caller can tell "we are shutting down" from "the pipe broke".
    std::unique_ptr<Connection> Accept(std::string& error);

    // Safe to call from another thread, and safe to call more than once. This is
    // how the application's listener thread is unwound at exit.
    void Stop();

    const std::string& address() const;

private:
    struct Impl;

    explicit Listener(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace bridge
