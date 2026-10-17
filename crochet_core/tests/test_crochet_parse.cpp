// The CrochetPARADE bridge importer (decision B; docs/CROCHET_PLAN.md
// sections 5.0/5.1): the committed `patterns/cp/exported/*.json` exports plus
// the `#!` extension lines of `patterns/cp/*.txt`, whose syntax is pinned in
// docs/CROCHET_DSL.md. This test reads only committed files below
// FL_PATTERNS_DIR: no node and no CrochetPARADE checkout at test time.
#include <cstdio>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "failoring/crochet/pattern.hpp"

using namespace fl;

// CHECK plus a PASS line carrying the measured number, so ctest output is
// readable when the test fails on one of the bounds.
#define CHECKP(cond, ...)                                 \
    do {                                                  \
        CHECK(cond);                                      \
        if (cond) {                                       \
            std::printf("PASS ");                         \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

static std::string fixturePath(const std::string& name) {
    return std::string(FL_PATTERNS_DIR) + "/cp/" + name + ".txt";
}
static std::string exportPath(const std::string& name) {
    return std::string(FL_PATTERNS_DIR) + "/cp/exported/" + name + ".json";
}
static StitchGraph loadFixture(const std::string& name) {
    return importPatternFile(exportPath(name), fixturePath(name));
}

static int countType(const StitchGraph& g, const char* type) {
    int n = 0;
    for (const Stitch& s : g.st)
        if (s.type == type) ++n;
    return n;
}
static int countLoop(const StitchGraph& g, const char* loop) {
    int n = 0;
    for (const Stitch& s : g.st)
        if (s.loop == loop) ++n;
    return n;
}
static int countLinkKind(const StitchGraph& g, const char* kind) {
    int n = 0;
    for (const Link& l : g.links)
        if (l.kind == kind) ++n;
    return n;
}
static int rowCount(const StitchGraph& g, int row) {
    int n = 0;
    for (const Stitch& s : g.st)
        if (s.row == row) ++n;
    return n;
}
// The `#! link` addressing: the stitch with this exported (row, index); -1
// when the fixture has none.
static int idAt(const StitchGraph& g, int row, int index) {
    for (const Stitch& s : g.st)
        if (s.row == row && s.index == index) return s.id;
    return -1;
}
static const Stitch* stitchById(const StitchGraph& g, int id) {
    return id >= 0 && id < static_cast<int>(g.st.size()) ? &g.st[id] : nullptr;
}
static const Link* findLink(const StitchGraph& g, int a, int b, const char* kind) {
    for (const Link& l : g.links)
        if (l.a == a && l.b == b && l.kind == kind) return &l;
    return nullptr;
}
static const PointSeam* findSeam(const std::vector<PointSeam>& seams, int stitch) {
    for (const PointSeam& s : seams)
        if (s.stitch == stitch) return &s;
    return nullptr;
}
// The stitch at the `#!` (row, index) address; nullptr when there is none.
static const Stitch* stitchAt(const StitchGraph& g, int row, int index) {
    return stitchById(g, idAt(g, row, index));
}
// Stitches whose resolved RS-relative loop differs from the as-written one:
// exactly the blo/flo on WS rows.
static int countSwappedLoop(const StitchGraph& g) {
    int n = 0;
    for (const Stitch& s : g.st)
        if (s.loopRs != s.loop) ++n;
    return n;
}
static bool rsAt(const StitchGraph& g, int row, int index) {
    const int id = idAt(g, row, index);
    return id >= 0 && g.st[id].rs;
}

// Invariants every import must satisfy: ids are positions, rows cover 0..N-1,
// every target/prev/link endpoint exists, no stitch is in two links, every
// (row,index) address is unique. Returns the number of violations.
static int structuralViolations(const StitchGraph& g) {
    int bad = 0;
    for (size_t i = 0; i < g.st.size(); ++i) {
        const Stitch& s = g.st[i];
        if (s.id != static_cast<int>(i)) ++bad;
        if (s.row < 0 || s.row >= static_cast<int>(g.rows.size())) ++bad;
        for (const int t : s.targets)
            if (t < 0 || t >= s.id) ++bad;
        if (s.prev < -1 || s.prev >= static_cast<int>(g.st.size())) ++bad;
        if (s.line < 1) ++bad;
    }
    for (size_t i = 0; i < g.rows.size(); ++i)
        if (g.rows[i].id != static_cast<int>(i)) ++bad;
    std::set<int> linked;
    for (const Link& l : g.links) {
        if (l.a < 0 || l.a >= static_cast<int>(g.st.size()) || l.b < 0 || l.b >= static_cast<int>(g.st.size()) ||
            l.a == l.b)
            ++bad;
        if (l.kind != "both_layers" && l.kind != "join") ++bad;
        if (!linked.insert(l.a).second || !linked.insert(l.b).second) ++bad;
    }
    std::map<std::pair<int, int>, int> addresses;
    for (const Stitch& s : g.st)
        if (!addresses.emplace(std::make_pair(s.row, s.index), s.id).second) ++bad;
    return bad;
}

// The smallest well-formed export, used by the extension-line and error unit
// checks: rows 0/1 with ch 0/1 and sc 2/3. The members are written one per
// line so a test can replace a unique fragment to break exactly one thing.
static std::string tinyJson() {
    return R"JSON({"stitches": [
      {"id": 0, "type": "ch", "piece": "main", "row": 0, "index": 0, "loop": "space", "targets": [], "prev": -1, "line": 1},
      {"id": 1, "type": "ch", "piece": "main", "row": 0, "index": 1, "loop": "space", "targets": [], "prev": 0, "line": 1},
      {"id": 2, "type": "sc", "piece": "main", "row": 1, "index": 0, "loop": "both", "targets": [0], "prev": 1, "line": 2},
      {"id": 3, "type": "sc", "piece": "main", "row": 1, "index": 1, "loop": "both", "targets": [1], "prev": 2, "line": 2}],
     "rows": [
      {"id": 0, "piece": "main", "kind": "row", "turned": true},
      {"id": 1, "piece": "main", "kind": "row", "turned": true}],
     "links": [], "positions": [], "errors": []})JSON";
}
// tinyJson() with the first occurrence of `from` replaced by `to`; the tests
// use unique fragments, so exactly one member changes.
static std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    if (at == std::string::npos)
        std::printf("  [check setup] fragment not found: %s\n", from.c_str());
    else
        text.replace(at, from.size(), to);
    return text;
}

// importPattern(json, ext) -> "" on success, the thrown message on error, so
// the checks can assert the wording (and the line number) of each failure.
static std::string importError(const std::string& json, const std::string& ext = {}) {
    try {
        importPattern(json, ext);
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}
static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

int main() {
    std::printf("[crochet parse] fixtures from %s\n", FL_PATTERNS_DIR);

    // ---- the pouch: 50 through-both-layers seams + 5 strap-end joins -------
    {
        const StitchGraph g = loadFixture("pouch");
        CHECKP(g.st.size() == 1380, "pouch: 1380 stitches (%zu)", g.st.size());
        CHECKP(g.rows.size() == 133, "pouch: 133 rows (%zu)", g.rows.size());
        CHECKP(countType(g, "ch") == 144 && countType(g, "sc") == 1230 && countType(g, "slst") == 6,
               "pouch: 144 ch + 1230 sc + 6 slst (%d/%d/%d)", countType(g, "ch"), countType(g, "sc"), countType(g, "slst"));
        CHECKP(g.links.size() == 55 && countLinkKind(g, "both_layers") == 50 && countLinkKind(g, "join") == 5,
               "pouch: 55 links = 50 both_layers + 5 join (%zu total)", g.links.size());
        const int bad = structuralViolations(g);
        CHECKP(bad == 0, "pouch: graph structure consistent (%d violations)", bad);

        CHECKP(rowCount(g, 50) == 16 && rowCount(g, 51) == 81 && rowCount(g, 131) == 6 && rowCount(g, 132) == 5,
               "pouch: row sizes 16/81/6/5 at rows 50/51/131/132 (%d/%d/%d/%d)", rowCount(g, 50), rowCount(g, 51),
               rowCount(g, 131), rowCount(g, 132));
        CHECKP(g.rows[26].kind == "row" && g.rows[26].turned && g.rows[51].kind == "round" && !g.rows[51].turned &&
                   g.rows[132].kind == "round" && !g.rows[132].turned,
               "pouch: rows 26/51/132 are turned row, border round, join round");

        CHECKP(idAt(g, 51, 0) == 815, "pouch: [51,0] is stitch 815 (%d)", idAt(g, 51, 0));
        CHECKP(idAt(g, 26, 15) == 430, "pouch: [26,15] is stitch 430 (%d)", idAt(g, 26, 15));
        CHECKP(idAt(g, 132, 0) == 1375, "pouch: [132,0] is stitch 1375 (%d)", idAt(g, 132, 0));
        CHECKP(idAt(g, 131, 1) == 1370, "pouch: [131,1] is stitch 1370 (%d)", idAt(g, 131, 1));
        const Link* seam0 = findLink(g, 815, 430, "both_layers");
        const Link* join0 = findLink(g, 1375, 1370, "join");
        CHECKP(seam0 != nullptr, "pouch: both_layers link 815 <-> 430 exists");
        CHECKP(join0 != nullptr, "pouch: join link 1375 <-> 1370 exists");
        CHECKP(!g.links.empty() && g.links.front().a == 815 && g.links.front().b == 430 &&
                   g.links.front().kind == "both_layers",
               "pouch: links keep the written order (first is 815 <-> 430)");

        const Stitch* s815 = stitchById(g, 815);
        CHECKP(s815 != nullptr && s815->type == "sc" && s815->row == 51 && s815->index == 0 && s815->loop == "both" &&
                   s815->targets == std::vector<int>{400} && s815->prev == 814 && s815->line == 58,
               "pouch: stitch 815 keeps the export's single target 400 (the second is the link)");
        const Stitch* s840 = stitchById(g, 840);
        CHECKP(s840 != nullptr && s840->type == "sc" && s840->row == 51 && s840->index == 25 && s840->loop == "both" &&
                   s840->targets == std::vector<int>{14} && s840->prev == 839 && s840->line == 58,
               "pouch: stitch 840 (row 51, index 25, prev 839, line 58) imports field-for-field");

        // Every link's two addresses, from the T4.0 spike's verified pairing:
        // side 1 zig-zags [51,0..24] to rows 26..50, side 2 [51,40..64] back to
        // rows 50..26, the strap join [132,0..4] to [131,1..5].
        int side1 = 0, side2 = 0, joins = 0;
        for (int i = 0; i < 25; ++i) {
            const int a = idAt(g, 51, i);
            const int r = 26 + i;
            const int b = idAt(g, r, r % 2 == 0 ? 15 : 1);
            if (a >= 0 && b >= 0 && findLink(g, a, b, "both_layers") != nullptr) ++side1;
        }
        CHECKP(side1 == 25, "pouch: 25 both_layers links [51,0..24] to rows 26..50 (%d)", side1);
        for (int j = 0; j < 25; ++j) {
            const int a = idAt(g, 51, 40 + j);
            const int r = 50 - j;
            const int b = idAt(g, r, r % 2 == 0 ? 1 : 15);
            if (a >= 0 && b >= 0 && findLink(g, a, b, "both_layers") != nullptr) ++side2;
        }
        CHECKP(side2 == 25, "pouch: 25 both_layers links [51,40..64] to rows 50..26 (%d)", side2);
        for (int i = 0; i < 5; ++i) {
            const int a = idAt(g, 132, i);
            const int b = idAt(g, 131, 1 + i);
            if (a >= 0 && b >= 0 && findLink(g, a, b, "join") != nullptr) ++joins;
        }
        CHECKP(joins == 5, "pouch: 5 joins [132,0..4] to [131,1..5] (%d)", joins);
    }

    // ---- the five round bags: 6 strap-end joins each -----------------------
    const auto checkBag = [&](const char* name, size_t stCount, size_t rowsCount, int scCount, int slstCount,
                              int bloCount, int floCount, int joinRow, int strapEndRow) {
        const StitchGraph g = loadFixture(name);
        CHECKP(g.st.size() == stCount, "%s: %zu stitches (%zu)", name, stCount, g.st.size());
        CHECKP(g.rows.size() == rowsCount, "%s: %zu rows (%zu)", name, rowsCount, g.rows.size());
        CHECKP(countType(g, "ch") == 150 && countType(g, "sc") == scCount && countType(g, "slst") == slstCount,
               "%s: 150 ch + %d sc + %d slst", name, scCount, slstCount);
        CHECKP(countLoop(g, "blo") == bloCount && countLoop(g, "flo") == floCount,
               "%s: round 4 loops blo %d / flo %d (%d/%d)", name, bloCount, floCount, countLoop(g, "blo"),
               countLoop(g, "flo"));
        CHECKP(g.links.size() == 6 && countLinkKind(g, "join") == 6, "%s: 6 join links (%zu)", name, g.links.size());
        int found = 0;
        if (g.links.size() == 6)
            for (int i = 0; i < 6; ++i) {
                const int a = idAt(g, joinRow, i);
                const int b = idAt(g, strapEndRow, 6 - i);
                if (a >= 0 && b >= 0 && g.links[i].a == a && g.links[i].b == b && g.links[i].kind == "join") ++found;
            }
        CHECKP(found == 6, "%s: joins [%d,0..5] to [%d,6..1], written order (%d)", name, joinRow, strapEndRow, found);
        const int bad = structuralViolations(g);
        CHECKP(bad == 0, "%s: graph structure consistent (%d violations)", name, bad);
        return g;
    };
    const StitchGraph bagPlain = checkBag("bag_round_plain", 5798, 146, 5598, 50, 0, 0, 145, 144);
    const StitchGraph bagBlo = checkBag("bag_round_blo", 5798, 146, 5598, 50, 114, 0, 145, 144);
    const StitchGraph bagFlo = checkBag("bag_round_flo", 5798, 146, 5598, 50, 0, 114, 145, 144);
    const StitchGraph bagSlst = checkBag("bag_round_slst", 5798, 146, 5484, 164, 114, 0, 145, 144);
    const StitchGraph bagSurface = checkBag("bag_round_surface", 5912, 147, 5598, 164, 0, 0, 146, 145);
    CHECKP(rowCount(bagPlain, 45) == 6 && bagPlain.rows[45].kind == "row" && bagPlain.rows[45].turned,
           "bag_round_plain: row 45 is the turned strap row 1 (%d stitches)", rowCount(bagPlain, 45));
    CHECKP(idAt(bagSlst, 4, 0) >= 0 && bagSlst.st[idAt(bagSlst, 4, 0)].type == "slst" &&
               bagSlst.st[idAt(bagSlst, 4, 0)].loop == "blo",
           "bag_round_slst: round 4 is slst blo");
    CHECKP(rowCount(bagSurface, 45) == 114 && bagSurface.rows[45].kind == "round" && !bagSurface.rows[45].turned,
           "bag_round_surface: row 45 is the 114-slst surface round (%d)", rowCount(bagSurface, 45));

    // ---- the three flat/lumpy circles and the tube -------------------------
    const auto checkCircle = [&](const char* name, size_t stCount, int inc) {
        const StitchGraph g = loadFixture(name);
        CHECKP(g.st.size() == stCount, "%s: %zu stitches (%zu)", name, stCount, g.st.size());
        CHECKP(g.rows.size() == 9, "%s: 9 rounds (%zu)", name, g.rows.size());
        CHECKP(g.links.empty(), "%s: 0 links (%zu)", name, g.links.size());
        CHECKP(countType(g, "ch") == 1 && countType(g, "sc") == static_cast<int>(stCount) - 1,
               "%s: 1 ch + %zu sc (%d ch)", name, stCount - 1, countType(g, "ch"));
        bool roundsOk = rowCount(g, 0) == 1 && g.rows[0].kind == "round" && !g.rows[0].turned &&
                        countLoop(g, "space") == 1;
        for (int r = 1; r <= 8; ++r) {
            const int want = r == 1 ? 6 : 6 + (r - 1) * inc;
            roundsOk = roundsOk && rowCount(g, r) == want;
        }
        CHECKP(roundsOk, "%s: 6 sc then +%d per round (%d sc at round 8)", name, inc, rowCount(g, 8));
        const int bad = structuralViolations(g);
        CHECKP(bad == 0, "%s: graph structure consistent (%d violations)", name, bad);
    };
    checkCircle("circle_plus5", 189, 5);
    checkCircle("circle_plus6", 217, 6);
    checkCircle("circle_plus8", 273, 8);
    {
        const StitchGraph g = loadFixture("tube40");
        CHECKP(g.st.size() == 512, "tube40: 512 stitches (%zu)", g.st.size());
        CHECKP(g.rows.size() == 13, "tube40: 13 rounds (%zu)", g.rows.size());
        CHECKP(countType(g, "ch") == 20 && countType(g, "sc") == 480 && countType(g, "slst") == 12,
               "tube40: 20 ch + 480 sc + 12 slst (%d/%d/%d)", countType(g, "ch"), countType(g, "sc"),
               countType(g, "slst"));
        CHECKP(g.links.empty(), "tube40: 0 links (%zu)", g.links.size());
        bool roundsOk = rowCount(g, 0) == 20 && g.rows[0].kind == "round";
        for (int r = 1; r <= 12; ++r) roundsOk = roundsOk && rowCount(g, r) == 41;
        CHECKP(roundsOk, "tube40: 20 ch then 12 rounds of 40 sc + 1 join (%d at round 12)", rowCount(g, 12));
        const int bad = structuralViolations(g);
        CHECKP(bad == 0, "tube40: graph structure consistent (%d violations)", bad);
    }

    // ---- importer unit checks: a tiny export, no fixtures ------------------
    {
        const StitchGraph g = importPattern(tinyJson());
        CHECKP(g.st.size() == 4 && g.rows.size() == 2 && g.links.empty(), "tiny: 4 stitches, 2 rows, 0 links");
        CHECKP(g.st[2].targets == std::vector<int>{0} && g.st[2].prev == 1 && g.st[2].line == 2 && g.st[3].prev == 2,
               "tiny: targets/prev/line import field-for-field");
        CHECKP(g.rows[0].kind == "row" && g.rows[0].turned && g.rows[0].piece == "main", "tiny: row fields import");
        CHECKP(g.st[0].loop == "space" && g.st[2].loop == "both", "tiny: loop strings import");
    }
    {
        const std::string ext =
            "# plain comment line, not an extension\n"
            "#!link [1,0] [1,1] both_layers\n"
            "#!\t link \t [0,1] [0,0] join\n"
            "\n";
        const StitchGraph g = importPattern(tinyJson(), ext);
        CHECKP(g.links.size() == 2 && g.links[0].a == 2 && g.links[0].b == 3 && g.links[0].kind == "both_layers" &&
                   g.links[1].a == 1 && g.links[1].b == 0 && g.links[1].kind == "join",
               "extension: 2 links appended in written order (%zu; comments and `#!` spacing tolerated)", g.links.size());
        const StitchGraph crlf = importPattern(tinyJson(), "#! link [1,0] [1,1] both_layers\r\n#! link [0,1] [0,0] join\r\n");
        CHECKP(crlf.links.size() == 2, "extension: CRLF line endings (%zu links)", crlf.links.size());
    }

    // ---- malformed inputs: each error names the offending place ------------
    {
        // Five inputs whose error must carry the right physical line number.
        const std::string l1 = importError(tinyJson(), "a pattern line\n#! seam [1,0] [1,1] both_layers\n");
        CHECKP(has(l1, "line 2: unknown keyword 'seam'"), "error: unknown keyword names line 2 (%s)", l1.c_str());
        const std::string l2 = importError(tinyJson(), "# comment\n\n#! link [9,0] [1,1] both_layers\n");
        CHECKP(has(l2, "line 3: address [9,0] does not name a stitch"), "error: bad address names line 3 and [9,0] (%s)",
               l2.c_str());
        const std::string l3 = importError(tinyJson(), "#! link [1,0] [1,1] both_layers\n#! link [1,0] [0,0] join\n");
        CHECKP(has(l3, "line 2: [1,0] already appears in another link"), "error: stitch in two links names line 2 (%s)",
               l3.c_str());
        const std::string l4 =
            importError(R"({"errors": [{"line": 7, "msg": "unexpected token 'x'"}], "stitches": [], "rows": []})");
        CHECKP(has(l4, "CrochetPARADE line 7: unexpected token 'x'"),
               "error: export errors array passes through with its line 7 (%s)", l4.c_str());
        const std::string l5 = importError(tinyJson(), "#! link [1,0] [1,1] maybe\n");
        CHECKP(has(l5, "line 1: unknown kind 'maybe'"), "error: unknown kind names line 1 and the kind (%s)", l5.c_str());

        const std::string e1 = importError("{\"stitches\": [ oops");
        CHECKP(has(e1, "JSON error"), "error: malformed JSON throws (%s)", e1.c_str());
        const std::string e2 = importError(std::string("\xff") + tinyJson());
        CHECKP(has(e2, "invalid UTF-8 at byte offset 0"), "error: invalid UTF-8 names the byte offset (%s)", e2.c_str());
        const std::string e3 = importError(replaced(tinyJson(), "\"id\": 3, \"type\": \"sc\"", "\"id\": 2, \"type\": \"sc\""));
        CHECKP(has(e3, "stitches[3]: id is 2"), "error: out-of-order stitch id (%s)", e3.c_str());
        const std::string e4 = importError(replaced(tinyJson(), "\"row\": 1, \"index\": 1", "\"row\": 5, \"index\": 1"));
        CHECKP(has(e4, "row id 5 does not exist"), "error: stitch row id out of range (%s)", e4.c_str());
        const std::string e5 = importError(
            replaced(tinyJson(), "\"loop\": \"both\", \"targets\": [0]", "\"loop\": \"sideways\", \"targets\": [0]"));
        CHECKP(has(e5, "loop 'sideways'"), "error: unknown loop value (%s)", e5.c_str());
        const std::string e6 = importError(replaced(tinyJson(), "\"targets\": [0], \"prev\": 1", "\"targets\": [2], \"prev\": 1"));
        CHECKP(has(e6, "target 2 must be an existing stitch id smaller than 2"), "error: target not below the id (%s)",
               e6.c_str());
        const std::string e7 = importError(replaced(tinyJson(), "\"prev\": 2, \"line\": 2", "\"prev\": 9, \"line\": 2"));
        CHECKP(has(e7, "prev 9 is neither -1 nor an existing stitch id"), "error: prev out of range (%s)", e7.c_str());
        const std::string e8 =
            importError(replaced(tinyJson(), "\"links\": [],", "\"links\": [{\"a\": 0, \"b\": 1, \"kind\": \"maybe\"}],"));
        CHECKP(has(e8, "links[0]: kind 'maybe'"), "error: unknown link kind in the JSON (%s)", e8.c_str());
        const std::string e9 =
            importError(replaced(tinyJson(), "\"links\": [],", "\"links\": [{\"a\": 0, \"b\": 0, \"kind\": \"both_layers\"}],"));
        CHECKP(has(e9, "a and b must be different stitches"), "error: link to itself (%s)", e9.c_str());
        const std::string e10 = importError(replaced(
            tinyJson(), "\"links\": [],",
            "\"links\": [{\"a\": 0, \"b\": 1, \"kind\": \"both_layers\"}, {\"a\": 1, \"b\": 2, \"kind\": \"join\"}],"));
        CHECKP(has(e10, "stitch 1 appears in more than one link"), "error: a stitch in two JSON links (%s)", e10.c_str());
        const std::string e11 = importError(replaced(tinyJson(), "\"row\": 1, \"index\": 1", "\"row\": 1, \"index\": 0"),
                                            "#! link [1,0] [1,1] both_layers\n");
        CHECKP(has(e11, "share the address [1,0]"), "error: two stitches with the same [row,pos] address (%s)", e11.c_str());
        const std::string e12 = importError(tinyJson(), "#! link [1,0]\n");
        CHECKP(has(e12, "line 1: expected `link [row,pos] [row,pos] <kind>`, got 1 argument(s)"),
               "error: wrong arity names line 1 (%s)", e12.c_str());
        const std::string e13 = importError(tinyJson(), "#! link [1,0] 1,1 both_layers\n");
        CHECKP(has(e13, "cannot parse address '1,1'"), "error: address without brackets (%s)", e13.c_str());
        const std::string e14 = importError(tinyJson(), "#! link [-1,0] [1,1] both_layers\n");
        CHECKP(has(e14, "cannot parse address '[-1,0]'"), "error: negative address rejected (%s)", e14.c_str());
        const std::string e15 = importError(replaced(tinyJson(), "\"links\": [],", "\"links\": [{\"a\": 0, \"b\": 1, \"kind\": \"join\"}],"),
                                            "#! link [0,1] [1,0] both_layers\n");
        CHECKP(has(e15, "line 1: [0,1] already appears in another link"),
               "error: extension link reuses a JSON link's stitch (%s)", e15.c_str());
        const std::string e16 = importError(tinyJson(), "#!\n");
        CHECKP(has(e16, "line 1: missing keyword"), "error: `#!` alone names line 1 (%s)", e16.c_str());
    }
    {
        bool threw = false;
        std::string msg;
        try {
            importPatternFile("/no/such/failoring-fixture.json");
        } catch (const std::exception& e) {
            threw = true;
            msg = e.what();
        }
        CHECKP(threw && has(msg, "cannot open pattern JSON file"), "error: missing export file names the path (%s)", msg.c_str());
    }
    {
        // The extension text is optional: the raw export alone imports, with
        // the JSON's own (empty) links array and no `#!` reading.
        const StitchGraph g = importPatternFile(exportPath("pouch"));
        CHECKP(g.st.size() == 1380 && g.links.empty(), "pouch.json without the pattern: 1380 stitches, 0 links (%zu/%zu)",
               g.st.size(), g.links.size());
    }

    // ---- facing/RS, resolved by resolveFacing on every import (plan 5.5) ----
    {
        // Rule: rs(0) = true. Row 0 here is the 16-ch foundation (every node
        // `ch`), so its `turn` is part of working back along the chain and does
        // not flip row 1 ("row 1 is RS by default"); body rows 1..50 all turn,
        // so rs(r) = rows[r-1].turned ? !rs(r-1) : rs(r-1) alternates from row
        // 1 RS: odd body rows RS (row 49 RS), even WS (row 50 WS). The border
        // round 51 does not turn, so the strap restarts RS at 52 and turns
        // again: 52 RS, 53 WS, ..., 131 WS (79 turns after 52). The strap's
        // last row turns, so the join round 132 is RS.
        const StitchGraph g = loadFixture("pouch");
        CHECKP(rsAt(g, 1, 0), "pouch facing: [1,0] RS");
        CHECKP(!rsAt(g, 2, 0), "pouch facing: [2,0] WS");
        CHECKP(rsAt(g, 49, 0), "pouch facing: [49,0] RS (odd body rows are RS)");
        CHECKP(!rsAt(g, 50, 0), "pouch facing: [50,0] WS");
        CHECKP(rsAt(g, 51, 0), "pouch facing: border round [51,0] RS");
        CHECKP(rsAt(g, 52, 0), "pouch facing: strap row 1 [52,0] RS (the border round does not turn)");
        CHECKP(!rsAt(g, 131, 0), "pouch facing: strap row 80 [131,0] WS");
        CHECKP(rsAt(g, 132, 0), "pouch facing: join round [132,0] RS");
        int bodyAlternating = 0;
        for (int r = 1; r <= 50; ++r)
            if (rsAt(g, r, 0) == (r % 2 == 1)) ++bodyAlternating;
        CHECKP(bodyAlternating == 50, "pouch facing: all 50 body rows alternate from row 1 RS (%d)", bodyAlternating);
        int strapAlternating = 0;
        for (int r = 52; r <= 131; ++r)  // even rows are RS after round 51
            if (rsAt(g, r, 0) == (r % 2 == 0)) ++strapAlternating;
        CHECKP(strapAlternating == 80, "pouch facing: all 80 strap rows alternate from [52,0] RS to [131,0] WS (%d)",
               strapAlternating);
    }
    {
        // Bag: row 0 is the 51-ch foundation (no flip); the rounds 1..44 never
        // turn, so all are RS, and strap row 45 (row 1 of the strap) is RS.
        // Strap rows 45..144 all turn: 45 RS, 46 WS, ..., 144 WS (99 turns
        // after 45); row 144 turns, so the join round 145 is RS again.
        const StitchGraph g = loadFixture("bag_round_blo");
        CHECKP(rsAt(g, 1, 0) && rsAt(g, 4, 0) && rsAt(g, 45, 0), "bag_round_blo facing: rounds 1 and 4 RS, strap row 45 RS");
        CHECKP(!rsAt(g, 46, 0) && !rsAt(g, 144, 0), "bag_round_blo facing: strap rows 46 and 144 WS");
        CHECKP(rsAt(g, 145, 0), "bag_round_blo facing: join round 145 RS");
        int roundsRs = 0;
        for (int r = 1; r <= 44; ++r)
            if (rsAt(g, r, 0)) ++roundsRs;
        CHECKP(roundsRs == 44, "bag_round_blo facing: all 44 wall rounds RS (%d)", roundsRs);
        int strapAlternating = 0;
        for (int r = 45; r <= 144; ++r)  // odd rows are RS after round 44
            if (rsAt(g, r, 0) == (r % 2 == 1)) ++strapAlternating;
        CHECKP(strapAlternating == 100, "bag_round_blo facing: all 100 strap rows alternate from [45,0] RS to [144,0] WS (%d)",
               strapAlternating);
    }
    {
        // Circles and the tube never turn: every stitch keeps the RS of round 1.
        const char* names[] = {"circle_plus5", "circle_plus6", "circle_plus8", "tube40"};
        for (const char* name : names) {
            const StitchGraph g = loadFixture(name);
            int rsStitches = 0;
            for (const Stitch& s : g.st)
                if (s.rs) ++rsStitches;
            CHECKP(rsStitches == static_cast<int>(g.st.size()), "%s facing: all stitches RS (%d/%zu)", name, rsStitches,
                   g.st.size());
        }
    }

    // ---- RS-relative loop insertion: blo/flo swap exactly on WS rows --------
    {
        // The facing swatch: 6-ch foundation, then scbl (row 1 RS), scbl (row 2
        // WS), scfl (row 3 RS), scfl (row 4 WS); every row turns. Row 1 and the
        // sc of rows 2..4 sit at index 0/1 (index 0 of rows 2..4 is the turning
        // chain).
        const StitchGraph g = loadFixture("facing");
        CHECKP(g.st.size() == 29 && g.rows.size() == 5, "facing: 29 stitches in 5 rows (%zu/%zu)", g.st.size(),
               g.rows.size());
        CHECKP(rowCount(g, 0) == 6 && rowCount(g, 1) == 5 && rowCount(g, 2) == 6 && rowCount(g, 3) == 6 &&
                   rowCount(g, 4) == 6,
               "facing: node counts 6/5/6/6/6 (%d/%d/%d/%d/%d)", rowCount(g, 0), rowCount(g, 1), rowCount(g, 2),
               rowCount(g, 3), rowCount(g, 4));
        CHECKP(structuralViolations(g) == 0, "facing: graph structure consistent (%d violations)", structuralViolations(g));
        const Stitch* r1 = stitchAt(g, 1, 0);  // RS scbl
        const Stitch* r2 = stitchAt(g, 2, 1);  // WS scbl
        const Stitch* r3 = stitchAt(g, 3, 1);  // RS scfl
        const Stitch* r4 = stitchAt(g, 4, 1);  // WS scfl
        CHECKP(r1 && r1->rs && r1->loop == "blo" && r1->loopRs == "blo", "facing: RS row + scbl keeps blo (%s -> %s)",
               r1 ? r1->loop.c_str() : "(missing)", r1 ? r1->loopRs.c_str() : "(missing)");
        CHECKP(r2 && !r2->rs && r2->loop == "blo" && r2->loopRs == "flo", "facing: WS row + scbl becomes flo (%s -> %s)",
               r2 ? r2->loop.c_str() : "(missing)", r2 ? r2->loopRs.c_str() : "(missing)");
        CHECKP(r3 && r3->rs && r3->loop == "flo" && r3->loopRs == "flo", "facing: RS row + scfl keeps flo (%s -> %s)",
               r3 ? r3->loop.c_str() : "(missing)", r3 ? r3->loopRs.c_str() : "(missing)");
        CHECKP(r4 && !r4->rs && r4->loop == "flo" && r4->loopRs == "blo", "facing: WS row + scfl becomes blo (%s -> %s)",
               r4 ? r4->loop.c_str() : "(missing)", r4 ? r4->loopRs.c_str() : "(missing)");
        CHECKP(countSwappedLoop(g) == 10, "facing: 10 of the 20 blo/flo stitches swap (the two WS rows) (%d)",
               countSwappedLoop(g));
    }
    {
        // bag_round_blo's round 4 works 114 scbl on an RS round: no swap.
        const StitchGraph g = loadFixture("bag_round_blo");
        const Stitch* s = stitchAt(g, 4, 0);
        CHECKP(s && s->rs && s->loop == "blo" && s->loopRs == "blo", "bag_round_blo: round 4 scbl on RS keeps blo (%s)",
               s ? s->loopRs.c_str() : "(missing)");
        CHECKP(countSwappedLoop(g) == 0, "bag_round_blo: 0 stitches swap their loop (%d)", countSwappedLoop(g));
    }
    {
        // every other fixture: the as-written both/space (and the RS blo/flo of
        // the bag and slst variants) copy through unchanged.
        const char* names[] = {"pouch",         "bag_round_plain", "bag_round_flo", "bag_round_slst",
                               "bag_round_surface", "circle_plus5",  "circle_plus6",  "circle_plus8",
                               "tube40"};
        int total = 0;
        for (const char* name : names) total += countSwappedLoop(loadFixture(name));
        CHECKP(total == 0, "nine fixtures: as-written both/space/blo/flo keep loopRs == loop (%d swapped)", total);
    }

    // ---- `#! count [row] N`: one line per row in every fixture ---------------
    {
        // The committed fixtures carry a mechanically generated count block for
        // every row (node counts from the committed exports: turning chains and
        // join sl sts included). Importing a fixture checks all of its lines,
        // so a clean import is the check.
        const char* names[] = {"pouch",         "bag_round_plain", "bag_round_blo", "bag_round_flo", "bag_round_slst",
                               "bag_round_surface", "circle_plus5",  "circle_plus6",  "circle_plus8", "tube40",
                               "facing"};
        int imported = 0;
        for (const char* name : names) {
            bool ok = true;
            std::string err;
            try {
                loadFixture(name);
            } catch (const std::exception& e) {
                ok = false;
                err = e.what();
            }
            const std::string msg = ok ? "" : (" (" + err + ")");
            CHECKP(ok, "%s: imports with its per-row #! count lines%s", name, msg.c_str());
            if (ok) ++imported;
        }
        CHECKP(imported == 11, "counts: all 11 fixtures import with their count lines (%d)", imported);
    }
    {
        // `#! count [row] N` against the tiny export (rows 0 and 1 have two
        // nodes each): correct counts pass, each failure names its line and
        // token in the T4.1 error style.
        const StitchGraph g = importPattern(tinyJson(), "#! count [0] 2\n#! count [1] 2\n");
        CHECKP(g.st.size() == 4, "count: correct counts import cleanly (%zu stitches)", g.st.size());
        const std::string c1 = importError(tinyJson(), "a pattern line\n#! count [1] 3\n");
        CHECKP(has(c1, "line 2: row [1] has 2 stitches, the pattern says 3"),
               "count: wrong count names line 2, row [1] and both numbers (%s)", c1.c_str());
        const std::string c2 = importError(tinyJson(), "#! count [9] 2\n");
        CHECKP(has(c2, "line 1: row [9] does not exist"), "count: nonexistent row names line 1 and [9] (%s)", c2.c_str());
        const std::string c3 = importError(tinyJson(), "#! count [1]\n");
        CHECKP(has(c3, "line 1: expected `count [row] N`, got 1 argument(s)"),
               "count: wrong arity names line 1 (%s)", c3.c_str());
        const std::string c4 = importError(tinyJson(), "#! count 1 2\n");
        CHECKP(has(c4, "cannot parse address '1' (expected [row])"), "count: address without brackets (%s)", c4.c_str());
        const std::string c5 = importError(tinyJson(), "#! count [1,0] 2\n");
        CHECKP(has(c5, "cannot parse address '[1,0]' (expected [row])"),
               "count: the [row,pos] link address is rejected here (%s)", c5.c_str());
        const std::string c6 = importError(tinyJson(), "#! count [1] x\n");
        CHECKP(has(c6, "cannot parse count 'x'"), "count: non-integer count value (%s)", c6.c_str());
    }

    // ---- point seams resolved from the links (plan section 6.7) -------------
    {
        const StitchGraph g = loadFixture("pouch");
        const std::vector<PointSeam> seams = pointSeams(g);
        int both = 0, joins = 0, matched = 0;
        for (const PointSeam& s : seams) {
            if (s.kind == "both_layers") ++both;
            if (s.kind == "join") ++joins;
            if (findLink(g, s.stitch, s.partner, s.kind.c_str()) != nullptr) ++matched;
        }
        CHECKP(seams.size() == 55 && both == 50 && joins == 5,
               "point seams: pouch resolves 55 = 50 both_layers + 5 join (%zu/%d/%d)", seams.size(), both, joins);
        CHECKP(matched == 55, "point seams: every pouch seam mirrors its link, kind included (%d)", matched);
        const PointSeam* s0 = findSeam(seams, idAt(g, 51, 0));
        const PointSeam* s24 = findSeam(seams, idAt(g, 51, 24));
        const PointSeam* s40 = findSeam(seams, idAt(g, 51, 40));
        const PointSeam* j0 = findSeam(seams, idAt(g, 132, 0));
        const PointSeam* j4 = findSeam(seams, idAt(g, 132, 4));
        CHECKP(s0 && s0->partner == idAt(g, 26, 15) && s0->kind == "both_layers",
               "point seams: seam stitch [51,0] has partner [26,15] (stitch %d both_layers)", s0 ? s0->partner : -1);
        CHECKP(s24 && s24->partner == idAt(g, 50, 15) && s40 && s40->partner == idAt(g, 50, 1),
               "point seams: side-2 seams [51,24] -> [50,15] and [51,40] -> [50,1] resolve");
        CHECKP(j0 && j0->partner == idAt(g, 131, 1) && j0->kind == "join" && j4 && j4->partner == idAt(g, 131, 5) &&
                   j4->kind == "join",
               "point seams: joins [132,0] -> [131,1] and [132,4] -> [131,5] resolve (%d/%d)", j0 ? j0->partner : -1,
               j4 ? j4->partner : -1);
    }
    {
        // Each bag has its six strap-end joins; the circles and the tube none.
        const char* bags[] = {"bag_round_plain", "bag_round_blo", "bag_round_flo", "bag_round_slst",
                              "bag_round_surface"};
        for (const char* name : bags) {
            const StitchGraph g = loadFixture(name);
            const std::vector<PointSeam> seams = pointSeams(g);
            int joins = 0;
            for (const PointSeam& s : seams)
                if (s.kind == "join") ++joins;
            CHECKP(seams.size() == 6 && joins == 6, "%s: 6 join point seams (%zu/%d)", name, seams.size(), joins);
        }
        const char* none[] = {"circle_plus5", "circle_plus6", "circle_plus8", "tube40"};
        int total = 0;
        for (const char* name : none) total += static_cast<int>(pointSeams(loadFixture(name)).size());
        CHECKP(total == 0, "point seams: the circles and the tube have none (%d)", total);
    }
    {
        // Validation: a link whose seam stitch has no export target, and a
        // partner that is already a target of the seam stitch; both name the
        // stitch ids. The link itself imports (the importer cannot see this).
        const StitchGraph noTarget = importPattern(tinyJson(), "#! link [0,1] [1,0] both_layers\n");
        std::string m1;
        try {
            pointSeams(noTarget);
        } catch (const std::exception& e) {
            m1 = e.what();
        }
        CHECKP(has(m1, "seam stitch 1 has no target"), "point seam error: empty targets names stitch 1 (%s)", m1.c_str());
        const StitchGraph dupPartner = importPattern(tinyJson(), "#! link [1,0] [0,0] both_layers\n");
        std::string m2;
        try {
            pointSeams(dupPartner);
        } catch (const std::exception& e) {
            m2 = e.what();
        }
        CHECKP(has(m2, "stitch 0 is already a target of seam stitch 2"),
               "point seam error: an existing target names both ids (%s)", m2.c_str());
    }

    FINISH();
}
