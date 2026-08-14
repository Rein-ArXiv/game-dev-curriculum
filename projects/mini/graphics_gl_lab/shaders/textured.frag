#version 450 core
// 디퓨즈 텍스처 + Lambert. Step 6 의 토글이 활성 시 사용.

in vec3 vNormalWS;
in vec3 vWorldPos;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDir;
uniform sampler2D uDiffuse;

// Scene 의 회전 + 격자 위치를 활용해 UV 생성 — 외부 정점 속성 없이.
void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);
    // 간단한 triplanar — abs(normal) 가중치로 세 축 텍스처 샘플.
    vec3 absN = abs(N);
    absN /= (absN.x + absN.y + absN.z);
    vec2 uvX = vWorldPos.zy * 0.1;
    vec2 uvY = vWorldPos.xz * 0.1;
    vec2 uvZ = vWorldPos.xy * 0.1;
    vec3 tex = absN.x * texture(uDiffuse, uvX).rgb
             + absN.y * texture(uDiffuse, uvY).rgb
             + absN.z * texture(uDiffuse, uvZ).rgb;
    vec3 albedo = mix(vColor, tex, 0.7);
    FragColor = vec4(albedo * (0.15 + diff), 1.0);
}
