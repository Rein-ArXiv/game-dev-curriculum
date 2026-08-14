#version 450 core
// SSBO 광원 배열. std430 layout, 가변 길이 지원.

struct Light {
    vec4 posRadius;
    vec4 colorIntensity;
};

layout(std430, binding = 0) readonly buffer LightStorage {
    Light lights[];
};

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform int uLightCount;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 result = 0.05 * vColor;
    for (int i = 0; i < uLightCount; ++i) {
        vec3 L = lights[i].posRadius.xyz - vWorldPos;
        float d = length(L);
        L /= max(d, 0.0001);
        float att = 1.0 / (1.0 + 0.01 * d + 0.001 * d * d);
        float diff = max(dot(N, L), 0.0);
        vec3 col = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;
        result += vColor * col * diff * att;
    }
    FragColor = vec4(result, 1.0);
}
