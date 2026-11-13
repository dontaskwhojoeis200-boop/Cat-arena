// N8: see include/cat/net.hpp.
#include "cat/net.hpp"

#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

namespace cat::net {
namespace {
#ifdef _WIN32
using socklen = int;
bool would_block() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINTR; }
void close_native(Socket::Native fd) { closesocket(static_cast<SOCKET>(fd)); }
SOCKET raw(Socket::Native fd) { return static_cast<SOCKET>(fd); }
#else
using socklen = socklen_t;
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
void close_native(Socket::Native fd) { ::close(fd); }
int raw(Socket::Native fd) { return fd; }
#endif
std::string last_error_text() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}
std::string lower(std::string s) { for(char& c : s) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return s; }
std::uint32_t random_u32() {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return static_cast<std::uint32_t>(gen());
}
std::int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}  // namespace

// ---- URLs -------------------------------------------------------------------------------------------------
std::string Url::host_header() const {
    const bool v6 = host.find(':') != std::string::npos;
    const std::string h = v6 ? "[" + host + "]" : host;
    const int def = secure() ? 443 : 80;
    return port == def ? h : h + ":" + std::to_string(port);
}
std::string Url::text() const { return scheme + "://" + host_header() + path; }
std::optional<Url> parse_url(std::string_view text) {
    Url u;
    const auto sep = text.find("://");
    if(sep == std::string_view::npos) return std::nullopt;
    u.scheme = lower(std::string(text.substr(0, sep)));
    if(u.scheme != "ws" && u.scheme != "wss" && u.scheme != "http" && u.scheme != "https") return std::nullopt;
    std::string_view rest = text.substr(sep + 3);
    const auto slash = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, slash);
    std::string path = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
    if(const auto hash = path.find('#'); hash != std::string::npos) path.erase(hash);
    if(path.empty() || path[0] != '/') path = "/" + path;
    if(const auto at = authority.rfind('@'); at != std::string_view::npos) authority = authority.substr(at + 1);   // user info is dropped
    u.port = u.secure() ? 443 : 80;
    if(!authority.empty() && authority[0] == '[') {
        const auto close = authority.find(']');
        if(close == std::string_view::npos) return std::nullopt;
        u.host = std::string(authority.substr(1, close - 1));
        authority = authority.substr(close + 1);
        if(!authority.empty()) { if(authority[0] != ':') return std::nullopt; authority = authority.substr(1); if(authority.empty()) return std::nullopt; u.port = std::stoi(std::string(authority)); }
    } else {
        const auto colon = authority.rfind(':');
        u.host = std::string(authority.substr(0, colon));
        if(colon != std::string_view::npos) {
            const std::string p(authority.substr(colon + 1));
            if(p.empty() || p.find_first_not_of("0123456789") != std::string::npos || p.size() > 5) return std::nullopt;
            u.port = std::stoi(p);
        }
    }
    if(u.host.empty() || u.port <= 0 || u.port > 65535) return std::nullopt;
    u.host = lower(u.host);
    u.path = path;
    return u;
}
RelayAddress relay_address(std::string_view typed) {   // netRelayURL / netRoomsURL (index.html 15571-15589)
    RelayAddress r;
    std::string s(typed);
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while(!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    if(s.empty()) { r.error = "Enter a server address in Advanced settings."; return r; }
    const std::string ls = lower(s);
    if(ls.rfind("http://", 0) == 0 || ls.rfind("https://", 0) == 0) s = "ws" + s.substr(4);
    else if(ls.rfind("ws://", 0) != 0 && ls.rfind("wss://", 0) != 0) s = "ws://" + s;
    auto u = parse_url(s);
    if(!u || (u->scheme != "ws" && u->scheme != "wss")) { r.error = "Use a ws:// or wss:// server address."; return r; }
    std::string path = u->path.substr(0, u->path.find('?'));
    if(path.size() < 3 || path.compare(path.size() - 3, 3, "/ws") != 0) {
        while(!path.empty() && path.back() == '/') path.pop_back();
        path += "/ws";
    }
    u->path = path;
    r.ws = *u;
    Url rooms = *u;
    rooms.scheme = u->scheme == "wss" ? "https" : "http";
    rooms.path = "/rooms";
    r.rooms = rooms;
    return r;
}

// ---- sockets ------------------------------------------------------------------------------------------------
bool startup() {
#ifdef _WIN32
    static const bool ok = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    return ok;
#else
    return true;
#endif
}
void Socket::close() { if(fd_ != kInvalid) { close_native(fd_); fd_ = kInvalid; } }
bool Socket::set_blocking(bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    return ioctlsocket(raw(fd_), static_cast<long>(FIONBIO), &mode) == 0;
#else
    const int flags = fcntl(fd_, F_GETFL, 0);
    return flags >= 0 && fcntl(fd_, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
#endif
}
void Socket::set_nodelay() { int one = 1; setsockopt(raw(fd_), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one); }
bool Socket::connect_start(const std::string& host, int port, std::string& error) {
    startup();
    close();
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    addrinfo* list = nullptr;
    const std::string service = std::to_string(port);
    if(getaddrinfo(host.c_str(), service.c_str(), &hints, &list) != 0 || !list) { error = "Cannot resolve " + host; return false; }
    // Try the addresses in order with a short blocking-free attempt each (IPv4 first: the relays listen on 0.0.0.0).
    std::vector<addrinfo*> order;
    for(addrinfo* a = list; a; a = a->ai_next) if(a->ai_family == AF_INET) order.push_back(a);
    for(addrinfo* a = list; a; a = a->ai_next) if(a->ai_family != AF_INET) order.push_back(a);
    bool started = false;
    for(addrinfo* a : order) {
        const auto fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
#ifdef _WIN32
        if(fd == INVALID_SOCKET) continue;
        fd_ = static_cast<Native>(fd);
#else
        if(fd < 0) continue;
        fd_ = fd;
#endif
        set_blocking(false);
        const int r = ::connect(raw(fd_), a->ai_addr, static_cast<socklen>(a->ai_addrlen));
        if(r == 0 || would_block()) { started = true; break; }
        close();
    }
    freeaddrinfo(list);
    if(!started) { error = "Cannot connect to " + host + ":" + service; return false; }
    set_nodelay();
    return true;
}
int Socket::connected(int wait_ms) {
    if(fd_ == kInvalid) return -1;
#ifdef _WIN32
    WSAPOLLFD p{}; p.fd = raw(fd_); p.events = POLLWRNORM;
    const int r = WSAPoll(&p, 1, wait_ms);
    if(r < 0) return -1;
    if(r == 0) return 0;
    if(p.revents & (POLLERR | POLLHUP)) return -1;
#else
    pollfd p{}; p.fd = fd_; p.events = POLLOUT;
    const int r = ::poll(&p, 1, wait_ms);
    if(r < 0) return errno == EINTR ? 0 : -1;
    if(r == 0) return 0;
#endif
    int err = 0; socklen len = sizeof err;
    if(getsockopt(raw(fd_), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0 || err != 0) return -1;
    return 1;
}
bool Socket::listen(const std::string& host, int port, std::string& error) {
    startup();
    close();
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_PASSIVE;
    addrinfo* list = nullptr;
    const std::string service = std::to_string(port);
    if(getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints, &list) != 0 || !list) { error = "Cannot resolve " + host; return false; }
    const auto fd = ::socket(list->ai_family, list->ai_socktype, list->ai_protocol);
#ifdef _WIN32
    if(fd == INVALID_SOCKET) { freeaddrinfo(list); error = last_error_text(); return false; }
    fd_ = static_cast<Native>(fd);
    BOOL excl = TRUE; setsockopt(raw(fd_), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof excl);
#else
    if(fd < 0) { freeaddrinfo(list); error = last_error_text(); return false; }
    fd_ = fd;
    int one = 1; setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    const bool ok = ::bind(raw(fd_), list->ai_addr, static_cast<socklen>(list->ai_addrlen)) == 0 && ::listen(raw(fd_), 64) == 0;
    freeaddrinfo(list);
    if(!ok) { error = "Cannot listen on port " + service + " (" + last_error_text() + ")"; close(); return false; }
    set_blocking(false);
    return true;
}
int Socket::local_port() const {
    sockaddr_storage a{}; socklen len = sizeof a;
    if(getsockname(raw(fd_), reinterpret_cast<sockaddr*>(&a), &len) != 0) return 0;
    if(a.ss_family == AF_INET) return ntohs(reinterpret_cast<sockaddr_in*>(&a)->sin_port);
    if(a.ss_family == AF_INET6) return ntohs(reinterpret_cast<sockaddr_in6*>(&a)->sin6_port);
    return 0;
}
std::string Socket::peer() const {
    sockaddr_storage a{}; socklen len = sizeof a;
    if(getpeername(raw(fd_), reinterpret_cast<sockaddr*>(&a), &len) != 0) return "?";
    char host[64] = "?";
    if(a.ss_family == AF_INET) inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&a)->sin_addr, host, sizeof host);
    else if(a.ss_family == AF_INET6) inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&a)->sin6_addr, host, sizeof host);
    return host;
}
Socket Socket::accept() {
    const auto fd = ::accept(raw(fd_), nullptr, nullptr);
#ifdef _WIN32
    if(fd == INVALID_SOCKET) return Socket{};
    Socket s(static_cast<Native>(fd));
#else
    if(fd < 0) return Socket{};
    Socket s(fd);
#endif
    s.set_blocking(false);
    s.set_nodelay();
    return s;
}
long Socket::send_some(const char* data, std::size_t size) {
    if(fd_ == kInvalid) return -1;
    if(size == 0) return 0;
#ifdef _WIN32
    const int n = ::send(raw(fd_), data, static_cast<int>(std::min<std::size_t>(size, 1 << 20)), 0);
#else
    const auto n = ::send(fd_, data, size, MSG_NOSIGNAL);
#endif
    if(n > 0) return static_cast<long>(n);
    return would_block() ? 0 : -1;
}
long Socket::recv_some(char* data, std::size_t size) {
    if(fd_ == kInvalid) return -1;
#ifdef _WIN32
    const int n = ::recv(raw(fd_), data, static_cast<int>(std::min<std::size_t>(size, 1 << 20)), 0);
#else
    const auto n = ::recv(fd_, data, size, 0);
#endif
    if(n > 0) return static_cast<long>(n);
    if(n == 0) return -1;   // orderly close
    return would_block() ? 0 : -1;
}
void wait_readable(const std::vector<const Socket*>& sockets, int timeout_ms) {
#ifdef _WIN32
    std::vector<WSAPOLLFD> p;
    for(const auto* s : sockets) if(s && s->valid()) { WSAPOLLFD e{}; e.fd = static_cast<SOCKET>(s->handle()); e.events = POLLRDNORM; p.push_back(e); }
    if(p.empty()) { Sleep(static_cast<DWORD>(std::max(0, timeout_ms))); return; }
    WSAPoll(p.data(), static_cast<ULONG>(p.size()), timeout_ms);
#else
    std::vector<pollfd> p;
    for(const auto* s : sockets) if(s && s->valid()) { pollfd e{}; e.fd = static_cast<int>(s->handle()); e.events = POLLIN; p.push_back(e); }
    ::poll(p.data(), p.size(), timeout_ms);
#endif
}

// ---- SHA-1 / Base64 --------------------------------------------------------------------------------------------
std::array<std::uint8_t, 20> sha1(std::string_view data) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while(m.size() % 64 != 56) m += '\0';
    for(int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
    auto rol = [](std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for(std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t w[80];
        for(std::size_t i = 0; i < 16; ++i) w[i] = (std::uint32_t(std::uint8_t(m[off + i * 4])) << 24) | (std::uint32_t(std::uint8_t(m[off + i * 4 + 1])) << 16) | (std::uint32_t(std::uint8_t(m[off + i * 4 + 2])) << 8) | std::uint32_t(std::uint8_t(m[off + i * 4 + 3]));
        for(int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for(int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if(i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if(i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if(i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::array<std::uint8_t, 20> out{};
    for(int i = 0; i < 5; ++i) for(int j = 0; j < 4; ++j) out[static_cast<std::size_t>(i * 4 + j)] = static_cast<std::uint8_t>(h[i] >> (24 - j * 8));
    return out;
}
std::string base64(const std::uint8_t* d, std::size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t v = (std::uint32_t(d[i]) << 16) | (i + 1 < n ? std::uint32_t(d[i + 1]) << 8 : 0) | (i + 2 < n ? std::uint32_t(d[i + 2]) : 0);
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63];
        out += i + 1 < n ? t[(v >> 6) & 63] : '=';
        out += i + 2 < n ? t[v & 63] : '=';
    }
    return out;
}
std::string websocket_accept(std::string_view key) {
    const auto h = sha1(std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64(h.data(), h.size());
}

// ---- frames -------------------------------------------------------------------------------------------------------
std::string encode_frame(Opcode op, std::string_view payload, bool mask, std::uint32_t mask_key) {
    std::string f;
    f += static_cast<char>(0x80 | op);
    const std::size_t n = payload.size();
    const char m = mask ? char(0x80) : char(0);
    if(n < 126) f += static_cast<char>(m | static_cast<char>(n));
    else if(n < 65536) { f += static_cast<char>(m | 126); f += static_cast<char>((n >> 8) & 0xFF); f += static_cast<char>(n & 0xFF); }
    else { f += static_cast<char>(m | 127); for(int i = 7; i >= 0; --i) f += static_cast<char>((static_cast<std::uint64_t>(n) >> (i * 8)) & 0xFF); }
    if(!mask) { f.append(payload); return f; }
    const std::uint8_t k[4] = {std::uint8_t(mask_key >> 24), std::uint8_t(mask_key >> 16), std::uint8_t(mask_key >> 8), std::uint8_t(mask_key)};
    f.append(reinterpret_cast<const char*>(k), 4);
    for(std::size_t i = 0; i < n; ++i) f += static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ k[i & 3]);
    return f;
}
std::optional<Message> FrameReader::next() {
    while(error_.empty()) {
        if(buffer_.size() < 2) return std::nullopt;
        const auto b0 = static_cast<std::uint8_t>(buffer_[0]), b1 = static_cast<std::uint8_t>(buffer_[1]);
        const bool fin = b0 & 0x80, masked = b1 & 0x80;
        const auto op = static_cast<Opcode>(b0 & 0x0F);
        if(b0 & 0x70) { error_ = "reserved bits set"; return std::nullopt; }
        if(masked != require_masked_) { error_ = require_masked_ ? "client frame not masked" : "server frame masked"; return std::nullopt; }
        std::uint64_t len = b1 & 0x7F;
        std::size_t head = 2;
        if(len == 126) { if(buffer_.size() < 4) return std::nullopt; len = (std::uint64_t(std::uint8_t(buffer_[2])) << 8) | std::uint8_t(buffer_[3]); head = 4; }
        else if(len == 127) { if(buffer_.size() < 10) return std::nullopt; len = 0; for(int i = 0; i < 8; ++i) len = (len << 8) | std::uint8_t(buffer_[2 + static_cast<std::size_t>(i)]); head = 10; }
        if(len > max_) { error_ = "message too large"; return std::nullopt; }
        const std::size_t key_at = head;
        if(masked) head += 4;
        if(buffer_.size() < head + len) return std::nullopt;
        std::string payload = buffer_.substr(head, static_cast<std::size_t>(len));
        if(masked) for(std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ buffer_[key_at + (i & 3)]);
        buffer_.erase(0, head + static_cast<std::size_t>(len));
        if(op >= op_close) {   // control frames: never fragmented, at most 125 bytes
            if(!fin || len > 125) { error_ = "bad control frame"; return std::nullopt; }
            return Message{op, std::move(payload)};
        }
        if(op == op_continuation) {
            if(!in_message_) { error_ = "unexpected continuation"; return std::nullopt; }
            partial_ += payload;
        } else {
            if(in_message_) { error_ = "interleaved message"; return std::nullopt; }
            if(op != op_text && op != op_binary) { error_ = "unknown opcode"; return std::nullopt; }
            partial_ = std::move(payload); partial_op_ = op; in_message_ = true;
        }
        if(partial_.size() > max_) { error_ = "message too large"; return std::nullopt; }
        if(fin) { in_message_ = false; return Message{partial_op_, std::move(partial_)}; }
    }
    return std::nullopt;
}

// ---- WebSocket client over TCP (ws://) -----------------------------------------------------------------------------
namespace {
class TcpConnection final : public Connection {
public:
    TcpConnection(const Url& url, int timeout_ms) : url_(url), reader_(false, 4 << 20) {
        deadline_ = now_ms() + timeout_ms;
        if(!sock_.connect_start(url.host, url.port, error_)) { state_ = State::closed; return; }
        std::uint8_t key[16];
        for(int i = 0; i < 16; i += 4) { const auto r = random_u32(); std::memcpy(key + i, &r, 4); }
        key_ = base64(key, 16);
        out_ = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host_header() + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key_ +
               "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: CatArenaNative/N8\r\n\r\n";
    }
    State state() const override { return state_; }
    const std::string& error() const override { return error_; }
    std::uint64_t bytes_sent() const override { return sent_; }
    std::uint64_t bytes_received() const override { return received_; }
    bool send(std::string_view text) override {
        if(state_ != State::open) return false;
        out_ += encode_frame(op_text, text, true, random_u32());
        flush();
        return state_ == State::open;
    }
    void close() override {
        if(state_ == State::open) { out_ += encode_frame(op_close, std::string("\x03\xe8", 2), true, random_u32()); flush(); }
        sock_.close();
        state_ = State::closed;
    }
    void poll(std::vector<std::string>& messages) override {
        if(state_ == State::closed) return;
        if(!connected_) {
            const int c = sock_.connected(0);
            if(c < 0) return fail("Cannot connect to " + url_.host_header());
            if(c == 0) { if(now_ms() > deadline_) fail("Connection timeout"); return; }
            connected_ = true;
        }
        flush();
        char buf[16384];
        while(state_ != State::closed) {
            const long n = sock_.recv_some(buf, sizeof buf);
            if(n == 0) break;
            if(n < 0) return fail(state_ == State::open ? "Connection closed by the server" : "The server closed the connection during the handshake");
            received_ += static_cast<std::uint64_t>(n);
            if(state_ == State::connecting) {
                handshake_ += std::string(buf, static_cast<std::size_t>(n));
                const auto end = handshake_.find("\r\n\r\n");
                if(end == std::string::npos) { if(handshake_.size() > 16384) return fail("Bad handshake"); continue; }
                const std::string head = lower(handshake_.substr(0, end));
                if(head.rfind("http/1.1 101", 0) != 0) return fail("The server refused the WebSocket upgrade (" + handshake_.substr(0, handshake_.find("\r\n")) + ")");
                if(head.find("sec-websocket-accept: " + lower(websocket_accept(key_))) == std::string::npos) return fail("Bad Sec-WebSocket-Accept");
                state_ = State::open;
                reader_.feed(handshake_.data() + end + 4, handshake_.size() - end - 4);
                handshake_.clear();
            } else reader_.feed(buf, static_cast<std::size_t>(n));
        }
        if(state_ == State::connecting && now_ms() > deadline_) return fail("Connection timeout");
        while(state_ == State::open) {
            auto m = reader_.next();
            if(reader_.failed()) return fail("Protocol error: " + reader_.error());
            if(!m) break;
            if(m->op == op_text) messages.push_back(std::move(m->payload));
            else if(m->op == op_ping) { out_ += encode_frame(op_pong, m->payload, true, random_u32()); flush(); }
            else if(m->op == op_close) {
                out_ += encode_frame(op_close, m->payload.substr(0, 2), true, random_u32()); flush();
                return fail("Connection closed by the server");
            }
        }
    }
private:
    void fail(const std::string& why) { if(error_.empty()) error_ = why; sock_.close(); state_ = State::closed; }
    void flush() {
        if(!connected_) return;
        while(!out_.empty()) {
            const long n = sock_.send_some(out_.data(), out_.size());
            if(n < 0) return fail("Connection lost");
            if(n == 0) break;
            sent_ += static_cast<std::uint64_t>(n);
            out_.erase(0, static_cast<std::size_t>(n));
        }
    }
    Url url_;
    Socket sock_;
    State state_{State::connecting};
    bool connected_{};
    std::string key_, out_, handshake_, error_;
    FrameReader reader_;
    std::int64_t deadline_{};
    std::uint64_t sent_{}, received_{};
};
class ClosedConnection final : public Connection {
public:
    explicit ClosedConnection(std::string e) : error_(std::move(e)) {}
    State state() const override { return State::closed; }
    void poll(std::vector<std::string>&) override {}
    bool send(std::string_view) override { return false; }
    void close() override {}
    const std::string& error() const override { return error_; }
    std::uint64_t bytes_sent() const override { return 0; }
    std::uint64_t bytes_received() const override { return 0; }
private:
    std::string error_;
};
}  // namespace

#ifdef _WIN32
// CAT_NET_WINHTTP=1 sends ws:// and http:// through WinHTTP too: the same code path as wss:// / https://
// minus TLS, so that the Windows CI can test it against a local relay.
bool force_winhttp() { static const bool on = [] { const char* v = std::getenv("CAT_NET_WINHTTP"); return v && *v == '1'; }(); return on; }
#endif
std::unique_ptr<Connection> connect(const Url& url, int timeout_ms) {
#ifdef _WIN32
    if(url.scheme == "ws" && force_winhttp()) return winhttp_connect(url, timeout_ms);
#endif
    if(url.scheme == "wss") {
#ifdef _WIN32
        return winhttp_connect(url, timeout_ms);
#else
        return std::make_unique<ClosedConnection>("Secure wss:// servers need the Windows build (WinHTTP); use a ws:// relay here.");
#endif
    }
    if(url.scheme != "ws") return std::make_unique<ClosedConnection>("Use a ws:// or wss:// server address.");
    return std::make_unique<TcpConnection>(url, timeout_ms);
}

// ---- HTTP GET over TCP --------------------------------------------------------------------------------------------------
HttpResult http_get(const Url& url, int timeout_ms) {
    HttpResult r;
#ifdef _WIN32
    if(url.scheme == "http" && force_winhttp()) return winhttp_get(url, timeout_ms);
#endif
    if(url.scheme == "https") {
#ifdef _WIN32
        return winhttp_get(url, timeout_ms);
#else
        r.error = "https:// needs the Windows build (WinHTTP).";
        return r;
#endif
    }
    if(url.scheme != "http") { r.error = "Not an http:// address"; return r; }
    const auto deadline = now_ms() + timeout_ms;
    Socket s;
    if(!s.connect_start(url.host, url.port, r.error)) return r;
    while(true) {
        const int c = s.connected(50);
        if(c > 0) break;
        if(c < 0) { r.error = "Cannot connect to " + url.host_header(); return r; }
        if(now_ms() > deadline) { r.error = "Timed out connecting to " + url.host_header(); return r; }
    }
    std::string out = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host_header() + "\r\nAccept: application/json\r\nConnection: close\r\nUser-Agent: CatArenaNative/N8\r\n\r\n";
    std::string in;
    char buf[16384];
    while(true) {
        if(!out.empty()) {
            const long n = s.send_some(out.data(), out.size());
            if(n < 0) { r.error = "Connection lost"; return r; }
            out.erase(0, static_cast<std::size_t>(n));
        }
        const long n = s.recv_some(buf, sizeof buf);
        if(n > 0) { in.append(buf, static_cast<std::size_t>(n)); if(in.size() > (8u << 20)) { r.error = "Response too large"; return r; } continue; }
        if(n < 0) break;   // closed: the whole response is in
        // Content-Length known and complete? (the server may keep the connection open)
        const auto end = in.find("\r\n\r\n");
        if(end != std::string::npos) {
            const std::string head = lower(in.substr(0, end));
            const auto cl = head.find("\r\ncontent-length:");
            if(cl != std::string::npos && in.size() - end - 4 >= std::stoul(head.substr(cl + 17))) break;
        }
        if(now_ms() > deadline) { r.error = "Timed out waiting for " + url.text(); return r; }
        wait_readable({&s}, 50);
    }
    const auto end = in.find("\r\n\r\n");
    if(end == std::string::npos || in.rfind("HTTP/1.", 0) != 0) { r.error = "Bad HTTP response"; return r; }
    r.status = std::atoi(in.c_str() + 9);
    const std::string head = lower(in.substr(0, end));
    std::string body = in.substr(end + 4);
    if(const auto cl = head.find("\r\ncontent-length:"); cl != std::string::npos) body.resize(std::min<std::size_t>(body.size(), std::stoul(head.substr(cl + 17))));
    else if(head.find("\r\ntransfer-encoding: chunked") != std::string::npos) {   // de-chunk
        std::string plain;
        std::size_t at = 0;
        while(at < body.size()) {
            const auto line_end = body.find("\r\n", at);
            if(line_end == std::string::npos) break;
            const std::size_t size = std::stoul(body.substr(at, line_end - at), nullptr, 16);
            if(size == 0) break;
            plain += body.substr(line_end + 2, size);
            at = line_end + 2 + size + 2;
        }
        body = plain;
    }
    r.body = std::move(body);
    return r;
}

}  // namespace cat::net
