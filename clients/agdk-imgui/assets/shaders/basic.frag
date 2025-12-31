#version 300 es
precision mediump float;

in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec3 vFragPos;

uniform sampler2D uTexture;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform float uAmbientStrength;

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    vec4 baseColor = texColor * vColor;
    
    // Ambient
    vec3 ambient = uAmbientStrength * uLightColor;
    
    // Diffuse
    vec3 norm = normalize(vNormal);
    float diff = max(dot(norm, uLightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    
    // Specular (simplified)
    vec3 viewDir = normalize(uViewPos - vFragPos);
    vec3 reflectDir = reflect(-uLightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = vec3(0.5) * spec;
    
    vec3 result = (ambient + diffuse + specular) * baseColor.rgb;
    fragColor = vec4(result, baseColor.a);
}