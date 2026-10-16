#include "failoring/crochet/yarn.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "failoring/error.hpp"

namespace fl {
namespace {

// ---- tiny JSON reader (objects, arrays, strings, numbers, bool, null) -------
// Same reader as cloth_core/src/material.cpp: just enough for the data files,
// kept dependency-free on purpose. Copied so crochet_core gets no new deps.
struct JValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JValue> a;
    std::map<std::string, JValue> o;
};

class Parser {
public:
    explicit Parser(const std::string& t) : txt_(t) {}
    JValue parse() {
        JValue v = value();
        ws();
        if (pos_ != txt_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& txt_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const char* msg) const {
        std::ostringstream os;
        os << "JSON error at offset " << pos_ << ": " << msg;
        throw std::runtime_error(os.str());
    }
    void ws() {
        while (pos_ < txt_.size() && std::isspace(static_cast<unsigned char>(txt_[pos_]))) ++pos_;
    }
    char peek() {
        ws();
        if (pos_ >= txt_.size()) fail("unexpected end");
        return txt_[pos_];
    }
    void expect(char c) {
        if (peek() != c) fail("unexpected character");
        ++pos_;
    }
    bool match(const char* lit) {
        const size_t n = std::char_traits<char>::length(lit);
        if (txt_.compare(pos_, n, lit) == 0) { pos_ += n; return true; }
        return false;
    }
    std::string str() {
        expect('"');
        std::string out;
        while (pos_ < txt_.size() && txt_[pos_] != '"') {
            char c = txt_[pos_++];
            if (c == '\\') {
                if (pos_ >= txt_.size()) fail("bad escape");
                char e = txt_[pos_++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {  // keep BMP code points as UTF-8
                        if (pos_ + 4 > txt_.size()) fail("bad \\u escape");
                        unsigned cp = static_cast<unsigned>(std::strtoul(txt_.substr(pos_, 4).c_str(), nullptr, 16));
                        pos_ += 4;
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                        else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += e;  // \" \\ \/
                }
            } else {
                out += c;
            }
        }
        if (pos_ >= txt_.size()) fail("unterminated string");
        ++pos_;
        return out;
    }
    JValue value() {
        JValue v;
        const char c = peek();
        if (c == '{') {
            ++pos_;
            v.type = JValue::Obj;
            if (peek() == '}') { ++pos_; return v; }
            for (;;) {
                std::string k = str();
                expect(':');
                v.o[k] = value();
                if (peek() == ',') { ++pos_; continue; }
                expect('}');
                break;
            }
        } else if (c == '[') {
            ++pos_;
            v.type = JValue::Arr;
            if (peek() == ']') { ++pos_; return v; }
            for (;;) {
                v.a.push_back(value());
                if (peek() == ',') { ++pos_; continue; }
                expect(']');
                break;
            }
        } else if (c == '"') {
            v.type = JValue::Str;
            v.s = str();
        } else if (match("true")) {
            v.type = JValue::Bool; v.b = true;
        } else if (match("false")) {
            v.type = JValue::Bool; v.b = false;
        } else if (match("null")) {
            v.type = JValue::Null;
        } else {
            char* end = nullptr;
            v.n = std::strtod(txt_.c_str() + pos_, &end);
            if (end == txt_.c_str() + pos_) fail("invalid value");
            pos_ = static_cast<size_t>(end - txt_.c_str());
            v.type = JValue::Num;
        }
        return v;
    }
};

double getNum(const JValue& o, const char* key, double fallback) {
    auto it = o.o.find(key);
    if (it == o.o.end()) return fallback;
    if (it->second.type != JValue::Num)
        FL_THROW(std::runtime_error, std::string("yarns JSON: field '") + key + "' must be a number");
    return it->second.n;
}
std::string getStr(const JValue& o, const char* key) {
    auto it = o.o.find(key);
    return (it != o.o.end() && it->second.type == JValue::Str) ? it->second.s : std::string();
}

const JValue* requireObjMember(const JValue& root, const char* key, JValue::Type type) {
    auto it = root.o.find(key);
    if (it == root.o.end()) return nullptr;
    if (it->second.type != type)
        FL_THROW(std::runtime_error, std::string("yarns JSON: '") + key + "' has the wrong type");
    return &it->second;
}

}  // namespace

const Yarn* YarnBook::find(const std::string& id) const {
    for (const auto& y : yarns)
        if (y.id == id) return &y;
    return nullptr;
}

const YarnFibre* YarnBook::fibre(const std::string& id) const {
    auto it = fibres.find(id);
    return it == fibres.end() ? nullptr : &it->second;
}

YarnBook loadYarnsFromString(const std::string& jsonText) {
    JValue root = Parser(jsonText).parse();
    if (root.type != JValue::Obj) FL_THROW(std::runtime_error, "yarns JSON: root must be an object");

    YarnBook book;

    if (const JValue* hooks = requireObjMember(root, "hooks_mm", JValue::Obj)) {
        for (const auto& kv : hooks->o) {
            if (kv.second.type != JValue::Num)
                FL_THROW(std::runtime_error, "yarns JSON: hook '" + kv.first + "' must be a number");
            book.hooksMm[kv.first] = kv.second.n;
        }
    }

    if (const JValue* fibres = requireObjMember(root, "fibres", JValue::Obj)) {
        for (const auto& kv : fibres->o) {
            if (kv.second.type != JValue::Obj)
                FL_THROW(std::runtime_error, "yarns JSON: fibre '" + kv.first + "' must be an object");
            YarnFibre f;
            f.bend = getNum(kv.second, "bend", f.bend);
            f.tensile = getNum(kv.second, "tensile", f.tensile);
            f.lock = getNum(kv.second, "lock", f.lock);
            f.friction = getNum(kv.second, "friction", f.friction);
            f.sheen = getNum(kv.second, "sheen", f.sheen);
            book.fibres[kv.first] = f;
        }
    }

    auto yit = root.o.find("yarns");
    if (yit == root.o.end() || yit->second.type != JValue::Arr)
        FL_THROW(std::runtime_error, "yarns JSON: missing 'yarns' array");
    const JValue* list = &yit->second;

    std::set<std::string> seen;
    for (const JValue& y : list->a) {
        if (y.type != JValue::Obj) FL_THROW(std::runtime_error, "yarns JSON: yarn entry must be an object");
        Yarn yarn;
        yarn.id = getStr(y, "id");
        if (yarn.id.empty()) FL_THROW(std::runtime_error, "yarns JSON: yarn without 'id'");
        if (!seen.insert(yarn.id).second)
            FL_THROW(std::runtime_error, "yarns JSON: duplicate yarn id '" + yarn.id + "'");
        yarn.name = getStr(y, "name");
        yarn.fibre = getStr(y, "fibre");
        yarn.colour = getStr(y, "colour");
        yarn.weightClass = static_cast<int>(getNum(y, "weight_class", yarn.weightClass));
        yarn.grams = getNum(y, "grams", yarn.grams);
        yarn.metres = getNum(y, "metres", yarn.metres);
        yarn.diameterMm = getNum(y, "diameter_mm", yarn.diameterMm);
        yarn.hookMm = getNum(y, "hook_mm", yarn.hookMm);
        if (book.fibres.find(yarn.fibre) == book.fibres.end())
            FL_THROW(std::runtime_error, "yarns JSON: yarn '" + yarn.id + "' has unknown fibre '" + yarn.fibre + "'");
        yarn.fibreParams = book.fibres[yarn.fibre];  // T3.1: multipliers for the stitch model
        book.yarns.push_back(std::move(yarn));
    }
    return book;
}

YarnBook loadYarnsFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) FL_THROW(std::runtime_error, "cannot open yarns file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return loadYarnsFromString(ss.str());
}

}  // namespace fl
