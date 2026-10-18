// The headless pipeline and the crochet_cli tool (docs/CROCHET_PLAN.md section 7.1, ticket T6.4): fixtures run from the CLI with
// the documented exit codes, and write the JSON, the OBJ (with stitch ids), the rest OBJ and the PNG snapshots.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/run.hpp"

using namespace fl;

#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

static std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static int countLines(const std::string& text, const std::string& prefix) {
    int n = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t e = text.find('\n', pos);
        const std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (line.rfind(prefix, 0) == 0) ++n;
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return n;
}
static std::string fixture(const std::string& name) { return std::string(FL_PATTERNS_DIR) + "/cp/" + name + ".txt"; }

// Run the tool, return its exit code.
static int cli(const std::string& args) {
    const std::string cmd = std::string("\"") + FL_CLI_PATH + "\" " + args + " --yarns \"" + FL_YARNS_PATH + "\" --quiet > /dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

// A PNG written by snapshot.cpp: signature, IHDR 1440 x 360, ends with IEND, small (the deflate works).
static bool validPng(const std::string& path, size_t* size) {
    const std::string d = slurp(path);
    *size = d.size();
    if (d.size() < 60 || d.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return false;
    auto be = [&](size_t o) { return (static_cast<unsigned char>(d[o]) << 24) | (static_cast<unsigned char>(d[o + 1]) << 16) | (static_cast<unsigned char>(d[o + 2]) << 8) | static_cast<unsigned char>(d[o + 3]); };
    return d.compare(12, 4, "IHDR") == 0 && be(16) == 1440 && be(20) == 360 && d.compare(d.size() - 8, 4, "IEND") == 0 && d.size() < 400000;
}

// An independent reader for exactly the PNGs snapshot.cpp writes (one fixed-Huffman deflate block, matches only at distance 3): returns the
// unfiltered RGB pixels (rows of 1440 x 3 bytes), or an empty vector when the stream is not what the writer promises.
static std::vector<unsigned char> readSnapshotPixels(const std::string& d) {
    // find the IDAT chunk
    size_t pos = 8;
    std::string idat;
    while (pos + 12 <= d.size()) {
        const size_t n = (static_cast<unsigned char>(d[pos]) << 24) | (static_cast<unsigned char>(d[pos + 1]) << 16) | (static_cast<unsigned char>(d[pos + 2]) << 8) | static_cast<unsigned char>(d[pos + 3]);
        if (d.compare(pos + 4, 4, "IDAT") == 0) idat += d.substr(pos + 8, n);
        pos += 12 + n;
    }
    if (idat.size() < 8) return {};
    size_t bytePos = 2;  // after the zlib header
    int bit = 0;
    auto getBit = [&]() {
        const int b = (static_cast<unsigned char>(idat[bytePos]) >> bit) & 1;
        if (++bit == 8) { bit = 0; ++bytePos; }
        return b;
    };
    auto getBits = [&](int n) { int v = 0; for (int i = 0; i < n; ++i) v |= getBit() << i; return v; };
    if (getBits(1) != 1 || getBits(2) != 1) return {};
    static const int lenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    std::vector<unsigned char> raw;
    while (bytePos < idat.size()) {
        int code = 0, len = 0;
        // fixed Huffman: 7, 8 or 9 bits, most significant bit first
        for (; len < 9;) {
            code = (code << 1) | getBit();
            ++len;
            if (len == 7 && code <= 0x17) { code = 256 + code; break; }
            if (len == 8 && code >= 0x30 && code <= 0xbf) { code -= 0x30; break; }
            if (len == 8 && code >= 0xc0 && code <= 0xc7) { code = 280 + code - 0xc0; break; }
            if (len == 9 && code >= 0x190 && code <= 0x1ff) { code = 144 + code - 0x190; break; }
        }
        if (code < 256) {
            raw.push_back(static_cast<unsigned char>(code));
        } else if (code == 256) {
            break;
        } else {
            const int c = code - 257;
            if (c < 0 || c > 28) return {};
            const int length = lenBase[c] + getBits(lenExtra[c]);
            int dist = 0;
            for (int i = 0; i < 5; ++i) dist = (dist << 1) | getBit();  // 5-bit distance code, no extra bits for code 2
            if (dist != 2 || raw.size() < 3) return {};
            for (int i = 0; i < length; ++i) raw.push_back(raw[raw.size() - 3]);
        }
    }
    const size_t W = 1440 * 3;
    if (raw.size() != 360 * (W + 1)) return {};
    std::vector<unsigned char> rgb;
    for (size_t y = 0; y < 360; ++y) {
        if (raw[y * (W + 1)] != 1) return {};
        for (size_t x = 0; x < W; ++x) rgb.push_back(static_cast<unsigned char>(raw[y * (W + 1) + 1 + x] + (x >= 3 ? rgb[y * W + x - 3] : 0)));
    }
    return rgb;
}

int main() {
    std::printf("[crochet cli] %s\n", FL_CLI_PATH);
    const std::string out = std::string(FL_OUT_DIR) + "/crochet_cli_test";
    std::filesystem::create_directories(out);

    // --- library: a relax-only fixture writes every file, and the files agree with each other
    {
        RunOptions o;
        o.patternPath = fixture("circle_plus6");
        o.yarnsPath = FL_YARNS_PATH;
        o.outPrefix = out + "/circle6";
        o.verbose = false;
        const RunResult r = runCrochet(o);
        CHECKP(r.exitCode == 0 && r.relaxConverged && !r.ranHang, "circle_plus6 via runCrochet: exit %d, relaxed in %d frames, no strap so no hang scene (%s)", r.exitCode, r.relaxFrames, r.error.c_str());
        const std::string json = slurp(o.outPrefix + ".json"), obj = slurp(o.outPrefix + ".obj"), rest = slurp(o.outPrefix + "_rest.obj");
        const int verts = countLines(obj, "v "), faces = countLines(obj, "f ");
        CHECKP(verts == 217 && faces == 384 && countLines(rest, "v ") == 217 && countLines(rest, "f ") == 384 && countLines(obj, "# stitch ") == 217,
               "OBJ: %d vertices, %d faces, a `# stitch <id> row <r>` comment before each vertex; the rest OBJ matches", verts, faces);
        size_t s1 = 0, s2 = 0;
        CHECKP(validPng(o.outPrefix + ".png", &s1) && validPng(o.outPrefix + "_rest.png", &s2), "both PNG snapshots are valid 1440 x 360 images (%zu and %zu bytes)", s1, s2);
        {
            const std::vector<unsigned char> px = readSnapshotPixels(slurp(o.outPrefix + ".png"));
            // the top-left pixel is the background; the third tile (top view) has the orange disc at its centre
            bool bg = px.size() == 1440 * 360 * 3 && px[3 * 5] == 235 && px[3 * 5 + 1] == 235 && px[3 * 5 + 2] == 235;
            const size_t centre = (180 * 1440 + 2 * 360 + 180) * 3;
            const bool disc = px.size() == 1440 * 360 * 3 && px[centre] > px[centre + 2] + 40;  // orange: red well above blue
            CHECKP(bg && disc, "PNG decodes with an independent reader: background (235,235,235) at the corner, orange fabric at the centre of the top view");
        }
        CHECKP(json.find("\"settled\": true") != std::string::npos && json.find("\"yarn_m\"") != std::string::npos && json.find("\"hang\": null") != std::string::npos &&
                   json.find("\"corner_angle_deg\": null") != std::string::npos,
               "run JSON: settled, metrics inside, hang null, corner angle null (a flat disc has no base-to-wall row)");
    }

    // --- the tool: exit codes
    CHECKP(cli("") == 1, "no pattern: exit 1");
    CHECKP(cli("/no/such/pattern.txt") == 1, "missing pattern: exit 1");
    CHECKP(cli("\"" + fixture("circle_plus6") + "\" --scene sideways") == 1, "unknown scene: exit 1");
    CHECKP(cli("\"" + fixture("circle_plus6") + "\" --load laptop") == 1, "unknown load: exit 1");
    CHECKP(cli("\"" + fixture("circle_plus6") + "\" --yarn no_such_yarn") == 1, "unknown yarn: exit 1");
    CHECKP(cli("\"" + fixture("circle_plus6") + "\" --scene hang_pin") == 1, "a scene without a strap: exit 1");
    CHECKP(cli("\"" + fixture("circle_plus6") + "\" --bogus") == 1, "unknown option: exit 1");

    // --- every relax-only fixture runs from the tool and settles (exit 0) with its snapshots
    for (const char* name : {"circle_plus5", "circle_plus6", "circle_plus8", "tube40", "facing"}) {
        const int code = cli("\"" + fixture(name) + "\" --out \"" + out + "/" + name + "\"");
        size_t sz = 0;
        CHECKP(code == 0 && validPng(out + "/" + name + ".png", &sz) && std::filesystem::exists(out + "/" + name + ".json"), "crochet_cli %s: exit %d, snapshot %zu bytes", name, code, sz);
    }

    // --- the pouch through the tool: exit 0 when it settles (default cap), exit 2 when the hang is cut off after one frame
    {
        const int ok = cli("\"" + fixture("pouch") + "\" --scene hang_pin --out \"" + out + "/pouch\"");
        const std::string json = slurp(out + "/pouch.json");
        CHECKP(ok == 0 && json.find("\"hang\": {\"frames\"") != std::string::npos && json.find("\"drop_cm\": 38.") != std::string::npos && json.find("\"strap_length_rest_cm\": 47.4") != std::string::npos,
               "crochet_cli pouch --scene hang_pin: exit %d, hang phase and metrics in the JSON (drop 38.x cm, strap 47.4 cm)", ok);
        const int cut = cli("\"" + fixture("pouch") + "\" --scene hang_pin --frames 1");
        CHECKP(cut == 2, "crochet_cli pouch --frames 1: exit %d (not settled)", cut);
    }
    FINISH();
}
