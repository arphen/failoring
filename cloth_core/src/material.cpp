#include "failoring/material.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace fl {
namespace {

// ---- tiny JSON reader (objects, arrays, strings, numbers, bool, null) -------
// Just enough for the materials file; kept dependency-free on purpose.
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
    if (it->second.type != JValue::Num) throw std::runtime_error(std::string("material field '") + key + "' must be a number");
    return it->second.n;
}
std::string getStr(const JValue& o, const char* key) {
    auto it = o.o.find(key);
    return (it != o.o.end() && it->second.type == JValue::Str) ? it->second.s : std::string();
}

}  // namespace

std::vector<FabricMaterial> loadMaterialsFromString(const std::string& text) {
    JValue root = Parser(text).parse();
    const JValue* list = &root;
    if (root.type == JValue::Obj) {
        auto it = root.o.find("fabrics");
        if (it == root.o.end()) throw std::runtime_error("materials JSON: missing 'fabrics' array");
        list = &it->second;
    }
    if (list->type != JValue::Arr) throw std::runtime_error("materials JSON: 'fabrics' must be an array");

    std::vector<FabricMaterial> out;
    for (const JValue& f : list->a) {
        if (f.type != JValue::Obj) throw std::runtime_error("materials JSON: fabric entry must be an object");
        FabricMaterial m;
        m.id = getStr(f, "id");
        if (m.id.empty()) throw std::runtime_error("materials JSON: fabric without 'id'");
        m.name = getStr(f, "name");
        m.category = getStr(f, "category");
        m.notes = getStr(f, "notes");
        const JValue* p = &f;
        auto pit = f.o.find("params");
        if (pit != f.o.end() && pit->second.type == JValue::Obj) p = &pit->second;
        m.density = getNum(*p, "density_kg_m2", m.density);
        m.thickness = getNum(*p, "thickness_m", m.thickness);
        m.tensileWarp = getNum(*p, "tensile_warp_N_m", m.tensileWarp);
        m.tensileWeft = getNum(*p, "tensile_weft_N_m", m.tensileWeft);
        m.shear = getNum(*p, "shear_N_m_rad", m.shear);
        m.bendWarp = getNum(*p, "bending_warp_Nm", m.bendWarp);
        m.bendWeft = getNum(*p, "bending_weft_Nm", m.bendWeft);
        m.compressionRatio = getNum(*p, "compression_ratio", m.compressionRatio);
        m.frictionStatic = getNum(*p, "friction_static", m.frictionStatic);
        m.frictionKinetic = getNum(*p, "friction_kinetic", m.frictionKinetic);
        m.damping = getNum(*p, "damping_1_s", m.damping);
        // J-curve lock terms (CROCHET_PLAN §2.3), optional so the old JSON files stay valid and unchanged in meaning.
        m.lockStrainWarp = getNum(*p, "lock_strain_warp", m.lockStrainWarp);
        m.lockStrainWeft = getNum(*p, "lock_strain_weft", m.lockStrainWeft);
        m.lockStiffnessRatio = getNum(*p, "lock_stiffness_ratio", m.lockStiffnessRatio);
        if (m.lockStrainWarp < 0 || m.lockStrainWeft < 0 || m.lockStiffnessRatio < 1)
            throw std::runtime_error("materials JSON: bad lock parameters in fabric '" + m.id + "'");
        if (m.density <= 0 || m.tensileWarp <= 0 || m.tensileWeft <= 0 || m.shear <= 0 || m.bendWarp <= 0 || m.bendWeft <= 0)
            throw std::runtime_error("materials JSON: non-positive parameter in fabric '" + m.id + "'");
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<FabricMaterial> loadMaterialsFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open materials file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return loadMaterialsFromString(ss.str());
}

const FabricMaterial* findMaterial(const std::vector<FabricMaterial>& list, const std::string& id) {
    for (const auto& m : list)
        if (m.id == id) return &m;
    return nullptr;
}

}  // namespace fl
