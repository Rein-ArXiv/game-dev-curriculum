#version 450

layout(location = 0) in vec3 vNormalWS;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vColor;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 lightDir;     // .xyz = 방향
    vec4 cameraPos;    // .xyz = 위치
} frame;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(frame.lightDir.xyz);
    vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    FragColor = vec4(vColor * (0.15 + diff) + 0.5 * spec, 1.0);
}
