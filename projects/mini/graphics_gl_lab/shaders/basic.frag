#version 450 core

in vec3 vNormalWS;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDir;

void main() {
    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = 0.15 * vColor;
    vec3 diffuse = diff * vColor;
    FragColor = vec4(ambient + diffuse, 1.0);
}
