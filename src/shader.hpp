#pragma once

#include <d3d11.h>
#include <wrl/client.h>

struct Shader {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> pVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pPixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> pInputLayout;
};

namespace shader {

bool create(const wchar_t *vertexFile, const wchar_t *pixelFile, ID3D11Device *device, const D3D11_INPUT_ELEMENT_DESC *input_layout, UINT input_layout_count, Shader *out_shader);
void bind(Shader *shader, ID3D11DeviceContext *context);

} // namespace shader
