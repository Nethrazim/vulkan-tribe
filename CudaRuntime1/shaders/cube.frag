#version 450

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

const vec3 kLightDirection = normalize(vec3(-0.4, -1.0, -0.6));
const float kAmbient = 0.25;

void main() {
    vec3 surface = texture(albedo, fragUV).rgb;

    float lambert = max(dot(normalize(fragNormal), -kLightDirection), 0.0);
    float lighting = kAmbient + (1.0 - kAmbient) * lambert;

    outColor = vec4(surface * lighting, 1.0);
}
