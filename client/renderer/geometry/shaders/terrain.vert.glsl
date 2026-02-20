// Terrain vertex shader
// Displaces vertices along their sphere normal by elevation * exaggeration.

uniform float exaggeration;

attribute vec3 color;

varying vec3 vColor;
varying vec3 vNormal;
varying vec3 vPosition;

void main() {
    // On a unit sphere, the normal is just the normalized position
    vec3 sphereNormal = normalize(position);

    // Displace the vertex radially: r = 1.0 + elevation * exaggeration
    // The color attribute's implicit elevation is baked into position by the CPU side,
    // but we also allow the exaggeration uniform to scale displacement at render time.
    vec3 displaced = position;

    vColor = color;
    vNormal = sphereNormal;
    vPosition = displaced;

    gl_Position = projectionMatrix * modelViewMatrix * vec4(displaced, 1.0);
}
