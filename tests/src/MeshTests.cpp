// MeshTests.cpp — MeshImporter(glTF) 검증 (04): cgltf 파싱·좌표 변환·바운딩·인덱스 정확성
//
// GPU 없는 단위 테스트만 둔다(MeshImporter::Upload는 실디바이스 필요 → M2-C 통합 데모).
// .glb 바이너리를 테스트 내에서 최소 빌더로 조립해(임베드 BIN 청크) MeshImporter::ParseGltf가
// 정점 수·바운딩·인덱스(와인딩 뒤집기)·왼손 z-반전을 정확히 산출하는지 대조한다.
#include "TestFramework.h"

#include "mye/asset/Mesh.h"
#include "mye/asset/MeshImporter.h"
#include "mye/core/Math.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace mye;
using namespace mye::asset;

// ===========================================================================
// 최소 GLB 빌더 (테스트 전용) — POSITION/NORMAL/TEXCOORD0 + 인덱스(UNSIGNED_SHORT).
// 하나의 mesh·하나의 primitive. cgltf가 파싱 가능한 유효 glTF 2.0 JSON + BIN 청크.
// ===========================================================================
namespace {

void PutLE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

void AppendFloat(std::vector<uint8_t>& bin, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    PutLE32(bin, bits);
}

void AppendU16(std::vector<uint8_t>& bin, uint16_t x) {
    bin.push_back(static_cast<uint8_t>(x & 0xFF));
    bin.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

struct Vtx {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// verts/indices → 유효 .glb 바이트. 접근자 min/max는 위치에 대해서만 채운다(cgltf는 필수 아님).
std::vector<std::byte> BuildGlb(const std::vector<Vtx>& verts, const std::vector<uint16_t>& indices) {
    // BIN 레이아웃: [positions f32x3][normals f32x3][uv f32x2][indices u16]
    std::vector<uint8_t> bin;
    const uint32_t posOffset = 0;
    for (const auto& v : verts) { AppendFloat(bin, v.px); AppendFloat(bin, v.py); AppendFloat(bin, v.pz); }
    const uint32_t nrmOffset = static_cast<uint32_t>(bin.size());
    for (const auto& v : verts) { AppendFloat(bin, v.nx); AppendFloat(bin, v.ny); AppendFloat(bin, v.nz); }
    const uint32_t uvOffset = static_cast<uint32_t>(bin.size());
    for (const auto& v : verts) { AppendFloat(bin, v.u); AppendFloat(bin, v.v); }
    // 인덱스는 4바이트 정렬 필요(componentType u16, 오프셋은 4의 배수 권장).
    while (bin.size() % 4 != 0) bin.push_back(0);
    const uint32_t idxOffset = static_cast<uint32_t>(bin.size());
    for (uint16_t i : indices) AppendU16(bin, i);
    while (bin.size() % 4 != 0) bin.push_back(0);
    const uint32_t binLen = static_cast<uint32_t>(bin.size());

    // 위치 min/max 계산.
    float mn[3] = {verts[0].px, verts[0].py, verts[0].pz};
    float mx[3] = {verts[0].px, verts[0].py, verts[0].pz};
    for (const auto& v : verts) {
        const float p[3] = {v.px, v.py, v.pz};
        for (int k = 0; k < 3; ++k) { mn[k] = p[k] < mn[k] ? p[k] : mn[k]; mx[k] = p[k] > mx[k] ? p[k] : mx[k]; }
    }

    const uint32_t vc = static_cast<uint32_t>(verts.size());
    const uint32_t ic = static_cast<uint32_t>(indices.size());

    // glTF JSON. 4개 접근자(pos/nrm/uv/idx), 4개 bufferView, 1개 buffer(BIN).
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":" + std::to_string(binLen) + "}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(posOffset) + ",\"byteLength\":" + std::to_string(vc * 12) + ",\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(nrmOffset) + ",\"byteLength\":" + std::to_string(vc * 12) + ",\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(uvOffset) + ",\"byteLength\":" + std::to_string(vc * 8) + ",\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":" + std::to_string(idxOffset) + ",\"byteLength\":" + std::to_string(ic * 2) + ",\"target\":34963}"
        "],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(vc) + ",\"type\":\"VEC3\","
          "\"min\":[" + std::to_string(mn[0]) + "," + std::to_string(mn[1]) + "," + std::to_string(mn[2]) + "],"
          "\"max\":[" + std::to_string(mx[0]) + "," + std::to_string(mx[1]) + "," + std::to_string(mx[2]) + "]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":" + std::to_string(vc) + ",\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":" + std::to_string(vc) + ",\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5123,\"count\":" + std::to_string(ic) + ",\"type\":\"SCALAR\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"mode\":4}]}],"
        "\"nodes\":[{\"mesh\":0}],"
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"scene\":0}";

    // JSON 청크는 4바이트 정렬(공백 패딩).
    std::vector<uint8_t> jsonBytes(json.begin(), json.end());
    while (jsonBytes.size() % 4 != 0) jsonBytes.push_back(0x20);  // space

    const uint32_t jsonLen = static_cast<uint32_t>(jsonBytes.size());
    const uint32_t totalLen = 12 + 8 + jsonLen + 8 + binLen;

    std::vector<uint8_t> glb;
    // 헤더.
    glb.push_back('g'); glb.push_back('l'); glb.push_back('T'); glb.push_back('F');
    PutLE32(glb, 2);            // version
    PutLE32(glb, totalLen);     // total length
    // JSON 청크.
    PutLE32(glb, jsonLen);
    PutLE32(glb, 0x4E4F534A);   // "JSON"
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    // BIN 청크.
    PutLE32(glb, binLen);
    PutLE32(glb, 0x004E4942);   // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());

    std::vector<std::byte> out(glb.size());
    std::memcpy(out.data(), glb.data(), glb.size());
    return out;
}

// 단일 삼각형: 좌표를 명확히 구분해 z-반전·와인딩 검증 가능하게.
std::vector<std::byte> BuildTriangleGlb() {
    std::vector<Vtx> verts = {
        {0.0f, 0.0f, 1.0f,  0, 0, 1,  0.0f, 0.0f},   // z=+1 (glTF)
        {2.0f, 0.0f, 3.0f,  0, 0, 1,  1.0f, 0.0f},   // z=+3
        {0.0f, 4.0f, 1.0f,  0, 0, 1,  0.0f, 1.0f},
    };
    std::vector<uint16_t> idx = {0, 1, 2};
    return BuildGlb(verts, idx);
}

} // namespace

// ===========================================================================
// 테스트
// ===========================================================================

// 정점/인덱스 수·서브메시 1개.
MYE_TEST(MeshGltfParseCounts) {
    auto glb = BuildTriangleGlb();
    auto res = MeshImporter::ParseGltf(glb, MeshImportSettings{});
    MYE_EXPECT(static_cast<bool>(res));
    if (!res) return;
    const MeshData& m = res.Value();
    MYE_EXPECT(m.vertices.size() == 3);
    MYE_EXPECT(m.indices.size() == 3);
    MYE_EXPECT(m.submeshes.size() == 1);
    MYE_EXPECT(m.submeshes[0].firstIndex == 0);
    MYE_EXPECT(m.submeshes[0].indexCount == 3);
}

// 왼손 변환: glTF z=+1,+3 → 엔진 z=-1,-3. UV 보존. 법선 z 반전.
MYE_TEST(MeshGltfLeftHandedZFlip) {
    auto glb = BuildTriangleGlb();
    auto res = MeshImporter::ParseGltf(glb, MeshImportSettings{});
    MYE_EXPECT(static_cast<bool>(res));
    if (!res) return;
    const MeshData& m = res.Value();
    MYE_EXPECT_NEAR(m.vertices[0].position.z, -1.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.vertices[1].position.z, -3.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.vertices[0].position.x, 0.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.vertices[1].position.x, 2.0f, 1e-5f);
    // 법선 (0,0,1) → (0,0,-1).
    MYE_EXPECT_NEAR(m.vertices[0].normal.z, -1.0f, 1e-5f);
    // UV 보존.
    MYE_EXPECT_NEAR(m.vertices[1].uv.x, 1.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.vertices[2].uv.y, 1.0f, 1e-5f);
}

// 와인딩 뒤집기: 입력 인덱스 (0,1,2) → 출력 (0,2,1).
MYE_TEST(MeshGltfWindingFlip) {
    auto glb = BuildTriangleGlb();
    auto res = MeshImporter::ParseGltf(glb, MeshImportSettings{});
    MYE_EXPECT(static_cast<bool>(res));
    if (!res) return;
    const MeshData& m = res.Value();
    MYE_EXPECT(m.indices[0] == 0);
    MYE_EXPECT(m.indices[1] == 2);
    MYE_EXPECT(m.indices[2] == 1);
}

// 바운딩: z-반전 후 min/max 재계산. glTF z[1,3] → 엔진 z[-3,-1].
MYE_TEST(MeshGltfBounds) {
    auto glb = BuildTriangleGlb();
    auto res = MeshImporter::ParseGltf(glb, MeshImportSettings{});
    MYE_EXPECT(static_cast<bool>(res));
    if (!res) return;
    const MeshData& m = res.Value();
    MYE_EXPECT(m.bounds.IsValid());
    MYE_EXPECT_NEAR(m.bounds.min.x, 0.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.max.x, 2.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.min.y, 0.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.max.y, 4.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.min.z, -3.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.max.z, -1.0f, 1e-5f);
}

// 큐브(석상 대용): 24정점·36인덱스·바운딩 [-1,1]³ → z-반전 후도 대칭이라 [-1,1]³.
MYE_TEST(MeshGltfCube) {
    // 6면 × 4정점. 각 면 법선. 인덱스 6개/면.
    std::vector<Vtx> verts;
    std::vector<uint16_t> idx;
    auto face = [&](float nx, float ny, float nz,
                    Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        const uint16_t base = static_cast<uint16_t>(verts.size());
        verts.push_back({a.x, a.y, a.z, nx, ny, nz, 0, 0});
        verts.push_back({b.x, b.y, b.z, nx, ny, nz, 1, 0});
        verts.push_back({c.x, c.y, c.z, nx, ny, nz, 1, 1});
        verts.push_back({d.x, d.y, d.z, nx, ny, nz, 0, 1});
        idx.insert(idx.end(), {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                               base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)});
    };
    face(0, 0, 1,  {-1,-1, 1}, {1,-1, 1}, {1, 1, 1}, {-1, 1, 1});   // +Z
    face(0, 0,-1,  {1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, {1, 1,-1});   // -Z
    face(1, 0, 0,  {1,-1, 1}, {1,-1,-1}, {1, 1,-1}, {1, 1, 1});     // +X
    face(-1,0, 0,  {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}); // -X
    face(0, 1, 0,  {-1, 1, 1}, {1, 1, 1}, {1, 1,-1}, {-1, 1,-1});   // +Y
    face(0,-1, 0,  {-1,-1,-1}, {1,-1,-1}, {1,-1, 1}, {-1,-1, 1});   // -Y

    auto glb = BuildGlb(verts, idx);
    auto res = MeshImporter::ParseGltf(glb, MeshImportSettings{});
    MYE_EXPECT(static_cast<bool>(res));
    if (!res) return;
    const MeshData& m = res.Value();
    MYE_EXPECT(m.vertices.size() == 24);
    MYE_EXPECT(m.indices.size() == 36);
    MYE_EXPECT(m.submeshes.size() == 1);
    MYE_EXPECT_NEAR(m.bounds.min.x, -1.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.max.x, 1.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.min.z, -1.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.bounds.max.z, 1.0f, 1e-5f);
}

// 스케일 설정 적용.
MYE_TEST(MeshGltfScale) {
    auto glb = BuildTriangleGlb();
    MeshImportSettings s;
    s.scale = 2.0f;
    auto res = MeshImporter::ParseGltf(glb, s);
    MYE_EXPECT(static_cast<bool>(res));
    if (!res) return;
    const MeshData& m = res.Value();
    // x=2 → 4, z=+3 → -6.
    MYE_EXPECT_NEAR(m.vertices[1].position.x, 4.0f, 1e-5f);
    MYE_EXPECT_NEAR(m.vertices[1].position.z, -6.0f, 1e-5f);
}

// 손상 입력 → 에러(파싱 실패). 빈 입력·잘못된 매직.
MYE_TEST(MeshGltfInvalidInput) {
    std::vector<std::byte> empty;
    auto r1 = MeshImporter::ParseGltf(empty, MeshImportSettings{});
    MYE_EXPECT(!r1);

    std::vector<std::byte> garbage(64, std::byte{0x7F});
    auto r2 = MeshImporter::ParseGltf(garbage, MeshImportSettings{});
    MYE_EXPECT(!r2);
}

// 정점 레이아웃 정합 — 02가 바인딩할 속성 서술.
MYE_TEST(MeshVertexLayout) {
    MYE_EXPECT(sizeof(MeshVertex) == 32);
    MYE_EXPECT(kMeshVertexStride == 32);
    auto attrs = MeshVertexAttributes();
    MYE_EXPECT(attrs.size() == 3);
    MYE_EXPECT(std::string(attrs[0].semantic) == "POSITION");
    MYE_EXPECT(attrs[0].offset == 0);
    MYE_EXPECT(std::string(attrs[1].semantic) == "NORMAL");
    MYE_EXPECT(attrs[1].offset == 12);
    MYE_EXPECT(std::string(attrs[2].semantic) == "TEXCOORD");
    MYE_EXPECT(attrs[2].offset == 24);
}
