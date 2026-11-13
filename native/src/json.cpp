// N8: see include/cat/json.hpp.
#include "cat/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace cat::json {
namespace {
const std::string kEmptyString;
const Array kEmptyArray;
const Object kEmptyObject;
const Value kNull;

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}
    Value document() {
        Value v = value(0);
        space();
        if(i_ != s_.size()) fail("trailing characters");
        return v;
    }
private:
    [[noreturn]] void fail(const char* what) const { throw std::runtime_error(std::string("JSON: ") + what + " at byte " + std::to_string(i_)); }
    void space() { while(i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    bool eat(char c) { space(); if(i_ < s_.size() && s_[i_] == c) { ++i_; return true; } return false; }
    void expect(char c) { if(!eat(c)) fail("unexpected character"); }
    bool word(std::string_view w) { if(s_.substr(i_, w.size()) == w) { i_ += w.size(); return true; } return false; }
    Value value(int depth) {
        if(depth > 64) fail("nesting too deep");
        space();
        if(i_ >= s_.size()) fail("unexpected end");
        const char c = s_[i_];
        if(c == '{') {
            ++i_;
            Object o;
            if(eat('}')) return Value(std::move(o));
            do {
                space();
                if(i_ >= s_.size() || s_[i_] != '"') fail("expected a key");
                std::string key = string();
                expect(':');
                Value v = value(depth + 1);
                bool replaced = false;   // JSON.parse keeps the last duplicate, at the first key's place
                for(auto& kv : o) if(kv.first == key) { kv.second = std::move(v); replaced = true; break; }
                if(!replaced) o.emplace_back(std::move(key), std::move(v));
            } while(eat(','));
            expect('}');
            return Value(std::move(o));
        }
        if(c == '[') {
            ++i_;
            Array a;
            if(eat(']')) return Value(std::move(a));
            do a.push_back(value(depth + 1)); while(eat(','));
            expect(']');
            return Value(std::move(a));
        }
        if(c == '"') return Value(string());
        if(word("true")) return Value(true);
        if(word("false")) return Value(false);
        if(word("null")) return Value();
        return Value(number());
    }
    double number() {
        const std::size_t start = i_;
        if(i_ < s_.size() && s_[i_] == '-') ++i_;
        if(i_ >= s_.size() || !(s_[i_] >= '0' && s_[i_] <= '9')) fail("bad number");
        if(s_[i_] == '0') ++i_; else while(i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        if(i_ < s_.size() && s_[i_] == '.') { ++i_; if(i_ >= s_.size() || !(s_[i_] >= '0' && s_[i_] <= '9')) fail("bad number"); while(i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_; }
        if(i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if(i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if(i_ >= s_.size() || !(s_[i_] >= '0' && s_[i_] <= '9')) fail("bad number");
            while(i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        }
        double d = 0;
        const auto r = std::from_chars(s_.data() + start, s_.data() + i_, d);
        if(r.ec == std::errc::result_out_of_range) d = s_[start] == '-' ? -HUGE_VAL : HUGE_VAL;   // JSON.parse('1e999') = Infinity
        else if(r.ec != std::errc()) fail("bad number");
        return d;
    }
    static void utf8(std::string& out, std::uint32_t cp) {
        if(cp < 0x80) out += static_cast<char>(cp);
        else if(cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if(cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    std::uint32_t hex4() {
        if(i_ + 4 > s_.size()) fail("bad escape");
        std::uint32_t v = 0;
        for(int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            v <<= 4;
            if(h >= '0' && h <= '9') v |= static_cast<std::uint32_t>(h - '0');
            else if(h >= 'a' && h <= 'f') v |= static_cast<std::uint32_t>(h - 'a' + 10);
            else if(h >= 'A' && h <= 'F') v |= static_cast<std::uint32_t>(h - 'A' + 10);
            else fail("bad escape");
        }
        return v;
    }
    std::string string() {
        ++i_;   // opening quote
        std::string out;
        while(true) {
            if(i_ >= s_.size()) fail("unterminated string");
            const char c = s_[i_++];
            if(c == '"') break;
            if(static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if(c != '\\') { out += c; continue; }
            if(i_ >= s_.size()) fail("bad escape");
            const char e = s_[i_++];
            switch(e) {
                case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
                case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = hex4();
                    if(cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        const std::size_t save = i_;
                        i_ += 2;
                        const std::uint32_t lo = hex4();
                        if(lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else i_ = save;
                    }
                    if(cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;   // lone surrogate: not representable in UTF-8
                    utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
        return out;
    }
    std::string_view s_;
    std::size_t i_{};
};

void dump_string(std::string& out, const std::string& s) {
    out += '"';
    for(const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch(ch) {
            case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break; case '\f': out += "\\f"; break; case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
            default:
                if(c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += ch;
        }
    }
    out += '"';
}
}  // namespace

const std::string& Value::as_string() const { return is_string() ? std::get<std::string>(v_) : kEmptyString; }
const Array& Value::as_array() const { return is_array() ? std::get<Array>(v_) : kEmptyArray; }
const Object& Value::as_object() const { return is_object() ? std::get<Object>(v_) : kEmptyObject; }
Array& Value::array() { if(!is_array()) v_ = Array{}; return std::get<Array>(v_); }
Object& Value::object() { if(!is_object()) v_ = Object{}; return std::get<Object>(v_); }
const Value* Value::find(std::string_view key) const {
    if(!is_object()) return nullptr;
    for(const auto& kv : std::get<Object>(v_)) if(kv.first == key) return &kv.second;
    return nullptr;
}
const Value& Value::operator[](std::string_view key) const { const Value* v = find(key); return v ? *v : kNull; }
Value& Value::set(std::string_view key, Value v) {
    auto& o = object();
    for(auto& kv : o) if(kv.first == key) { kv.second = std::move(v); return kv.second; }
    o.emplace_back(std::string(key), std::move(v));
    return o.back().second;
}
bool Value::truthy() const {
    switch(type()) {
        case Type::null: return false;
        case Type::boolean: return std::get<bool>(v_);
        case Type::number: { const double d = std::get<double>(v_); return d != 0 && d == d; }
        case Type::string: return !std::get<std::string>(v_).empty();
        default: return true;
    }
}
bool Value::finite_number() const { return is_number() && std::isfinite(std::get<double>(v_)); }

Value parse(std::string_view text) { return Parser(text).document(); }
std::optional<Value> try_parse(std::string_view text) {
    try { return parse(text); } catch(const std::exception&) { return std::nullopt; }
}

std::string number_text(double d) {
    if(d == 0) return "0";   // also -0, as JavaScript prints it
    // Shortest round-trip digits, then the ECMAScript Number::toString layout.
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);
    std::string sci(buf, r.ptr);
    const bool neg = sci[0] == '-';
    if(neg) sci.erase(0, 1);
    const auto e_at = sci.find('e');
    std::string digits = sci.substr(0, e_at);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int exp = std::stoi(sci.substr(e_at + 1));
    const int k = static_cast<int>(digits.size()), n = exp + 1;
    std::string out = neg ? "-" : "";
    if(k <= n && n <= 21) { out += digits; out.append(static_cast<std::size_t>(n - k), '0'); }
    else if(0 < n && n <= 21) { out += digits.substr(0, static_cast<std::size_t>(n)); out += '.'; out += digits.substr(static_cast<std::size_t>(n)); }
    else if(-6 < n && n <= 0) { out += "0."; out.append(static_cast<std::size_t>(-n), '0'); out += digits; }
    else {
        out += digits[0];
        if(k > 1) { out += '.'; out += digits.substr(1); }
        out += 'e'; out += n - 1 >= 0 ? '+' : '-'; out += std::to_string(std::abs(n - 1));
    }
    return out;
}

void dump_to(std::string& out, const Value& v) {
    switch(v.type()) {
        case Value::Type::null: out += "null"; break;
        case Value::Type::boolean: out += v.as_bool() ? "true" : "false"; break;
        case Value::Type::number: { const double d = v.as_number(); out += std::isfinite(d) ? number_text(d) : "null"; break; }
        case Value::Type::string: dump_string(out, v.as_string()); break;
        case Value::Type::array: {
            out += '[';
            bool first = true;
            for(const auto& e : v.as_array()) { if(!first) out += ','; first = false; dump_to(out, e); }
            out += ']';
            break;
        }
        case Value::Type::object: {
            out += '{';
            bool first = true;
            for(const auto& [k, e] : v.as_object()) { if(!first) out += ','; first = false; dump_string(out, k); out += ':'; dump_to(out, e); }
            out += '}';
            break;
        }
    }
}
std::string dump(const Value& v) { std::string s; dump_to(s, v); return s; }

}  // namespace cat::json
