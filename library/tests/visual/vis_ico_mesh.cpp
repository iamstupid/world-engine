#include <cstdio>
#include <cmath>
#include "mesh/icosahedral_geodesic.hpp"

// Export IcoMesh as OBJ for visual inspection in MeshLab/Blender
void export_obj(const char* filename, int N) {
    IcoMesh mesh(N);
    const auto& positions = mesh.positions();
    const auto& faces = mesh.faces();

    FILE* f = fopen(filename, "w");
    if (!f) { fprintf(stderr, "Cannot open %s\n", filename); return; }

    fprintf(f, "# IcoMesh OBJ export (N=%d, cells=%d, faces=%d)\n",
            N, mesh.num_cells(), (int)faces.size());

    // Vertices
    for (const auto& p : positions) {
        fprintf(f, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
    }

    // Faces (OBJ is 1-indexed)
    for (const auto& tri : faces) {
        fprintf(f, "f %d %d %d\n", tri[0] + 1, tri[1] + 1, tri[2] + 1);
    }

    fclose(f);
    printf("Exported %s: %d vertices, %d faces\n", filename, mesh.num_cells(), (int)faces.size());
}

int main() {
    export_obj("ico_N1.obj", 1);
    export_obj("ico_N4.obj", 4);
    export_obj("ico_N8.obj", 8);
    export_obj("ico_N20.obj", 20);
    printf("Done. Open OBJ files in MeshLab/Blender to inspect.\n");
    return 0;
}
