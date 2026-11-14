// Минимальный JSON-парсер (достаточно для glTF/GLB).
#pragma once
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace eng {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    static const Json& nul() { static const Json n; return n; }
    const Json& operator[](const char* key) const {
        for (auto& p : obj) if (p.first == key) return p.second;
        return nul();
    }
    const Json& operator[](int i) const { return i >= 0 ? (*this)[(size_t)i] : nul(); }
    const Json& operator[](size_t i) const { return i < arr.size() ? arr[i] : nul(); }
    bool has(const char* key) const {
        for (auto& p : obj) if (p.first == key) return true;
        return false;
    }
    size_t size() const { return type == Arr ? arr.size() : obj.size(); }
    bool isNull() const { return type == Null; }
    double number(double def = 0) const { return type == Num ? num : def; }
    int integer(int def = 0) const { return type == Num ? (int)num : def; }
    const std::string& string() const { return str; }
};

class JsonParser {
public:
    JsonParser(const char* s, size_t n) : p_(s), end_(s + n) {}
    bool parse(Json& out) {
        ws();
        if (!value(out)) return false;
        ws();
        return true;
    }

private:
    const char* p_;
    const char* end_;
    void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t')) p_++; }
    bool lit(const char* s) {
        size_t n = 0;
        while (s[n]) n++;
        if ((size_t)(end_ - p_) < n) return false;
        for (size_t i = 0; i < n; i++) if (p_[i] != s[i]) return false;
        p_ += n;
        return true;
    }
    bool value(Json& o) {
        if (p_ >= end_) return false;
        char c = *p_;
        if (c == '{') return object(o);
        if (c == '[') return array(o);
        if (c == '"') { o.type = Json::Str; return string(o.str); }
        if (c == 't') { o.type = Json::Bool; o.b = true; return lit("true"); }
        if (c == 'f') { o.type = Json::Bool; o.b = false; return lit("false"); }
        if (c == 'n') { o.type = Json::Null; return lit("null"); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* e = nullptr;
            o.type = Json::Num;
            o.num = std::strtod(p_, &e);
            if (e == p_) return false;
            p_ = e;
            return true;
        }
        return false;
    }
    bool string(std::string& s) {
        p_++;  // "
        s.clear();
        while (p_ < end_ && *p_ != '"') {
            if (*p_ == '\\') {
                p_++;
                if (p_ >= end_) return false;
                switch (*p_) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u': {
                        if (end_ - p_ < 5) return false;
                        unsigned cp = (unsigned)std::strtoul(std::string(p_ + 1, 4).c_str(), nullptr, 16);
                        p_ += 4;
                        if (cp < 0x80) s += (char)cp;
                        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
                        else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
                        break;
                    }
                    default: s += *p_;
                }
                p_++;
            } else {
                s += *p_++;
            }
        }
        if (p_ >= end_) return false;
        p_++;  // closing "
        return true;
    }
    bool array(Json& o) {
        o.type = Json::Arr;
        p_++;
        ws();
        if (p_ < end_ && *p_ == ']') { p_++; return true; }
        while (true) {
            o.arr.emplace_back();
            ws();
            if (!value(o.arr.back())) return false;
            ws();
            if (p_ >= end_) return false;
            if (*p_ == ',') { p_++; continue; }
            if (*p_ == ']') { p_++; return true; }
            return false;
        }
    }
    bool object(Json& o) {
        o.type = Json::Obj;
        p_++;
        ws();
        if (p_ < end_ && *p_ == '}') { p_++; return true; }
        while (true) {
            ws();
            if (p_ >= end_ || *p_ != '"') return false;
            std::string key;
            if (!string(key)) return false;
            ws();
            if (p_ >= end_ || *p_ != ':') return false;
            p_++;
            ws();
            o.obj.emplace_back(key, Json());
            if (!value(o.obj.back().second)) return false;
            ws();
            if (p_ >= end_) return false;
            if (*p_ == ',') { p_++; continue; }
            if (*p_ == '}') { p_++; return true; }
            return false;
        }
    }
};

}  // namespace eng
