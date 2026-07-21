// SpriteImporter.cpp — Aseprite/스프라이트시트 임포터 (docs/04 §임포터 라인업, M3-A)
//
// 두 경로:
//   (a) ParseAsepriteJson — Aseprite `--sheet --data` JSON(하나의 json::Value)을 파싱해
//       AsepriteData(프레임 rect·duration·슬라이스·태그→클립)로 굳힌다. aseprite.exe 불요:
//       이미 익스포트된 JSON 을 순수 데이터로 받는 파서라 단위 테스트가 결정적이다.
//   (b) SpriteSheetImporter::SliceGrid — 균등 그리드 시트 PNG 를 프레임 rect 로 자른다.
//
// Aseprite JSON 규약:
//   - frames: array 또는 hash(파일명→frame). 각 항목 { frame:{x,y,w,h}, duration:ms }.
//   - meta.size: 시트 픽셀 크기. meta.frameTags: [{name,from,to,direction}].
//   - meta.slices: [{name, keys:[{frame,bounds:{x,y,w,h}, pivot?:{x,y}, center?:{x,y,w,h}}]}].
//   - duration 은 밀리초 → 초로 변환(/1000).
#include "mye/asset/SpriteImporter.h"

#include <algorithm>
#include <array>
#include <string>

namespace mye::asset {

namespace {

// json::Value 에서 {x,y,w,h} rect 를 읽는다(없는 필드는 0).
RectInt ReadRectInt(const json::Value& v) {
    RectInt r;
    if (const json::Value* x = v.Find("x")) r.x = static_cast<int32_t>(x->AsInt());
    if (const json::Value* y = v.Find("y")) r.y = static_cast<int32_t>(y->AsInt());
    if (const json::Value* w = v.Find("w")) r.w = static_cast<int32_t>(w->AsInt());
    if (const json::Value* h = v.Find("h")) r.h = static_cast<int32_t>(h->AsInt());
    return r;
}

Vec2i ReadVec2i(const json::Value& v) {
    Vec2i p;
    if (const json::Value* x = v.Find("x")) p.x = static_cast<int32_t>(x->AsInt());
    if (const json::Value* y = v.Find("y")) p.y = static_cast<int32_t>(y->AsInt());
    return p;
}

AnimationClipData::Direction ParseDirection(std::string_view s) {
    if (s == "reverse") return AnimationClipData::Direction::Reverse;
    if (s == "pingpong") return AnimationClipData::Direction::PingPong;
    return AnimationClipData::Direction::Forward;
}

// 하나의 frame 오브젝트 → SpriteFrame(rect·기본 UV) + duration(초).
void ReadFrameEntry(const json::Value& entry, SpriteFrame& outFrame, float& outDurationSec) {
    if (const json::Value* fr = entry.Find("frame")) {
        outFrame.rect = ReadRectInt(*fr);
    }
    outFrame.uv = {0.0f, 0.0f, 1.0f, 1.0f};   // 아틀라스 배치 전 기본 UV
    outFrame.atlasPage = 0;
    double durMs = 100.0;   // Aseprite 기본 100ms
    if (const json::Value* d = entry.Find("duration")) durMs = d->AsDouble(100.0);
    outDurationSec = static_cast<float>(durMs / 1000.0);
}

} // namespace

// ---------------------------------------------------------------------------
// ParseAsepriteJson
// ---------------------------------------------------------------------------
Expected<AsepriteData, Error> ParseAsepriteJson(const json::Value& asepriteJson) {
    if (!asepriteJson.IsObject()) {
        return Error{"ParseAsepriteJson: root is not a JSON object", -1};
    }

    AsepriteData out;

    // ---- frames: array 또는 hash 지원 ----
    const json::Value* framesNode = asepriteJson.Find("frames");
    if (!framesNode) {
        return Error{"ParseAsepriteJson: missing 'frames'", -1};
    }
    if (framesNode->IsArray()) {
        const auto& arr = framesNode->AsArray();
        out.frames.reserve(arr.size());
        out.frameDurations.reserve(arr.size());
        for (const json::Value& entry : arr) {
            SpriteFrame f;
            float dur = 0.1f;
            ReadFrameEntry(entry, f, dur);
            out.frames.push_back(f);
            out.frameDurations.push_back(dur);
        }
    } else if (framesNode->IsObject()) {
        // hash 형: 키 순서는 std::map(사전순)으로 고정 — Aseprite 파일명 "name N.ase" 는
        // 사전순과 프레임순이 어긋날 수 있어 경고를 남긴다(결정성은 유지).
        const auto& obj = framesNode->AsObject();
        out.frames.reserve(obj.size());
        out.frameDurations.reserve(obj.size());
        for (const auto& [key, entry] : obj) {
            SpriteFrame f;
            float dur = 0.1f;
            ReadFrameEntry(entry, f, dur);
            out.frames.push_back(f);
            out.frameDurations.push_back(dur);
        }
        out.warnings.push_back(
            "frames is a hash; frame order taken as lexicographic key order "
            "(prefer array export '--list-tags --format json-array')");
    } else {
        return Error{"ParseAsepriteJson: 'frames' is neither array nor object", -1};
    }

    // ---- meta ----
    if (const json::Value* meta = asepriteJson.Find("meta")) {
        if (const json::Value* size = meta->Find("size")) {
            // meta.size 는 {w,h} 키를 쓴다(프레임 rect 의 x/y/w/h 와 다름).
            if (const json::Value* w = size->Find("w"))
                out.sheetSize.x = static_cast<int32_t>(w->AsInt());
            if (const json::Value* h = size->Find("h"))
                out.sheetSize.y = static_cast<int32_t>(h->AsInt());
        }

        // ---- frameTags → 클립 ----
        if (const json::Value* tags = meta->Find("frameTags"); tags && tags->IsArray()) {
            for (const json::Value& tag : tags->AsArray()) {
                AnimationClipData clip;
                if (const json::Value* n = tag.Find("name")) {
                    clip.name = std::string(n->AsString());
                }
                int32_t from = 0;
                int32_t to = -1;
                if (const json::Value* f = tag.Find("from")) from = static_cast<int32_t>(f->AsInt());
                if (const json::Value* t = tag.Find("to")) to = static_cast<int32_t>(t->AsInt());
                if (const json::Value* dir = tag.Find("direction")) {
                    clip.direction = ParseDirection(dir->AsString());
                }
                const int32_t frameCount = static_cast<int32_t>(out.frames.size());
                // 범위 클램프 + 유효성 경고.
                if (from < 0) from = 0;
                if (to < from || to >= frameCount) {
                    if (to >= frameCount) {
                        out.warnings.push_back("tag '" + clip.name +
                                               "' range exceeds frame count; clamped");
                    }
                    to = std::min(to, frameCount - 1);
                }
                for (int32_t i = from; i <= to && i >= 0; ++i) {
                    clip.frameIndices.push_back(static_cast<uint32_t>(i));
                    clip.frameDurations.push_back(out.frameDurations[static_cast<size_t>(i)]);
                }
                // Forward 가 아니면 loop 는 유지하되 방향 필드로 표현. 기본 loop=true.
                clip.version = 1;
                out.clips.push_back(std::move(clip));
            }
        }

        // ---- slices → SpriteSlice(pivot/9-slice/히트박스) ----
        if (const json::Value* slices = meta->Find("slices"); slices && slices->IsArray()) {
            for (const json::Value& s : slices->AsArray()) {
                SpriteSlice slice;
                if (const json::Value* n = s.Find("name")) {
                    slice.name = std::string(n->AsString());
                }
                // 첫 key 를 대표값으로 사용(프레임별 슬라이스 애니메이션은 후속).
                if (const json::Value* keys = s.Find("keys");
                    keys && keys->IsArray() && !keys->AsArray().empty()) {
                    const json::Value& k0 = keys->AsArray().front();
                    if (const json::Value* b = k0.Find("bounds")) {
                        slice.bounds = ReadRectInt(*b);
                    }
                    if (const json::Value* piv = k0.Find("pivot")) {
                        slice.hasPivot = true;
                        slice.pivot = ReadVec2i(*piv);
                    }
                    if (const json::Value* c = k0.Find("center")) {
                        slice.hasCenter = true;
                        slice.center = ReadRectInt(*c);
                    }
                }
                out.slices.push_back(std::move(slice));
            }
        }
    } else {
        out.warnings.push_back("missing 'meta'; sheet size and tags unavailable");
    }

    // pivot 슬라이스가 있으면 프레임 pivot 을 픽셀 pivot 으로 반영(프레임 로컬 좌표).
    // 임포터가 pivotSliceName 규약으로 선택하지만, 파서는 "pivot" 이름을 기본 적용해
    // 프레임 pivot 필드를 채운다(소비자·테스트 편의). 이름 규약 최종 선택은 Import 단.
    for (const SpriteSlice& sl : out.slices) {
        if (sl.name == "pivot" && sl.hasPivot) {
            for (SpriteFrame& f : out.frames) {
                // 슬라이스 pivot 은 시트 절대 좌표 → 프레임 로컬로 변환.
                f.pivot = {static_cast<float>(sl.pivot.x - f.rect.x),
                           static_cast<float>(sl.pivot.y - f.rect.y)};
                f.pivotInPixels = true;
            }
            break;
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// AsepriteImporter
// ---------------------------------------------------------------------------
std::span<const std::string_view> AsepriteImporter::SourceExtensions() const {
    static const std::array<std::string_view, 2> kExt{".ase", ".aseprite"};
    return kExt;
}

Expected<void*, Error> AsepriteImporter::Import(ImportContext& ctx) {
    // M3-A: ctx.sourceBytes 는 Aseprite `--data` JSON(사이드카) 을 담는다고 가정한다.
    //   .ase 바이너리 직접 파싱·CLI 호출은 후속(문서 라인업 조건부 항목). 여기서는 이미
    //   익스포트된 JSON 을 SpriteSheet 로 굳히는 경로를 제공한다.
    if (ctx.sourceBytes.empty()) {
        return Error{"AsepriteImporter::Import: empty source (expect Aseprite --data JSON)", -1};
    }
    std::string_view text(reinterpret_cast<const char*>(ctx.sourceBytes.data()),
                          ctx.sourceBytes.size());
    auto parsed = json::Parse(text);
    if (!parsed) return parsed.GetError();

    auto data = ParseAsepriteJson(parsed.Value());
    if (!data) return data.GetError();

    const AsepriteImportSettings* settings =
        static_cast<const AsepriteImportSettings*>(ctx.settings);

    AsepriteData& ad = data.Value();
    auto* sheet = new SpriteSheet();
    sheet->frameSize = {0, 0};   // 태그/rect 기반
    sheet->frames = ad.frames;
    sheet->slices = ad.slices;
    sheet->version = 1;

    // pivot 슬라이스 이름 규약 재적용(설정이 있으면 그 이름 우선).
    if (settings && settings->pivotSliceName != "pivot") {
        for (const SpriteSlice& sl : ad.slices) {
            if (sl.name == settings->pivotSliceName && sl.hasPivot) {
                for (SpriteFrame& f : sheet->frames) {
                    f.pivot = {static_cast<float>(sl.pivot.x - f.rect.x),
                               static_cast<float>(sl.pivot.y - f.rect.y)};
                    f.pivotInPixels = true;
                }
                break;
            }
        }
    }
    // 클립은 서브 에셋(AnimationClipData)으로 분리 저장하는 것이 규약이나(.meta subAssets),
    // M3-A 는 임포트 캐시/서브에셋 등록 API 를 확정하지 않으므로 sheet 에 clips 참조를
    // 비워 둔다. 실제 클립 데이터는 AsepriteData 로 노출되어 테스트·M3-B 가 소비한다.
    // (M3-B 애니메이션이 서브에셋 등록으로 승격.)

    return static_cast<void*>(sheet);
}

void AsepriteImporter::Destroy(void* obj) { delete static_cast<SpriteSheet*>(obj); }

// ---------------------------------------------------------------------------
// SpriteSheetImporter — 균등 그리드 시트
// ---------------------------------------------------------------------------
std::span<const std::string_view> SpriteSheetImporter::SourceExtensions() const {
    static const std::array<std::string_view, 1> kExt{".sheet.png"};
    return kExt;
}

std::vector<SpriteFrame> SpriteSheetImporter::SliceGrid(Vec2i sheetSize,
                                                        const SpriteSheetImportSettings& s) {
    std::vector<SpriteFrame> frames;
    const int32_t sw = sheetSize.x;
    const int32_t sh = sheetSize.y;
    if (sw <= 0 || sh <= 0) return frames;

    // 프레임 크기 0 → 시트 전체를 단일 프레임으로.
    int32_t cw = s.frameSize.x;
    int32_t ch = s.frameSize.y;
    if (cw <= 0 || ch <= 0) {
        SpriteFrame f;
        f.rect = {0, 0, sw, sh};
        f.uv = {0.0f, 0.0f, 1.0f, 1.0f};
        frames.push_back(f);
        return frames;
    }

    const int32_t mx = s.margin.x;
    const int32_t my = s.margin.y;
    const int32_t sx = s.spacing.x;
    const int32_t sy = s.spacing.y;

    // 셀 스트라이드 = 셀 크기 + 간격. 행/열 개수는 여백·간격을 고려해 산출.
    const int32_t strideX = cw + sx;
    const int32_t strideY = ch + sy;
    if (strideX <= 0 || strideY <= 0) return frames;

    const int32_t usableW = sw - mx;
    const int32_t usableH = sh - my;
    // (usableW + spacing) / stride: 마지막 셀 뒤 간격은 없으므로 +sx 로 보정.
    const int32_t cols = (usableW + sx) / strideX;
    const int32_t rows = (usableH + sy) / strideY;

    // 좌상단 원점, 행-우선(row-major) 순서로 프레임 생성(8방향 시트: 행=방향, 열=프레임).
    for (int32_t r = 0; r < rows; ++r) {
        for (int32_t c = 0; c < cols; ++c) {
            const int32_t x = mx + c * strideX;
            const int32_t y = my + r * strideY;
            if (x + cw > sw || y + ch > sh) continue;   // 부분 셀 스킵
            SpriteFrame f;
            f.rect = {x, y, cw, ch};
            f.pivot = {0.5f, 0.5f};
            f.pivotInPixels = false;
            const float fsw = static_cast<float>(sw);
            const float fsh = static_cast<float>(sh);
            f.uv = {static_cast<float>(x) / fsw, static_cast<float>(y) / fsh,
                    static_cast<float>(cw) / fsw, static_cast<float>(ch) / fsh};
            frames.push_back(f);
        }
    }
    return frames;
}

Expected<void*, Error> SpriteSheetImporter::Import(ImportContext& ctx) {
    // 그리드 시트 임포트: 시트 크기는 설정에 없으므로 PNG 디코드로 얻어야 한다.
    //   M3-A 는 GPU 없는 순수 경로만 다루므로, 시트 PNG 는 TextureImporter::DecodePng 로
    //   CPU 디코드해 크기를 얻고 그리드를 자른다. 텍스처 참조(하드 의존성)는 M1 로더가
    //   GUID 로 채운다(여기서는 소스 텍스처 GUID 미정 → 빈 AssetRef).
    if (ctx.sourceBytes.empty()) {
        return Error{"SpriteSheetImporter::Import: empty source PNG", -1};
    }
    const SpriteSheetImportSettings* settings =
        static_cast<const SpriteSheetImportSettings*>(ctx.settings);
    SpriteSheetImportSettings defaults;
    const SpriteSheetImportSettings& s = settings ? *settings : defaults;

    auto decoded = TextureImporter::DecodePng(
        std::span<const std::byte>(ctx.sourceBytes.data(), ctx.sourceBytes.size()),
        s.premultiplyAlpha);
    if (!decoded) return decoded.GetError();

    const DecodedImage& img = decoded.Value();
    Vec2i sheetSize{static_cast<int32_t>(img.width), static_cast<int32_t>(img.height)};

    auto* sheet = new SpriteSheet();
    sheet->frameSize = s.frameSize;
    sheet->frames = SliceGrid(sheetSize, s);
    sheet->version = 1;
    return static_cast<void*>(sheet);
}

void SpriteSheetImporter::Destroy(void* obj) { delete static_cast<SpriteSheet*>(obj); }

} // namespace mye::asset
