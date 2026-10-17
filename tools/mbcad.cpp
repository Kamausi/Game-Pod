// mbcad CLI - validate and export Mud Buddies CAD parts.
//   mbcad check                         build every part under cad/parts, report errors
//   mbcad info   <part> [k=v ...]       triangle count + bounds
//   mbcad export <part> [k=v ...] [--obj file] [--scad file]
//   mbcad export-all <outdir>           OBJ + SCAD for every part (used by CI artifacts)
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "cad/cad.h"

namespace fs = std::filesystem;
using namespace mb;

static int usage() {
    std::puts("usage: mbcad check | info <part> [k=v..] | export <part> [k=v..] [--obj f] [--scad f] | export-all <dir>");
    return 2;
}

static std::vector<std::string> allParts() {
    std::vector<std::string> out;
    for (auto& e : fs::recursive_directory_iterator(cadRoot()))
        if (e.path().extension() == ".mbcad") out.push_back(fs::relative(e.path(), cadRoot()).generic_string());
    std::sort(out.begin(), out.end());
    return out;
}

int main(int argc, char** argv) {
    if (const char* r = std::getenv("MBCAD_ROOT")) cadSetRoot(r);
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "check") {
        int bad = 0;
        for (auto& p : allParts()) {
            CadResult r = cadBuild(p);
            if (!r.error.empty()) { std::printf("FAIL %-36s %s\n", p.c_str(), r.error.c_str()); bad++; }
            else std::printf("ok   %-36s %6zu tris\n", p.c_str(), r.mesh.idx.size() / 3);
        }
        return bad ? 1 : 0;
    }
    if (cmd == "export-all" && argc >= 3) {
        fs::path out = argv[2];
        int bad = 0;
        for (auto& p : allParts()) {
            CadResult r = cadBuild(p);
            if (!r.error.empty()) { std::printf("FAIL %s: %s\n", p.c_str(), r.error.c_str()); bad++; continue; }
            fs::path base = out / fs::path(p).replace_extension("");
            fs::create_directories(base.parent_path());
            std::ofstream(base.string() + ".obj") << cadToObj(r.mesh, fs::path(p).stem().string());
            std::ofstream(base.string() + ".scad") << r.scad;
            std::printf("exported %s\n", p.c_str());
        }
        return bad ? 1 : 0;
    }
    if ((cmd == "info" || cmd == "export") && argc >= 3) {
        std::string part = argv[2], obj, scad;
        CadParams ov;
        for (int i = 3; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--obj" && i + 1 < argc) obj = argv[++i];
            else if (a == "--scad" && i + 1 < argc) scad = argv[++i];
            else if (auto eq = a.find('='); eq != std::string::npos) ov[a.substr(0, eq)] = float(std::atof(a.c_str() + eq + 1));
        }
        CadResult r = cadBuild(part, ov);
        if (!r.error.empty()) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        Vec3 mn = r.mesh.boundsMin(), mx = r.mesh.boundsMax();
        std::printf("%s: %zu verts, %zu tris, bounds [%.3f %.3f %.3f] - [%.3f %.3f %.3f]\n", part.c_str(), r.mesh.verts.size(),
                    r.mesh.idx.size() / 3, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
        if (!obj.empty()) std::ofstream(obj) << cadToObj(r.mesh, fs::path(part).stem().string());
        if (!scad.empty()) std::ofstream(scad) << r.scad;
        return 0;
    }
    return usage();
}
