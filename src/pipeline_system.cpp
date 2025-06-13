#include "pipeline_system.hpp"

#include "logger.hpp"
#include "math.hpp"
#include "shader_system.hpp"

// bool shader::system_initialize(ShaderSystemState *state) {
//     // Invalidate all modules
//     for (int i = 0; i < MAX_SHADER_MODULES; ++i) {
//         ShaderModule *module = &state->shader_modules[i];
//         module->id = id::invalid();
//         module->vs_bytecode_ptr = nullptr;
//     }
//
//     // Invalidate all pipelines
//     for (int i = 0; i < MAX_SHADER_PIPELINES; ++i) {
//         ShaderPipeline *pipeline = &state->shader_pipelines[i];
//         pipeline->id = id::invalid();
//
//         for (int j = 0; j < SHADER_STAGE_COUNT; ++j) {
//             pipeline->stage[j] = id::invalid();
//         }
//     }
//
//     return true;
// }

RasterizerStateId pipeline::create_rasterizer_state(PipelineSystemState *state, ID3D11Device *device, const D3D11_RASTERIZER_DESC *desc) {
    uint64_t desc_hash = hash_fnv1a_64(desc, sizeof(D3D11_RASTERIZER_DESC));

    // TODO: Potentially I could deduplicate this search part with
    // a common base that contains id and hash and that way I don't
    // have to write this ~200,000,000 times.

    // Linear search compare if we find the same hash in the state
    // in which case we don't recreate it.
    // At the same time we keep track of the first free slot in case
    // it is needed.
    uint16_t free_index = MAX_RASTER_STATES;
    for (uint16_t i = 0; i < MAX_RASTER_STATES; ++i) {
        RasterizerState *rstate = &state->rasterizer_states[i];
        if (id::is_valid(rstate->id) && rstate->hash == desc_hash) {
            // We found an already existing rasterizer state with the same description
            return rstate->id;
        }

        // If this is not found, we keep track of the first slot
        if (free_index == MAX_RASTER_STATES && id::is_invalid(rstate->id)) {
            free_index = i;
        }
    }

    // If we get here, we couldn't find a matching description
    // So we check if we found an empty slot
    if (free_index == MAX_RASTER_STATES) {
        LOG("%s: No more free slots for Rasterizer States", __func__);
        return id::invalid();
    }

    RasterizerState *rstate = &state->rasterizer_states[free_index];
    HRESULT hr = device->CreateRasterizerState(desc, rstate->state_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Failed to create Rasterizer State", __func__);
        return id::invalid();
    }

    rstate->id.id = free_index;
    rstate->hash = desc_hash;
    return rstate->id;
}

DepthStencilStateId pipeline::create_depth_stencil_state(PipelineSystemState *state, ID3D11Device *device, const D3D11_DEPTH_STENCIL_DESC *desc) {
    uint64_t desc_hash = hash_fnv1a_64(desc, sizeof(D3D11_DEPTH_STENCIL_DESC));

    // TODO: Potentially I could deduplicate this search part with
    // a common base that contains id and hash and that way I don't
    // have to write this ~200,000,000 times.

    uint16_t free_index = MAX_DEPTH_STENCIL_STATES;
    for (uint16_t i = 0; i < MAX_DEPTH_STENCIL_STATES; ++i) {
        DepthStencilState *dsstate = &state->depth_stencil_states[i];
        if (id::is_valid(dsstate->id) && dsstate->hash == desc_hash) {
            // We found an already existing rasterizer state with the same description
            return dsstate->id;
        }

        // If this is not found, we keep track of the first slot
        if (free_index == MAX_DEPTH_STENCIL_STATES && id::is_invalid(dsstate->id)) {
            free_index = i;
        }
    }

    // If we get here, we couldn't find a matching description
    // So we check if we found an empty slot
    if (free_index == MAX_DEPTH_STENCIL_STATES) {
        LOG("%s: No more free slots for Depth Stencil States", __func__);
        return id::invalid();
    }

    DepthStencilState *dsstate = &state->depth_stencil_states[free_index];
    HRESULT hr = device->CreateDepthStencilState(desc, dsstate->state_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Failed to create Depth Stencil State", __func__);
        return id::invalid();
    }

    dsstate->id.id = free_index;
    dsstate->hash = desc_hash;
    return dsstate->id;
}

BlendStateId pipeline::create_blend_state(PipelineSystemState *state, ID3D11Device *device, const D3D11_BLEND_DESC *desc) {
    uint64_t desc_hash = hash_fnv1a_64(desc, sizeof(D3D11_BLEND_DESC));

    // TODO: Potentially I could deduplicate this search part with
    // a common base that contains id and hash and that way I don't
    // have to write this ~200,000,000 times.

    uint16_t free_index = MAX_BLEND_STATES;
    for (uint16_t i = 0; i < MAX_BLEND_STATES; ++i) {
        BlendState *bstate = &state->blend_states[i];
        if (id::is_valid(bstate->id) && bstate->hash == desc_hash) {
            // We found an already existing rasterizer state with the same description
            return bstate->id;
        }

        // If this is not found, we keep track of the first slot
        if (free_index == MAX_BLEND_STATES && id::is_invalid(bstate->id)) {
            free_index = i;
        }
    }

    // If we get here, we couldn't find a matching description
    // So we check if we found an empty slot
    if (free_index == MAX_BLEND_STATES) {
        LOG("%s: No more free slots for Blend States", __func__);
        return id::invalid();
    }

    BlendState *bstate = &state->blend_states[free_index];
    HRESULT hr = device->CreateBlendState(desc, bstate->state_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Failed to create Blend State", __func__);
        return id::invalid();
    }

    bstate->id.id = free_index;
    bstate->hash = desc_hash;
    return bstate->id;
}

bool pipeline::create_pipeline(PipelineSystemState *state, ID3D11Device *device, const PipelineDesc *desc, PipelineId *out_pipeline) {
    // Linear search for an available pipeline slot
    Pipeline *pipeline = nullptr;
    for (uint8_t i = 0; i < MAX_PIPELINES; ++i) {
        if (id::is_invalid(state->pipelines[i].id)) {
            pipeline = &state->pipelines[i];
            pipeline->id.id = i;
            break;
        }
    }

    // When we couldn't find a pipeline slot
    if (!pipeline) {
        LOG("%s: Max render pipelines reached, adjust max count.", __func__);
        return false;
    }

    // Want to keep track of vertex shader module being in
    // the mix, because only then do I want to create input element
    // if it's passsed in.
    ID3DBlob *vs_bytecode = nullptr;

    // Thinking the shader modules would be passed in as pointers here
    // so users need to fetch them before.
    for (int i = 0; i < desc->shader_stages.count; ++i) {
        ShaderModule *sm = &desc->shader_stages.modules_ptr[i];
        if (!sm) continue;

        // Store the id to the shader module
        pipeline->shader_stages[sm->stage] = sm->id;

        // Check if the module is a vertex module
        if (sm->stage == SHADER_STAGE_VS) {
            vs_bytecode = sm->vs_bytecode_ptr.Get();
        }
    }

    if (vs_bytecode && desc->input_layout.desc && desc->input_layout.count > 0) {
        HRESULT hr = device->CreateInputLayout(
            desc->input_layout.desc,
            desc->input_layout.count,
            vs_bytecode->GetBufferPointer(),
            vs_bytecode->GetBufferSize(),
            pipeline->input_layout_ptr.GetAddressOf());

        if (FAILED(hr)) {
            LOG("%s: Couldn't create an input layout for the vertex shader in the pipeline", __func__);
            id::invalidate(&pipeline->id);
            return false;
        }
    }

    pipeline->type = desc->type;
    pipeline->rasterizer = desc->rasterizer;
    pipeline->depth_stencil = desc->depth_stencil;
    pipeline->blend = desc->blend;
    pipeline->topology = desc->topology;
    pipeline->viewport = desc->viewport;

    // Send out the pipeline's id as out value
    *out_pipeline = pipeline->id;

    return true;
}

Pipeline *pipeline::get_pipeline(PipelineSystemState *state, PipelineId pipeline_id) {
    if (id::is_invalid(pipeline_id)) {
        return nullptr;
    }

    if (id::is_fresh(state->pipelines[pipeline_id.id].id, pipeline_id)) {
        return &state->pipelines[pipeline_id.id];
    }
    return nullptr;
}

void pipeline::bind_pipeline(PipelineSystemState *state, ID3D11DeviceContext *context, Pipeline *pipeline) {
    // Shader binding
    for (int i = 0; i < SHADER_STAGE_COUNT; ++i) {
        ShaderModule *sm = shader::get_module(state->shader_system, pipeline->shader_stages[i]);
        switch (i) {
            case SHADER_STAGE_VS:
                context->VSSetShader(sm ? sm->vs.Get() : nullptr, nullptr, 0);
                break;
            case SHADER_STAGE_PS:
                context->PSSetShader(sm ? sm->ps.Get() : nullptr, nullptr, 0);
                break;
            case SHADER_STAGE_CS:
                context->CSSetShader(sm ? sm->cs.Get() : nullptr, nullptr, 0);
                break;
        }
    }

    // Input layout -- for graphics pipelines
    if (pipeline->type == PIPELINE_TYPE_GRAPHICS) {
        context->IASetInputLayout(pipeline->input_layout_ptr.Get());
        context->IASetPrimitiveTopology(pipeline->topology);
        context->RSSetViewports(1, &pipeline->viewport);
    }

}
