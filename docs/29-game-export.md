# 29. 게임 내보내기

MyEditor의 명령줄로 Windows용 프로젝트 플레이어 폴더를 만들 수 있다. `Play.cmd`를 실행하면 내보낸 프로젝트가 열린다. 0.5.0의 첫 내보내기 경로이며 에디터 메뉴·설치 프로그램·서버 배포는 아직 제공하지 않는다.

```powershell
MyEditor.exe --project "D:/Game/project.myeproj" --export-game "D:/Games/MyGame" --runtime "D:/MyEngine-0.5.0-windows-x64"
```

`--runtime`을 생략하면 MyEditor.exe 폴더를 사용한다. 공식 배포 폴더처럼 같은 엔진 버전의 Windows x64 Release `release-manifest.json`, MyGame.exe, 폰트와 라이선스 파일이 있어야 한다. Debug 소스 빌드 폴더만으로는 내보낼 수 없다. 서로 다른 버전의 플레이어를 넣으면 새 입력·맵 계약을 처리하지 못하므로 버전 불일치는 거부한다.

출력은 기존 부모 아래의 **새 폴더**여야 한다. 기존 폴더나 원본 프로젝트/런타임 안쪽에는 만들지 않는다. 새 임시 폴더에 복사하고 모두 성공하면 이름을 바꾸므로 실패한 결과를 완성 게임으로 게시하지 않는다. 저장하지 않은 에디터 초안은 포함되지 않는다. 씬·프로젝트·에셋을 저장한 뒤 실행한다.

| 포함 | 이유 |
|---|---|
| MyGame.exe, 기본 한국어 폰트, licenses/, LICENSE | 플레이와 필수 고지 |
| assets/의 .scene/.anim/.animstate/.ui/.lua, PNG/BMP/TGA/JPG/JPEG, GLB/glTF, WAV/OGG/MP3/FLAC와 해당 .meta | 기존 GUID/VFS 로딩 경로 보존 |
| game.myeproj의 version/name/mainScene/inputMap/onlineCombat | 런타임에서 사용하는 명시적 설정 |
| 프로젝트 licenses/와 LICENSE → game-licenses/ | 프로젝트 에셋의 배포 고지 보존 |
| Play.cmd, README.txt | 독립 폴더 실행과 필수 환경 안내 |

프로젝트 루트의 문서·로그·계정 파일·.myeditor는 복사하지 않는다. assets 안의 지원하지 않는 확장자는 생략하며 링크와 숨김 이름은 거부한다. 런타임 에셋 폴더에 비밀번호를 보관하지 않는다. Lua가 동적으로 참조하는 에셋을 보존하기 위해 지원 형식은 전부 복사한다. 미사용 의존성 삭제나 임의 JSON/바이너리 게임 데이터의 포함 규칙은 다음 작업이다.

내보내기는 모든 씬의 에셋 참조를 검증하는 빌드 컴파일러가 아니다. 실행 시 지정 PNG/메시/모션 누락은 기존 MyGame 오류 경계에서 실패한다. 원본 프로젝트가 없는 다른 폴더에서 시작·포털·HUD·입력과 해당 실패를 재검증한다. 권리 확인과 에셋 고지 내용의 완전성은 프로젝트 제작자가 확인한다.

Windows 10/11 x64, DirectX 11 장치/드라이버와 Microsoft Visual C++ v14 x64 Redistributable이 필요하다. 온라인 서버·개인 로그인 파일·저장 데이터·배포 키는 별도로 관리한다. 클린 PC·서명·설치/업데이트·압축 pak·서버 exporter·Release 소비자 MCP는 별도 완료 조건이다.

```powershell
./tools/verify-game-export.ps1 -Runtime "D:/MyEngine-0.5.0-windows-x64"
```

실제 export CLI, 원본과 분리한 실행 위치, 파일/픽셀 비교, 기존 출력 거부를 검사한다. 실행 가능한 게임 폴더 검증과 실제 출시 환경 검증을 구분한다.
