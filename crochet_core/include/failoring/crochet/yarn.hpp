#pragma once
// The yarn database (materials/yarns.json): hooks, fibre behaviour multipliers
// and the yarns themselves. Units are the ones a crocheter uses and the ones
// the DSL/UI fix: mm, grams and metres. The cloth solver works in SI; the
// crochet compiler converts at one place, not here.
#include <map>
#include <string>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

// Multipliers relative to the acrylic-worsted reference, which is all 1.0.
// STARTER values, not measured; see docs/CROCHET_PLAN.md section 10 (swatch
// calibration).
struct YarnFibre {
    Real bend = 1, tensile = 1, lock = 1, friction = 0.5, sheen = 0.3;
};

struct Yarn {
    std::string id, name, fibre, colour;
    int weightClass = 0;  // CYC weight class, 1..7
    Real grams = 0, metres = 0, diameterMm = 0, hookMm = 0;
    YarnFibre fibreParams;  // the fibre's multipliers; the loader fills this (T3.1)
    Real lambda() const { return grams / metres; }  // linear density, g/m
};

struct YarnBook {
    std::map<std::string, Real> hooksMm;      // US name (e.g. "H-8") -> diameter in mm
    std::map<std::string, YarnFibre> fibres;  // fibre id -> multipliers
    std::vector<Yarn> yarns;

    const Yarn* find(const std::string& id) const;
    const YarnFibre* fibre(const std::string& id) const;
};

// Parse a yarns file (see materials/yarns.json). Throws std::runtime_error on
// I/O or format problems, naming the offending key/value: malformed JSON, a
// duplicate yarn id, or a yarn whose "fibre" is not in "fibres". A missing
// field keeps its default; unknown extra fields are ignored.
YarnBook loadYarnsFromString(const std::string& jsonText);
YarnBook loadYarnsFromFile(const std::string& path);

}  // namespace fl
