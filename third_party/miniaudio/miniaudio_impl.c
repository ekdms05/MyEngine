// miniaudio_impl.c — miniaudio 단일 헤더의 유일한 구현 TU (M3-B, docs/06 §4 오디오)
//
// 규약(third_party/CMakeLists.txt stb_vorbis 와 동일): 단일 헤더 라이브러리의 구현은 정확히
//   한 TU 에서만 MINIAUDIO_IMPLEMENTATION 을 켠다. 여기가 그 TU 다. 다른 어떤 파일도 이 매크로를
//   정의하면 안 된다(중복 심볼). 소비자는 miniaudio.h 를 헤더로만 include 한다.
//
// 비활성화한 서브시스템(엔진이 쓰지 않아 빌드/의존을 줄인다):
//   - 디코더는 우리 04 AudioImporter(WAV 자체 파서 + stb_vorbis)가 담당하므로 ma 내장 디코더 불필요.
//     단, ma_engine/ma_sound 편의 API 는 디코더가 있으면 편하나 우리는 raw PCM 데이터소스만 쓰므로
//     전 디코더를 끈다(코드 크기·경고 축소).
//   - 인코더·리소스 매니저·제너레이션 노드 등 미사용 기능도 컴파일 아웃한다.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
