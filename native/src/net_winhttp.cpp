// N8: wss:// and https:// on Windows through WinHTTP (the system HTTP stack; TLS by Schannel, the
// system proxy settings apply). See include/cat/net.hpp.
#ifdef _WIN32
#include "cat/net.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <condition_variable>

namespace cat::net {
namespace {
std::wstring widen(const std::string& s) {
    if(s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string error_text(const char* what) { return std::string(what) + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")"; }
struct Handle {
    HINTERNET h{};
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if(h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HINTERNET x = nullptr) { if(h) WinHttpCloseHandle(h); h = x; }
    explicit operator bool() const { return h != nullptr; }
};
HINTERNET open_session(int timeout_ms) {
    HINTERNET s = WinHttpOpen(L"CatArenaNative/N8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if(!s) s = WinHttpOpen(L"CatArenaNative/N8", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);   // before Windows 8.1
    if(s) WinHttpSetTimeouts(s, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    return s;
}

class WinHttpConnection final : public Connection {
public:
    WinHttpConnection(const Url& url, int timeout_ms) : url_(url), timeout_(timeout_ms) { worker_ = std::thread([this] { run(); }); }
    ~WinHttpConnection() override { close(); if(worker_.joinable()) worker_.join(); }
    State state() const override { return state_.load(); }
    const std::string& error() const override { std::lock_guard<std::mutex> l(m_); return error_; }
    std::uint64_t bytes_sent() const override { return sent_; }
    std::uint64_t bytes_received() const override { return received_; }
    void poll(std::vector<std::string>& messages) override {
        std::lock_guard<std::mutex> l(m_);
        for(auto& s : inbox_) messages.push_back(std::move(s));
        inbox_.clear();
    }
    bool send(std::string_view text) override {
        if(state_ != State::open) return false;
        std::lock_guard<std::mutex> l(send_m_);
        if(WinHttpWebSocketSend(socket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(text.data()), static_cast<DWORD>(text.size())) != NO_ERROR) {
            fail("Connection lost");
            return false;
        }
        sent_ += text.size();
        return true;
    }
    void close() override {
        const State was = state_.exchange(State::closed);
        if(was == State::open && socket_) WinHttpWebSocketClose(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        HINTERNET r = request_.exchange(nullptr);
        if(r) WinHttpCloseHandle(r);   // unblocks a pending connect / receive
    }
private:
    void fail(const std::string& why) {
        { std::lock_guard<std::mutex> l(m_); if(error_.empty()) error_ = why; }
        state_ = State::closed;
    }
    void run() {
        Handle session(open_session(timeout_));
        if(!session) return fail(error_text("WinHttpOpen"));
        Handle connection(WinHttpConnect(session.h, widen(url_.host).c_str(), static_cast<INTERNET_PORT>(url_.port), 0));
        if(!connection) return fail(error_text("WinHttpConnect"));
        HINTERNET req = WinHttpOpenRequest(connection.h, L"GET", widen(url_.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url_.secure() ? WINHTTP_FLAG_SECURE : 0);
        if(!req) return fail(error_text("WinHttpOpenRequest"));
        request_ = req;
        if(!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return fail(error_text("WebSocket upgrade option"));
        if(!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            if(state_ != State::closed) fail("Cannot reach " + url_.host_header() + " (" + error_text("WebSocket handshake") + ")");
            return;
        }
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if(status != 101) return fail("The server refused the WebSocket upgrade (HTTP " + std::to_string(status) + ")");
        socket_ = WinHttpWebSocketCompleteUpgrade(req, 0);
        if(!socket_) return fail(error_text("WebSocket upgrade"));
        if(HINTERNET r = request_.exchange(nullptr)) WinHttpCloseHandle(r);   // the request handle is no longer needed
        State expected = State::connecting;
        if(!state_.compare_exchange_strong(expected, State::open)) { WinHttpCloseHandle(socket_); socket_ = nullptr; return; }
        std::string message;
        std::vector<char> buf(64 * 1024);
        while(state_ == State::open) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD r = WinHttpWebSocketReceive(socket_, buf.data(), static_cast<DWORD>(buf.size()), &read, &type);
            if(r != NO_ERROR) { if(state_ == State::open) fail("Connection closed"); break; }
            received_ += read;
            if(type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { fail("Connection closed by the server"); break; }
            message.append(buf.data(), read);
            if(message.size() > (4u << 20)) { fail("Message too large"); break; }
            if(type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { std::lock_guard<std::mutex> l(m_); inbox_.push_back(std::move(message)); message.clear(); }
            else if(type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) message.clear();
        }
        WinHttpCloseHandle(socket_);
        socket_ = nullptr;
    }
    Url url_;
    int timeout_;
    std::thread worker_;
    std::atomic<State> state_{State::connecting};
    std::atomic<HINTERNET> request_{nullptr};
    HINTERNET socket_{};
    mutable std::mutex m_;
    std::mutex send_m_;
    std::vector<std::string> inbox_;
    std::string error_;
    std::atomic<std::uint64_t> sent_{0}, received_{0};
};
}  // namespace

std::unique_ptr<Connection> winhttp_connect(const Url& url, int timeout_ms) { return std::make_unique<WinHttpConnection>(url, timeout_ms); }

HttpResult winhttp_get(const Url& url, int timeout_ms) {
    HttpResult r;
    Handle session(open_session(timeout_ms));
    if(!session) { r.error = error_text("WinHttpOpen"); return r; }
    Handle connection(WinHttpConnect(session.h, widen(url.host).c_str(), static_cast<INTERNET_PORT>(url.port), 0));
    if(!connection) { r.error = error_text("WinHttpConnect"); return r; }
    Handle req(WinHttpOpenRequest(connection.h, L"GET", widen(url.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure() ? WINHTTP_FLAG_SECURE : 0));
    if(!req) { r.error = error_text("WinHttpOpenRequest"); return r; }
    if(!WinHttpSendRequest(req.h, L"Accept: application/json\r\n", static_cast<DWORD>(-1L), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = "Cannot reach " + url.host_header() + " (" + error_text("HTTP request") + ")";
        return r;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    for(;;) {
        DWORD available = 0;
        if(!WinHttpQueryDataAvailable(req.h, &available)) { r.error = error_text("Reading the response"); return r; }
        if(available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if(!WinHttpReadData(req.h, chunk.data(), available, &read)) { r.error = error_text("Reading the response"); return r; }
        r.body.append(chunk.data(), read);
        if(r.body.size() > (8u << 20)) { r.error = "Response too large"; return r; }
    }
    return r;
}

}  // namespace cat::net
#endif
