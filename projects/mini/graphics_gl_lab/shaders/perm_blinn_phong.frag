#version 450 core
// static permutation — Blinn-Phong 전용.

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uCameraPos;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    FragColor = vec4(vColor * (0.15 + diff) + vec3(spec * 0.5), 1.0);
}
