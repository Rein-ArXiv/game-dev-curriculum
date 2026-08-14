#version 450

layout(location = 0) in vec3 vNormalWS;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vColor;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 lightDir;
    vec4 cameraPos;
} frame;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(frame.lightDir.xyz);
    float diff = max(dot(N, L), 0.0);
    FragColor = vec4(vColor * (0.15 + diff), 1.0);
}
