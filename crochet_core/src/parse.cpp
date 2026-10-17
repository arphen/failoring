#include "failoring/crochet/pattern.hpp"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "failoring/error.hpp"

namespace fl {
namespace {

// ---- tiny JSON reader (objects, arrays, strings, numbers, bool, null) -------
// Same reader as cloth_core/src/material.cpp and crochet_core/src/yarn.cpp:
// just enough for the data files, kept dependency-free on purpose.
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

[[noreturn]] void jsonFail(const std::string& msg) {
    FL_THROW(std::runtime_error, "pattern JSON: " + msg);
}

int utf8Fail(size_t offset, const std::string& msg) {
    std::ostringstream os;
    os << "pattern JSON: invalid UTF-8 at byte offset " << offset << ": " << msg;
    throw std::runtime_error(os.str());
}

// Reject invalid UTF-8 before the JSON reader copies bytes through unchanged,
// naming the first offending byte's offset.
void validateUtf8(const std::string& t) {
    const size_t n = t.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        size_t extra = 0;
        unsigned cp = 0;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
        else utf8Fail(i, "invalid leading byte");
        if (i + extra >= n) utf8Fail(i, "truncated multi-byte sequence");
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(t[i + k]);
            if ((cc & 0xC0) != 0x80) utf8Fail(i, "invalid continuation byte");
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
            utf8Fail(i, "invalid code point");
        i += extra + 1;
    }
}

const JValue* member(const JValue& o, const char* key) {
    auto it = o.o.find(key);
    return it == o.o.end() ? nullptr : &it->second;
}

const JValue* arrayMember(const JValue& o, const char* key, bool required) {
    const JValue* v = member(o, key);
    if (v == nullptr) {
        if (required) jsonFail(std::string("missing '") + key + "' array");
        return nullptr;
    }
    if (v->type != JValue::Arr) jsonFail(std::string("'") + key + "' must be an array");
    return v;
}

int intField(const JValue& o, const char* key, const std::string& where) {
    const JValue* v = member(o, key);
    if (v == nullptr || v->type != JValue::Num) jsonFail(where + ": field '" + key + "' must be a number");
    if (v->n != std::floor(v->n)) jsonFail(where + ": field '" + key + "' must be an integer");
    return static_cast<int>(v->n);
}

std::string strField(const JValue& o, const char* key, const std::string& where) {
    const JValue* v = member(o, key);
    if (v == nullptr || v->type != JValue::Str) jsonFail(where + ": field '" + key + "' must be a string");
    return v->s;
}

bool boolField(const JValue& o, const char* key, const std::string& where) {
    const JValue* v = member(o, key);
    if (v == nullptr || v->type != JValue::Bool) jsonFail(where + ": field '" + key + "' must be true or false");
    return v->b;
}

std::vector<int> intArray(const JValue& o, const char* key, const std::string& where) {
    const JValue* v = member(o, key);
    if (v == nullptr || v->type != JValue::Arr) jsonFail(where + ": field '" + key + "' must be an array");
    std::vector<int> out;
    out.reserve(v->a.size());
    for (size_t i = 0; i < v->a.size(); ++i) {
        const JValue& e = v->a[i];
        if (e.type != JValue::Num || e.n != std::floor(e.n))
            jsonFail(where + ": field '" + key + "'[" + std::to_string(i) + "] must be an integer");
        out.push_back(static_cast<int>(e.n));
    }
    return out;
}

const char* const kLoops[] = {"both", "blo", "flo", "fpost", "bpost", "space"};
bool isLoop(const std::string& v) {
    for (const char* l : kLoops)
        if (v == l) return true;
    return false;
}

// ---- `#!` extension lines ---------------------------------------------------

[[noreturn]] void extFail(int lineNo, const std::string& msg) {
    FL_THROW(std::runtime_error, "crochet extension line " + std::to_string(lineNo) + ": " + msg);
}

// One whitespace-separated token list; trailing whitespace (and a CR from a
// CRLF file) is not a token.
std::vector<std::string> tokensOf(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

// A whole non-negative decimal integer: no sign, no trailing garbage. Returns
// false when the token has anything else.
bool wholeNumber(const std::string& s, int* out) {
    const char* begin = s.c_str();
    char* end = nullptr;
    const long v = std::strtol(begin, &end, 10);
    if (end == begin || *end != '\0' || v < 0 || v > 0x7FFFFFFF) return false;
    *out = static_cast<int>(v);
    return true;
}

// `[row,pos]` with free whitespace: the brackets are required, the two numbers
// are decimal integers. Returns false when the token has anything else.
bool parseAddress(const std::string& tok, int* row, int* pos) {
    if (tok.size() < 5 || tok.front() != '[' || tok.back() != ']') return false;
    const std::string inner = tok.substr(1, tok.size() - 2);
    const size_t comma = inner.find(',');
    if (comma == std::string::npos) return false;
    const std::string a = inner.substr(0, comma);
    const std::string b = inner.substr(comma + 1);
    return wholeNumber(a, row) && wholeNumber(b, pos);
}

// `[row]`, the `#! count` address: one number, not the `[row,pos]` form.
bool parseRowAddress(const std::string& tok, int* row) {
    if (tok.size() < 3 || tok.front() != '[' || tok.back() != ']') return false;
    return wholeNumber(tok.substr(1, tok.size() - 2), row);
}

std::string addressText(int row, int pos) {
    return "[" + std::to_string(row) + "," + std::to_string(pos) + "]";
}

std::string rowAddressText(int row) { return "[" + std::to_string(row) + "]"; }

// Apply every `#!` line of the pattern text to the imported graph. Lines whose
// first two characters are not `#!` are pattern text and ignored here.
void applyExtensionLines(const std::string& text, StitchGraph& g) {
    // (row, index) -> stitch id, the addressing `#! link` uses.
    std::map<std::pair<int, int>, int> byAddress;
    for (const Stitch& s : g.st) {
        const auto ins = byAddress.emplace(std::make_pair(s.row, s.index), s.id);
        if (!ins.second)
            jsonFail("stitches " + std::to_string(ins.first->second) + " and " + std::to_string(s.id) +
                     " share the address " + addressText(s.row, s.index));
    }
    std::set<int> linked;  // stitches already used by a link (JSON links included)
    for (const Link& l : g.links) {
        linked.insert(l.a);
        linked.insert(l.b);
    }

    // The `#! count` expected values: the stitch nodes of every row, turning
    // chains and join sl sts included (that is exactly Stitch::row).
    std::vector<int> rowNodes(g.rows.size(), 0);
    for (const Stitch& s : g.st) ++rowNodes[s.row];

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.size() < 2 || line[0] != '#' || line[1] != '!') continue;
        const std::vector<std::string> tok = tokensOf(line.substr(2));
        if (tok.empty())
            extFail(lineNo, "missing keyword (expected `link [row,pos] [row,pos] <kind>` or `count [row] N`)");
        if (tok[0] == "count") {
            if (tok.size() != 3)
                extFail(lineNo, "expected `count [row] N`, got " + std::to_string(tok.size() - 1) +
                                    " argument(s) after the keyword");
            int row = 0;
            if (!parseRowAddress(tok[1], &row))
                extFail(lineNo, "cannot parse address '" + tok[1] + "' (expected [row])");
            int want = 0;
            if (!wholeNumber(tok[2], &want))
                extFail(lineNo, "cannot parse count '" + tok[2] + "' (expected a non-negative integer)");
            if (rowNodes.empty())
                extFail(lineNo, "row " + rowAddressText(row) + " does not exist (the export has no rows)");
            if (row >= static_cast<int>(rowNodes.size()))
                extFail(lineNo, "row " + rowAddressText(row) + " does not exist (rows are 0.." +
                                    std::to_string(rowNodes.size() - 1) + ")");
            const int have = rowNodes[row];
            if (have != want)
                extFail(lineNo, "row " + rowAddressText(row) + " has " + std::to_string(have) +
                                    " stitches, the pattern says " + std::to_string(want));
            continue;
        }
        if (tok[0] != "link")
            extFail(lineNo, "unknown keyword '" + tok[0] + "' (T4.2 implements 'link' and 'count')");
        if (tok.size() != 4)
            extFail(lineNo, "expected `link [row,pos] [row,pos] <kind>`, got " + std::to_string(tok.size() - 1) +
                                " argument(s) after the keyword");
        int rowA = 0, posA = 0, rowB = 0, posB = 0;
        if (!parseAddress(tok[1], &rowA, &posA))
            extFail(lineNo, "cannot parse address '" + tok[1] + "' (expected [row,pos])");
        if (!parseAddress(tok[2], &rowB, &posB))
            extFail(lineNo, "cannot parse address '" + tok[2] + "' (expected [row,pos])");
        const std::string& kind = tok[3];
        if (kind != "both_layers" && kind != "join")
            extFail(lineNo, "unknown kind '" + kind + "' (expected both_layers or join)");
        const auto itA = byAddress.find(std::make_pair(rowA, posA));
        if (itA == byAddress.end()) extFail(lineNo, "address " + addressText(rowA, posA) + " does not name a stitch");
        const auto itB = byAddress.find(std::make_pair(rowB, posB));
        if (itB == byAddress.end()) extFail(lineNo, "address " + addressText(rowB, posB) + " does not name a stitch");
        if (itA->second == itB->second)
            extFail(lineNo, "a link needs two different stitches, both addresses name stitch " + std::to_string(itA->second));
        if (!linked.insert(itA->second).second)
            extFail(lineNo, addressText(rowA, posA) + " already appears in another link");
        if (!linked.insert(itB->second).second)
            extFail(lineNo, addressText(rowB, posB) + " already appears in another link");
        g.links.push_back(Link{itA->second, itB->second, kind});
    }
}

}  // namespace

StitchGraph importPattern(const std::string& jsonText, const std::string& extensionText) {
    validateUtf8(jsonText);
    JValue root = Parser(jsonText).parse();
    if (root.type != JValue::Obj) jsonFail("root must be an object");

    // CrochetPARADE's own errors win: the rest of the export may be incomplete.
    if (const JValue* errs = arrayMember(root, "errors", false)) {
        if (!errs->a.empty()) {
            if (errs->a.front().type != JValue::Obj) jsonFail("'errors[0]' must be an object");
            const int line = intField(errs->a.front(), "line", "errors[0]");
            const std::string msg = strField(errs->a.front(), "msg", "errors[0]");
            FL_THROW(std::runtime_error, "CrochetPARADE line " + std::to_string(line) + ": " + msg);
        }
    }

    StitchGraph g;

    // rows first: stitch validation checks against the row id range.
    if (const JValue* rows = arrayMember(root, "rows", false)) {
        g.rows.reserve(rows->a.size());
        for (size_t i = 0; i < rows->a.size(); ++i) {
            const JValue& r = rows->a[i];
            const std::string where = "rows[" + std::to_string(i) + "]";
            if (r.type != JValue::Obj) jsonFail(where + " must be an object");
            Row row;
            row.id = intField(r, "id", where);
            if (row.id != static_cast<int>(i))
                jsonFail(where + ": id is " + std::to_string(row.id) + ", but row ids must be 0..N-1 in order");
            row.piece = strField(r, "piece", where);
            row.kind = strField(r, "kind", where);
            if (row.kind != "row" && row.kind != "round")
                jsonFail(where + ": kind '" + row.kind + "' is not 'row' or 'round'");
            row.turned = boolField(r, "turned", where);
            g.rows.push_back(std::move(row));
        }
    }

    const JValue* stitches = arrayMember(root, "stitches", true);
    g.st.reserve(stitches->a.size());
    for (size_t i = 0; i < stitches->a.size(); ++i) {
        const JValue& s = stitches->a[i];
        const std::string where = "stitches[" + std::to_string(i) + "]";
        if (s.type != JValue::Obj) jsonFail(where + " must be an object");
        Stitch st;
        st.id = intField(s, "id", where);
        if (st.id != static_cast<int>(i))
            jsonFail(where + ": id is " + std::to_string(st.id) + ", but stitch ids must be 0..N-1 in order");
        st.type = strField(s, "type", where);
        st.piece = strField(s, "piece", where);
        st.row = intField(s, "row", where);
        st.index = intField(s, "index", where);
        st.loop = strField(s, "loop", where);
        if (!isLoop(st.loop))
            jsonFail(where + ": loop '" + st.loop + "' is not one of both|blo|flo|fpost|bpost|space");
        if (g.rows.empty())
            jsonFail(where + ": row id " + std::to_string(st.row) + " does not exist (the JSON has no rows)");
        if (st.row < 0 || st.row >= static_cast<int>(g.rows.size()))
            jsonFail(where + ": row id " + std::to_string(st.row) + " does not exist (rows are 0.." +
                     std::to_string(g.rows.size() - 1) + ")");
        st.targets = intArray(s, "targets", where);
        for (const int t : st.targets)
            if (t < 0 || t >= st.id)
                jsonFail(where + ": target " + std::to_string(t) + " must be an existing stitch id smaller than " +
                         std::to_string(st.id));
        st.prev = intField(s, "prev", where);
        if (st.prev < -1 || st.prev >= static_cast<int>(stitches->a.size()))
            jsonFail(where + ": prev " + std::to_string(st.prev) + " is neither -1 nor an existing stitch id");
        st.line = intField(s, "line", where);
        g.st.push_back(std::move(st));
    }

    if (const JValue* links = arrayMember(root, "links", false)) {
        std::set<int> linked;
        for (size_t i = 0; i < links->a.size(); ++i) {
            const JValue& l = links->a[i];
            const std::string where = "links[" + std::to_string(i) + "]";
            if (l.type != JValue::Obj) jsonFail(where + " must be an object");
            Link link;
            link.a = intField(l, "a", where);
            link.b = intField(l, "b", where);
            link.kind = strField(l, "kind", where);
            if (link.kind != "both_layers" && link.kind != "join")
                jsonFail(where + ": kind '" + link.kind + "' is not 'both_layers' or 'join'");
            if (link.a < 0 || link.a >= static_cast<int>(g.st.size()))
                jsonFail(where + ": a " + std::to_string(link.a) + " does not exist");
            if (link.b < 0 || link.b >= static_cast<int>(g.st.size()))
                jsonFail(where + ": b " + std::to_string(link.b) + " does not exist");
            if (link.a == link.b) jsonFail(where + ": a and b must be different stitches (both " + std::to_string(link.a) + ")");
            for (const int id : {link.a, link.b})
                if (!linked.insert(id).second)
                    jsonFail(where + ": stitch " + std::to_string(id) + " appears in more than one link");
            g.links.push_back(std::move(link));
        }
    }

    if (!extensionText.empty()) applyExtensionLines(extensionText, g);

    // RS/WS facing and RS-relative loop insertion (plan section 5.5), resolved
    // for every import, after the extension lines have been applied.
    resolveFacing(g);
    return g;
}

StitchGraph importPatternFile(const std::string& jsonPath, const std::string& patternPath) {
    std::ifstream jsonIn(jsonPath, std::ios::binary);
    if (!jsonIn) FL_THROW(std::runtime_error, "cannot open pattern JSON file: " + jsonPath);
    std::stringstream jsonBuf;
    jsonBuf << jsonIn.rdbuf();

    std::string extText;
    if (!patternPath.empty()) {
        std::ifstream extIn(patternPath, std::ios::binary);
        if (!extIn) FL_THROW(std::runtime_error, "cannot open pattern file: " + patternPath);
        std::stringstream extBuf;
        extBuf << extIn.rdbuf();
        extText = extBuf.str();
    }
    return importPattern(jsonBuf.str(), extText);
}

}  // namespace fl
