// The result numbers of a crochet run (docs/CROCHET_PLAN.md section 7.5). See crochet/metrics.hpp.
#include "failoring/crochet/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace fl {

namespace {

struct Box {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    void add(const Vec3& p) {
        lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
};

// Largest distance of x from its best-fit plane (smallest eigenvector of the covariance, Jacobi).
Real planeDeviation(const std::vector<Vec3>& x, Vec3* normalOut = nullptr) {
    if (x.size() < 3) return 0;
    Vec3 c(0, 0, 0);
    for (const Vec3& p : x) c += p;
    c = c / static_cast<Real>(x.size());
    Real a[3][3] = {};
    for (const Vec3& p : x) {
        const Vec3 d = p - c;
        const Real v[3] = {d.x, d.y, d.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) a[i][j] += v[i] * v[j];
    }
    Real V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int sweep = 0; sweep < 60; ++sweep)
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-30) continue;
                const Real th = 0.5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]), cs = std::cos(th), sn = std::sin(th);
                for (int k = 0; k < 3; ++k) {
                    const Real akp = a[k][p], akq = a[k][q];
                    a[k][p] = cs * akp - sn * akq;
                    a[k][q] = sn * akp + cs * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const Real apk = a[p][k], aqk = a[q][k];
                    a[p][k] = cs * apk - sn * aqk;
                    a[q][k] = sn * apk + cs * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const Real vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = cs * vkp - sn * vkq;
                    V[k][q] = sn * vkp + cs * vkq;
                }
            }
    int m = 0;
    for (int i = 1; i < 3; ++i)
        if (a[i][i] < a[m][m]) m = i;
    const Vec3 n = normalized(Vec3(V[0][m], V[1][m], V[2][m]));
    if (normalOut != nullptr) *normalOut = n;
    Real dev = 0;
    for (const Vec3& p : x) dev = std::max(dev, std::fabs(dot(p - c, n)));
    return dev;
}

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * (v.size() - 1))];
}

std::string num(double x) {
    if (!std::isfinite(x)) return "null";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", x);
    return buf;
}

std::string esc(const std::string& s) {
    std::string o;
    for (const char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        o += c;
    }
    return o;
}

}  // namespace

Metrics computeMetrics(const MetricsInput& in) {
    Metrics m;
    const CrochetModel& model = *in.model;
    const StitchGraph& g = *in.graph;
    const LayoutReport& layout = *in.layout;
    const CrochetMesh& topo = model.topo;
    const int nv = static_cast<int>(topo.vertexStitch.size());

    // yarn
    for (const Stitch& s : g.st) m.yarn_m += model.dims[s.id].yarnLen;
    m.mass_g = model.yarnMass * 1e3;
    m.skeins = in.yarn != nullptr && in.yarn->metres > 0 ? m.yarn_m / in.yarn->metres : 0;

    // the strap rows and the body vertices
    const LayoutRegion* strap = nullptr;
    for (const LayoutRegion& r : layout.regions)
        if (r.type == LayoutRegion::Strip && (strap == nullptr || r.lastRow - r.firstRow > strap->lastRow - strap->firstRow)) strap = &r;
    std::vector<char> isStrap(nv, 0);
    for (int v = 0; v < nv; ++v) {
        const int row = g.st[topo.vertexStitch[v]].row;
        isStrap[v] = strap != nullptr && row >= strap->firstRow && row <= strap->lastRow;
    }

    // finished flat size (relaxed, zero g): the body's bounding box
    if (!in.relaxed.empty()) {
        Box b;
        for (int v = 0; v < nv; ++v)
            if (!isStrap[v]) b.add(in.relaxed[v]);
        m.flat_width_cm = (b.hi.x - b.lo.x) * 100;
        m.flat_depth_cm = (b.hi.z - b.lo.z) * 100;
        m.flat_height_cm = (b.hi.y - b.lo.y) * 100;
    }

    const std::vector<Vec3>& finalX = !in.loaded.empty() ? in.loaded : (!in.hanging.empty() ? in.hanging : in.relaxed);

    // hanging
    if (!in.hanging.empty()) {
        m.hasHang = true;
        Box all, body;
        for (int v = 0; v < nv; ++v) {
            all.add(in.hanging[v]);
            if (!isStrap[v]) body.add(in.hanging[v]);
        }
        m.drop_cm = (all.hi.y - all.lo.y) * 100;
        m.bag_height_cm = (body.hi.y - body.lo.y) * 100;
        // the gape: the mouth's extent along the horizontal axis of least extent
        std::vector<Vec3> mouth;
        for (const int row : layout.openingRows)
            for (int v = 0; v < nv; ++v)
                if (g.st[topo.vertexStitch[v]].row == row) mouth.push_back(in.hanging[v]);
        if (!mouth.empty()) {
            Real cx = 0, cz = 0;
            for (const Vec3& p : mouth) {
                cx += p.x;
                cz += p.z;
            }
            cx /= mouth.size();
            cz /= mouth.size();
            Real sxx = 0, szz = 0, sxz = 0;
            for (const Vec3& p : mouth) {
                sxx += (p.x - cx) * (p.x - cx);
                szz += (p.z - cz) * (p.z - cz);
                sxz += (p.x - cx) * (p.z - cz);
            }
            const Real th = 0.5 * std::atan2(2 * sxz, sxx - szz);  // major axis angle
            const Vec3 minor(-std::sin(th), 0, std::cos(th));
            Real lo = 1e300, hi = -1e300;
            for (const Vec3& p : mouth) {
                const Real t = dot(p, minor);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
            }
            m.opening_gape_cm = (hi - lo) * 100;
        }
    }

    // the strap: centre line through the row centroids
    if (strap != nullptr) {
        m.hasStrap = true;
        auto centreLine = [&](const std::vector<Vec3>& x) {
            Real len = 0;
            Vec3 prev;
            bool first = true;
            for (int row = strap->firstRow; row <= strap->lastRow; ++row) {
                Vec3 c(0, 0, 0);
                int n = 0;
                for (int v = 0; v < nv; ++v)
                    if (g.st[topo.vertexStitch[v]].row == row) {
                        c += x[v];
                        ++n;
                    }
                if (n == 0) continue;
                c = c / n;
                if (!first) len += length(c - prev);
                prev = c;
                first = false;
            }
            return len;
        };
        Real rest = 0;
        for (int row = strap->firstRow + 1; row <= strap->lastRow; ++row) {
            Real h = 0;
            int n = 0;
            for (const Stitch& s : g.st)
                if (s.row == row && topo.stitchVertex[s.id] >= 0) {
                    h += model.dims[s.id].h;
                    ++n;
                }
            if (n > 0) rest += h / n;
        }
        m.strap_length_rest_cm = rest * 100;
        m.strap_length_loaded_cm = centreLine(finalX) * 100;
        m.strap_elongation_pct = rest > 0 ? 100 * (centreLine(finalX) / rest - 1) : 0;
    }

    // base sag and corner angle
    int baseEnd = in.baseEndRow;
    if (baseEnd < 0) {
        // the last increase round of the revolve region
        const LayoutRegion* rev = nullptr;
        for (const LayoutRegion& r : layout.regions)
            if (r.type == LayoutRegion::Revolve) rev = &r;
        if (rev != nullptr) {
            auto count = [&](int row) {
                int n = 0;
                for (const Stitch& s : g.st) n += s.row == row && topo.stitchVertex[s.id] >= 0;
                return n;
            };
            baseEnd = rev->firstRow;
            for (int row = rev->firstRow + 1; row <= rev->lastRow; ++row)
                if (count(row) > count(row - 1)) baseEnd = row;
        }
    }
    if (baseEnd >= 0) {
        m.hasBase = true;
        std::vector<Vec3> basePts;
        for (int v = 0; v < nv; ++v) {
            const int row = g.st[topo.vertexStitch[v]].row;
            if (row <= baseEnd && !isStrap[v]) basePts.push_back(finalX[v]);
        }
        m.base_sag_mm = planeDeviation(basePts) * 1e3;
        // edges between faces whose source is in the base (row <= baseEnd) and above it (row > baseEnd, not the strap)
        std::map<std::pair<int, int>, std::vector<int>> et;
        for (size_t t = 0; t < topo.tris.size(); ++t)
            for (int k = 0; k < 3; ++k) {
                int a = topo.tris[t][k], b = topo.tris[t][(k + 1) % 3];
                if (a > b) std::swap(a, b);
                et[{a, b}].push_back(static_cast<int>(t));
            }
        auto normal = [&](int t) {
            const auto& v = topo.tris[t];
            return normalized(cross(finalX[v[1]] - finalX[v[0]], finalX[v[2]] - finalX[v[0]]));
        };
        double sum = 0;
        int cnt = 0;
        for (const auto& kv : et) {
            if (kv.second.size() != 2) continue;
            const int r0 = g.st[topo.triSource[kv.second[0]]].row, r1 = g.st[topo.triSource[kv.second[1]]].row;
            if (!((r0 <= baseEnd && r1 > baseEnd) || (r1 <= baseEnd && r0 > baseEnd))) continue;
            const Real c = std::max(-1.0, std::min(1.0, dot(normal(kv.second[0]), normal(kv.second[1]))));
            sum += std::acos(c) * 180 / kPi;
            ++cnt;
        }
        m.corner_angle_deg = cnt > 0 ? 180 - sum / cnt : 180;
    }

    // strain per triangle
    {
        std::vector<double> sw, sc;
        double worstW = -1e300, worstC = -1e300;
        for (size_t t = 0; t < topo.tris.size(); ++t) {
            const auto& tv = topo.tris[t];
            const auto& r = model.mesh.triRest[t];
            const Real du1 = r[1].x - r[0].x, dv1 = r[1].y - r[0].y, du2 = r[2].x - r[0].x, dv2 = r[2].y - r[0].y;
            const Real det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) < 1e-18) continue;
            const Vec3 e1 = finalX[tv[1]] - finalX[tv[0]], e2 = finalX[tv[2]] - finalX[tv[0]];
            // F = [e1 e2] [d1 d2]^-1: columns dx/du, dx/dv
            const Vec3 fu = (e1 * dv2 - e2 * dv1) / det;
            const Vec3 fv = (e2 * du1 - e1 * du2) / det;
            const double eu = length(fu) - 1, ev = length(fv) - 1;
            sw.push_back(std::max(eu, 0.0));
            sc.push_back(std::max(ev, 0.0));
            const Stitch& src = g.st[topo.triSource[t]];
            if (eu > worstW) {
                worstW = eu;
                m.worst_wale_row = src.row;
                m.worst_wale_stitch = src.id;
            }
            if (ev > worstC) {
                worstC = ev;
                m.worst_course_row = src.row;
                m.worst_course_stitch = src.id;
            }
        }
        m.strain_wale_max = sw.empty() ? 0 : *std::max_element(sw.begin(), sw.end());
        m.strain_course_max = sc.empty() ? 0 : *std::max_element(sc.begin(), sc.end());
        m.strain_wale_p95 = percentile(sw, 0.95);
        m.strain_course_p95 = percentile(sc, 0.95);
    }

    // load
    if (in.hasLoad) {
        m.hasLoad = true;
        m.load_tilt_deg = 0;  // stage A: translation only
        Real rim = 0;
        int n = 0;
        for (const int row : layout.openingRows)
            for (int v = 0; v < nv; ++v)
                if (g.st[topo.vertexStitch[v]].row == row) {
                    rim += finalX[v].y;
                    ++n;
                }
        rim = n ? rim / n : 0;
        m.load_top_above_rim_cm = (in.loadCenter.y + 0.5 * in.load.height - rim) * 100;
        m.max_contact_pressure_kPa = in.maxContactPressure / 1000;
        m.fits = in.fit.fits;
        m.tight = in.fit.tight;
        m.fit_message = in.fit.message;
    }

    m.frames = in.frames;
    m.newton_iterations = in.newtonIterations;
    m.cap_hits = in.iterCapHits;
    m.line_search_failures = in.lineSearchFailures;
    m.final_kinetic_energy = in.finalKineticEnergy;
    return m;
}

std::string Metrics::toJson() const {
    std::ostringstream o;
    auto key = [&](const char* k, const std::string& v, bool last = false) { o << "  \"" << k << "\": " << v << (last ? "\n" : ",\n"); };
    auto opt = [&](bool has, double x) { return has ? num(x) : std::string("null"); };
    o << "{\n";
    key("yarn_m", num(yarn_m));
    key("mass_g", num(mass_g));
    key("skeins", num(skeins));
    key("flat_width_cm", num(flat_width_cm));
    key("flat_depth_cm", num(flat_depth_cm));
    key("flat_height_cm", num(flat_height_cm));
    key("drop_cm", opt(hasHang, drop_cm));
    key("bag_height_cm", opt(hasHang, bag_height_cm));
    key("opening_gape_cm", opt(hasHang, opening_gape_cm));
    key("strap_length_rest_cm", opt(hasStrap, strap_length_rest_cm));
    key("strap_length_loaded_cm", opt(hasStrap && hasHang, strap_length_loaded_cm));
    key("strap_elongation_pct", opt(hasStrap && hasHang, strap_elongation_pct));
    key("base_sag_mm", opt(hasBase, base_sag_mm));
    key("corner_angle_deg", opt(hasBase, corner_angle_deg));
    key("strain_wale_max", num(strain_wale_max));
    key("strain_wale_p95", num(strain_wale_p95));
    key("strain_course_max", num(strain_course_max));
    key("strain_course_p95", num(strain_course_p95));
    key("worst_wale_strain_at", "{\"row\": " + std::to_string(worst_wale_row) + ", \"stitch\": " + std::to_string(worst_wale_stitch) + "}");
    key("worst_course_strain_at", "{\"row\": " + std::to_string(worst_course_row) + ", \"stitch\": " + std::to_string(worst_course_stitch) + "}");
    key("load_tilt_deg", opt(hasLoad, load_tilt_deg));
    key("load_top_above_rim_cm", opt(hasLoad, load_top_above_rim_cm));
    key("max_contact_pressure_kPa", opt(hasLoad, max_contact_pressure_kPa));
    key("fits", hasLoad ? (fits ? "true" : "false") : "null");
    key("tight_fit", hasLoad ? (tight ? "true" : "false") : "null");
    key("fit_message", hasLoad ? "\"" + esc(fit_message) + "\"" : "null");
    key("frames", std::to_string(frames));
    key("newton_iterations", std::to_string(newton_iterations));
    key("cap_hits", std::to_string(cap_hits));
    key("line_search_failures", std::to_string(line_search_failures));
    key("final_kinetic_energy", num(final_kinetic_energy), true);
    o << "}\n";
    return o.str();
}

}  // namespace fl
