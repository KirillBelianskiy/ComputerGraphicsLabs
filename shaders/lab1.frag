#version 450

layout(location = 0) in vec3 vertexColor;

layout(std140, set = 0, binding = 0) uniform ObjectUniforms {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 tint;
} object;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vertexColor * object.tint.rgb, 1.0);
}
