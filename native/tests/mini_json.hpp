#pragma once
// Minimal JSON reader for the test fixtures (objects, arrays, numbers, strings, true / false / null).
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace testjson {
struct Json {
    std::variant<std::nullptr_t, double, std::string, std::vector<Json>, std::map<std::string, Json>, bool> v;
    const Json& operator[](const std::string& k) const { return std::get<std::map<std::string, Json>>(v).at(k); }
    const Json& operator[](std::size_t i) const { return std::get<std::vector<Json>>(v).at(i); }
    std::size_t size() const { return std::get<std::vector<Json>>(v).size(); }
    double num() const { return std::get<double>(v); }
    bool boolean() const { return std::get<bool>(v); }
    bool has(const std::string& k) const { const auto* m = std::get_if<std::map<std::string, Json>>(&v); return m && m->count(k); }
    bool is_bool() const { return std::holds_alternative<bool>(v); }
    const std::string& str() const { return std::get<std::string>(v); }
};
struct Parser {
    const std::string& s; std::size_t i = 0;
    void ws() { while(i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    Json parse() {
        ws();
        if(s[i] == '{') {
            ++i; std::map<std::string, Json> m; ws();
            if(s[i] == '}') { ++i; return {m}; }
            for(;;) { ws(); std::string k = parse().str(); ws(); ++i; m[k] = parse(); ws(); if(s[i] == ',') { ++i; continue; } ++i; return {m}; }
        }
        if(s[i] == '[') {
            ++i; std::vector<Json> a; ws();
            if(s[i] == ']') { ++i; return {a}; }
            for(;;) { a.push_back(parse()); ws(); if(s[i] == ',') { ++i; continue; } ++i; return {a}; }
        }
        if(s[i] == '"') { ++i; std::string o; while(s[i] != '"') { if(s[i] == '\\') ++i; o += s[i++]; } ++i; return {o}; }
        if(s.compare(i, 4, "true") == 0) { i += 4; return {true}; }
        if(s.compare(i, 5, "false") == 0) { i += 5; return {false}; }
        if(s.compare(i, 4, "null") == 0) { i += 4; return {nullptr}; }
        std::size_t used = 0; const double d = std::stod(s.substr(i, 40), &used); i += used; return {d};
    }
};
Json load_json(const std::filesystem::path& p) {
    std::ifstream in(p); if(!in) throw std::runtime_error("cannot open " + p.string());
    std::stringstream ss; ss << in.rdbuf(); const std::string text = ss.str();
    Parser parser{text}; return parser.parse();
}

}  // namespace testjson
