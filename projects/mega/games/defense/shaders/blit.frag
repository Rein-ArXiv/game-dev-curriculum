#version 450
// 디펜스 프레임버퍼 텍스처를 그대로 샘플링해 출력.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D uTex;
void main() {
    outColor = texture(uTex, vUV);
}
