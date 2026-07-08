#pragma once

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

private:
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
