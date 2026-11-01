#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 vertexColor;
layout(location = 3) in vec2 uv;
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
layout(set = 1, binding = 1) uniform sampler2D albedoSampler;
layout(set = 1, binding = 2) uniform sampler2D specularSampler;
layout(set = 1, binding = 3) uniform sampler2D emissiveSampler;
layout(push_constant) uniform TextureEffect {
    uvec4 control;
    vec4 parameters;
} effect;

vec2 rotateUV(vec2 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}
vec3 sampleAlbedo(vec2 coordinates, float footprint) {
    // Explicit gradients avoid false mip transitions across reflected sector seams.
    return textureGrad(albedoSampler, coordinates, dFdx(uv) * footprint, dFdy(uv) * footprint).rgb;
}
vec3 sampleAlbedo(vec2 coordinates) {
    return sampleAlbedo(coordinates, 1.0);
}
vec3 texturedAlbedo() {
    vec3 original = sampleAlbedo(uv);
    if (effect.control.y == 0u || effect.parameters.y <= 0.0) return original;
    float time = effect.parameters.x;
    float strength = effect.parameters.y;
    vec2 p = (uv - 0.5) * effect.parameters.z;
    vec3 filtered = original;
    if (effect.control.x == 0u) {
        float radius = length(p) * (0.9 + 0.12 * sin(time * 0.7));
        float angle = atan(p.y, p.x) + time * 0.35;
        float sector = 6.28318530718 / float(max(effect.control.z, 2u));
        float folded = abs(mod(angle + sector, 2.0 * sector) - sector);
        vec2 q = radius * vec2(cos(folded), sin(folded));
        vec2 uvA = rotateUV(q, time * 0.17) + 0.5;
        vec2 uvB = rotateUV(q * 1.21, -time * 0.23 + 1.1) + vec2(0.37, 0.61);
        vec2 uvC = rotateUV(q.yx * 0.83, time * 0.11 - 0.7) + vec2(0.63, 0.42);
        float footprint = max(effect.parameters.z, 1.0);
        filtered = sampleAlbedo(uvA, footprint) * 0.50
                 + sampleAlbedo(uvB, footprint) * 0.30
                 + sampleAlbedo(uvC, footprint) * 0.20;
    } else if (effect.control.x == 1u) {
        vec2 shift = vec2(cos(time), sin(time * 0.83)) * (0.003 + 0.04 * strength);
        filtered = vec3(sampleAlbedo(uv + shift).r, sampleAlbedo(uv).g, sampleAlbedo(uv - shift).b);
    } else if (effect.control.x == 2u) {
        vec2 wave = vec2(sin(p.y * 17.0 + time), cos(p.x * 15.0 - time * 0.9)) * 0.045;
        filtered = sampleAlbedo(uv + wave) * 0.5
                 + sampleAlbedo(uv - wave + vec2(0.025 * sin(time), 0.03)) * 0.3
                 + sampleAlbedo(uv + wave.yx * 1.7 - vec2(0.02, 0.035 * cos(time))) * 0.2;
    } else if (effect.control.x == 3u) {
        vec2 texel = 1.0 / vec2(textureSize(albedoSampler, 0));
        float radius = (1.0 + 7.0 * strength) * (0.8 + 0.2 * sin(time));
        filtered = vec3(0.0);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                filtered += sampleAlbedo(uv + vec2(x, y) * texel * radius) / 9.0;
    } else if (effect.control.x == 4u) {
        vec3 sampled = sampleAlbedo(uv + vec2(0.01 * sin(time), 0.0));
        float value = dot(sampled, vec3(0.2126, 0.7152, 0.0722));
        filtered = vec3(step(0.45 + 0.18 * sin(time * 0.6), value));
    }
    vec3 modulation = 1.0 + effect.parameters.w * sin(vec3(0.0, 2.1, 4.2) + time * 0.8);
    return mix(original, filtered * modulation, strength);
}

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
vec3 blinnPhong(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, vec3 specularColor) {
    float diffuse = max(dot(N, L), 0.0);
    vec3 H = safeNormalize(L + V);
    float specular = diffuse > 0.0
        ? pow(max(dot(N, H), 0.0), max(object.specularShininess.w, 1.0)) : 0.0;
    return radiance * (albedo * diffuse + specularColor * specular);
}
void main() {
    // Markers and cone lines have a constant color, independent of all lighting.
    if (object.emission.w > 0.5) {
        outColor = vec4(object.emission.rgb, 1.0);
        return;
    }
    bool textured = object.textureParameters.x > 0.5;
    vec3 albedo = object.albedo.rgb * mix(vec3(1.0), vertexColor, object.albedo.w);
    vec3 specularColor = object.specularShininess.rgb;
    vec3 emissiveColor = vec3(0.0);
    if (textured) {
        albedo *= texturedAlbedo();
        specularColor *= texture(specularSampler, uv).rgb * object.textureParameters.z;
        emissiveColor = texture(emissiveSampler, uv).rgb
                      * object.textureParameters.y * object.textureParameters.w;
    }
    vec3 N = safeNormalize(worldNormal);
    vec3 V = safeNormalize(scene.cameraPosition.xyz - worldPosition);
    vec3 color = scene.ambient.rgb * scene.ambient.w * albedo;
    color += blinnPhong(-scene.directionalDirectionIntensity.xyz,
        scene.directionalColor.rgb * scene.directionalDirectionIntensity.w, N, V, albedo, specularColor);
    for (uint i = 0; i < scene.lightCounts.x; ++i) {
        PointLight light = points.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.0001);
        color += blinnPhong(safeNormalize(delta),
            light.colorIntensity.rgb * light.colorIntensity.w / distanceSquared, N, V, albedo, specularColor);
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
                            * cone / distanceSquared, N, V, albedo, specularColor);
    }
    outColor = vec4(color + emissiveColor, 1.0);
}
