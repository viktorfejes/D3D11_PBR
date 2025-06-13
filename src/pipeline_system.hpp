#pragma once

#include "id.hpp"
#include "shader_system.hpp"

using PipelineId = Id;
using RasterizerStateId = Id;
using DepthStencilStateId = Id;
using BlendStateId = Id;

#define MAX_PIPELINES 32
#define MAX_RASTER_STATES 32
#define MAX_DEPTH_STENCIL_STATES 32
#define MAX_BLEND_STATES 32

struct RasterizerState {
    RasterizerStateId id;
    uint64_t hash;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> state_ptr;
};

struct DepthStencilState {
    DepthStencilStateId id;
    uint64_t hash;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state_ptr;
};

struct BlendState {
    BlendStateId id;
    uint64_t hash;
    Microsoft::WRL::ComPtr<ID3D11BlendState> state_ptr;
};

enum PipelineType {
    PIPELINE_TYPE_GRAPHICS,
    PIPELINE_TYPE_COMPUTE,
};

struct Pipeline {
    PipelineId id;
    PipelineType type;

    ShaderId shader_stages[SHADER_STAGE_COUNT];
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_ptr;

    RasterizerStateId rasterizer;
    DepthStencilStateId depth_stencil;
    BlendStateId blend;

    D3D11_PRIMITIVE_TOPOLOGY topology;
    D3D11_VIEWPORT viewport;
};

struct PipelineDesc {
    PipelineType type;

    struct {
        ShaderModule *modules_ptr;
        uint8_t count;
    } shader_stages;

    RasterizerStateId rasterizer;
    DepthStencilStateId depth_stencil;
    BlendStateId blend;

    D3D11_PRIMITIVE_TOPOLOGY topology;
    D3D11_VIEWPORT viewport;

    struct {
        const D3D11_INPUT_ELEMENT_DESC *desc;
        uint16_t count;
    } input_layout;
};

struct PipelineSystemState {
    ShaderSystemState *shader_system;

    RasterizerState rasterizer_states[MAX_RASTER_STATES];
    DepthStencilState depth_stencil_states[MAX_DEPTH_STENCIL_STATES];
    BlendState blend_states[MAX_BLEND_STATES];

    Pipeline pipelines[MAX_PIPELINES];
};

namespace pipeline {

RasterizerStateId create_rasterizer_state(PipelineSystemState *state, ID3D11Device *device, const D3D11_RASTERIZER_DESC *desc);
DepthStencilStateId create_depth_stencil_state(PipelineSystemState *state, ID3D11Device *device, const D3D11_DEPTH_STENCIL_DESC *desc);
BlendStateId create_blend_state(PipelineSystemState *state, ID3D11Device *device, const D3D11_BLEND_DESC *desc);

bool create_pipeline(PipelineSystemState *state, ID3D11Device *device, const PipelineDesc *desc, PipelineId *out_pipeline);
Pipeline *get_pipeline(PipelineSystemState *state, PipelineId pipeline_id);
void bind_pipeline(PipelineSystemState *state, ID3D11DeviceContext *context, Pipeline *pipeline);

} // namespace pipeline
