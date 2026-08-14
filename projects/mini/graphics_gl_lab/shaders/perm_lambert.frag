#version 450 core
// static permutation — Lambert 전용. uniform branch 없음.

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDir;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);
    FragColor = vec4(vColor * (0.15 + diff), 1.0);
}
