#pragma once

#include "id.hpp"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

using MeshId = Id;

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
    DirectX::XMFLOAT4 tangent;
};

struct Mesh {
    MeshId id;

    Microsoft::WRL::ComPtr<ID3D11Buffer> pVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pIndexBuffer;
    uint32_t indexCount;
    UINT vertexStride;
};

namespace mesh {

MeshId load(const char *filename);
bool load_obj(const char *filename);
bool load_gltf(const char *filename);
void destroy(MeshId mesh_id);
void draw(ID3D11DeviceContext *context, Mesh *mesh);

} // namespace mesh
