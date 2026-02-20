#include <cstdio>
#include <cmath>
#include <algorithm>
#include "mesh/icosahedral_geodesic.hpp"
#include "noise/noise_generator.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Terrain colormap: elevation [-1,1] -> RGB
struct ColorStop { float val; float r, g, b; };
static const ColorStop TERRAIN[] = {
    {-1.00f, 0.00f, 0.00f, 0.13f},
    {-0.40f, 0.00f, 0.15f, 0.40f},
    {-0.05f, 0.00f, 0.30f, 0.70f},
    { 0.00f, 0.76f, 0.70f, 0.50f},
    { 0.05f, 0.13f, 0.55f, 0.13f},
    { 0.25f, 0.09f, 0.45f, 0.09f},
    { 0.45f, 0.55f, 0.35f, 0.17f},
    { 0.70f, 0.55f, 0.55f, 0.55f},
    { 0.90f, 0.85f, 0.85f, 0.85f},
    { 1.00f, 1.00f, 1.00f, 1.00f},
};
static const int NUM_STOPS = sizeof(TERRAIN) / sizeof(TERRAIN[0]);

void sample_colormap(float val, float& r, float& g, float& b) {
    val = std::clamp(val, -1.0f, 1.0f);
    for (int i = 1; i < NUM_STOPS; i++) {
        if (val <= TERRAIN[i].val) {
            float t = (val - TERRAIN[i-1].val) / (TERRAIN[i].val - TERRAIN[i-1].val);
            r = TERRAIN[i-1].r + t * (TERRAIN[i].r - TERRAIN[i-1].r);
            g = TERRAIN[i-1].g + t * (TERRAIN[i].g - TERRAIN[i-1].g);
            b = TERRAIN[i-1].b + t * (TERRAIN[i].b - TERRAIN[i-1].b);
            return;
        }
    }
    r = g = b = 1.0f;
}

// Export colored OBJ
void export_colored_obj(const char* filename, const IcoMesh& mesh, const Field<IcoMesh>& field) {
    const auto& positions = mesh.positions();
    const auto& faces = mesh.faces();

    FILE* f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "# Colored IcoMesh (N=%d)\n", mesh.N());

    for (int i = 0; i < mesh.num_cells(); i++) {
        float r, g, b;
        sample_colormap(field[i], r, g, b);
        fprintf(f, "v %.6f %.6f %.6f %.4f %.4f %.4f\n",
                positions[i].x, positions[i].y, positions[i].z, r, g, b);
    }

    for (const auto& tri : faces) {
        fprintf(f, "f %d %d %d\n", tri[0] + 1, tri[1] + 1, tri[2] + 1);
    }

    fclose(f);
    printf("Exported %s\n", filename);
}

// Export equirectangular PNG
void export_equirect_png(const char* filename, const IcoMesh& mesh, const Field<IcoMesh>& field,
                         int width, int height) {
    std::vector<uint8_t> pixels(width * height * 3, 0);

    const auto& positions = mesh.positions();
    std::vector<float> pixel_val(width * height, -999.0f);

    for (int i = 0; i < mesh.num_cells(); i++) {
        float lat = positions[i].latitude();
        float lon = positions[i].longitude();

        int px = (int)((lon + M_PI) / (2.0 * M_PI) * width) % width;
        int py = (int)((M_PI / 2.0 - lat) / M_PI * height);
        py = std::clamp(py, 0, height - 1);
        px = std::clamp(px, 0, width - 1);

        pixel_val[py * width + px] = field[i];
    }

    // Fill gaps with nearest neighbor (forward pass)
    for (int y = 0; y < height; y++) {
        float last = 0;
        for (int x = 0; x < width; x++) {
            if (pixel_val[y * width + x] > -998.0f) last = pixel_val[y * width + x];
            else pixel_val[y * width + x] = last;
        }
    }

    // Render
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r, g, b;
            sample_colormap(pixel_val[y * width + x], r, g, b);
            int idx = (y * width + x) * 3;
            pixels[idx]     = (uint8_t)(std::clamp(r, 0.0f, 1.0f) * 255);
            pixels[idx + 1] = (uint8_t)(std::clamp(g, 0.0f, 1.0f) * 255);
            pixels[idx + 2] = (uint8_t)(std::clamp(b, 0.0f, 1.0f) * 255);
        }
    }

    stbi_write_png(filename, width, height, 3, pixels.data(), width * 3);
    printf("Exported %s (%dx%d)\n", filename, width, height);
}

int main() {
    printf("Generating noise on IcoMesh(N=200)...\n");
    IcoMesh mesh(200);

    NoiseParams params;
    params.seed = 42;
    params.octaves = 6;
    params.frequency = 1.5f;
    params.warp_amplitude = 0.3f;
    params.ocean_fraction = 0.55f;

    Field<IcoMesh> field = generate_noise(mesh, params);

    printf("  min=%.4f max=%.4f mean=%.4f\n", field.min_val(), field.max_val(), field.mean_val());

    export_colored_obj("noise_ico_N200.obj", mesh, field);
    export_equirect_png("noise_equirect_N200.png", mesh, field, 1024, 512);

    printf("Done.\n");
    return 0;
}
