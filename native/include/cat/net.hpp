#pragma once
// N8 networking without third-party libraries: TCP sockets (Winsock 2 / BSD), RFC 6455 WebSocket
// framing, a WebSocket client and a small HTTP GET, all for the legacy relay protocol
// (reference/multiplayer/server.js: HTTP GET /rooms and /health, WebSocket at /ws, JSON text frames).
//
// Transports:
//   ws://   and http://   plain TCP, implemented here (Windows and Linux).
//   wss://  and https://  Windows: WinHTTP (the system's HTTP stack, TLS by Schannel; no extra
//                         library). Other systems: not available (the game is Windows-only; Linux is
//                         the development host and uses ws:// against a local relay).
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cat::net {

// ---- URLs -------------------------------------------------------------------------------------------
struct Url {
    std::string scheme;   // ws, wss, http, https
    std::string host;     // without brackets for IPv6 literals
    int port{};
    std::string path{"/"};   // with the query, if any
    bool secure() const { return scheme == "wss" || scheme == "https"; }
    std::string host_header() const;
    std::string text() const;
};
std::optional<Url> parse_url(std::string_view text);
// netRelayURL(): "host:port" / http(s)://… / ws(s)://… → a ws(s):// URL whose path ends in /ws.
// Returns an error message instead when the address cannot be used.
struct RelayAddress { std::optional<Url> ws, rooms; std::string error; };
RelayAddress relay_address(std::string_view typed);

// ---- process-wide socket start (WSAStartup on Windows) -------------------------------------------------
bool startup();

// ---- TCP socket ------------------------------------------------------------------------------------------
class Socket {
public:
    Socket() = default;
    ~Socket() { close(); }
    Socket(Socket&& o) noexcept : fd_(o.fd_) { o.fd_ = kInvalid; }
    Socket& operator=(Socket&& o) noexcept { if(this != &o) { close(); fd_ = o.fd_; o.fd_ = kInvalid; } return *this; }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    bool valid() const { return fd_ != kInvalid; }
    void close();
    // Starts a non-blocking connect to host:port (DNS lookup is blocking). Then poll connected().
    bool connect_start(const std::string& host, int port, std::string& error);
    int connected(int wait_ms = 0);   // 1 connected, 0 still connecting, -1 failed
    bool listen(const std::string& host, int port, std::string& error);   // non-blocking listener (SO_REUSEADDR)
    int local_port() const;
    Socket accept();                                                        // invalid when none waiting
    // Non-blocking I/O: bytes moved, 0 = would block, -1 = closed / error.
    long send_some(const char* data, std::size_t size);
    long recv_some(char* data, std::size_t size);
    bool set_blocking(bool blocking);
    void set_nodelay();
    std::intptr_t handle() const { return static_cast<std::intptr_t>(fd_); }
    std::string peer() const;
#ifdef _WIN32
    using Native = std::uintptr_t;
    static constexpr Native kInvalid = ~Native(0);
#else
    using Native = int;
    static constexpr Native kInvalid = -1;
#endif
private:
    explicit Socket(Native fd) : fd_(fd) {}
    Native fd_{kInvalid};
};
// Waits until any of the sockets is readable (or writable when `want_write`), at most `timeout_ms`.
void wait_readable(const std::vector<const Socket*>& sockets, int timeout_ms);

// ---- SHA-1 / Base64 (the WebSocket handshake) ------------------------------------------------------------
std::array<std::uint8_t, 20> sha1(std::string_view data);
std::string base64(const std::uint8_t* data, std::size_t size);
std::string websocket_accept(std::string_view key);   // Sec-WebSocket-Accept for Sec-WebSocket-Key

// ---- RFC 6455 frames -----------------------------------------------------------------------------------
enum Opcode : std::uint8_t { op_continuation = 0, op_text = 1, op_binary = 2, op_close = 8, op_ping = 9, op_pong = 10 };
// Clients mask (RFC 6455 5.3); servers do not.
std::string encode_frame(Opcode op, std::string_view payload, bool mask, std::uint32_t mask_key = 0);
struct Message { Opcode op{op_text}; std::string payload; };
class FrameReader {
public:
    explicit FrameReader(bool require_masked, std::size_t max_message = 128 * 1024) : require_masked_(require_masked), max_(max_message) {}
    void feed(const char* data, std::size_t size) { buffer_.append(data, size); }
    // Next complete message (fragments joined; control frames returned as they arrive).
    std::optional<Message> next();
    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    std::string& buffer() { return buffer_; }
private:
    std::string buffer_, partial_;
    Opcode partial_op_{op_text};
    bool in_message_{}, require_masked_;
    std::size_t max_;
    std::string error_;
};

// ---- client connection (ws:// or wss://) -------------------------------------------------------------------
class Connection {
public:
    enum class State : std::uint8_t { connecting, open, closed };
    virtual ~Connection() = default;
    virtual State state() const = 0;
    // Moves the connection forward; appends every complete text message received.
    virtual void poll(std::vector<std::string>& messages) = 0;
    virtual bool send(std::string_view text) = 0;   // queued; false when not open
    virtual void close() = 0;
    virtual const std::string& error() const = 0;
    virtual std::uint64_t bytes_sent() const = 0;
    virtual std::uint64_t bytes_received() const = 0;
};
std::unique_ptr<Connection> connect(const Url& url, int timeout_ms = 18000);   // never null

// ---- HTTP GET (blocking; run it on a worker thread) ----------------------------------------------------------
struct HttpResult { int status{}; std::string body, error; };
HttpResult http_get(const Url& url, int timeout_ms);

#ifdef _WIN32
// WinHTTP implementations (src/net_winhttp.cpp).
std::unique_ptr<Connection> winhttp_connect(const Url& url, int timeout_ms);
HttpResult winhttp_get(const Url& url, int timeout_ms);
#endif

}  // namespace cat::net
