#pragma once

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace topopt {

// Parsed problem specification (the input language, cf. docs/INPUT_LANGUAGE.md).
// v1 covers the structural subset; unknown/absent fields take defaults so the
// schema stays forward/backward compatible.
struct ProblemSpec {
    // meta
    std::string name = "problem";
    std::string dim = "3d";           // "3d" | "axi"
    int version = 1;

    // domain
    std::array<int, 3> grid = {1, 1, 1};
    std::array<double, 3> size_mm = {1, 1, 1};
    std::string geometry = "box";

    // material
    double E0 = 1.0, Emin = 1e-4, nu = 0.3, penal = 3.0;
    double k_solid = 1.0, k_fluid = 0.3, alpha_th = 1e-3, Tref = 0.0;
    double mu = 1.0, brinkman_max = 1e2, brinkman_q = 0.1;

    // physics
    std::vector<std::string> physics = {"elastic"};

    // boundary conditions (raw selectors, resolved by BCResolver)
    struct BCEntry {
        std::string face, edge, node, region;  // one selector is set
        std::string dof;                        // "x"|"y"|"z"|"all"|"T"
        double value = 0.0;                     // load / pressure / Q / T
    };
    std::vector<BCEntry> fixed, loads, pressure, thermal, flow;

    // filter
    double filter_radius_mm = 1.5;
    std::vector<double> heaviside_beta;   // empty = no projection
    double heaviside_eta = 0.5;

    // optimize
    std::string objective = "compliance";
    struct Constraint { std::string type; double max = 0.0; };
    std::vector<Constraint> constraints;
    std::string optimizer = "mma";
    int max_iter = 60;
    bool penal_continuation = false;

    // output
    std::string output_dir = "output";
    std::vector<std::string> formats = {"vti"};
    std::vector<std::string> fields = {"density"};
    double stl_iso = 0.5;
    std::string stl_method = "marching_cubes";

    // --- parsing ---
    static ProblemSpec fromJson(const nlohmann::json& j) {
        ProblemSpec s;
        if (j.contains("meta")) {
            const auto& m = j["meta"];
            s.name = m.value("name", s.name);
            s.dim = m.value("dim", s.dim);
            s.version = m.value("version", s.version);
        }
        if (j.contains("domain")) {
            const auto& d = j["domain"];
            if (d.contains("grid")) s.grid = d["grid"].get<std::array<int, 3>>();
            if (d.contains("size_mm"))
                s.size_mm = d["size_mm"].get<std::array<double, 3>>();
            s.geometry = d.value("geometry", s.geometry);
        }
        if (j.contains("material")) {
            const auto& m = j["material"];
            s.E0 = m.value("E0", s.E0);
            s.Emin = m.value("Emin", s.Emin);
            s.nu = m.value("nu", s.nu);
            s.penal = m.value("penal", s.penal);
            s.k_solid = m.value("k_solid", s.k_solid);
            s.k_fluid = m.value("k_fluid", s.k_fluid);
            s.alpha_th = m.value("alpha_th", s.alpha_th);
            s.Tref = m.value("Tref", s.Tref);
            s.mu = m.value("mu", s.mu);
            s.brinkman_max = m.value("brinkman_max", s.brinkman_max);
            s.brinkman_q = m.value("brinkman_q", s.brinkman_q);
        }
        if (j.contains("physics"))
            s.physics = j["physics"].get<std::vector<std::string>>();
        if (j.contains("bc")) {
            const auto& b = j["bc"];
            auto parseList = [](const nlohmann::json& arr) {
                std::vector<BCEntry> out;
                for (const auto& e : arr) {
                    BCEntry x;
                    x.face = e.value("face", "");
                    x.edge = e.value("edge", "");
                    x.node = e.value("node", "");
                    x.region = e.value("region", "");
                    x.dof = e.value("dof", "");
                    // value may be under "value","T","Q","inlet_velocity"...
                    if (e.contains("value")) x.value = e["value"].get<double>();
                    else if (e.contains("T")) x.value = e["T"].get<double>();
                    else if (e.contains("Q")) x.value = e["Q"].get<double>();
                    out.push_back(x);
                }
                return out;
            };
            if (b.contains("fixed")) s.fixed = parseList(b["fixed"]);
            if (b.contains("loads")) s.loads = parseList(b["loads"]);
            if (b.contains("pressure")) s.pressure = parseList(b["pressure"]);
            if (b.contains("thermal")) s.thermal = parseList(b["thermal"]);
            if (b.contains("flow")) s.flow = parseList(b["flow"]);
        }
        if (j.contains("filter")) {
            const auto& f = j["filter"];
            s.filter_radius_mm = f.value("radius_mm", s.filter_radius_mm);
            if (f.contains("heaviside")) {
                const auto& h = f["heaviside"];
                if (h.contains("beta"))
                    s.heaviside_beta = h["beta"].get<std::vector<double>>();
                s.heaviside_eta = h.value("eta", s.heaviside_eta);
            }
        }
        if (j.contains("optimize")) {
            const auto& o = j["optimize"];
            s.objective = o.value("objective", s.objective);
            s.optimizer = o.value("optimizer", s.optimizer);
            s.max_iter = o.value("max_iter", s.max_iter);
            s.penal_continuation = o.value("penal_continuation", s.penal_continuation);
            if (o.contains("constraints"))
                for (const auto& c : o["constraints"])
                    s.constraints.push_back({c.value("type", ""), c.value("max", 0.0)});
        }
        if (j.contains("output")) {
            const auto& o = j["output"];
            s.output_dir = o.value("dir", s.output_dir);
            if (o.contains("formats"))
                s.formats = o["formats"].get<std::vector<std::string>>();
            if (o.contains("fields"))
                s.fields = o["fields"].get<std::vector<std::string>>();
            s.stl_iso = o.value("stl_iso", s.stl_iso);
            s.stl_method = o.value("stl_method", s.stl_method);
        }
        return s;
    }

    static ProblemSpec fromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("ProblemSpec: cannot open " + path);
        nlohmann::json j;
        f >> j;
        return fromJson(j);
    }
};

} // namespace topopt
