#pragma once
// N8: a small JSON value, parser and writer for the multiplayer protocol (the legacy relay and game
// packets are JSON text; see docs/N8_MULTIPLAYER.md). Objects keep their key order, so messages are
// written with the keys in the order the legacy writes them. Numbers are doubles (JSON / JavaScript).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cat::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

class Value {
public:
    enum class Type : std::uint8_t { null, boolean, number, string, array, object };
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(double d) : v_(d) {}
    Value(int i) : v_(static_cast<double>(i)) {}
    Value(unsigned i) : v_(static_cast<double>(i)) {}
    Value(long long i) : v_(static_cast<double>(i)) {}
    Value(unsigned long long i) : v_(static_cast<double>(i)) {}
    Value(unsigned long i) : v_(static_cast<double>(i)) {}
    Value(long i) : v_(static_cast<double>(i)) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(std::string_view s) : v_(std::string(s)) {}
    Value(Array a) : v_(std::move(a)) {}
    Value(Object o) : v_(std::move(o)) {}

    Type type() const { return static_cast<Type>(v_.index()); }
    bool is_null() const { return type() == Type::null; }
    bool is_bool() const { return type() == Type::boolean; }
    bool is_number() const { return type() == Type::number; }
    bool is_string() const { return type() == Type::string; }
    bool is_array() const { return type() == Type::array; }
    bool is_object() const { return type() == Type::object; }

    bool as_bool(bool fallback = false) const { return is_bool() ? std::get<bool>(v_) : fallback; }
    double as_number(double fallback = 0) const { return is_number() ? std::get<double>(v_) : fallback; }
    const std::string& as_string() const;   // "" unless a string
    const Array& as_array() const;          // empty unless an array
    const Object& as_object() const;        // empty unless an object
    Array& array();                         // turns this value into an array if it is not one
    Object& object();                       // turns this value into an object if it is not one

    // Object access: nullptr / a null value when absent.
    const Value* find(std::string_view key) const;
    const Value& operator[](std::string_view key) const;
    Value& set(std::string_view key, Value v);   // replaces or appends (keeps the key order)
    // JavaScript truthiness (!!value): false, 0, NaN, "", null are false.
    bool truthy() const;
    // `typeof v === 'number' && Number.isFinite(v)`
    bool finite_number() const;
private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v_;
};

// Throws std::runtime_error on malformed text (with the byte offset). Depth limit 64.
Value parse(std::string_view text);
std::optional<Value> try_parse(std::string_view text);
// JSON.stringify for these values: shortest round-trip numbers ("1", "0.5", "1e+21"), non-finite
// numbers as null, strings escaped as JavaScript does (control characters as \uXXXX, U+2028/2029 kept).
std::string dump(const Value& v);
void dump_to(std::string& out, const Value& v);
std::string number_text(double d);   // JavaScript Number#toString for finite d

}  // namespace cat::json
