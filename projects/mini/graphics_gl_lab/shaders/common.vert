#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 aInstanceModel;
layout(location = 6) in vec4 aInstanceColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uColor;
uniform int  uUseInstancing;

out vec3 vNormalWS;
out vec3 vWorldPos;
out vec3 vColor;

void main() {
    mat4 model = (uUseInstancing == 1) ? aInstanceModel : uModel;
    vec4 wp    = model * vec4(aPos, 1.0);
    gl_Position = uProj * uView * wp;
    vNormalWS = mat3(model) * aNormal;
    vWorldPos = wp.xyz;
    vColor    = (uUseInstancing == 1) ? aInstanceColor.rgb : uColor;
}
