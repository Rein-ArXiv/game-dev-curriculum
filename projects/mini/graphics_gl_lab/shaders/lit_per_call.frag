#version 450 core
// per-call uniform 광원 배열.

const int MAX_LIGHTS = 64;

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightPos[MAX_LIGHTS];
uniform vec3 uLightColor[MAX_LIGHTS];
uniform int  uLightCount;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 result = 0.05 * vColor;
    for (int i = 0; i < uLightCount; ++i) {
        vec3 L = uLightPos[i] - vWorldPos;
        float d = length(L);
        L /= max(d, 0.0001);
        float att = 1.0 / (1.0 + 0.01 * d + 0.001 * d * d);
        float diff = max(dot(N, L), 0.0);
        result += vColor * uLightColor[i] * diff * att;
    }
    FragColor = vec4(result, 1.0);
}
