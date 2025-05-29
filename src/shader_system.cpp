#include "shader_system.hpp"

#include "id.hpp"
#include "logger.hpp"
#include <cstring>
#include <d3dcompiler.h>

// Just testing something for branch prediction
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

bool shader::system_initialize(ShaderSystemState *state) {
    // Invalidate all modules
    for (int i = 0; i < MAX_SHADER_MODULES; ++i) {
        ShaderModule *module = &state->shader_modules[i];
        module->id = id::invalid();
        module->vs_bytecode_ptr = nullptr;
    }

    // Invalidate all pipelines
    for (int i = 0; i < MAX_SHADER_PIPELINES; ++i) {
        ShaderPipeline *pipeline = &state->shader_pipelines[i];
        pipeline->id = id::invalid();

        for (int j = 0; j < SHADER_STAGE_COUNT; ++j) {
            pipeline->stage[j] = id::invalid();
        }
    }

    return true;
}

ShaderId shader::create_module_from_file(ShaderSystemState *state, ID3D11Device *device, const wchar_t *path, ShaderStage stage, const char *entry_point) {
    // Linear search for an available module slot
    ShaderModule *module = nullptr;
    for (uint8_t i = 0; i < MAX_SHADER_MODULES; ++i) {
        if (id::is_invalid(state->shader_modules[i].id)) {
            module = &state->shader_modules[i];
            module->id.id = i;
            break;
        }
    }

    // When we couldn't find a module slot
    if (!module) {
        LOG("%s: Max shader modules reached, adjust max count.", __func__);
        return id::invalid();
    }

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = E_FAIL;
    Microsoft::WRL::ComPtr<ID3DBlob> error_blob_ptr;
    Microsoft::WRL::ComPtr<ID3DBlob> shader_blob_ptr;

    // Targets for different shader stages
    static const char *shader_target[SHADER_STAGE_COUNT] = {"vs_5_0", "ps_5_0", "cs_5_0"};

    // Compile the file from file using d3dcompiler
    hr = D3DCompileFromFile(
        path,
        NULL,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry_point,
        shader_target[stage],
        compileFlags,
        0,
        shader_blob_ptr.GetAddressOf(),
        error_blob_ptr.GetAddressOf());

    if (FAILED(hr)) {
        if (error_blob_ptr) {
            LOG("%s: Shader module failed to compile from file: %ls. Error: %s", __func__, path, (char *)error_blob_ptr->GetBufferPointer());
        }
        return id::invalid();
    }

    switch (stage) {
        case SHADER_STAGE_VS: {
            hr = device->CreateVertexShader(
                shader_blob_ptr->GetBufferPointer(),
                shader_blob_ptr->GetBufferSize(),
                nullptr,
                module->vs.GetAddressOf());
            
            // Copy the bytecode -- we'll need it for input layout
            module->vs_bytecode_ptr = shader_blob_ptr;
        } break;

        case SHADER_STAGE_PS: {
            hr = device->CreatePixelShader(
                shader_blob_ptr->GetBufferPointer(),
                shader_blob_ptr->GetBufferSize(),
                nullptr,
                module->ps.GetAddressOf());
        } break;

        case SHADER_STAGE_CS: {
            hr = device->CreateComputeShader(
                shader_blob_ptr->GetBufferPointer(),
                shader_blob_ptr->GetBufferSize(),
                nullptr,
                module->cs.GetAddressOf());
        } break;

        default:
            LOG("%s: Unknown shader stage", __func__);
            return id::invalid();
    }

    // Check if the shader was successfully created or not
    if (FAILED(hr)) {
        LOG("%s: Shader creation failed for file: %ls", __func__, path);
        return id::invalid();
    }

    module->stage = stage;
    return module->id;
}

ShaderId shader::create_module_from_bytecode(ShaderSystemState *state, ID3D11Device *device, ShaderStage stage, const void *bytecode, size_t bytecode_size) {
    // Linear search for an available module slot
    ShaderModule *module = nullptr;
    for (uint8_t i = 0; i < MAX_SHADER_MODULES; ++i) {
        if (id::is_invalid(state->shader_modules[i].id)) {
            module = &state->shader_modules[i];
            module->id.id = i;
            break;
        }
    }

    // When we couldn't find a module slot
    if (!module) {
        LOG("%s: Max shader modules reached, adjust max count.", __func__);
        return id::invalid();
    }

    HRESULT hr = E_FAIL;

    switch (stage) {
        case SHADER_STAGE_VS: {
            hr = device->CreateVertexShader(
                bytecode,
                bytecode_size,
                nullptr,
                module->vs.GetAddressOf());

            LOG("TODO: Copy the bytecode!");
        } break;

        case SHADER_STAGE_PS: {
            hr = device->CreatePixelShader(
                bytecode, 
                bytecode_size,
                nullptr,
                module->ps.GetAddressOf());
        } break;

        case SHADER_STAGE_CS: {
            hr = device->CreateComputeShader(
                bytecode, 
                bytecode_size,
                nullptr,
                module->cs.GetAddressOf());
        } break;

        default:
            LOG("%s: Unknown shader stage", __func__);
            return id::invalid();
    }

    // Check if the shader was successfully created or not
    if (FAILED(hr)) {
        LOG("%s: Shader creation failed", __func__);
        return id::invalid();
    }

    module->stage = stage;
    return module->id;
    
}

PipelineId shader::create_pipeline(ShaderSystemState *state, ID3D11Device *device, ShaderId *shader_modules, uint8_t shader_module_count, const D3D11_INPUT_ELEMENT_DESC *input_desc, uint16_t input_count) {
    // Linear search for an available pipeline slot
    ShaderPipeline *pipeline = nullptr;
    for (uint8_t i = 0; i < MAX_SHADER_PIPELINES; ++i) {
        if (id::is_invalid(state->shader_pipelines[i].id)) {
            pipeline = &state->shader_pipelines[i];
            pipeline->id.id = i;
            break;
        }
    }

    // When we couldn't find a pipeline slot
    if (!pipeline) {
        LOG("%s: Max shader pipelines reached, adjust max count.", __func__);
        return id::invalid();
    }

    // Want to keep track of vertex shader module being in
    // the mix, because only then do I want to create input element
    // if it's passsed in.
    ID3DBlob *vs_bytecode = nullptr;

    // Should I inspect the shader modules' validity here?
    // Anyway, this is to get them into the array
    for (int i = 0; i < shader_module_count; ++i) {
        ShaderModule *sm = &state->shader_modules[shader_modules[i].id];
        if (id::is_stale(sm->id, shader_modules[i])) {
            LOG("%s: One of the shader modules' id is stale", __func__);
            id::invalidate(&pipeline->id);
            return id::invalid();
        }

        // Store the id to the shader module
        pipeline->stage[sm->stage] = sm->id;

        // Check if the module is a vertex module
        if (sm->stage == SHADER_STAGE_VS) {
            vs_bytecode = sm->vs_bytecode_ptr.Get();
        }
    }

    if (vs_bytecode && input_desc && input_count > 0) {
        HRESULT hr = device->CreateInputLayout(
            input_desc,
            input_count,
            vs_bytecode->GetBufferPointer(),
            vs_bytecode->GetBufferSize(),
            pipeline->input_layout_ptr.GetAddressOf());

        if (FAILED(hr)) {
            LOG("%s: Couldn't create an input layout for the vertex shader in the pipeline", __func__);
            id::invalidate(&pipeline->id);
            return id::invalid();
        }
    }

    return pipeline->id;
}

void shader::bind_pipeline(ShaderSystemState *state, ID3D11DeviceContext *context, ShaderPipeline *pipeline) {
    // Vertex Shader
    if (ShaderModule *vs_mod = get_module(state, pipeline->stage[SHADER_STAGE_VS])) {
        context->VSSetShader(vs_mod->vs.Get(), nullptr, 0);
    } else {
        context->VSSetShader(nullptr, nullptr, 0);
    }

    // Pixel Shader
    if (ShaderModule *ps_mod = get_module(state, pipeline->stage[SHADER_STAGE_PS])) {
        context->PSSetShader(ps_mod->ps.Get(), nullptr, 0);
    } else {
        context->PSSetShader(nullptr, nullptr, 0);
    }

    // Compute Shader
    if (ShaderModule *cs_mod = get_module(state, pipeline->stage[SHADER_STAGE_CS])) {
        context->CSSetShader(cs_mod->cs.Get(), nullptr, 0);
    } else {
        context->CSSetShader(nullptr, nullptr, 0);
    }

    // Input Layout
    context->IASetInputLayout(pipeline->input_layout_ptr.Get());
}

ShaderModule *shader::get_module(ShaderSystemState *state, ShaderId shader_id) {
    if (id::is_invalid(shader_id)) {
        return nullptr;
    }

    if (LIKELY(id::is_fresh(state->shader_modules[shader_id.id].id, shader_id))) {
        return &state->shader_modules[shader_id.id];
    }
    return nullptr;
}

ShaderPipeline *shader::get_pipeline(ShaderSystemState *state, PipelineId pipeline_id) {
    if (id::is_invalid(pipeline_id)) {
        return nullptr;
    }

    if (LIKELY(id::is_fresh(state->shader_pipelines[pipeline_id.id].id, pipeline_id))) {
        return &state->shader_pipelines[pipeline_id.id];
    }
    return nullptr;
}
