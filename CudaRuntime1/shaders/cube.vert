#version 450

layout(set = 0, binding = 0) uniform CameraBlock {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PushBlock {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;

void main() {
    vec4 worldPosition = push.model * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPosition;

    fragUV = inUV;
    // Uniform scaling only, so the upper 3x3 is fine for normals.
    fragNormal = normalize(mat3(push.model) * inNormal);
}
