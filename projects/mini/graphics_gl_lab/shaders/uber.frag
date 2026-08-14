#version 450 core
// uber shader — runtime branch on uShadingModel.
// 모든 코드 경로가 컴파일된 단일 프로그램. uniform 값에 따라 분기.

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uCameraPos;
uniform int  uShadingModel;    // 0=lambert, 1=blinn-phong, 2=cell

vec3 ShadeLambert(vec3 N, vec3 L, vec3 albedo) {
    float diff = max(dot(N, L), 0.0);
    return albedo * (0.15 + diff);
}

vec3 ShadeBlinnPhong(vec3 N, vec3 L, vec3 V, vec3 albedo) {
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 64.0);
    return albedo * (0.15 + diff) + vec3(spec * 0.5);
}

vec3 ShadeCel(vec3 N, vec3 L, vec3 albedo) {
    float d = max(dot(N, L), 0.0);
    float band = d > 0.7 ? 1.0 : (d > 0.3 ? 0.6 : 0.25);
    return albedo * band;
}

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 result;
    if (uShadingModel == 0) {
        result = ShadeLambert(N, L, vColor);
    } else if (uShadingModel == 1) {
        result = ShadeBlinnPhong(N, L, V, vColor);
    } else {
        result = ShadeCel(N, L, vColor);
    }
    FragColor = vec4(result, 1.0);
}
