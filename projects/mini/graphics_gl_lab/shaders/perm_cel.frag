#version 450 core
// static permutation — Cel shading 전용.

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDir;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    float d = max(dot(N, L), 0.0);
    float band = d > 0.7 ? 1.0 : (d > 0.3 ? 0.6 : 0.25);
    FragColor = vec4(vColor * band, 1.0);
}
