// stb_vorbis_impl.c — 벤더링된 stb_vorbis 컴파일 단위 (M3-A, 오디오 임포터 OGG 디코드).
//
// AudioImporter::DecodeOgg 가 stb_vorbis_decode_memory 로 OGG Vorbis 를 S16 인터리브드
// PCM 으로 디코드한다. 파일 I/O·푸시데이터 API 는 쓰지 않으므로 꺼서 CRT 의존을 줄인다.
//   - STB_VORBIS_NO_STDIO       : fopen 등 파일 경로 API 제거(메모리 디코드만 사용).
//   - STB_VORBIS_NO_PUSHDATA_API: 스트리밍 푸시 API 제거(M3-A는 원샷 메모리 디코드).
#define STB_VORBIS_NO_STDIO 1
#define STB_VORBIS_NO_PUSHDATA_API 1

// stb_vorbis.c 는 <alloca.h> 를 유닉스에서만 include 하고, MSVC 에서는 <malloc.h> 를
// 자동으로 끌어온다(내부 처리). 별도 정의 불필요.
#include "stb_vorbis.c"
