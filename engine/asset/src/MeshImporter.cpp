// mye/asset/MeshImporter.cpp — glTF 정적 메시 임포터 (docs/04 §임포터 glTF)
//
// CGLTF_IMPLEMENTATION은 이 TU 하나에서만 켠다(third_party/cgltf 소비 규약).
// 경로: .gltf/.glb 바이트 → cgltf 파싱 → 정점/인덱스(왼손 변환) → rhi 정점/인덱스 버퍼.
//
// 좌표 변환(glTF 오른손·+Y업·-Z전방 → 엔진 왼손·+Y업·+Z전방):
//   position.z, normal.z 부호 반전. 삼각형 인덱스 와인딩(0,1,2 → 0,2,1) 뒤집기로
//   반전된 축의 정면성(front-face) 유지. UV는 glTF 그대로(좌상단 원점).
#define CGLTF_IMPLEMENTATION
// cgltf는 실패 시 result 코드를 돌려주므로 예외 불사용 규약과 정합.
#include "cgltf.h"

#include "mye/asset/MeshImporter.h"
#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"

#include <array>
#include <cstring>
#include <limits>

namespace mye::asset {

namespace {
constexpr std::array<std::string_view, 2> kGltfExts{".gltf", ".glb"};

const char* CgltfResultString(cgltf_result r) {
    switch (r) {
        case cgltf_result_success:          return "success";
        case cgltf_result_data_too_short:   return "data_too_short";
        case cgltf_result_unknown_format:   return "unknown_format";
        case cgltf_result_invalid_json:     return "invalid_json";
        case cgltf_result_invalid_gltf:     return "invalid_gltf";
        case cgltf_result_invalid_options:  return "invalid_options";
        case cgltf_result_file_not_found:   return "file_not_found";
        case cgltf_result_io_error:         return "io_error";
        case cgltf_result_out_of_memory:    return "out_of_memory";
        case cgltf_result_legacy_gltf:      return "legacy_gltf";
        default:                            return "unknown_error";
    }
}

// 누적 바운딩 확장.
void ExpandAabb(Aabb& box, const Vec3& p) {
    box.min.x = p.x < box.min.x ? p.x : box.min.x;
    box.min.y = p.y < box.min.y ? p.y : box.min.y;
    box.min.z = p.z < box.min.z ? p.z : box.min.z;
    box.max.x = p.x > box.max.x ? p.x : box.max.x;
    box.max.y = p.y > box.max.y ? p.y : box.max.y;
    box.max.z = p.z > box.max.z ? p.z : box.max.z;
}

Aabb EmptyAabb() {
    const float inf = std::numeric_limits<float>::infinity();
    return Aabb{{inf, inf, inf}, {-inf, -inf, -inf}};
}

} // namespace

std::span<const std::string_view> MeshImporter::SourceExtensions() const {
    return kGltfExts;
}

std::span<const rhi::VertexAttribute> MeshVertexAttributes() {
    // 정적 저장 수명 — 02가 그대로 파이프라인 VertexLayoutDesc에 넘길 수 있다.
    static const std::array<rhi::VertexAttribute, 3> kAttrs{{
        {"POSITION", 0, rhi::Format::RGB32Float, static_cast<uint32_t>(offsetof(MeshVertex, position)), 0},
        {"NORMAL",   0, rhi::Format::RGB32Float, static_cast<uint32_t>(offsetof(MeshVertex, normal)),   0},
        {"TEXCOORD", 0, rhi::Format::RG32Float,  static_cast<uint32_t>(offsetof(MeshVertex, uv)),        0},
    }};
    return kAttrs;
}

Expected<MeshData, Error>
MeshImporter::ParseGltf(std::span<const std::byte> gltfBytes, const MeshImportSettings& settings) {
    if (gltfBytes.empty()) {
        return Error{"MeshImporter::ParseGltf: empty input", 1};
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse(&options, gltfBytes.data(), gltfBytes.size(), &data);
    if (res != cgltf_result_success) {
        return Error{std::string("MeshImporter::ParseGltf: cgltf_parse failed: ") +
                         CgltfResultString(res),
                     2};
    }

    // 버퍼 로드: .glb 임베드 bin·data-URI base64는 path=null로 로드된다.
    // 외부 .bin 파일은 미지원(메모리 전용 임포트 경로) — 실패 시 명시적 에러.
    res = cgltf_load_buffers(&options, data, nullptr);
    if (res != cgltf_result_success) {
        cgltf_free(data);
        return Error{std::string("MeshImporter::ParseGltf: cgltf_load_buffers failed (외부 .bin "
                                 "미지원, .glb 또는 data-URI 사용): ") +
                         CgltfResultString(res),
                     3};
    }

    MeshData out;
    out.bounds = EmptyAabb();

    const float scale = settings.scale;

    // glTF mesh[].primitive[] 를 순회 — primitive 하나 = 서브메시 하나. 모든 primitive의 정점을
    // 하나의 통합 정점 버퍼에 이어붙이고, 인덱스는 통합 정점 인덱스로 재기입한다(baseVertex=0).
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& gmesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < gmesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = gmesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) {
                // 삼각형만 지원(선·점 프리미티브는 스킵).
                continue;
            }

            // 속성 접근자 확보.
            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* nrmAcc = nullptr;
            const cgltf_accessor* uvAcc = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& attr = prim.attributes[ai];
                if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_normal) nrmAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
            }
            if (!posAcc || posAcc->count == 0) {
                continue;  // 위치 없는 프리미티브는 무의미.
            }

            const cgltf_size vertBase = out.vertices.size();
            const cgltf_size vertCount = posAcc->count;

            Submesh sub;
            sub.baseVertex = 0;  // 통합 정점 버퍼 — 인덱스에 vertBase를 더해 재기입.
            sub.materialSlot = prim.material
                                   ? static_cast<int32_t>(prim.material - data->materials)
                                   : -1;
            sub.bounds = EmptyAabb();

            // 정점 언팩.
            out.vertices.reserve(out.vertices.size() + vertCount);
            for (cgltf_size v = 0; v < vertCount; ++v) {
                MeshVertex mv{};

                float p[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(posAcc, v, p, 3);
                // 왼손 변환: z 부호 반전. 스케일 적용.
                mv.position = Vec3{p[0] * scale, p[1] * scale, -p[2] * scale};

                if (nrmAcc && nrmAcc->count == vertCount) {
                    float n[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_float(nrmAcc, v, n, 3);
                    mv.normal = Vec3{n[0], n[1], -n[2]}.Normalized();
                }

                if (uvAcc && uvAcc->count == vertCount) {
                    float t[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_float(uvAcc, v, t, 2);
                    mv.uv = Vec2{t[0], settings.flipUv ? (1.0f - t[1]) : t[1]};
                }

                ExpandAabb(sub.bounds, mv.position);
                ExpandAabb(out.bounds, mv.position);
                out.vertices.push_back(mv);
            }

            // 인덱스 언팩(없으면 순차 인덱스). 와인딩 뒤집기(0,1,2 → 0,2,1)로 z-반전 정면성 유지.
            sub.firstIndex = static_cast<uint32_t>(out.indices.size());
            if (prim.indices && prim.indices->count > 0) {
                const cgltf_size idxCount = prim.indices->count;
                out.indices.reserve(out.indices.size() + idxCount);
                // 삼각형 단위로 뒤집는다.
                for (cgltf_size tri = 0; tri + 2 < idxCount; tri += 3) {
                    const uint32_t i0 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, tri + 0));
                    const uint32_t i1 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, tri + 1));
                    const uint32_t i2 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, tri + 2));
                    const uint32_t base = static_cast<uint32_t>(vertBase);
                    out.indices.push_back(base + i0);
                    out.indices.push_back(base + i2);
                    out.indices.push_back(base + i1);
                }
            } else {
                // 비인덱스: 순차 삼각형. 와인딩 뒤집기.
                const uint32_t base = static_cast<uint32_t>(vertBase);
                for (cgltf_size tri = 0; tri + 3 <= vertCount; tri += 3) {
                    out.indices.push_back(base + static_cast<uint32_t>(tri + 0));
                    out.indices.push_back(base + static_cast<uint32_t>(tri + 2));
                    out.indices.push_back(base + static_cast<uint32_t>(tri + 1));
                }
            }

            sub.indexCount = static_cast<uint32_t>(out.indices.size()) - sub.firstIndex;
            if (sub.indexCount > 0) {
                out.submeshes.push_back(sub);
            }
        }
    }

    cgltf_free(data);

    if (out.vertices.empty() || out.submeshes.empty()) {
        return Error{"MeshImporter::ParseGltf: no triangle geometry found in glTF", 4};
    }
    if (!out.bounds.IsValid()) {
        out.bounds = Aabb{};  // 안전 폴백.
    }
    return out;
}

Expected<Mesh, Error> MeshImporter::Upload(rhi::IDevice& device, MeshData data) {
    if (data.vertices.empty() || data.indices.empty()) {
        return Error{"MeshImporter::Upload: empty mesh data", 1};
    }

    rhi::BufferDesc vbDesc;
    vbDesc.size = data.vertices.size() * sizeof(MeshVertex);
    vbDesc.usage = rhi::BufferUsage::Vertex;
    vbDesc.access = rhi::CpuAccess::None;
    vbDesc.debugName = "MeshImporter.vb";
    rhi::BufferHandle vb = device.CreateBuffer(vbDesc, data.vertices.data());
    if (!vb.IsValid()) {
        return Error{"MeshImporter::Upload: CreateBuffer(vertex) failed", 2};
    }

    rhi::BufferDesc ibDesc;
    ibDesc.size = data.indices.size() * sizeof(uint32_t);
    ibDesc.usage = rhi::BufferUsage::Index;
    ibDesc.access = rhi::CpuAccess::None;
    ibDesc.debugName = "MeshImporter.ib";
    rhi::BufferHandle ib = device.CreateBuffer(ibDesc, data.indices.data());
    if (!ib.IsValid()) {
        device.Destroy(vb);
        return Error{"MeshImporter::Upload: CreateBuffer(index) failed", 3};
    }

    Mesh m;
    m.vertexBuffer = vb;
    m.indexBuffer = ib;
    m.indexFormat = rhi::IndexFormat::Uint32;
    m.vertexCount = static_cast<uint32_t>(data.vertices.size());
    m.indexCount = static_cast<uint32_t>(data.indices.size());
    m.submeshes = data.submeshes;
    m.bounds = data.bounds;
    m.cpu = std::move(data);
    return m;
}

Expected<void*, Error> MeshImporter::Import(ImportContext& ctx) {
    MeshImportSettings settings;
    if (ctx.settings) {
        settings = *static_cast<const MeshImportSettings*>(ctx.settings);
    }

    auto parsed = ParseGltf(ctx.sourceBytes, settings);
    if (!parsed) {
        return parsed.GetError();
    }

    if (!ctx.device) {
        return Error{"MeshImporter::Import: no rhi device injected", 5};
    }

    auto uploaded = Upload(*ctx.device, std::move(parsed.Value()));
    if (!uploaded) {
        return uploaded.GetError();
    }

    Mesh* obj = new Mesh(std::move(uploaded.Value()));
    return static_cast<void*>(obj);
}

void MeshImporter::Destroy(void* assetObject) {
    delete static_cast<Mesh*>(assetObject);
}

void MeshImporter::DestroyWithDevice(void* assetObject, rhi::IDevice* device) {
    if (auto* m = static_cast<Mesh*>(assetObject); m && device) {
        if (m->vertexBuffer.IsValid()) device->Destroy(m->vertexBuffer);
        if (m->indexBuffer.IsValid()) device->Destroy(m->indexBuffer);
    }
    delete static_cast<Mesh*>(assetObject);
}

// ---- 비동기 로더 ------------------------------------------------------------
Expected<ParsedAsset, Error>
MeshAsyncLoader::Parse(LoadContext& /*ctx*/, std::span<const std::byte> runtimeData) {
    // 워커 스레드: 순수 CPU 파싱. GPU/디바이스 접근 금지.
    // M2-A: .meta 설정 주입은 후속 — 기본값 사용.
    MeshImportSettings settings;
    auto parsed = MeshImporter::ParseGltf(runtimeData, settings);
    if (!parsed) return parsed.GetError();

    auto* md = new MeshData(std::move(parsed.Value()));
    ParsedAsset out;
    out.cpuData = md;
    return out;
}

Expected<void*, Error>
MeshAsyncLoader::Finalize(LoadContext& /*ctx*/, ParsedAsset& parsed, rhi::IDevice* device) {
    auto* md = static_cast<MeshData*>(parsed.cpuData);
    if (!md) return Error{"MeshAsyncLoader::Finalize: null parsed data", 1};

    if (!device) {
        // 헤드리스(테스트): GPU 핸들 없이 CPU 데이터만 담은 Mesh를 커밋한다.
        auto* obj = new Mesh();
        obj->indexFormat = rhi::IndexFormat::Uint32;
        obj->vertexCount = static_cast<uint32_t>(md->vertices.size());
        obj->indexCount = static_cast<uint32_t>(md->indices.size());
        obj->submeshes = md->submeshes;
        obj->bounds = md->bounds;
        obj->cpu = std::move(*md);
        delete md;
        parsed.cpuData = nullptr;
        return static_cast<void*>(obj);
    }

    auto uploaded = MeshImporter::Upload(*device, std::move(*md));
    delete md;
    parsed.cpuData = nullptr;
    if (!uploaded) return uploaded.GetError();

    auto* obj = new Mesh(std::move(uploaded.Value()));
    return static_cast<void*>(obj);
}

void MeshAsyncLoader::DiscardParsed(ParsedAsset& parsed) {
    delete static_cast<MeshData*>(parsed.cpuData);
    parsed.cpuData = nullptr;
}

void MeshAsyncLoader::Unload(void* assetObject, rhi::IDevice* device) {
    if (auto* m = static_cast<Mesh*>(assetObject); m && device) {
        if (m->vertexBuffer.IsValid()) device->Destroy(m->vertexBuffer);
        if (m->indexBuffer.IsValid()) device->Destroy(m->indexBuffer);
    }
    delete static_cast<Mesh*>(assetObject);
}

} // namespace mye::asset
