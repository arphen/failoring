#include "crochet_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "failoring/crochet/pattern.hpp"

namespace godot {

namespace {

std::string toStd(const String& s) { return s.utf8().get_data(); }

// "...line 12: msg" -> 12 (0 when there is no line number)
int lineOf(const std::string& what) {
    const size_t p = what.find("line ");
    if (p == std::string::npos) return 0;
    return std::atoi(what.c_str() + p + 5);
}

std::string readTextFile(const std::string& path) {
    Ref<FileAccess> f = FileAccess::open(String(path.c_str()), FileAccess::READ);
    return f.is_valid() ? toStd(f->get_as_text()) : std::string();
}

std::string globalPath(const String& p) { return toStd(ProjectSettings::get_singleton()->globalize_path(p)); }

}  // namespace

// ---- CrochetPattern ----------------------------------------------------------------------------------------------

void CrochetPattern::set_error(int line, const std::string& msg) { errors_.push_back({line, msg}); }

bool CrochetPattern::compile(const String& text, const String& yarns_path) {
    errors_.clear();
    compiled_ = false;
    modelReady_ = false;
    try {
        const std::string yarnsText = readTextFile(toStd(yarns_path));
        if (yarnsText.empty()) {
            set_error(0, "cannot read the yarn database " + toStd(yarns_path));
            return false;
        }
        book_ = fl::loadYarnsFromString(yarnsText);
        if (book_.find(yarn_id_) == nullptr && !book_.yarns.empty()) yarn_id_ = book_.yarns.front().id;
        // the tools: the repo root is the parent of the Godot project
        std::string tools = tools_dir_;
        if (tools.empty()) tools = globalPath("res://") + "/..";
        const std::string exporter = tools + "/tools/crochet/cp_export.mjs";
        const std::string cpDir = tools + "/third_party/crochetparade";
        // a temp dir under user://
        DirAccess::make_dir_recursive_absolute("user://crochet");
        const std::string tmpTxt = globalPath("user://crochet/_compile.txt"), tmpJson = globalPath("user://crochet/_compile.json");
        {
            Ref<FileAccess> f = FileAccess::open("user://crochet/_compile.txt", FileAccess::WRITE);
            if (f.is_null()) {
                set_error(0, "cannot write user://crochet/_compile.txt");
                return false;
            }
            f->store_string(text);
            f->close();
        }
        DirAccess::remove_absolute("user://crochet/_compile.json");
        const std::string cmd = "PATH=\"$PATH:/opt/homebrew/bin:/usr/local/bin\" CP_DIR='" + cpDir + "' node '" + exporter + "' --out '" + tmpJson + "' '" + tmpTxt + "' > /dev/null 2>&1";
        const int rc = std::system(cmd.c_str());
        const std::string json = readTextFile(tmpJson);
        if (json.empty()) {
            set_error(0, std::string("the pattern exporter did not run (exit ") + std::to_string(rc) + "): is node installed, and is CrochetPARADE at " + cpDir + "?");
            return false;
        }
        graph_ = fl::importPattern(json, toStd(text));
        compiled_ = true;
    } catch (const std::exception& e) {
        set_error(lineOf(e.what()), e.what());
        compiled_ = false;
    }
    return compiled_;
}

TypedArray<Dictionary> CrochetPattern::get_errors() const {
    TypedArray<Dictionary> out;
    for (const auto& e : errors_) {
        Dictionary d;
        d["line"] = e.first;
        d["msg"] = String(e.second.c_str());
        out.push_back(d);
    }
    return out;
}

void CrochetPattern::set_yarn(const String& id) {
    yarn_id_ = toStd(id);
    modelReady_ = false;
}
void CrochetPattern::set_hook(double mm) {
    hook_mm_ = mm;
    modelReady_ = false;
}
void CrochetPattern::set_tension(double t) {
    tension_ = t;
    modelReady_ = false;
}

PackedStringArray CrochetPattern::get_yarn_ids() const {
    PackedStringArray out;
    for (const fl::Yarn& y : book_.yarns) out.push_back(String(y.id.c_str()));
    return out;
}

Dictionary CrochetPattern::get_yarn_info(const String& id) const {
    Dictionary d;
    const fl::Yarn* y = book_.find(toStd(id));
    if (y == nullptr) return d;
    d["id"] = String(y->id.c_str());
    d["name"] = String(y->name.c_str());
    d["fibre"] = String(y->fibre.c_str());
    d["colour"] = String(y->colour.c_str());
    d["weight_class"] = y->weightClass;
    d["grams"] = y->grams;
    d["metres"] = y->metres;
    d["diameter_mm"] = y->diameterMm;
    d["hook_mm"] = y->hookMm;
    return d;
}

void CrochetPattern::rebuild_model() {
    if (modelReady_ || !compiled_) return;
    modelError_.clear();
    try {
        const fl::Yarn* y = book_.find(yarn_id_);
        if (y == nullptr) throw std::runtime_error("unknown yarn '" + yarn_id_ + "'");
        fl::ModelOptions mo;
        mo.hook_m = (hook_mm_ > 0 ? hook_mm_ : y->hookMm) * 1e-3;
        mo.tension = tension_;
        model_ = fl::buildCrochetModel(graph_, *y, mo);
        layout_ = fl::layoutModel(model_, graph_);
        modelReady_ = true;
    } catch (const std::exception& e) {
        modelError_ = e.what();
        modelReady_ = false;
    }
}

Dictionary CrochetPattern::get_summary() const {
    Dictionary d;
    if (!compiled_) return d;
    const_cast<CrochetPattern*>(this)->rebuild_model();
    d["stitches"] = static_cast<int>(graph_.st.size());
    d["rows"] = static_cast<int>(graph_.rows.size());
    d["links"] = static_cast<int>(graph_.links.size());
    if (!modelReady_) {
        d["error"] = String(modelError_.c_str());
        return d;
    }
    double yarn = 0;
    for (const fl::Stitch& s : graph_.st) yarn += model_.dims[s.id].yarnLen;
    d["yarn_m"] = yarn;
    d["mass_g"] = model_.yarnMass * 1e3;
    const fl::Yarn* y = book_.find(yarn_id_);
    d["skeins"] = y != nullptr && y->metres > 0 ? yarn / y->metres : 0.0;
    d["hook_mm"] = hook_mm_ > 0 ? hook_mm_ : (y != nullptr ? y->hookMm : 0.0);
    d["vertices"] = static_cast<int>(model_.topo.vertexStitch.size());
    d["faces"] = static_cast<int>(model_.mesh.tris.size());
    // the size of the layout (before the relaxation): the body's extents
    fl::Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    std::vector<char> strap(model_.topo.vertexStitch.size(), 0);
    for (const fl::LayoutRegion& r : layout_.regions)
        if (r.type == fl::LayoutRegion::Strip)
            for (size_t v = 0; v < strap.size(); ++v) {
                const int row = graph_.st[model_.topo.vertexStitch[v]].row;
                if (row >= r.firstRow && row <= r.lastRow) strap[v] = 1;
            }
    for (size_t v = 0; v < model_.mesh.x0.size(); ++v) {
        if (strap[v]) continue;
        const fl::Vec3& p = model_.mesh.x0[v];
        lo = fl::Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = fl::Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    d["flat_width_cm"] = (hi.x - lo.x) * 100;
    d["flat_depth_cm"] = (hi.z - lo.z) * 100;
    d["flat_height_cm"] = (hi.y - lo.y) * 100;
    d["has_strap"] = std::any_of(layout_.regions.begin(), layout_.regions.end(), [](const fl::LayoutRegion& r) { return r.type == fl::LayoutRegion::Strip; });
    d["fold_after_row"] = layout_.foldAfterRow;
    return d;
}

Dictionary CrochetPattern::get_chart() const {
    Dictionary chart;
    if (!compiled_) return chart;
    const_cast<CrochetPattern*>(this)->rebuild_model();
    if (!modelReady_) return chart;
    // chart coordinates in stitch units, from the layout: flat pieces as laid out (row 1 at the bottom, y up), rounds from above,
    // everything else beside it
    const int n = static_cast<int>(graph_.st.size());
    std::vector<double> cx(n, 0), cy(n, 0);
    std::vector<char> have(n, 0);
    const double w0 = model_.dims.empty() ? 1 : model_.dims[std::min(6, n - 1)].w, h0 = model_.dims.empty() ? 1 : model_.dims[std::min(6, n - 1)].h;
    for (const fl::LayoutRegion& r : layout_.regions) {
        for (const fl::Stitch& s : graph_.st) {
            if (s.row < r.firstRow || s.row > r.lastRow || model_.topo.stitchVertex[s.id] < 0) continue;
            const fl::Vec3& p = model_.mesh.x0[model_.topo.stitchVertex[s.id]];
            switch (r.type) {
                case fl::LayoutRegion::Flat: cx[s.id] = -p.x / w0; cy[s.id] = -p.y / h0; break;
                case fl::LayoutRegion::Revolve: cx[s.id] = p.x / w0; cy[s.id] = p.z / w0; break;
                default: cx[s.id] = s.index; cy[s.id] = s.row; break;
            }
            have[s.id] = 1;
        }
    }
    for (const fl::Stitch& s : graph_.st)  // dropped nodes sit beside their predecessor
        if (!have[s.id]) {
            const int p = s.prev;
            cx[s.id] = p >= 0 ? cx[p] + 0.5 : 0;
            cy[s.id] = p >= 0 ? cy[p] : 0;
        }
    Array stitches;
    for (const fl::Stitch& s : graph_.st) {
        Dictionary d;
        d["id"] = s.id;
        d["piece"] = String(s.piece.c_str());
        d["row"] = s.row;
        d["index"] = s.index;
        d["type"] = String(s.type.c_str());
        d["insertion"] = String(s.loopRs.c_str());  // RS-relative
        d["written"] = String(s.loop.c_str());      // as written (worker-relative)
        d["facing"] = String(s.rs ? "RS" : "WS");
        d["line"] = s.line;
        PackedInt32Array targets;
        for (const int t : s.targets) targets.push_back(t);
        d["targets"] = targets;
        d["x"] = cx[s.id];
        d["y"] = cy[s.id];
        d["in_mesh"] = model_.topo.stitchVertex[s.id] >= 0;
        stitches.push_back(d);
    }
    chart["stitches"] = stitches;
    Array rows;
    for (const fl::Row& r : graph_.rows) {
        Dictionary d;
        d["id"] = r.id;
        d["kind"] = String(r.kind.c_str());
        d["turned"] = r.turned;
        rows.push_back(d);
    }
    chart["rows"] = rows;
    Array links;
    for (const fl::Link& l : graph_.links) {
        Dictionary d;
        d["a"] = l.a;
        d["b"] = l.b;
        d["kind"] = String(l.kind.c_str());
        links.push_back(d);
    }
    chart["links"] = links;
    return chart;
}

void CrochetPattern::_bind_methods() {
    ClassDB::bind_method(D_METHOD("compile", "text", "yarns_path"), &CrochetPattern::compile);
    ClassDB::bind_method(D_METHOD("get_errors"), &CrochetPattern::get_errors);
    ClassDB::bind_method(D_METHOD("get_summary"), &CrochetPattern::get_summary);
    ClassDB::bind_method(D_METHOD("get_chart"), &CrochetPattern::get_chart);
    ClassDB::bind_method(D_METHOD("set_yarn", "id"), &CrochetPattern::set_yarn);
    ClassDB::bind_method(D_METHOD("get_yarn"), &CrochetPattern::get_yarn);
    ClassDB::bind_method(D_METHOD("set_hook", "mm"), &CrochetPattern::set_hook);
    ClassDB::bind_method(D_METHOD("get_hook"), &CrochetPattern::get_hook);
    ClassDB::bind_method(D_METHOD("set_tension", "tension"), &CrochetPattern::set_tension);
    ClassDB::bind_method(D_METHOD("get_yarn_ids"), &CrochetPattern::get_yarn_ids);
    ClassDB::bind_method(D_METHOD("get_yarn_info", "id"), &CrochetPattern::get_yarn_info);
    ClassDB::bind_method(D_METHOD("set_tools_dir", "dir"), &CrochetPattern::set_tools_dir);
    ClassDB::bind_method(D_METHOD("is_compiled"), &CrochetPattern::is_compiled);
}

// ---- CrochetScene ------------------------------------------------------------------------------------------------

bool CrochetScene::build(const Ref<CrochetPattern>& pattern, const String& scene, const String& load) {
    error_.clear();
    session_.reset();
    if (pattern.is_null() || !pattern->is_compiled()) {
        error_ = "the pattern is not compiled";
        return false;
    }
    try {
        const fl::Yarn* y = pattern->book().find(pattern->yarn_id());
        if (y == nullptr) throw std::runtime_error("unknown yarn '" + pattern->yarn_id() + "'");
        fl::ModelOptions mo;
        mo.hook_m = (pattern->hook_mm() > 0 ? pattern->hook_mm() : y->hookMm) * 1e-3;
        mo.tension = pattern->tension();
        fl::SessionSettings ss;
        ss.scene = toStd(scene);
        ss.load = toStd(load);
        session_ = std::make_unique<fl::CrochetSession>(pattern->graph(), *y, mo, ss);
    } catch (const std::exception& e) {
        error_ = e.what();
        session_.reset();
        return false;
    }
    return true;
}

void CrochetScene::relax_step(int n) {
    if (session_ != nullptr && session_->phase() == fl::CrochetSession::Phase::Relax) session_->step(n);
}
void CrochetScene::step(int n) {
    if (session_ != nullptr) session_->step(n);
}
String CrochetScene::phase() const { return session_ != nullptr ? String(session_->phaseName()) : String(); }
bool CrochetScene::is_done() const { return session_ != nullptr && session_->done(); }
bool CrochetScene::is_settled() const { return session_ != nullptr && session_->settled(); }
int CrochetScene::get_phase_frames() const { return session_ != nullptr ? session_->phaseFrames() : 0; }

PackedVector3Array CrochetScene::get_positions() const {
    PackedVector3Array out;
    if (session_ == nullptr) return out;
    const std::vector<fl::Vec3>& x = session_->positions();
    out.resize(static_cast<int64_t>(x.size()));
    for (size_t i = 0; i < x.size(); ++i) out.set(static_cast<int64_t>(i), Vector3(static_cast<float>(x[i].x), static_cast<float>(x[i].y), static_cast<float>(x[i].z)));
    return out;
}

PackedInt32Array CrochetScene::get_indices() const {
    PackedInt32Array out;
    if (session_ == nullptr) return out;
    for (const auto& t : session_->model().mesh.tris) {
        out.push_back(t[0]);
        out.push_back(t[1]);
        out.push_back(t[2]);
    }
    return out;
}

PackedVector3Array CrochetScene::get_normals() const {
    PackedVector3Array out;
    if (session_ == nullptr) return out;
    const std::vector<fl::Vec3>& x = session_->positions();
    std::vector<fl::Vec3> n(x.size(), fl::Vec3(0, 0, 0));
    for (const auto& t : session_->model().mesh.tris) {
        const fl::Vec3 fn = fl::cross(x[t[1]] - x[t[0]], x[t[2]] - x[t[0]]);  // area weighted
        n[t[0]] += fn;
        n[t[1]] += fn;
        n[t[2]] += fn;
    }
    out.resize(static_cast<int64_t>(x.size()));
    for (size_t i = 0; i < n.size(); ++i) {
        const fl::Vec3 u = fl::normalized(n[i]);
        out.set(static_cast<int64_t>(i), Vector3(static_cast<float>(u.x), static_cast<float>(u.y), static_cast<float>(u.z)));
    }
    return out;
}

PackedInt32Array CrochetScene::get_stitch_of_vertex() const {
    PackedInt32Array out;
    if (session_ == nullptr) return out;
    for (const int s : session_->model().topo.vertexStitch) out.push_back(s);
    return out;
}

PackedVector2Array CrochetScene::get_vertex_uv() const {
    PackedVector2Array out;
    if (session_ == nullptr) return out;
    const std::vector<fl::Vec2>& uv = session_->stitchUv();
    for (const int s : session_->model().topo.vertexStitch) out.push_back(Vector2(static_cast<float>(uv[s].x), static_cast<float>(uv[s].y)));
    return out;
}

PackedColorArray CrochetScene::get_vertex_colors() const {
    PackedColorArray out;
    if (session_ == nullptr) return out;
    const fl::StitchGraph& g = session_->graph();
    for (const int s : session_->model().topo.vertexStitch) {
        const fl::Stitch& st = g.st[s];
        float type = 0.f;
        if (st.type == "ch") type = 0.2f;
        else if (st.type == "slst") type = 0.4f;
        else if (st.type == "sc") type = 0.6f;
        else if (st.type == "hdc") type = 0.7f;
        else if (st.type == "dc") type = 0.8f;
        else if (st.type == "tr") type = 1.0f;
        const float ins = st.loopRs == "blo" ? 0.33f : st.loopRs == "flo" ? 0.66f : st.loopRs == "space" ? 1.0f : 0.f;
        out.push_back(Color(type, ins, st.rs ? 1.f : 0.f, 1.f));
    }
    return out;
}

double CrochetScene::get_thickness() const {
    double t = 0;
    if (session_ != nullptr)
        for (const fl::FabricMaterial& m : session_->model().materials) t = std::max<double>(t, m.thickness);
    return t;
}

Dictionary CrochetScene::get_metrics() const {
    Dictionary d;
    if (session_ == nullptr) return d;
    for (const fl::MetricEntry& e : session_->metrics().entries()) {
        const String key(e.name.c_str());
        switch (e.kind) {
            case fl::MetricEntry::Null: d[key] = Variant(); break;
            case fl::MetricEntry::Number: d[key] = e.number; break;
            case fl::MetricEntry::Bool: d[key] = e.number != 0; break;
            case fl::MetricEntry::Text: d[key] = String(e.text.c_str()); break;
            case fl::MetricEntry::Position: {
                Dictionary p;
                p["row"] = e.row;
                p["stitch"] = e.stitch;
                d[key] = p;
                break;
            }
        }
    }
    return d;
}

Transform3D CrochetScene::get_load_transform() const {
    Transform3D t;
    if (session_ != nullptr && session_->hasLoad()) {
        const fl::Vec3 c = session_->loadCenter();
        t.origin = Vector3(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z));
    }
    return t;
}

Dictionary CrochetScene::get_load() const {
    Dictionary d;
    if (session_ == nullptr || !session_->fit().known) return d;
    const fl::LoadSpec& l = session_->loadSpec();
    d["name"] = String(l.name.c_str());
    d["size"] = Vector3(static_cast<float>(l.width), static_cast<float>(l.height), static_cast<float>(l.thickness));
    d["corner_radius"] = l.cornerRadius;
    d["mass"] = l.mass;
    d["fits"] = session_->fit().fits;
    d["tight"] = session_->fit().tight;
    d["message"] = String(session_->fit().message.c_str());
    d["inserted"] = session_->hasLoad();
    return d;
}

Dictionary CrochetScene::get_peg() const {
    Dictionary d;
    if (session_ == nullptr || !session_->hasPeg()) return d;
    const fl::SceneOptions defaults;
    // the peg is a capsule along x at the height the scene put it; read it back through the solver's collider is not possible, so the
    // scene's own numbers are reported: radius and length are the options, the centre is the apex of the strap minus the gap
    d["radius"] = defaults.pegRadius;
    d["length"] = defaults.pegLength;
    d["center"] = Vector3(0, static_cast<float>(session_->scene()->pegCenterY()), 0);
    return d;
}

void CrochetScene::_bind_methods() {
    ClassDB::bind_method(D_METHOD("build", "pattern", "scene", "load"), &CrochetScene::build);
    ClassDB::bind_method(D_METHOD("relax_step", "n"), &CrochetScene::relax_step);
    ClassDB::bind_method(D_METHOD("step", "n"), &CrochetScene::step);
    ClassDB::bind_method(D_METHOD("phase"), &CrochetScene::phase);
    ClassDB::bind_method(D_METHOD("is_done"), &CrochetScene::is_done);
    ClassDB::bind_method(D_METHOD("is_settled"), &CrochetScene::is_settled);
    ClassDB::bind_method(D_METHOD("get_phase_frames"), &CrochetScene::get_phase_frames);
    ClassDB::bind_method(D_METHOD("get_positions"), &CrochetScene::get_positions);
    ClassDB::bind_method(D_METHOD("get_indices"), &CrochetScene::get_indices);
    ClassDB::bind_method(D_METHOD("get_normals"), &CrochetScene::get_normals);
    ClassDB::bind_method(D_METHOD("get_stitch_of_vertex"), &CrochetScene::get_stitch_of_vertex);
    ClassDB::bind_method(D_METHOD("get_vertex_uv"), &CrochetScene::get_vertex_uv);
    ClassDB::bind_method(D_METHOD("get_vertex_colors"), &CrochetScene::get_vertex_colors);
    ClassDB::bind_method(D_METHOD("get_thickness"), &CrochetScene::get_thickness);
    ClassDB::bind_method(D_METHOD("get_metrics"), &CrochetScene::get_metrics);
    ClassDB::bind_method(D_METHOD("get_load_transform"), &CrochetScene::get_load_transform);
    ClassDB::bind_method(D_METHOD("get_load"), &CrochetScene::get_load);
    ClassDB::bind_method(D_METHOD("get_peg"), &CrochetScene::get_peg);
    ClassDB::bind_method(D_METHOD("get_error"), &CrochetScene::get_error);
}

}  // namespace godot
