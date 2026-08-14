#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

// Per-frame 데이터 — viewProj/조명/카메라. set 0, binding 0 의 UBO.
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 lightDir;     // xyz = 방향
    vec4 cameraPos;    // xyz = 위치
} frame;

// Per-instance 데이터 — Instanced 모드에서 gl_InstanceIndex 로 읽는 SSBO.
struct Instance {
    mat4 model;
    vec4 color;
};
layout(set = 0, binding = 1) readonly buffer Instances {
    Instance instances[];
};

// Per-draw 데이터 — Per-Draw 모드에서 오브젝트마다 push 한다.
layout(push_constant) uniform PC {
    mat4 model;
    vec4 color;
    uint mode;         // 0 = Per-Draw, 1 = Instanced
} pc;

layout(location = 0) out vec3 vNormalWS;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vColor;

void main() {
    mat4 model;
    vec3 col;
    if (pc.mode == 1u) {
        model = instances[gl_InstanceIndex].model;
        col   = instances[gl_InstanceIndex].color.rgb;
    } else {
        model = pc.model;
        col   = pc.color.rgb;
    }

    vec4 wp = model * vec4(aPos, 1.0);
    gl_Position = frame.viewProj * wp;
    vNormalWS = mat3(model) * aNormal;
    vWorldPos = wp.xyz;
    vColor    = col;
}
