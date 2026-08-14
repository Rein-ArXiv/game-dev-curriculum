#version 450
// 정점 버퍼 없이 gl_VertexIndex 3개로 화면을 덮는 디펜스 표시 삼각형.
layout(location = 0) out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = p;                                   // 0..1(화면 안), 끝은 오버드로
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
