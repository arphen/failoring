// `materials/yarns.json`: loading, defaults, duplicate ids and unknown fibres.
#include <cmath>
#include <cstdio>
#include <exception>
#include <set>
#include <string>

#include "check.hpp"
#include "failoring/crochet/yarn.hpp"

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

int main() {
    std::printf("[yarns] %s\n", FL_YARNS_PATH);
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);

    CHECKP(book.yarns.size() == 6, "6 yarns loaded (%zu)", book.yarns.size());
    std::printf("  yarns:");
    for (const auto& y : book.yarns) std::printf(" %s", y.id.c_str());
    std::printf("\n");

    bool idsUnique = true;
    {
        std::set<std::string> ids;
        for (const auto& y : book.yarns) idsUnique = idsUnique && ids.insert(y.id).second;
    }
    CHECKP(idsUnique, "every yarn id unique (%zu ids)", book.yarns.size());

    bool fibresExist = true;
    for (const auto& y : book.yarns)
        if (book.fibre(y.fibre) == nullptr) {
            std::printf("  yarn '%s' has unknown fibre '%s'\n", y.id.c_str(), y.fibre.c_str());
            fibresExist = false;
        }
    CHECKP(fibresExist, "every yarn's fibre exists in 'fibres' (%zu fibres)", book.fibres.size());

    CHECKP(book.hooksMm.size() >= 14, "hooks_mm has at least 14 entries (%zu)", book.hooksMm.size());
    std::printf("  hooks_mm:");
    for (const auto& kv : book.hooksMm) std::printf(" %s=%.2f", kv.first.c_str(), kv.second);
    std::printf(" mm\n");

    const auto hook = [&book](const char* name) {
        auto it = book.hooksMm.find(name);
        return it == book.hooksMm.end() ? -1.0 : it->second;
    };
    CHECKP(std::fabs(hook("H-8") - 5.0) < 1e-12, "hook H-8 = 5.0 mm (measured %.2f)", hook("H-8"));
    CHECKP(std::fabs(hook("B-1") - 2.25) < 1e-12, "hook B-1 = 2.25 mm (measured %.2f)", hook("B-1"));
    CHECKP(std::fabs(hook("N/P-15") - 10.0) < 1e-12, "hook N/P-15 = 10.0 mm (measured %.2f)", hook("N/P-15"));

    const Yarn* acrylic = book.find("acrylic_worsted");
    CHECKP(acrylic != nullptr, "find('acrylic_worsted') returns the yarn");
    if (acrylic) {
        std::printf("  acrylic_worsted: %.0f g / %.0f m -> lambda = %.9f g/m\n", acrylic->grams, acrylic->metres, acrylic->lambda());
        CHECKP(std::fabs(acrylic->lambda() - 100.0 / 180.0) < 1e-9,
               "acrylic_worsted lambda = 100/180 g/m (measured %.9f, want %.9f)", acrylic->lambda(), 100.0 / 180.0);
        CHECKP(acrylic->fibre == "acrylic", "acrylic_worsted fibre = 'acrylic' (%s)", acrylic->fibre.c_str());
        const YarnFibre* f = book.fibre("acrylic");
        CHECKP(f != nullptr && f->bend == 1.0 && f->sheen == 0.3, "acrylic fibre multipliers load (bend %.2f, sheen %.2f)",
               f ? f->bend : -1.0, f ? f->sheen : -1.0);
    }

    const Yarn* cottonDk = book.find("cotton_dk");
    CHECKP(cottonDk != nullptr, "find('cotton_dk') returns the yarn");
    if (cottonDk)
        CHECKP(std::fabs(cottonDk->diameterMm - 2.0) < 1e-12, "cotton_dk diameter = 2.0 mm (measured %.2f)", cottonDk->diameterMm);

    CHECKP(book.find("nope") == nullptr, "unknown yarn id -> nullptr");
    CHECKP(book.fibre("nope") == nullptr, "unknown fibre id -> nullptr");

    // Duplicate id: an error, named after the id.
    bool dupThrew = false;
    try {
        loadYarnsFromString(
            "{\"fibres\": {\"acrylic\": {}}, \"yarns\": [{\"id\": \"dup\", \"fibre\": \"acrylic\"}, {\"id\": \"dup\", \"fibre\": \"acrylic\"}]}");
    } catch (const std::exception& e) {
        dupThrew = std::string(e.what()).find("dup") != std::string::npos;
        if (!dupThrew) std::printf("  duplicate-id error did not name the id: %s\n", e.what());
    }
    CHECKP(dupThrew, "duplicate yarn id throws an error naming the id");

    // A fibre that is not in "fibres"; likewise one that is missing entirely.
    bool unknownFibreThrew = false;
    try {
        loadYarnsFromString("{\"fibres\": {\"acrylic\": {}}, \"yarns\": [{\"id\": \"a\", \"fibre\": \"silk\"}]}");
    } catch (const std::exception& e) {
        unknownFibreThrew = std::string(e.what()).find("silk") != std::string::npos;
    }
    CHECKP(unknownFibreThrew, "yarn with a fibre not in 'fibres' throws an error naming the fibre");

    bool missingFibreThrew = false;
    try {
        loadYarnsFromString("{\"fibres\": {\"acrylic\": {}}, \"yarns\": [{\"id\": \"a\"}]}");
    } catch (const std::exception&) {
        missingFibreThrew = true;
    }
    CHECKP(missingFibreThrew, "yarn without 'fibre' throws");

    bool malformedThrew = false;
    try {
        loadYarnsFromString("{\"yarns\": [ oops");
    } catch (const std::exception&) {
        malformedThrew = true;
    }
    CHECKP(malformedThrew, "malformed JSON throws");

    // Missing optional fields keep their defaults; unknown extra fields are ignored.
    bool defaultsOk = false;
    try {
        const YarnBook sparse =
            loadYarnsFromString("{\"fibres\": {\"x\": {\"bend\": 2.5}}, \"yarns\": [{\"id\": \"a\", \"fibre\": \"x\", \"mystery\": 42}]}");
        defaultsOk = sparse.yarns.size() == 1 && sparse.yarns[0].weightClass == 0 && sparse.yarns[0].grams == 0 &&
                     sparse.yarns[0].metres == 0 && sparse.yarns[0].diameterMm == 0 && sparse.yarns[0].hookMm == 0 &&
                     sparse.fibre("x") != nullptr && sparse.fibre("x")->bend == 2.5 && sparse.fibre("x")->tensile == 1.0 &&
                     sparse.fibre("x")->sheen == 0.3;
    } catch (const std::exception& e) {
        std::printf("  sparse yarn load threw: %s\n", e.what());
    }
    CHECKP(defaultsOk, "missing fields default, unknown extra fields ignored");

    FINISH();
}
