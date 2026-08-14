#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

// per-instance — Instanced / MDI 모드에서만 사용.
layout(location = 2) in mat4 aInstanceModel;   // 2,3,4,5 (4 vec4)
layout(location = 6) in vec4 aInstanceColor;

uniform mat4 uModel;       // Individual 모드용
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uColor;        // Individual 모드용
uniform int  uUseInstancing;

out vec3 vNormalWS;
out vec3 vColor;

void main() {
    mat4 model = (uUseInstancing == 1) ? aInstanceModel : uModel;
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position   = uProj * uView * worldPos;
    vNormalWS     = mat3(model) * aNormal;
    vColor        = (uUseInstancing == 1) ? aInstanceColor.rgb : uColor;
}
