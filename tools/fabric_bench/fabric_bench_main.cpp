// fabric_bench: run virtual textile tests on fabric presets.
//   fabric_bench [--materials materials/fabrics.json] [--fabric ID|all] [--out DIR] [--md FILE]
//                [--skip bending,tensile,shear,drape] [--bend-levels 6,10,16] [--drape-levels 0.014,0.010,0.007] [--quiet]
//   fabric_bench --bias-strip [--ortho R] --fabric ID   bias-angle strip check (0..90 deg, 15 deg steps), one mesh level.
//                --ortho R sets bending_weft = bending_warp / R (synthetic, strongly orthotropic variant of ID).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "fabric_bench.hpp"

#ifndef FL_MATERIALS_PATH
#define FL_MATERIALS_PATH "materials/fabrics.json"
#endif

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(t);
    return v;
}

int main(int argc, char** argv) {
    std::string matPath = FL_MATERIALS_PATH, which = "all", outDir = "out/fabric_bench", mdPath;
    bool biasStrip = false;
    double ortho = 0;
    int biasN = 10;
    fb::BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--materials") matPath = next();
        else if (a == "--fabric") which = next();
        else if (a == "--out") outDir = next();
        else if (a == "--md") mdPath = next();
        else if (a == "--quiet") cfg.verbose = false;
        else if (a == "--bias-strip") biasStrip = true;
        else if (a == "--ortho") ortho = std::atof(next().c_str());
        else if (a == "--bias-n") biasN = std::atoi(next().c_str());
        else if (a == "--drape-frames") cfg.drapeFrames = std::atoi(next().c_str());
        else if (a == "--skip") {
            for (const auto& s : split(next())) {
                if (s == "bending") cfg.doBending = false;
                else if (s == "tensile") cfg.doTensile = false;
                else if (s == "shear") cfg.doShear = false;
                else if (s == "drape") cfg.doDrape = false;
            }
        } else if (a == "--bend-levels") {
            cfg.bendLevels.clear();
            for (const auto& s : split(next())) cfg.bendLevels.push_back(std::atoi(s.c_str()));
        } else if (a == "--drape-levels") {
            cfg.drapeLevels.clear();
            for (const auto& s : split(next())) cfg.drapeLevels.push_back(std::atof(s.c_str()));
        } else {
            std::fprintf(stderr, "unknown option %s (see the header comment of fabric_bench_main.cpp)\n", a.c_str());
            return 2;
        }
    }
    std::vector<fl::FabricMaterial> mats;
    try {
        mats = fl::loadMaterialsFromFile(matPath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cannot load %s: %s\n", matPath.c_str(), e.what());
        return 1;
    }
    std::string mkdir = "mkdir -p '" + outDir + "'";
    if (std::system(mkdir.c_str()) != 0) return 1;
    if (biasStrip) {
        std::vector<double> angles;
        for (int a = 0; a <= 90; a += 15) angles.push_back(a);
        int ran = 0;
        for (auto m : mats) {
            if (which != "all" && m.id != which) continue;
            if (ortho > 0) {
                m.bendWeft = m.bendWarp / ortho;
                m.id += "_ortho" + std::to_string(static_cast<int>(ortho));
                m.name += " (synthetic bending ratio " + std::to_string(static_cast<int>(ortho)) + ":1)";
            }
            const auto pts = fb::runBiasStrip(m, cfg, angles, biasN);
            ++ran;
            std::string tbl = "| angle (deg) | B model (N m) | B derived (N m) | derived / model |\n|---|---|---|---|\n";
            char row[256];
            for (const auto& p : pts) {
                std::snprintf(row, sizeof row, "| %.0f | %.3e | %.3e | %.3f |\n", p.angleDeg, p.Bmodel, p.Bderived, p.ratio);
                tbl += row;
            }
            std::ofstream(outDir + "/bias_" + m.id + ".md") << "### " << m.id << " bias-angle strip (n=" << biasN << " elements)\n\n" << tbl;
            std::printf("### %s bias-angle strip (n=%d)\n\n%s\n", m.id.c_str(), biasN, tbl.c_str());
            std::fflush(stdout);
        }
        if (ran == 0) { std::fprintf(stderr, "no fabric matched '%s'\n", which.c_str()); return 1; }
        return 0;
    }
    std::vector<fb::FabricBench> all;
    for (const auto& m : mats) {
        if (which != "all" && m.id != which) continue;
        if (cfg.verbose) std::fprintf(stderr, "== %s\n", m.id.c_str());
        all.push_back(fb::runFabricBench(m, cfg));
        std::ofstream(outDir + "/" + m.id + ".json") << fb::benchToJson(all.back(), cfg);
        std::ofstream(outDir + "/" + m.id + ".md") << fb::benchToMarkdown(all.back(), cfg);
        std::printf("%s", fb::benchToMarkdown(all.back(), cfg).c_str());
        std::fflush(stdout);
    }
    if (all.empty()) {
        std::fprintf(stderr, "no fabric matched '%s'\n", which.c_str());
        return 1;
    }
    const std::string summary = fb::summaryMarkdown(all, cfg);
    std::ofstream(outDir + "/summary.md") << summary;
    std::printf("\n%s", summary.c_str());
    if (!mdPath.empty()) {
        std::ofstream md(mdPath);
        md << summary << "\n";
        for (const auto& b : all) md << fb::benchToMarkdown(b, cfg) << "\n";
    }
    return 0;
}
