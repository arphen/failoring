// Self-checks for the fabric bench's theory helpers and analysis functions. Pure math, no solver runs (fast).
// The solver-based checks (cantilever B vs theory, bias strip) are in the bench itself (fabric_bench --fabric ...).
#include <cmath>
#include <cstdio>
#include <vector>

#include "fabric_bench.hpp"

using namespace fb;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                              \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL %s:%d: %s -> ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                     \
            std::printf("\n");                                            \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main() {
    // 1. Small-load limit of the inextensible elastica. Tip deflection of a uniformly loaded cantilever is
    //    w = q L^4 / (8 B) = lambda L / 8 ... so the chord angle tends to lambda / 8 (radians) for small lambda.
    {
        const double lambda = 0.1;
        const double chord = elasticaChordAngle(lambda);
        const double expect = lambda / 8.0;
        const double rel = std::fabs(chord - expect) / expect;
        std::printf("elastica small-load: chord(%.2f) = %.6f rad, lambda/8 = %.6f, rel err %.3f %%\n", lambda, chord, expect, 100 * rel);
        CHECK_MSG(rel < 0.02, "chord %.6f vs lambda/8 %.6f", chord, expect);
    }
    // 2. Round trip lambda -> chord -> lambda.
    for (double lam : {0.05, 0.5, 2.0, 10.0}) {
        const double chord = elasticaChordAngle(lam);
        const double back = elasticaLambdaForChord(chord);
        std::printf("elastica round trip: lambda %.3f -> chord %.4f rad -> %.5f\n", lam, chord, back);
        CHECK_MSG(std::fabs(back - lam) < 1e-4 * lam, "lambda %.6f came back as %.6f", lam, back);
    }
    // 3. Bending from overhang and the inverse agree.
    {
        const double rho = 0.12, thetaRad = 41.5 * kPi / 180;
        const double B = 1.2e-5;
        const double l = overhangFromBendingElastica(B, rho, thetaRad);
        const double Bback = bendingFromOverhangElastica(l, rho, thetaRad);
        CHECK_MSG(std::fabs(Bback - B) < 1e-9 * B, "B %.6e -> l %.6f -> B %.6e", B, l, Bback);
    }
    // 4. Peirce and elastica bending at the standard 41.5 deg angle. Printed, not asserted: both are UNVERIFIED
    //    against ISO 9073-7 in this repository.
    {
        const double rho = 0.12, thetaRad = 41.5 * kPi / 180, l = 0.05;
        const double Be = bendingFromOverhangElastica(l, rho, thetaRad);
        const double Bp = bendingFromOverhangPeirce(l, rho, thetaRad);
        const double Bh = bendingFromOverhangHalf(l, rho);
        std::printf("bending formulas at l=50 mm, 41.5 deg: elastica %.4e, Peirce %.4e (x%.3f), l/2 %.4e (x%.3f) N m\n", Be, Bp, Bp / Be, Bh,
                    Bh / Be);
    }
    // 5. Fold counter on synthetic radial profiles (360 bins).
    {
        std::vector<double> flat(360, 0.10);
        CHECK_MSG(countFolds(flat) == 0, "flat disc has %d folds", countFolds(flat));
        std::vector<double> four(360);
        for (int i = 0; i < 360; ++i) four[i] = 0.10 + 0.01 * std::cos(4 * i * kPi / 180);
        const int f4 = countFolds(four);
        CHECK_MSG(f4 == 4, "4-fold profile counted %d", f4);
        std::vector<double> ripple(360);
        for (int i = 0; i < 360; ++i) ripple[i] = 0.10 + 0.0005 * std::cos(9 * i * kPi / 180);
        CHECK_MSG(countFolds(ripple) == 0, "0.5 mm ripple counted %d folds (must be below 3 mm threshold)", countFolds(ripple));
    }
    // 6. Shadow raster on a disc of radius 0.1 m inside a 0.2 m half-extent window: area should be pi r^2 within 1 %.
    {
        std::vector<fl::Vec3> x;
        std::vector<std::array<int, 3>> tris;
        const int n = 720;
        const double r = 0.1;
        x.push_back({0, 0, 0});
        for (int i = 0; i < n; ++i) x.push_back({r * std::cos(2 * kPi * i / n), 0, r * std::sin(2 * kPi * i / n)});
        for (int i = 0; i < n; ++i) tris.push_back({0, 1 + i, 1 + (i + 1) % n});
        std::vector<double> radial;
        const double area = shadowRaster(x, tris, 0.2, 1000, &radial);
        const double expect = kPi * r * r;
        CHECK_MSG(std::fabs(area - expect) / expect < 0.01, "disc area %.6f vs %.6f", area, expect);
        std::printf("shadow raster disc: area %.6f m^2, pi r^2 = %.6f\n", area, expect);
    }
    std::printf(failures ? "RESULT: %d failure(s)\n" : "RESULT: ok\n", failures);
    return failures ? 1 : 0;
}
