#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(std140, set = 0, binding = 0) uniform ObjectUniforms {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 tint;
} object;

layout(location = 0) out vec3 vertexColor;

void main() {
    gl_Position = object.projection * object.view * object.model * vec4(inPosition, 1.0);
    vertexColor = inColor;
}
