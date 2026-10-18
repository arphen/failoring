// The hinge fit of docs/CROCHET_PLAN.md section 10.5 (T9.2): the droop of a clamped swatch and the bisection on bendScale.
#include <cmath>
#include <cstdio>

#include "check.hpp"
#include "failoring/crochet/hinge_fit.hpp"

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

static int countType(const StitchGraph& g, const char* t, int row = -1) {
    int n = 0;
    for (const Stitch& s : g.st) n += s.type == t && (row < 0 || s.row == row);
    return n;
}

int main() {
    std::printf("[crochet hinge fit]\n");
    const YarnBook book = loadYarnsFromFile(FL_YARNS_PATH);
    const Yarn* yarn = book.find("acrylic_worsted");
    CHECK(yarn != nullptr);
    if (yarn == nullptr) FINISH();
    HingeSetup setup;

    // the swatch: 10 cols, 10 + 1 + 10 rows
    {
        const StitchGraph blo = makeHingeSwatch(10, 10, 10, "blo"), sl = makeHingeSwatch(10, 10, 10, "slst");
        int bloStitches = 0;
        for (const Stitch& s : blo.st) bloStitches += s.row == 11 && s.loop == "blo";
        CHECKP(blo.rows.size() == 22 && countType(blo, "sc") == 210 && countType(blo, "ch") == 11 + 20 && bloStitches == 10,
               "swatch: foundation chain of 11, 21 rows of 10 sc, 20 turning chains, the hinge row (11) BLO: %zu rows, %d sc", blo.rows.size(), countType(blo, "sc"));
        CHECKP(countType(sl, "slst", 11) == 10 && countType(sl, "sc") == 200, "slst swatch: the hinge row is 10 sl sts");
    }

    // the droop falls as the hinge stiffens, for a flat and for a folded hinge; every run settles
    for (const Real rest : {0.0, 60.0}) {
        Real prev = 1e9;
        bool falling = true, settled = true;
        Real first = 0, last = 0;
        for (const Real scale : {0.01, 0.1, 0.3, 1.0, 3.0}) {
            bool conv = false;
            const Real d = simulateDroop(setup, *yarn, scale, rest, &conv);
            falling = falling && d < prev;
            settled = settled && conv;
            prev = d;
            if (scale == 0.01) first = d;
            last = d;
        }
        CHECKP(falling && settled && first > 85 && first < 100 && last > rest - 1 && last < first - 15,
               "rest %.0f deg: the droop falls from %.1f deg (bendScale 0.01) to %.1f deg (3.0), every run settled", rest, first, last);
    }

    // a stiffer fabric droops less at the same bendScale: the fabric record is used
    {
        Calibration stiff;
        stiff.cantilever = true;
        stiff.bendWale = stiff.bendCourse = 3e-3;  // ten times the starter bending
        HingeSetup s2 = setup;
        s2.fabric = &stiff;
        const Real a = simulateDroop(setup, *yarn, 0.3, 0.0), b = simulateDroop(s2, *yarn, 0.3, 0.0);
        CHECKP(b < a - 3, "fabric record: a ten times stiffer fabric droops %.1f deg instead of %.1f", b, a);
    }

    // recover a known bendScale from a simulated droop, within 5 %
    for (const auto& c : {std::make_pair(0.1, 0.0), std::make_pair(0.3, 0.0), std::make_pair(0.6, 0.0), std::make_pair(0.2, 60.0)}) {
        const Real truth = c.first, rest = c.second;
        const Real droop = simulateDroop(setup, *yarn, truth, rest);
        const HingeFit fit = fitHingeBendScale(setup, *yarn, rest, droop);
        CHECKP(fit.bracketed && std::fabs(fit.bendScale / truth - 1) < 0.05 && fit.runs <= 16,
               "recover bendScale %.2f at rest %.0f deg from its droop of %.2f deg: %.4f (%+.2f %%) in %d runs", truth, rest, droop, fit.bendScale, 100 * (fit.bendScale / truth - 1), fit.runs);
    }
    {
        const HingeFit stiffFit = fitHingeBendScale(setup, *yarn, 0.0, simulateDroop(setup, *yarn, 1.5, 0.0));
        bool noted = false;
        for (const std::string& n : stiffFit.notes) noted = noted || n.find("stiff hinge") != std::string::npos;
        CHECKP(stiffFit.bracketed && noted, "a stiff hinge (true 1.5, fitted %.2f): the notes say it is only roughly pinned", stiffFit.bendScale);
    }

    // a measurement no bendScale can give
    {
        const HingeFit soft = fitHingeBendScale(setup, *yarn, 0.0, 130.0);
        const HingeFit stiff = fitHingeBendScale(setup, *yarn, 0.0, 5.0);
        CHECKP(!soft.bracketed && soft.bendScale == kMinBendScale && !soft.notes.empty() && !stiff.bracketed && stiff.bendScale == kMaxBendScale && !stiff.notes.empty() && soft.runs == 2,
               "out of range: 130 deg gives the softest (%.2f) and 5 deg the stiffest (%.1f), each with a note, after only the two end runs", soft.bendScale, stiff.bendScale);
    }

    FINISH();
}
