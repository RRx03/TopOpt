#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "io/ProblemSpec.hpp"

namespace topopt {

// Resolves ProblemSpec boundary-condition selectors (faces / edges / nodes) into
// concrete fixed-DOF masks and load vectors on a Grid3D. This is the exact
// translation the 3D modeler will drive: a user picks a face and a constraint
// type, and this maps it to DOFs/loads for the solver (3 DOF/node: x,y,z).
class BCResolver {
public:
    // Enumerate the nodes selected by a face/edge/node selector string.
    // Faces: "x-","x+","y-","y+","z-","z+". Edge: two comma-separated faces
    // (their intersection). Node "corner:x+,y-,z-": three faces (a corner).
    static std::vector<int> selectNodes(const Grid3D& g, const ProblemSpec::BCEntry& e) {
        if (!e.region.empty()) return selectRegion(g, e.region);
        std::vector<std::string> faces;
        if (!e.face.empty()) faces = {e.face};
        else if (!e.edge.empty()) faces = split(e.edge);
        else if (!e.node.empty()) {
            std::string n = e.node;
            const auto pos = n.find(':');
            if (pos != std::string::npos) n = n.substr(pos + 1);  // drop "corner:"
            faces = split(n);
        }
        std::vector<int> nodes;
        for (int k = 0; k <= g.nelz(); ++k)
            for (int j = 0; j <= g.nely(); ++j)
                for (int i = 0; i <= g.nelx(); ++i)
                    if (onAllFaces(g, i, j, k, faces))
                        nodes.push_back(g.nodeId(i, j, k));
        return nodes;
    }

    // Fixed-DOF mask (nDof), 1 = constrained. dof: "x"|"y"|"z"|"all".
    static std::vector<std::uint8_t> fixedMask(const Grid3D& g, const ProblemSpec& s) {
        std::vector<std::uint8_t> mask(static_cast<size_t>(g.nDof()), 0);
        for (const auto& e : s.fixed)
            for (int n : selectNodes(g, e))
                for (int c : dofComponents(e.dof))
                    mask[static_cast<size_t>(3 * n + c)] = 1;
        return mask;
    }

    // Load vector (nDof). Each entry's "value" is the TOTAL force over the
    // selected nodes, distributed equally (consistent with the MBB convention).
    static Eigen::VectorXd loadVector(const Grid3D& g, const ProblemSpec& s) {
        Eigen::VectorXd F = Eigen::VectorXd::Zero(g.nDof());
        for (const auto& e : s.loads) {
            const auto nodes = selectNodes(g, e);
            if (nodes.empty()) continue;
            const double per = e.value / static_cast<double>(nodes.size());
            for (int n : nodes)
                for (int c : dofComponents(e.dof)) F(3 * n + c) += per;
        }
        return F;
    }

    // --- Thermal boundary conditions (two-block thermo-elastic path) ----------
    // The discrete two-block adjoint condenses Dirichlet temperature nodes to a
    // homogeneous value (T = 0), so only the node SET is resolved here; a
    // non-zero "value" on a thermal Dirichlet entry is not representable and is
    // ignored (documented). Selector "dof":"T" marks a Dirichlet entry; any
    // other thermal entry is treated as a heat source Q.
    static std::vector<int> thermalFixedNodes(const Grid3D& g, const ProblemSpec& s) {
        std::vector<int> nodes;
        for (const auto& e : s.thermal)
            if (e.dof == "T")
                for (int n : selectNodes(g, e)) nodes.push_back(n);
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        return nodes;
    }

    // Nodal heat-source vector (length nNodes). Each source entry's "value" is
    // the TOTAL heat over its selected nodes, distributed equally (same
    // convention as loadVector). Entries with dof == "T" are Dirichlet, skipped.
    static Eigen::VectorXd thermalSource(const Grid3D& g, const ProblemSpec& s) {
        Eigen::VectorXd Q = Eigen::VectorXd::Zero(g.nNodes());
        for (const auto& e : s.thermal) {
            if (e.dof == "T") continue;
            const auto nodes = selectNodes(g, e);
            if (nodes.empty()) continue;
            const double per = e.value / static_cast<double>(nodes.size());
            for (int n : nodes) Q(n) += per;
        }
        return Q;
    }

    // --- Stokes boundary conditions (incompressible flow, 4 DOF/node) ----------
    // DOF layout dof(n,c) = 4n+c: c in {0,1,2} = velocity (u_x,u_y,u_z), c=3 =
    // pressure. Resolves bc.flow selectors into the fixed-DOF list the triple
    // adjoint constrains (homogeneous, i.e. u=0 / p=0):
    //   dof "wall"/"noslip" -> no-slip, fix all three velocity components;
    //   dof "slip"          -> fix the velocity component normal to the face;
    //   dof "pressure"/"p"  -> pin the pressure datum on the selected node(s);
    //   a "drive" entry (body force) carries no dof and is skipped here.
    static int stokesDof(int n, int c) { return 4 * n + c; }

    static std::vector<int> stokesFixedDofs(const Grid3D& g, const ProblemSpec& s) {
        std::vector<int> dofs;
        for (const auto& e : s.flow) {
            if (e.dof.empty()) continue;  // drive / unlabelled entries
            const auto nodes = selectNodes(g, e);
            if (e.dof == "wall" || e.dof == "noslip") {
                for (int n : nodes)
                    for (int c = 0; c < 3; ++c) dofs.push_back(stokesDof(n, c));
            } else if (e.dof == "slip") {
                const int c = faceNormal(e.face);
                if (c >= 0)
                    for (int n : nodes) dofs.push_back(stokesDof(n, c));
            } else if (e.dof == "pressure" || e.dof == "p" || e.dof == "pdatum") {
                for (int n : nodes) dofs.push_back(stokesDof(n, 3));
            }
        }
        std::sort(dofs.begin(), dofs.end());
        dofs.erase(std::unique(dofs.begin(), dofs.end()), dofs.end());
        return dofs;
    }

private:
    // Region selector "axis:lo:hi" (node indices, inclusive), axis in {x,y,z}.
    // Enables an interior volumetric source (e.g. a mid-height heat band) that
    // face/edge/node selectors cannot reach.
    static std::vector<int> selectRegion(const Grid3D& g, const std::string& r) {
        std::vector<int> nodes;
        const auto tok = split(r, ':');
        if (tok.size() != 3 || tok[0].empty()) return nodes;
        const char axis = tok[0][0];
        const int lo = std::stoi(tok[1]), hi = std::stoi(tok[2]);
        for (int k = 0; k <= g.nelz(); ++k)
            for (int j = 0; j <= g.nely(); ++j)
                for (int i = 0; i <= g.nelx(); ++i) {
                    const int idx = (axis == 'x') ? i : (axis == 'y') ? j : k;
                    if (idx >= lo && idx <= hi) nodes.push_back(g.nodeId(i, j, k));
                }
        return nodes;
    }

    static int faceNormal(const std::string& f) {
        if (f == "x-" || f == "x+") return 0;
        if (f == "y-" || f == "y+") return 1;
        if (f == "z-" || f == "z+") return 2;
        return -1;
    }

    static std::vector<std::string> split(const std::string& s, char sep = ',') {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
            else if (c != ' ') cur += c;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static bool onFace(const Grid3D& g, int i, int j, int k, const std::string& f) {
        if (f == "x-") return i == 0;
        if (f == "x+") return i == g.nelx();
        if (f == "y-") return j == 0;
        if (f == "y+") return j == g.nely();
        if (f == "z-") return k == 0;
        if (f == "z+") return k == g.nelz();
        return false;
    }
    static bool onAllFaces(const Grid3D& g, int i, int j, int k,
                           const std::vector<std::string>& faces) {
        for (const auto& f : faces)
            if (!onFace(g, i, j, k, f)) return false;
        return !faces.empty();
    }
    static std::vector<int> dofComponents(const std::string& dof) {
        if (dof == "x") return {0};
        if (dof == "y") return {1};
        if (dof == "z") return {2};
        if (dof == "all") return {0, 1, 2};
        return {};  // "T" etc. handled by the thermal path, not here
    }
};

} // namespace topopt
