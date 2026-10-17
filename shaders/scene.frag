#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 vertexColor;
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
} object;
struct PointLight { vec4 position; vec4 colorIntensity; };
struct SpotLight {
    vec4 position;
    vec4 directionInnerCos;
    vec4 colorIntensity;
    vec4 outerCos;
};
layout(std430, set = 0, binding = 1) readonly buffer Points { PointLight lights[]; } points;
layout(std430, set = 0, binding = 2) readonly buffer Spots { SpotLight lights[]; } spots;
layout(location = 0) out vec4 outColor;

vec3 safeNormalize(vec3 v) { return v * inversesqrt(max(dot(v, v), 1e-10)); }
vec3 blinnPhong(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo) {
    float diffuse = max(dot(N, L), 0.0);
    vec3 H = safeNormalize(L + V);
    float specular = diffuse > 0.0
        ? pow(max(dot(N, H), 0.0), max(object.specularShininess.w, 1.0)) : 0.0;
    return radiance * (albedo * diffuse + object.specularShininess.rgb * specular);
}
void main() {
    // Markers and cone lines have a constant color, independent of all lighting.
    if (object.emission.w > 0.5) {
        outColor = vec4(object.emission.rgb, 1.0);
        return;
    }
    vec3 albedo = object.albedo.rgb * mix(vec3(1.0), vertexColor, object.albedo.w);
    vec3 N = safeNormalize(worldNormal);
    vec3 V = safeNormalize(scene.cameraPosition.xyz - worldPosition);
    vec3 color = scene.ambient.rgb * scene.ambient.w * albedo;
    color += blinnPhong(-scene.directionalDirectionIntensity.xyz,
        scene.directionalColor.rgb * scene.directionalDirectionIntensity.w, N, V, albedo);
    for (uint i = 0; i < scene.lightCounts.x; ++i) {
        PointLight light = points.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.0001);
        color += blinnPhong(safeNormalize(delta),
            light.colorIntensity.rgb * light.colorIntensity.w / distanceSquared, N, V, albedo);
    }
    for (uint i = 0; i < scene.lightCounts.y; ++i) {
        SpotLight light = spots.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.0001);
        vec3 L = safeNormalize(delta);
        float cosine = dot(-L, light.directionInnerCos.xyz);
        float inner = light.directionInnerCos.w;
        float outer = light.outerCos.x;
        // Equal angles give a hard edge; smoothstep requires distinct endpoints.
        float cone = inner - outer > 1e-6 ? smoothstep(outer, inner, cosine)
                                          : step(outer, cosine);
        color += blinnPhong(L, light.colorIntensity.rgb * light.colorIntensity.w
                            * cone / distanceSquared, N, V, albedo);
    }
    outColor = vec4(color, 1.0);
}
