#include "mesh.hpp"

#include "application.hpp"
#include "logger.hpp"
#include "renderer.hpp"
#include <DirectXMath.h>
#include <cassert>
#include <cstring>
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <vector>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.hpp>

// Helpers for TinyObj
struct IndexTriplet {
    int v, vn, vt;
    bool operator==(IndexTriplet const &o) const {
        return v == o.v && vn == o.vn && vt == o.vt;
    }
};
struct TripletHasher {
    size_t operator()(IndexTriplet const &t) const noexcept {
        return ((size_t)t.v * 73856093) ^ ((size_t)t.vn * 19349663) ^ ((size_t)t.vt * 83492791);
    }
};

MeshId mesh::load(const char *filename) {
    Renderer *renderer = application::get_renderer();

    // Get the device through the application from the renderer
    // This way it doesn't need to be passed in and for these
    // loaders it's more ergonomic not to have to do that IMHO.
    ID3D11Device *device = renderer->pDevice.Get();

    // Check if we can find an empty slot for our mesh
    // by linear search (which for this size is probably the best)
    Mesh *m = nullptr;
    Mesh *meshes = renderer->meshes;
    for (uint8_t i = 0; i < MAX_MESHES; ++i) {
        if (id::is_invalid(meshes[i].id)) {
            m = &meshes[i];
            m->id.id = i;
            break;
        }
    }

    if (m == nullptr) {
        LOG("mesh::load: Max meshes reached, adjust max mesh count.");
        return id::invalid();
    }

    // Parse glTF model
    cgltf_options opts = {};
    cgltf_data *gltf_data = NULL;
    cgltf_result res = cgltf_parse_file(&opts, filename, &gltf_data);
    if (res != cgltf_result_success) {
        LOG("mesh::load: Failed to load and parse glTF file: %s", filename);
        return id::invalid();
    }

    // Throw in an extra validation provided by the lib
    if (cgltf_validate(gltf_data) != cgltf_result_success) {
        LOG("mesh::load: glTF model failed validation");
        cgltf_free(gltf_data);
        return id::invalid();
    }

    // For now, pick the very first primitive
    cgltf_primitive *primitive = &gltf_data->meshes->primitives[0];

    const cgltf_accessor *pos = cgltf_find_accessor(primitive, cgltf_attribute_type_position, 0);
    const cgltf_accessor *nor = cgltf_find_accessor(primitive, cgltf_attribute_type_normal, 0);
    const cgltf_accessor *uvs = cgltf_find_accessor(primitive, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor *tan = cgltf_find_accessor(primitive, cgltf_attribute_type_tangent, 0);

    // Accessor separately for the indices
    const cgltf_accessor *ind = primitive->indices;
    size_t index_count = ind->count;

    // Make sure all the required attribute types are present
    // NOTE: Normals could be calculated by smoothing or similar though...
    if (!pos || !nor || !uvs) {
        LOG("mesh::load: glTF model is missing one of the required attribute types (position, normal, uv)");
        cgltf_free(gltf_data);
        return id::invalid();
    }

    // Now that we made sure we have everything we need, let's load in the binary data
    if (cgltf_load_buffers(&opts, gltf_data, filename) != cgltf_result_success) {
        LOG("mesh::load: Failed to load buffer data for glTF file: %s", filename);
        cgltf_free(gltf_data);
        return id::invalid();
    }

    // Use vectors so I don't have to free them at the end
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.resize(pos->count);
    indices.resize(index_count);

    // Loop through all the unique vertices and interleave them into our own array
    for (cgltf_size i = 0; i < pos->count; ++i) {
        float vp[3];
        float vn[3];
        float vt[2];
        float vtan[4];

        cgltf_accessor_read_float(pos, i, vp, 3);
        cgltf_accessor_read_float(nor, i, vn, 3);
        cgltf_accessor_read_float(uvs, i, vt, 2);

        // NOTE: Negative here is to flip to LH from RH
        if (tan) {
            cgltf_accessor_read_float(tan, i, vtan, 4);
            vertices[i].tangent.x = vtan[0];
            vertices[i].tangent.y = vtan[1];
            vertices[i].tangent.z = -vtan[2];
            vertices[i].tangent.w = -vtan[3];
        }

        vertices[i].position.x = vp[0];
        vertices[i].position.y = vp[1];
        vertices[i].position.z = -vp[2];

        vertices[i].normal.x = vn[0];
        vertices[i].normal.y = vn[1];
        vertices[i].normal.z = -vn[2];

        vertices[i].texCoord.x = vt[0];
        vertices[i].texCoord.y = vt[1];
    }

    // Now we copy the indices (maybe later I can do memcpy)
    for (cgltf_size i = 0; i < index_count; i += 3) {
        // NOTE: We flip the winding to be LH, because glTF is RH
        uint32_t i0 = static_cast<uint32_t>(cgltf_accessor_read_index(ind, i + 0));
        uint32_t i1 = static_cast<uint32_t>(cgltf_accessor_read_index(ind, i + 1));
        uint32_t i2 = static_cast<uint32_t>(cgltf_accessor_read_index(ind, i + 2));

        indices[i + 0] = i0;
        indices[i + 1] = i2;
        indices[i + 2] = i1;
    }

    cgltf_free(gltf_data);

    // TODO: Calculate Tangents if they are missing!

    m->vertexStride = sizeof(Vertex);
    m->indexCount = index_count;

    // Create Vertex Buffer
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = UINT(sizeof(Vertex) * vertices.size());
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(
        &bufferDesc,
        &initData,
        m->pVertexBuffer.GetAddressOf());

    if (FAILED(hr)) {
        LOG("mesh::load: Vertex buffer couldn't be created.");
        return id::invalid();
    }

    // Reuse the vertex buffer description for index description
    bufferDesc.ByteWidth = UINT(sizeof(uint32_t) * m->indexCount);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT; // Maybe D3D11_USAGE_IMMUTABLE
    bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;

    // Reuse subresource data as well
    initData.pSysMem = indices.data();

    hr = device->CreateBuffer(
        &bufferDesc,
        &initData,
        m->pIndexBuffer.GetAddressOf());

    if (FAILED(hr)) {
        LOG("mesh::load: Index buffer couldn't be created.");
        return id::invalid();
    }

    return m->id;
}

// Id mesh::load_obj(const char *filename) {
//     // Load in the specified OBJ file using tinyobj
//     // This is much easier for now than writing my own
//     // parser, like before. For this project this is enough.
//     std::string inputfile(filename);
//     tinyobj::ObjReader reader;
//
//     if (!reader.ParseFromFile(inputfile)) {
//         LOG("mesh::load: Something happened, and couldn't open the obj.");
//         return id::invalid();
//     }
//
//     // Get attributes, shapes, and materials. Weird naming...
//     const tinyobj::attrib_t &attrib = reader.GetAttrib();
//     const std::vector<tinyobj::shape_t> &shapes = reader.GetShapes();
//     // const std::vector<tinyobj::material_t> &materials = reader.GetMaterials();
//
//     std::vector<Vertex> outVertices;
//     std::vector<uint32_t> outIndices;
//     std::unordered_map<IndexTriplet, uint32_t, TripletHasher> cache;
//
//     // Looping over shapes
//     for (auto const &shape : shapes) {
//         // Looping over faces/polygons
//         size_t index_offset = 0;
//         for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
//             size_t fv = shape.mesh.num_face_vertices[f];
//             // Looping over vertices in the face
//             for (size_t v = 0; v < fv; ++v) {
//                 tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
//                 IndexTriplet key{idx.vertex_index, idx.normal_index, idx.texcoord_index};
//
//                 auto it = cache.find(key);
//                 if (it != cache.end()) {
//                     // Reuse existing vertex
//                     outIndices.push_back(it->second);
//                 } else {
//                     // Create new vertex
//                     Vertex vert{};
//
//                     // Position
//                     vert.position = {
//                         attrib.vertices[3 * key.v + 0],
//                         attrib.vertices[3 * key.v + 1],
//                         attrib.vertices[3 * key.v + 2]};
//
//                     // Normal
//                     if (key.vn >= 0) {
//                         vert.normal = {
//                             attrib.normals[3 * key.vn + 0],
//                             attrib.normals[3 * key.vn + 1],
//                             attrib.normals[3 * key.vn + 2],
//                         };
//                     } else {
//                         // Setting this to zero, but maybe it should just point forward?
//                         vert.normal = {0, 0, 0};
//                     }
//
//                     // Texture Coordinates
//                     if (key.vt >= 0) {
//                         vert.texCoord = {
//                             attrib.texcoords[2 * key.vt + 0],
//                             attrib.texcoords[2 * key.vt + 1],
//                         };
//                     } else {
//                         vert.texCoord = {0, 0};
//                     }
//
//                     uint32_t newIndex = static_cast<uint32_t>(outVertices.size());
//                     cache[key] = newIndex;
//                     outVertices.push_back(vert);
//                     outIndices.push_back(newIndex);
//                 }
//             }
//             index_offset += fv;
//         }
//     }
//
//     // Initialize tangents
//     for (auto &v : outVertices) {
//         v.tangent = {0, 0, 0};
//     }
//
//     // Calculate tangents per triangle
//     for (size_t i = 0; i < outIndices.size(); i += 3) {
//         Vertex &v0 = outVertices[outIndices[i + 0]];
//         Vertex &v1 = outVertices[outIndices[i + 1]];
//         Vertex &v2 = outVertices[outIndices[i + 2]];
//
//         DirectX::XMFLOAT3 p0 = v0.position;
//         DirectX::XMFLOAT3 p1 = v1.position;
//         DirectX::XMFLOAT3 p2 = v2.position;
//
//         DirectX::XMFLOAT2 uv0 = v0.texCoord;
//         DirectX::XMFLOAT2 uv1 = v1.texCoord;
//         DirectX::XMFLOAT2 uv2 = v2.texCoord;
//
//         DirectX::XMVECTOR P0 = DirectX::XMLoadFloat3(&p0);
//         DirectX::XMVECTOR P1 = DirectX::XMLoadFloat3(&p1);
//         DirectX::XMVECTOR P2 = DirectX::XMLoadFloat3(&p2);
//
//         DirectX::XMVECTOR UV0 = DirectX::XMLoadFloat2(&uv0);
//         DirectX::XMVECTOR UV1 = DirectX::XMLoadFloat2(&uv1);
//         DirectX::XMVECTOR UV2 = DirectX::XMLoadFloat2(&uv2);
//
//         DirectX::XMVECTOR edge1 = DirectX::XMVectorSubtract(P1, P0);
//         DirectX::XMVECTOR edge2 = DirectX::XMVectorSubtract(P2, P0);
//
//         DirectX::XMVECTOR deltaUV1 = DirectX::XMVectorSubtract(UV1, UV0);
//         DirectX::XMVECTOR deltaUV2 = DirectX::XMVectorSubtract(UV2, UV0);
//
//         float du1 = DirectX::XMVectorGetX(deltaUV1);
//         float dv1 = DirectX::XMVectorGetY(deltaUV1);
//         float du2 = DirectX::XMVectorGetX(deltaUV2);
//         float dv2 = DirectX::XMVectorGetY(deltaUV2);
//
//         float f = 1.0f / (du1 * dv2 - du2 * dv1);
//
//         DirectX::XMVECTOR tangent = f * (dv2 * edge1 - dv1 * edge2);
//         tangent = DirectX::XMVector3Normalize(tangent);
//
//         // Accumulate (smoothing group-like)
//         DirectX::XMFLOAT3 t;
//         DirectX::XMStoreFloat3(&t, tangent);
//         v0.tangent.x += t.x;
//         v0.tangent.y += t.y;
//         v0.tangent.z += t.z;
//         v1.tangent.x += t.x;
//         v1.tangent.y += t.y;
//         v1.tangent.z += t.z;
//         v2.tangent.x += t.x;
//         v2.tangent.y += t.y;
//         v2.tangent.z += t.z;
//     }
//
//     // Normalize tangents
//     for (auto &v : outVertices) {
//         DirectX::XMVECTOR tangent = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&v.tangent));
//         XMStoreFloat3(&v.tangent, tangent);
//     }
//
// }

void mesh::destroy(MeshId mesh_id) {
    Renderer *renderer = application::get_renderer();
    assert(renderer && "mesh::destroy: Something went wrong, the renderer couldn't be retrieved");

    if (id::is_fresh(renderer->meshes[mesh_id.id].id, mesh_id)) {
        id::invalidate(&renderer->meshes[mesh_id.id].id);
        delete &renderer->meshes[mesh_id.id];
    }
}

Mesh *mesh::get(Renderer *renderer, MeshId mesh_id) {
    if (id::is_valid(mesh_id) && mesh_id.id < MAX_MESHES) {
        Mesh *m = &renderer->meshes[mesh_id.id];
        if (id::is_fresh(m->id, mesh_id)) {
            return m;
        }
    }
    return nullptr;
}

void mesh::draw(ID3D11DeviceContext *context, Mesh *mesh) {
    if (!mesh || !mesh->pVertexBuffer) {
        return;
    }

    // Set the vertex buffer
    UINT offset = 0;
    context->IASetVertexBuffers(
        0, // Start slot
        1, // Number of buffers
        mesh->pVertexBuffer.GetAddressOf(),
        &mesh->vertexStride,
        &offset);

    // Set index buffer...
    context->IASetIndexBuffer(
        mesh->pIndexBuffer.Get(),
        DXGI_FORMAT_R32_UINT,
        0);

    // Set the primitive topology
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw
    context->DrawIndexed(mesh->indexCount, 0, 0);
}
