// See failoring/crochet/calibration.hpp.
#include "failoring/crochet/calibration.hpp"

#include <cmath>
#include <cstdio>

namespace fl {
namespace {

constexpr Real kGravity = 9.81;

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

struct JFit {
    bool ok = false;
    Real k0 = 0, lock = 1, ratio = 1;
    bool locked = false;
};

// One direction of step 4; pushes an error and returns ok = false when the numbers cannot be a stretch test.
JFit fitStretch(const StretchMeasurement& s, const char* dir, CalibrationFit& out) {
    JFit f;
    if (!s.given()) return f;
    if (s.widthCm <= 0 || s.l0Mm <= 0 || s.w1G <= 0 || s.l1Mm <= s.l0Mm) {
        out.errors.push_back(std::string("stretch (") + dir + "): needs a width, an unloaded length L0, a weight W1 and a length L1 longer than L0");
        return f;
    }
    const Real width = s.widthCm * 1e-2;
    const Real t1 = s.w1G * 1e-3 * kGravity / width;
    const Real e1 = (s.l1Mm - s.l0Mm) / s.l0Mm;
    f.k0 = t1 / e1;
    f.ok = true;
    if (s.w2G <= s.w1G || s.l2Mm <= s.l1Mm) {
        out.notes.push_back(std::string("stretch (") + dir + "): only one weight (or W2 <= W1 or L2 <= L1): k0 = " + fmt("%.1f N/m", f.k0) + ", no lock reached, use a heavier W2");
        return f;
    }
    const Real t2 = s.w2G * 1e-3 * kGravity / width;
    const Real e2 = (s.l2Mm - s.l0Mm) / s.l0Mm;
    const Real slope2 = (t2 - t1) / (e2 - e1);
    const Real ratio = slope2 / f.k0;
    if (ratio >= kLockSteeper) {
        f.locked = true;
        f.lock = e1;
        f.ratio = ratio;
        out.notes.push_back(std::string("stretch (") + dir + "): k0 = " + fmt("%.1f N/m", f.k0) + ", locks at strain " + fmt("%.3f", e1) + ", second slope " + fmt("%.2f", ratio) + " x k0");
    } else {
        out.notes.push_back(std::string("stretch (") + dir + "): k0 = " + fmt("%.1f N/m", f.k0) + ", second slope " + fmt("%.2f", ratio) + " x k0: no lock reached, use a heavier W2");
    }
    return f;
}

}  // namespace

CalibrationFit fitCalibration(const SwatchMeasurements& m) {
    CalibrationFit out;
    Calibration& c = out.cal;

    // 1 gauge
    if (m.stitchesPer10cm > 0 || m.rowsPer10cm > 0) {
        if (m.stitchesPer10cm > 0 && m.rowsPer10cm > 0) {
            c.gauge = true;
            c.w = 0.1 / m.stitchesPer10cm;
            c.h = 0.1 / m.rowsPer10cm;
            out.notes.push_back("gauge: w = " + fmt("%.2f mm", c.w * 1e3) + ", h = " + fmt("%.2f mm", c.h * 1e3));
        } else {
            out.errors.push_back("gauge: needs both the stitches and the rows in 10 cm");
        }
    }

    // 2 weight
    if (m.massG > 0 || m.widthCm > 0 || m.heightCm > 0 || m.stitches > 0 || m.rows > 0) {
        Real area = 0;
        std::string why;
        if (m.massG <= 0) {
            why = "needs the swatch's mass";
        } else if (m.widthCm > 0 && m.heightCm > 0) {
            area = m.widthCm * m.heightCm * 1e-4;
        } else if (m.stitches > 0 && m.rows > 0) {
            if (c.gauge) area = (m.stitches * c.w) * (m.rows * c.h);
            else why = "the swatch size is given as stitch and row counts, which needs the gauge of step 1";
        } else {
            why = "needs the swatch's size (width and height in cm, or stitch and row counts)";
        }
        if (why.empty()) {
            c.mass = true;
            c.density = m.massG * 1e-3 / area;
            out.notes.push_back("weight: density = " + fmt("%.3f kg/m^2", c.density));
        } else {
            out.errors.push_back("weight: " + why);
        }
    }

    // 3 cantilever: B = density * g * c^3 for each strip
    if (m.overhangWaleMm > 0 || m.overhangCourseMm > 0) {
        if (!c.mass) {
            out.errors.push_back("cantilever: B = density * g * c^3 needs the density of step 2");
        } else if (m.overhangWaleMm <= 0 || m.overhangCourseMm <= 0) {
            out.errors.push_back("cantilever: needs both strips, the wale one and the course one");
        } else {
            const Real cw = 0.5 * m.overhangWaleMm * 1e-3, cc = 0.5 * m.overhangCourseMm * 1e-3;
            c.cantilever = true;
            c.bendWale = c.density * kGravity * cw * cw * cw;
            c.bendCourse = c.density * kGravity * cc * cc * cc;
            out.notes.push_back("cantilever: bendWale = " + fmt("%.3g N*m", c.bendWale) + ", bendCourse = " + fmt("%.3g N*m", c.bendCourse));
        }
    }

    // 4 stretch
    const JFit sw = fitStretch(m.wale, "wale", out), sc = fitStretch(m.course, "course", out);
    if (m.wale.given() || m.course.given()) {
        if (sw.ok && sc.ok) {
            c.stretch = true;
            c.kWale = sw.k0;
            c.kCourse = sc.k0;
            c.lockWale = sw.lock;
            c.lockCourse = sc.lock;
            out.lockReachedWale = sw.locked;
            out.lockReachedCourse = sc.locked;
            int n = 0;
            Real sum = 0;
            for (const JFit* f : {&sw, &sc})
                if (f->locked) {
                    sum += f->ratio;
                    ++n;
                }
            c.lockRatio = n > 0 ? sum / n : 1.0;
        } else if ((m.wale.given() && !m.course.given() && sw.ok) || (m.course.given() && !m.wale.given() && sc.ok)) {
            out.errors.push_back("stretch: needs both directions, wale and course");  // (a strip with impossible numbers has its own error)
        }
    }

    if (!out.any() && out.errors.empty()) out.notes.push_back("nothing measured yet");
    return out;
}

}  // namespace fl
