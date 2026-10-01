# 04. 에셋 파이프라인과 직렬화

`mye_asset`은 파일 접근·에셋 식별·임포트·로딩·재임포트를, `mye_reflect`는 공용 타입 메타·JSON 직렬화를 담당한다. 소스는 [asset](../engine/asset)과 [reflect](../engine/reflect)에 있다. 앱의 배선 범위는 [현재 구조](13-architecture-and-features.md)에 별도로 기록한다.

## 파일·식별자·수명

| 구성 | 현재 역할 | 소스 |
|---|---|---|
| VFS·IFileSystem | 느슨한 파일·pak 마운트와 가상 경로 | [FileSystem.h](../engine/asset/include/mye/asset/FileSystem.h) |
| AssetGuid·AssetMeta | 128비트 식별자·.meta 데이터 | [AssetGuid.h](../engine/asset/include/mye/asset/AssetGuid.h), [AssetMeta.h](../engine/asset/include/mye/asset/AssetMeta.h) |
| AssetDatabase | GUID·경로·메타의 인메모리 조회 | [AssetDatabase.h](../engine/asset/include/mye/asset/AssetDatabase.h) |
| AssetHandle | 참조 카운트·슬롯 수명 | [AssetHandle.h](../engine/asset/include/mye/asset/AssetHandle.h) |
| AssetManager | 임포터/로더 등록·동기/비동기 로드·교체 | [AssetManager.h](../engine/asset/include/mye/asset/AssetManager.h) |

핸들은 RAII로 유지하고 비소유 AssetManager와 GPU 디바이스보다 오래 살아남지 않게 한다. 앱마다 새 식별·직렬화 규칙을 만들기 전에 기존 GUID/.meta/VFS 경로를 재사용한다. 현재 MyGame에는 경로 기반 GUID·PNG 선로딩이 남아 있어 안정 GUID/임포트 설정 통합은 개선 항목이다.

## 로딩 경로

```text
VFS 읽기 → 임포터 CPU 파싱 → AssetManager finalize → AssetHandle
→ 렌더 텍스처/메시 또는 오디오 소비자
```

비동기 경로는 IO·CPU 파싱·메인 스레드 finalize를 나눈다. GPU 자원 생성과 교체는 디바이스의 주 스레드 계약을 지킨다. 실패 시 이전 핸들/자원을 성공처럼 폐기하지 않고 기존 Expected/Error 경계로 전달한다.

| 형식 | 구현·소비 |
|---|---|
| PNG | TextureImporter → 텍스처 |
| glTF/GLB | MeshImporter/cgltf → 정적 메시 |
| Aseprite·그리드 시트 | SpriteImporter → SpriteSheet·프레임·클립·아틀라스 |
| WAV/OGG | AudioImporter → AudioClip → AudioEngine |
| pak | PakFile → VFS 마운트 |

[AtlasPacker](../engine/asset/include/mye/asset/AtlasPacker.h)는 시트의 사각형 배치를 다룬다. 피벗·방향·프레임·PPU와 투명 영역은 원본 콘텐츠 계약이다. 이미지 임포트 성공을 정식 픽셀 아트 검수 완료로 해석하지 않는다. 스켈레탈 glTF·원격 CDN·서명된 패치·모든 셰이더/머티리얼 에셋 포맷은 현재 지원 목록에 넣지 않는다.

## 재임포트

[FileWatcher](../engine/asset/include/mye/asset/FileWatcher.h)·AssetDatabase·AssetManager가 파일 변경과 재임포트 교체를 처리한다. 기존 핸들 소비자는 교체된 데이터를 읽는 경로를 사용한다. watcher 구현·테스트와 제품 앱의 watcher 시작·DB 연결은 별개다. 앱 전체 자동 핫 리로드는 실제 호출 경로가 확인된 범위로만 설명한다.

## 리플렉션·JSON

[TypeInfo](../engine/reflect/include/mye/refl/TypeInfo.h)·TypeBuilder·TypeRegistry는 타입·필드·속성을 등록한다. [PropertyPath](../engine/reflect/include/mye/refl/PropertyPath.h)는 필드 접근, [JsonArchive](../engine/reflect/include/mye/ser/JsonArchive.h)는 값의 JSON 왕복에 사용한다. 에디터 Inspector·Undo·씬 데이터의 공용 기반이다.

코어 JSON은 정수 64비트를 보존한다. 파일 파싱·버전·범위 검사는 실제 저장/에셋 경계에서 수행한다. 리플렉션 등록만으로 모든 Lua 바인딩·스키마 이관·참조 검사가 자동 생성되는 것은 아니다. 현재 script/DDC 바인딩은 [05](05-scripting-plugins.md)를 따른다.

## 패키징과 검증

[paktool](../apps/paktool)과 [패키징 스크립트](../tools/package/package.ps1)를 사용한다. 게임 실행에 필요한 PNG·글꼴·Lua·설정·플랫폼 런타임을 실제 새 환경에서 확인해야 한다. pak 생성 성공과 배포본 완성은 구분한다.

에셋·시트·오디오·pak 왕복은 `mye_tests`, GPU finalize는 `asset_smoke`, 실제 재임포트 소비는 관련 샘플에서 검사한다. 앱 공통 에셋 부팅·watcher·import 설정·안정 참조 연결과 배포 글꼴은 [14](14-development-priorities.md)의 완료 조건으로 관리한다.

## MyEditor 인덱스와 애니메이션 파일

AssetDatabase::ScanDirectory는 assets/를 재귀 탐색해 .meta를 파싱하고 GUID↔assets:// 경로 인덱스를 후보 전체 검증 뒤 교체한다. 등록된 소스 또는 .anim에 .meta가 없으면 기존 AssetMeta·원자적 JSON 쓰기로 생성한다. 이미 로드한 소스는 CachedGuid를 재사용한다. 중복/0 GUID와 잘못된 메타·링크를 거부한다. 스캔 실패 시 이전 인덱스는 유지되지만 앞서 생성한 누락 sidecar는 남을 수 있다. 동기/비동기 AssetManager 로드는 .meta GUID를 보존한다.

MyEditor는 PNG importer·retained TextureHandle·하이브리드 texture resolver를 연결했다. 수동 에셋 새로 고침은 DB와 로더 자원을 다시 구성한다. 자동 watcher, .meta 임포트 설정 적용과 MyGame의 경로 GUID 수렴은 미완료다.

[AnimationAsset](../engine/asset/include/mye/asset/AnimationAsset.h)은 기존 SpriteSheet·AnimationClipData의 값 데이터다. `.anim` version=1은 texture GUID, width/height, name/loop/direction, frames(x/y/w/h/pivotX/pivotY), timeline(frame/seconds), events(frame/name/text/value)를 저장한다. direction은 정방향 0·역방향 1·왕복 2다. 피벗은 프레임 내부 기준 픽셀 좌표이며 UV는 영역과 시트 크기에서 계산한다.

이미지 크기 1~32768, 시트/타임라인 1~4096, 프레임 영역·시간 0.001~60초·인덱스·유한 피벗/이벤트 수치·이벤트 위치를 검증한다. 타임라인과 시간 개수는 같아야 한다. 실제 PNG 크기가 다르면 런타임에 연결하지 않는다. 읽기/저장은 기존 64 MiB JsonFile 경계를 사용한다. 새 범용 에셋 직렬화 프레임워크나 별도 이미지 인코더는 만들지 않았다.
