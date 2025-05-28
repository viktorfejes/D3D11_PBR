#include "shader.hpp"

#include "logger.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgiformat.h>

bool shader::create(const wchar_t *vertexFile, const wchar_t *pixelFile, ID3D11Device *device, const D3D11_INPUT_ELEMENT_DESC *input_layout, UINT input_layout_count, Shader *out_shader) {
    if (!device) {
        LOG("Shader error: passed in pointer to Device was null");
        return false;
    }

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = E_FAIL;
    Microsoft::WRL::ComPtr<ID3DBlob> pErrorBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> pVertexBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> pPixelBlob;

    // Compile Vertex shader from file
    hr = D3DCompileFromFile(
        vertexFile,
        NULL,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        "vs_5_0",
        compileFlags,
        0,
        pVertexBlob.GetAddressOf(),
        pErrorBlob.GetAddressOf());

    if (FAILED(hr)) {
        if (pErrorBlob) {
            LOG("ShaderProgram: D3DCompileFromFile failed for Vertex Shader (%ls). Error: %s",
                vertexFile, (char *)pErrorBlob->GetBufferPointer());
        }
        return false;
    }

    // Creating the Vertex Shader
    hr = device->CreateVertexShader(
        pVertexBlob->GetBufferPointer(),
        pVertexBlob->GetBufferSize(),
        nullptr,
        out_shader->pVertexShader.GetAddressOf());

    if (FAILED(hr)) {
        LOG("ShaderProgram: CreateVertexShader failed for Vertex Shader (%ls)", vertexFile);
        return false;
    }

    // Unsure about whether I should reset this here.
    // In C I usually don't have to reset the error blob.
    pErrorBlob.Reset();

    // Compile Pixel Shader from file
    hr = D3DCompileFromFile(
        pixelFile,
        NULL,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        "ps_5_0",
        compileFlags,
        0,
        pPixelBlob.GetAddressOf(),
        pErrorBlob.GetAddressOf());

    if (FAILED(hr)) {
        LOG("ShaderProgram: D3DCompileFromFile failed for Pixel Shader (%ls). Error: %s",
            pixelFile, (char *)pErrorBlob->GetBufferPointer());
        return false;
    }

    // Creating the Pixel Shader
    hr = device->CreatePixelShader(
        pPixelBlob->GetBufferPointer(),
        pPixelBlob->GetBufferSize(),
        nullptr,
        out_shader->pPixelShader.GetAddressOf());

    if (FAILED(hr)) {
        LOG("ShaderProgram: CreatePixelShader failed for Pixel Shader (%ls)", pixelFile);
        return false;
    }

    // Create Input Layout for the vertex shader, if provided
    if (input_layout && input_layout_count > 0) {
        hr = device->CreateInputLayout(
            input_layout,
            input_layout_count,
            pVertexBlob->GetBufferPointer(),
            pVertexBlob->GetBufferSize(),
            out_shader->pInputLayout.GetAddressOf());

        if (FAILED(hr)) {
            LOG("ShaderProgram: CreateInputLayout failed");
            return false;
        }
    }

    return true;
}

void shader::bind(Shader *shader, ID3D11DeviceContext *context) {
    context->IASetInputLayout(shader->pInputLayout.Get());
    context->VSSetShader(shader->pVertexShader.Get(), nullptr, 0);
    context->PSSetShader(shader->pPixelShader.Get(), nullptr, 0);
}
