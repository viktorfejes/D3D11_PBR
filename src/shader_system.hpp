#pragma once

#include "id.hpp"

#include <WRL/client.h>
#include <d3d11.h>

enum ShaderStage {
    SHADER_STAGE_VS,
    SHADER_STAGE_PS,
    SHADER_STAGE_CS,
    SHADER_STAGE_COUNT
};

using ShaderId = Id;
using PipelineId = Id;

struct ShaderModule {
    ShaderId id;
    ShaderStage stage;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> cs;

    // Bytecode blob for the vertex shader in case it's needed for input layout
    Microsoft::WRL::ComPtr<ID3DBlob> vs_bytecode_ptr;
};

struct ShaderPipeline {
    PipelineId id;
    Id stage[SHADER_STAGE_COUNT];
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_ptr;
};

#define MAX_SHADER_MODULES 64
#define MAX_SHADER_PIPELINES 32

struct ShaderSystemState {
    ShaderModule shader_modules[MAX_SHADER_MODULES];
    ShaderPipeline shader_pipelines[MAX_SHADER_PIPELINES];
};

namespace shader {

bool system_initialize(ShaderSystemState *state);

ShaderId create_module_from_file(ShaderSystemState *state, ID3D11Device *device, const wchar_t *path, ShaderStage stage, const char *entry_point);
ShaderId create_module_from_bytecode(ShaderSystemState *state, ID3D11Device *device, ShaderStage stage, const void *bytecode, size_t bytecode_size);
PipelineId create_pipeline(ShaderSystemState *state, ID3D11Device *device, ShaderId *shader_modules, uint8_t shader_module_count, const D3D11_INPUT_ELEMENT_DESC *input_desc, uint16_t input_count);

void bind_pipeline(ShaderSystemState *state, ID3D11DeviceContext *context, ShaderPipeline *pipeline);

ShaderModule *get_module(ShaderSystemState *state, ShaderId shader_id);
ShaderPipeline *get_pipeline(ShaderSystemState *state, PipelineId pipeline_id);

} // namespace shader
