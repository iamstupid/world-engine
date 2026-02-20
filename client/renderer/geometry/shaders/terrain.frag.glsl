// Terrain fragment shader
// Basic diffuse lighting with vertex colors.

varying vec3 vColor;
varying vec3 vNormal;
varying vec3 vPosition;

void main() {
    // Light direction (world-space, from upper right)
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));

    // Hemisphere ambient: blend sky (light blue) and ground (dark) based on normal.y
    vec3 skyColor = vec3(0.6, 0.7, 0.9);
    vec3 groundColor = vec3(0.1, 0.1, 0.15);
    float hemi = 0.5 + 0.5 * vNormal.y;
    vec3 ambient = mix(groundColor, skyColor, hemi) * 0.3;

    // Diffuse
    float diff = max(dot(vNormal, lightDir), 0.0);
    vec3 diffuse = vColor * diff * 0.7;

    vec3 finalColor = ambient + diffuse;

    gl_FragColor = vec4(finalColor, 1.0);
}
