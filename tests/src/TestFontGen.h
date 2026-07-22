// TestFontGen.h — 최소 유효 TrueType 폰트 생성기 (M5-A 텍스트 래스터 헤드리스 테스트용)
//
// 목적: 저장소에 대용량 한글 TTF 를 벤더링하지 않고도, FreeType 래스터 경로(FontFace·GlyphAtlas)
//   를 검증할 수 있도록 in-memory 로 유효한 TrueType 폰트 blob 을 만든다. 각 글리프는 단순
//   "채워진 사각형" 아웃라인이라 래스터 시 실제 non-zero 픽셀을 낸다(글리프 존재·크기 검증 가능).
//
// 커버 코드포인트: 요청한 codepoints 목록(ASCII·한글 음절 혼합 가능). 스페이스(0x20)는 빈
//   글리프(advance 만)로 만든다. cmap format 4 (BMP) 로 사상 — 한글 완성형 U+AC00.. 커버.
//
// 이 파일은 테스트 전용 헤더-온리 유틸이다(엔진 코드 아님).
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace mye::testfont {

// 빅엔디안 라이터.
struct BE {
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { buf.push_back(uint8_t(v >> 8)); buf.push_back(uint8_t(v)); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void u32(uint32_t v) {
        buf.push_back(uint8_t(v >> 24)); buf.push_back(uint8_t(v >> 16));
        buf.push_back(uint8_t(v >> 8));  buf.push_back(uint8_t(v));
    }
    void bytes(const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }
    void pad4() { while (buf.size() % 4) buf.push_back(0); }
    size_t size() const { return buf.size(); }
};

inline uint32_t TableChecksum(const std::vector<uint8_t>& t) {
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 4 <= t.size(); i += 4)
        sum += (uint32_t(t[i]) << 24) | (uint32_t(t[i+1]) << 16) |
               (uint32_t(t[i+2]) << 8) | uint32_t(t[i+3]);
    if (i < t.size()) {
        uint32_t last = 0;
        for (int k = 0; k < 4; ++k)
            last |= (i + k < t.size() ? uint32_t(t[i + k]) : 0u) << (24 - k * 8);
        sum += last;
    }
    return sum;
}

// unitsPerEm = 1024. 각 글리프 = 채워진 사각형(bbox 대부분 채움) 또는 빈 글리프(스페이스).
// codepoints 는 오름차순 정렬·중복 제거되어 들어온다고 가정하지 않음(내부 처리).
inline std::vector<uint8_t> MakeTtf(std::vector<uint32_t> codepoints) {
    std::sort(codepoints.begin(), codepoints.end());
    codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
    // BMP 만(cmap format 4).
    codepoints.erase(std::remove_if(codepoints.begin(), codepoints.end(),
                     [](uint32_t c){ return c > 0xFFFF || c == 0; }), codepoints.end());

    const uint16_t unitsPerEm = 1024;
    const int16_t boxMin = 80, boxMax = 900;   // 채워진 사각형 좌표(em 단위)
    const int16_t advance = 1024;

    // glyph 0 = .notdef (빈), 이후 각 codepoint 에 글리프 하나.
    const uint16_t numGlyphs = uint16_t(codepoints.size() + 1);

    // --- glyf + loca ---
    std::vector<uint32_t> locaOffsets;   // long 포맷
    BE glyf;
    auto emitBoxGlyph = [&](bool empty) {
        locaOffsets.push_back(uint32_t(glyf.size()));
        if (empty) return;   // 빈 글리프(스페이스) → loca 오프셋 동일(길이 0)
        // simple glyph: 1 contour, 4 점(사각형).
        glyf.i16(1);               // numberOfContours
        glyf.i16(boxMin); glyf.i16(boxMin);   // xMin,yMin
        glyf.i16(boxMax); glyf.i16(boxMax);   // xMax,yMax
        glyf.u16(3);               // endPtsOfContours[0] = 3 (4 points)
        glyf.u16(0);               // instructionLength
        // flags: 4 점, 모두 on-curve(0x01). x/y 는 SHORT 벡터로 인코딩하지 않고 델타 워드 사용.
        // 간단히 각 점 flag = ON_CURVE(0x01), x/y 는 아래에서 16bit 델타.
        for (int i = 0; i < 4; ++i) glyf.u8(0x01);
        // x 좌표 델타(첫 점은 절대→0 기준 델타). 점: (min,min)(max,min)(max,max)(min,max)
        // 델타: min-0=min, max-min, 0, min-max
        int16_t xs[4] = {boxMin, int16_t(boxMax - boxMin), 0, int16_t(boxMin - boxMax)};
        int16_t ys[4] = {boxMin, 0, int16_t(boxMax - boxMin), 0};
        for (int i = 0; i < 4; ++i) glyf.i16(xs[i]);
        for (int i = 0; i < 4; ++i) glyf.i16(ys[i]);
        glyf.pad4();
    };
    emitBoxGlyph(true);   // .notdef → 빈(테스트 단순화)
    for (uint32_t cp : codepoints)
        emitBoxGlyph(cp == 0x20);   // 스페이스만 빈 글리프
    locaOffsets.push_back(uint32_t(glyf.size()));   // 끝 오프셋(numGlyphs+1 개)

    BE loca;
    for (uint32_t off : locaOffsets) loca.u32(off);   // long 포맷

    // --- hmtx ---
    BE hmtx;
    for (uint16_t g = 0; g < numGlyphs; ++g) {
        hmtx.u16(advance);   // advanceWidth
        hmtx.i16(boxMin);    // lsb
    }

    // --- cmap (format 4, platform 3 enc 1) ---
    // 세그먼트: 연속 구간을 묶는다. 간단화 위해 각 코드포인트를 개별 세그먼트로 두면 커지므로
    //   연속 런으로 묶는다.
    struct Seg { uint16_t start, end; uint16_t startGlyph; };
    std::vector<Seg> segs;
    for (size_t i = 0; i < codepoints.size(); ) {
        uint16_t start = uint16_t(codepoints[i]);
        uint16_t glyphStart = uint16_t(i + 1);   // glyph id (notdef=0 이후)
        size_t j = i;
        while (j + 1 < codepoints.size() &&
               codepoints[j + 1] == codepoints[j] + 1) ++j;
        segs.push_back({start, uint16_t(codepoints[j]), glyphStart});
        i = j + 1;
    }
    // 종단 세그먼트(0xFFFF).
    segs.push_back({0xFFFF, 0xFFFF, 0});
    const uint16_t segCount = uint16_t(segs.size());
    const uint16_t segX2 = uint16_t(segCount * 2);
    uint16_t searchRange = 2; uint16_t entrySelector = 0;
    while (searchRange * 2 <= segX2) { searchRange *= 2; ++entrySelector; }
    const uint16_t rangeShift = uint16_t(segX2 - searchRange);

    BE sub;   // cmap subtable format 4
    sub.u16(4);                 // format
    // length 자리(뒤에서 채움) — 우선 자리표시.
    const size_t lenPos = sub.size();
    sub.u16(0);                 // length placeholder
    sub.u16(0);                 // language
    sub.u16(segX2);
    sub.u16(searchRange);
    sub.u16(entrySelector);
    sub.u16(rangeShift);
    for (auto& s : segs) sub.u16(s.end);    // endCode
    sub.u16(0);                             // reservedPad
    for (auto& s : segs) sub.u16(s.start);  // startCode
    // idDelta: glyph = start + idDelta → idDelta = startGlyph - start.
    for (auto& s : segs) {
        if (s.start == 0xFFFF) sub.u16(1);
        else sub.u16(uint16_t(int16_t(s.startGlyph - s.start)));
    }
    // idRangeOffset: 0 (직접 idDelta 사용).
    for (auto& s : segs) { (void)s; sub.u16(0); }
    // 길이 패치.
    const uint16_t subLen = uint16_t(sub.size());
    sub.buf[lenPos] = uint8_t(subLen >> 8);
    sub.buf[lenPos + 1] = uint8_t(subLen);

    BE cmap;
    cmap.u16(0);    // version
    cmap.u16(1);    // numTables
    cmap.u16(3);    // platformID (Windows)
    cmap.u16(1);    // encodingID (Unicode BMP)
    cmap.u32(12);   // offset to subtable (4 + 8)
    cmap.bytes(sub.buf.data(), sub.buf.size());

    // --- head ---
    BE head;
    head.u32(0x00010000);   // version
    head.u32(0x00010000);   // fontRevision
    head.u32(0);            // checkSumAdjustment (뒤에서 패치 생략 — FreeType 무시)
    head.u32(0x5F0F3CF5);   // magicNumber
    head.u16(0x000B);       // flags
    head.u16(unitsPerEm);
    head.u32(0); head.u32(0);   // created
    head.u32(0); head.u32(0);   // modified
    head.i16(boxMin); head.i16(boxMin);   // xMin,yMin
    head.i16(boxMax); head.i16(boxMax);   // xMax,yMax
    head.u16(0);            // macStyle
    head.u16(8);            // lowestRecPPEM
    head.i16(2);            // fontDirectionHint
    head.i16(1);            // indexToLocFormat (1 = long)
    head.i16(0);            // glyphDataFormat

    // --- hhea ---
    BE hhea;
    hhea.u32(0x00010000);
    hhea.i16(820);          // ascender
    hhea.i16(-204);         // descender
    hhea.i16(0);            // lineGap
    hhea.u16(advance);      // advanceWidthMax
    hhea.i16(boxMin);       // minLeftSideBearing
    hhea.i16(0);            // minRightSideBearing
    hhea.i16(boxMax);       // xMaxExtent
    hhea.i16(1); hhea.i16(0);   // caretSlopeRise/Run
    hhea.i16(0);            // caretOffset
    hhea.i16(0); hhea.i16(0); hhea.i16(0); hhea.i16(0);   // reserved
    hhea.i16(0);            // metricDataFormat
    hhea.u16(numGlyphs);    // numberOfHMetrics

    // --- maxp ---
    BE maxp;
    maxp.u32(0x00010000);
    maxp.u16(numGlyphs);
    maxp.u16(4);            // maxPoints
    maxp.u16(1);            // maxContours
    maxp.u16(0); maxp.u16(0);
    maxp.u16(0); maxp.u16(0);
    maxp.u16(0); maxp.u16(0);
    maxp.u16(0);            // maxZones (0 → FreeType 관대)
    maxp.u16(0); maxp.u16(0); maxp.u16(0); maxp.u16(0); maxp.u16(0);

    // --- name (최소: 빈 name 레코드) ---
    BE name;
    name.u16(0);   // format
    name.u16(0);   // count
    name.u16(6);   // stringOffset

    // --- post (version 3.0 = 글리프 이름 없음) ---
    BE post;
    post.u32(0x00030000);
    post.u32(0);   // italicAngle
    post.i16(0);   // underlinePosition
    post.i16(0);   // underlineThickness
    post.u32(0);   // isFixedPitch
    post.u32(0); post.u32(0); post.u32(0); post.u32(0);

    // --- OS/2 (version 4, 최소 필수 필드) ---
    BE os2;
    os2.u16(4);            // version
    os2.i16(512);          // xAvgCharWidth
    os2.u16(400);          // usWeightClass
    os2.u16(5);            // usWidthClass
    os2.u16(0);            // fsType
    for (int i = 0; i < 10; ++i) os2.i16(0);   // subscript/superscript/strikeout (y*)
    os2.i16(0);            // sFamilyClass
    for (int i = 0; i < 10; ++i) os2.u8(0);    // panose[10]
    os2.u32(0); os2.u32(0); os2.u32(0); os2.u32(0);   // ulUnicodeRange 1..4
    os2.u32(0x54455354);   // achVendID 'TEST'
    os2.u16(0x0040);       // fsSelection (REGULAR)
    os2.u16(0x20);         // usFirstCharIndex
    os2.u16(0xFFFF);       // usLastCharIndex
    os2.i16(820);          // sTypoAscender
    os2.i16(-204);         // sTypoDescender
    os2.i16(0);            // sTypoLineGap
    os2.u16(1024);         // usWinAscent
    os2.u16(204);          // usWinDescent
    os2.u32(0); os2.u32(0);   // ulCodePageRange 1,2
    os2.i16(512);          // sxHeight
    os2.i16(700);          // sCapHeight
    os2.u16(0);            // usDefaultChar
    os2.u16(0x20);         // usBreakChar
    os2.u16(0);            // usMaxContext

    // --- 테이블 디렉터리 조립 ---
    struct Tbl { const char* tag; std::vector<uint8_t> data; };
    std::vector<Tbl> tables = {
        {"OS/2", std::move(os2.buf)},
        {"cmap", std::move(cmap.buf)},
        {"glyf", std::move(glyf.buf)},
        {"head", std::move(head.buf)},
        {"hhea", std::move(hhea.buf)},
        {"hmtx", std::move(hmtx.buf)},
        {"loca", std::move(loca.buf)},
        {"maxp", std::move(maxp.buf)},
        {"name", std::move(name.buf)},
        {"post", std::move(post.buf)},
    };
    // 디렉터리는 태그 알파벳 순 정렬 권장.
    std::sort(tables.begin(), tables.end(),
              [](const Tbl& a, const Tbl& b){ return std::strcmp(a.tag, b.tag) < 0; });

    const uint16_t numTables = uint16_t(tables.size());
    uint16_t sr = 16, es = 0;
    while (sr * 2 <= numTables * 16) { sr *= 2; ++es; }
    const uint16_t rs = uint16_t(numTables * 16 - sr);

    BE out;
    out.u32(0x00010000);   // sfnt version (TrueType)
    out.u16(numTables);
    out.u16(sr); out.u16(es); out.u16(rs);

    // 각 테이블 오프셋 계산: 헤더(12) + 디렉터리(16*n) 이후.
    uint32_t offset = 12 + 16u * numTables;
    struct Rec { uint32_t off, len, sum; };
    std::vector<Rec> recs(tables.size());
    for (size_t i = 0; i < tables.size(); ++i) {
        recs[i].off = offset;
        recs[i].len = uint32_t(tables[i].data.size());
        recs[i].sum = TableChecksum(tables[i].data);
        offset += (recs[i].len + 3) & ~3u;   // 4바이트 정렬
    }
    for (size_t i = 0; i < tables.size(); ++i) {
        out.buf.push_back(uint8_t(tables[i].tag[0]));
        out.buf.push_back(uint8_t(tables[i].tag[1]));
        out.buf.push_back(uint8_t(tables[i].tag[2]));
        out.buf.push_back(uint8_t(tables[i].tag[3]));
        out.u32(recs[i].sum);
        out.u32(recs[i].off);
        out.u32(recs[i].len);
    }
    for (size_t i = 0; i < tables.size(); ++i) {
        out.bytes(tables[i].data.data(), tables[i].data.size());
        while (out.size() % 4) out.u8(0);
    }
    return out.buf;
}

// 편의: ASCII 인쇄 + 지정 한글 음절 목록으로 폰트 생성.
inline std::vector<uint8_t> MakeTestFont(const std::vector<uint32_t>& hangul) {
    std::vector<uint32_t> cps;
    for (uint32_t c = 0x20; c <= 0x7E; ++c) cps.push_back(c);
    for (uint32_t c : hangul) cps.push_back(c);
    return MakeTtf(std::move(cps));
}

} // namespace mye::testfont
