#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirectionIntensity;
    vec4 directionalColor;
    uvec4 lightCounts;
} scene;
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specularShininess;
    vec4 emission;
    vec4 textureParameters;
} object;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 vertexColor;
layout(location = 3) out vec2 uv;
void main() {
    vec4 position = object.model * vec4(inPosition, 1.0);
    worldPosition = position.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    vertexColor = inColor;
    uv = inUV;
    gl_Position = scene.viewProjection * position;
}
