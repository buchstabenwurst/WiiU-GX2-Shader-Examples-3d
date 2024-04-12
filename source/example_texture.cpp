#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "common.h"
#include "navigation.h"
#include "example_texture.h"


constexpr const char* s_textureVertexShader = R"(
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

// GLSL WiiU dosn't currently support uniform registers
// in_MVPMAtrix = ProjectionMatrix*ViewMatrix*modelMatrix
layout(location = 2) in vec4 in_MVPMAtrix0;
layout(location = 3) in vec4 in_MVPMAtrix1;
layout(location = 4) in vec4 in_MVPMAtrix2;
layout(location = 5) in vec4 in_MVPMAtrix3;


layout(location = 0) out vec2 TexCoord;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = mat4(in_MVPMAtrix0, in_MVPMAtrix1, in_MVPMAtrix2, in_MVPMAtrix3) * vec4(aPos, 1.0);
}
)";

constexpr const char* s_texturePixelShader = R"(
#version 450
#extension GL_ARB_shading_language_420pack: enable

layout(location = 0) in vec2 TexCoord;
layout(binding = 0) uniform uf_data
{
    float uf_time;
};

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D frogTexture;

void main()
{
    FragColor = texture(frogTexture, TexCoord);
    FragColor.x = mod(uf_time.x, 1.0f);
}
)";

#include "TGATexture.h"
#include "example_texture.h"

ExampleTexture::ExampleTexture() {
    launchTime = OSGetTime();

    // create shader group
    std::string errorLog(1024, '\0');
    GX2VertexShader *vertexShader = GLSL_CompileVertexShader(s_textureVertexShader, errorLog.data(), (int) errorLog.size(), GLSL_COMPILER_FLAG_NONE);
    if (!vertexShader) {
        WHBLogPrintf("Vertex shader compilation failed for texture example: %s", errorLog.data());
        return;
    }
    GX2PixelShader *pixelShader = GLSL_CompilePixelShader(s_texturePixelShader, errorLog.data(), (int) errorLog.size(), GLSL_COMPILER_FLAG_NONE);
    if (!pixelShader) {
        WHBLogPrintf("Pixel shader compilation failed for texture example: %s", errorLog.data());
        return;
    }

    memset(&s_shaderGroup, 0, sizeof(WHBGfxShaderGroup));
    s_shaderGroup.vertexShader = vertexShader;
    s_shaderGroup.pixelShader = pixelShader;
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, s_shaderGroup.vertexShader->program, s_shaderGroup.vertexShader->size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, s_shaderGroup.pixelShader->program, s_shaderGroup.pixelShader->size);

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);

    WHBGfxInitShaderAttribute(&s_shaderGroup, "aPos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
    WHBGfxInitShaderAttribute(&s_shaderGroup, "aTexCoord", 1, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(&s_shaderGroup, "in_MVPMAtrix0", 2, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
    WHBGfxInitShaderAttribute(&s_shaderGroup, "in_MVPMAtrix1", 3, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
    WHBGfxInitShaderAttribute(&s_shaderGroup, "in_MVPMAtrix2", 4, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);
    WHBGfxInitShaderAttribute(&s_shaderGroup, "in_MVPMAtrix3", 5, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32);

    WHBGfxInitFetchShader(&s_shaderGroup);

    // upload vertex position
    s_positionBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
    s_positionBuffer.elemSize = 3 * sizeof(float);
    s_positionBuffer.elemCount = 4;
    GX2RCreateBuffer(&s_positionBuffer);
    void *posUploadBuffer = GX2RLockBufferEx(&s_positionBuffer, GX2R_RESOURCE_BIND_NONE);
    memcpy(posUploadBuffer, s_positionData, s_positionBuffer.elemSize * s_positionBuffer.elemCount);
    GX2RUnlockBufferEx(&s_positionBuffer, GX2R_RESOURCE_BIND_NONE);

    // upload texture coords
    s_texCoordBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
    s_texCoordBuffer.elemSize = 2 * sizeof(float);
    s_texCoordBuffer.elemCount = 4;
    GX2RCreateBuffer(&s_texCoordBuffer);
    void *coordsUploadBuffer = GX2RLockBufferEx(&s_texCoordBuffer, GX2R_RESOURCE_BIND_NONE);
    memcpy(coordsUploadBuffer, s_texCoords, s_texCoordBuffer.elemSize * s_texCoordBuffer.elemCount);
    GX2RUnlockBufferEx(&s_texCoordBuffer, GX2R_RESOURCE_BIND_NONE);

    // upload projection matrix
    for(int i=0; i<4; i++){
        s_mvpMatrixBuffer[i].flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
        s_mvpMatrixBuffer[i].elemSize = 4 * sizeof(float);
        s_mvpMatrixBuffer[i].elemCount = 4;
        GX2RCreateBuffer(&s_mvpMatrixBuffer[i]);
    }

    // upload texture
    std::ifstream fs("romfs:/texture.tga", std::ios::in | std::ios::binary);
    if (!fs.is_open())
        return;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    s_texture = TGA_LoadTexture((uint8_t *) data.data(), data.size());
    if (s_texture == nullptr)
        return;

    GX2Sampler sampler;
    GX2InitSampler(&sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
}

const glm::mat4 projectionMatrix = glm::perspective(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
glm::vec3 camPos(-3.0f, 1.5f, 0.0f);
float yaw = 0.0f, pitch = -30.0f;
glm::vec3 position {0.0f, 0.0f, 0.0f};
glm::vec3 rotation {0.0f, 0.0f, 0.0f};

void ExampleTexture::Draw() {
    // render texture
    WHBGfxBeginRender();

    yaw += vpadBuffer[0].rightStick.x * 2.0f;
    pitch += vpadBuffer[0].rightStick.y * 2.0f;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front(cos(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(pitch)), sin(glm::radians(yaw)) * cos(glm::radians(pitch)));
    glm::vec3 camFront = glm::normalize(front);
    glm::vec3 camRight = glm::normalize(glm::cross(camFront, worldUp));
    glm::vec3 camUp = glm::normalize(glm::cross(camRight, camFront));

    camPos += camFront * vpadBuffer[0].leftStick.y * 0.5f;
    camPos += camRight * vpadBuffer[0].leftStick.x * 0.5f;

    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), position);
    modelMatrix = glm::rotate(modelMatrix, rotation.x += 0.0, glm::vec3(1, 0, 0));
    modelMatrix = glm::rotate(modelMatrix, rotation.y += 0.01, glm::vec3(0, 1, 0));
    modelMatrix = glm::rotate(modelMatrix, rotation.z += 0.0, glm::vec3(0, 0, 1));
    glm::mat4 viewMatrix = glm::lookAt(camPos, camPos + camFront, camUp);
    glm::mat4 mvpMatrix = projectionMatrix * viewMatrix * modelMatrix;

    for(int i=0; i<4; i++){
    float* projectionBuffer = (float *)GX2RLockBufferEx(&s_mvpMatrixBuffer[i], GX2R_RESOURCE_BIND_NONE);
    for(int x=0; x<4; x++){
        projectionBuffer[x] = mvpMatrix[i][x];
        projectionBuffer[x+4] = mvpMatrix[i][x];
        projectionBuffer[x+8] = mvpMatrix[i][x];
        projectionBuffer[x+12] = mvpMatrix[i][x];
    }
        
    GX2RUnlockBufferEx(&s_mvpMatrixBuffer[i], GX2R_RESOURCE_BIND_NONE);
    }
    

    WHBGfxBeginRenderTV();
    WHBGfxClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    GX2SetFetchShader(&s_shaderGroup.fetchShader);
    GX2SetVertexShader(s_shaderGroup.vertexShader);
    GX2SetPixelShader(s_shaderGroup.pixelShader);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);

    // update time uniform in uniform block
    float time = (float)OSTicksToMilliseconds((OSGetTime() - launchTime));
    time *= 0.001f;
    s_timePSUniformBlock[0] = _swapF32(time);
    GX2SetPixelUniformBlock(0, sizeof(s_timePSUniformBlock), (void*)s_timePSUniformBlock);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK, s_timePSUniformBlock, sizeof(s_timePSUniformBlock));

    GX2RSetAttributeBuffer(&s_positionBuffer, 0, s_positionBuffer.elemSize, 0);
    GX2RSetAttributeBuffer(&s_texCoordBuffer, 1, s_texCoordBuffer.elemSize, 0);
    for(int i=0; i<4; i++){
        GX2RSetAttributeBuffer(&s_mvpMatrixBuffer[i], i+2, s_mvpMatrixBuffer[i].elemSize, 0);
    }
    GX2SetPixelTexture(s_texture, s_shaderGroup.pixelShader->samplerVars[0].location);
    GX2SetPixelSampler(&s_sampler, s_shaderGroup.pixelShader->samplerVars[0].location);

    GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
    WHBGfxFinishRenderTV();

    WHBGfxBeginRenderDRC();
    WHBGfxClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    GX2SetFetchShader(&s_shaderGroup.fetchShader);
    GX2SetVertexShader(s_shaderGroup.vertexShader);
    GX2SetPixelShader(s_shaderGroup.pixelShader);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);

    GX2SetPixelUniformBlock(0, sizeof(s_timePSUniformBlock), (void*)s_timePSUniformBlock);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK, s_timePSUniformBlock, sizeof(s_timePSUniformBlock));

    GX2RSetAttributeBuffer(&s_positionBuffer, 0, s_positionBuffer.elemSize, 0);
    GX2RSetAttributeBuffer(&s_texCoordBuffer, 1, s_texCoordBuffer.elemSize, 0);
    for(int i=0; i<4; i++){
        GX2RSetAttributeBuffer(&s_mvpMatrixBuffer[i], i+2, s_mvpMatrixBuffer[i].elemSize, 0);
    }
    GX2SetPixelTexture(s_texture, s_shaderGroup.pixelShader->samplerVars[0].location);
    GX2SetPixelSampler(&s_sampler, s_shaderGroup.pixelShader->samplerVars[0].location);

    GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
    WHBGfxFinishRenderDRC();

    WHBGfxFinishRender();
}

ExampleTexture::~ExampleTexture() {
    GX2RDestroyBufferEx(&s_positionBuffer, GX2R_RESOURCE_BIND_NONE);
    GX2RDestroyBufferEx(&s_texCoordBuffer, GX2R_RESOURCE_BIND_NONE);
    for(int i=0; i<4; i++){
        GX2RCreateBuffer(&s_mvpMatrixBuffer[i]);
    }
    TGA_UnloadTexture(s_texture);
}