# 이미지 시퀀스 웹 뷰어

`manifest.json`에 기록된 컬러 및 깊이 PNG 시퀀스를 두 캔버스에 표시하고 같은 시간축에서 재생하는 정적 웹 뷰어입니다.

외부 런타임 라이브러리와 번들러는 사용하지 않습니다. TypeScript는 개발 시 ES 모듈을 컴파일하는 데만 사용하며, 실행에는 Node.js와 npm, Python 3, 최신 브라우저가 필요합니다.

## 빠른 실행

저장소 루트에서 개발 도구를 설치하고 TypeScript를 빌드한 뒤 더미 데이터를 생성합니다.

```powershell
npm ci
npm run build
python scripts/generate_dummy_data.py
```

같은 위치에서 정적 HTTP 서버를 실행합니다.

```powershell
python -m http.server 8000
```

브라우저에서 다음 주소를 엽니다.

```text
http://localhost:8000
```

브라우저 보안 정책 때문에 `index.html`을 `file://`로 직접 열지 마세요. 서버를 종료할 때는 실행 중인 터미널에서 `Ctrl+C`를 누릅니다.

브라우저는 `dist/src/app.js`의 컴파일된 ES 모듈을 실행합니다. `dist`는 빌드할 때 생성되며 Git에서 추적하지 않습니다.

## 더미 데이터

기본 명령은 저장소 루트에 320×180 크기의 컬러 30프레임, 깊이 30프레임과 `manifest.json`을 만듭니다. 생성 파일은 Git 추적에서 제외됩니다.

컬러 프레임에는 왼쪽에서 오른쪽으로 이동하는 도형이 있고, 깊이 프레임에는 같은 도형이 가까워지는 것처럼 점점 밝고 크게 표시됩니다. 생성기는 `struct`, `zlib`, `binascii`, `json` 등 Python 표준 라이브러리만 사용합니다.

프레임 수, 크기와 간격을 바꾸려면 다음처럼 실행합니다.

```powershell
python scripts/generate_dummy_data.py --frames 30 --width 640 --height 360 --interval-ms 100
```

## 실제 데이터 사용

뷰어는 저장소 루트의 `manifest.json`을 읽습니다. 컬러와 깊이 파일을 저장소 루트 또는 manifest에서 지정한 상대 경로에 둡니다. 이미지 경로는 `manifest.json`의 위치를 기준으로 해석됩니다.

컬러와 깊이 이미지 URL은 `manifest.json`과 동일한 출처여야 합니다. 상대 경로와 같은 출처의 절대 URL은 허용하지만 프로토콜, 호스트 또는 포트가 다른 교차 출처 URL은 CORS 헤더와 관계없이 manifest 검증 단계에서 거부합니다.

입력 형식은 다음과 같습니다.

```json
{
  "frame_count": 120,
  "interval_ms": 100,
  "depth_near_cm": 0,
  "depth_far_cm": 5000,
  "frames": [
    {
      "index": 0,
      "color": "color_000000.png",
      "depth": "depth_000000.png",
      "time_ms": 0
    }
  ]
}
```

실제 파일에서는 `frames` 배열 길이가 `frame_count`와 같아야 합니다. `index`는 0부터 빠짐없이 증가하고 `time_ms`는 0 이상이며 이전 프레임보다 작아질 수 없습니다. `interval_ms`는 0보다 커야 하고 `depth_near_cm`은 `depth_far_cm`보다 작아야 합니다.

## 조작

- 재생 버튼은 현재 위치부터 시퀀스를 재생하고 같은 버튼으로 일시정지합니다.
- 마지막 프레임에서 재생하면 처음부터 다시 시작합니다.
- 처음으로, 이전, 다음 버튼은 재생을 멈추고 해당 프레임으로 이동합니다.
- 프레임 탐색 슬라이더는 임의의 프레임으로 이동합니다.
- 재생 속도는 0.5x, 1x, 2x 중에서 선택합니다.
- 깊이 컬러맵 버튼은 회색조 원본과 가까울수록 따뜻한 색인 모드를 전환합니다.
- 상단에는 전체 이미지 로딩 진행률이 표시됩니다. 로딩이 끝나기 전에는 조작이 비활성화됩니다.

컬러맵은 깊이 PNG의 RGB 평균 밝기를 사용합니다. 픽셀값과 실제 센티미터 사이의 별도 인코딩 규칙이 입력 형식에 없으므로 밝은 값을 가까운 깊이로 해석합니다.

## 자동 검사

TypeScript 빌드와 컴파일된 JavaScript 검사는 Node.js 내장 테스트 러너를 사용합니다.

```powershell
npm test
```

더미 생성기 검사는 Python 내장 `unittest`를 사용합니다.

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

## 오류 해결

manifest를 가져오지 못했다는 메시지가 보이면 다음을 확인합니다.

- 주소가 `http://localhost:8000`인지 확인합니다.
- 서버 명령을 저장소 루트에서 실행했는지 확인합니다.
- `manifest.json`이 저장소 루트에 있는지 확인합니다.

manifest 내용이 잘못되었다는 메시지가 보이면 화면에 표시된 필드 이름과 위 입력 계약을 비교합니다.

프레임 이미지를 불러오지 못했다는 메시지가 보이면 안내된 파일명이 실제로 존재하는지, 대소문자와 6자리 번호가 manifest의 경로와 같은지 확인합니다.

## 파일 구성

- `index.html`: 두 캔버스와 재생 조작 화면
- `styles.css`: 데스크톱과 모바일 반응형 표현
- `src/manifest.ts`: 입력 검증과 이미지 경로 해석
- `src/preload.ts`: 전체 이미지 선로딩과 진행률
- `src/player.ts`: 프레임 탐색과 재생 시계
- `src/depth.ts`: 깊이 컬러맵 변환
- `src/app.ts`: 데이터, 캔버스와 화면 조작 연결
- `tsconfig.json`: 제품 코드와 Node 테스트의 단일 TypeScript 컴파일 설정
- `package-lock.json`: 개발 도구 버전과 무결성 잠금
- `dist/`: 빌드로 생성되는 비추적 JavaScript 출력
- `scripts/generate_dummy_data.py`: 표준 라이브러리 더미 데이터 생성기
- `tests/`: TypeScript와 Python 자동 검사

## Unreal Stage 5 데이터 캡처

이 저장소에는 정적 이미지 시퀀스 웹 뷰어와 Unreal Engine 5.5.4 과제 경로인 `ShipAutonomySim`이 함께 있습니다. `/Game/Maps/MainLevel`에서 Play하면 코스 생성, 자율주행과 Stage 5 컬러와 깊이 캡처가 입력 없이 자동으로 시작됩니다.

캡처 결과는 실행마다 다음 형식의 고유 디렉터리에 저장됩니다.

```text
ShipAutonomySim/Saved/ShipCaptures/YYYYMMDDTHHMMSSmmmZ_GUIDDIGITS/
├── color_000000.png
├── depth_000000.png
├── ...
└── manifest.json
```

컬러와 깊이는 같은 6자리 index를 공유하는 512×512 PNG pair입니다. `manifest.json`은 `capture_resolution`, `depth_far_cm`, `depth_near_cm`, `frame_count`, `frames`, `interval_ms`, `result`, `wall_slide_cm`의 정확한 여덟 최상위 필드를 가집니다. `frames`의 각 항목에는 `color`, `depth`, `index`, `time_ms`만 들어갑니다. 웹 뷰어는 이 manifest의 공통 재생 필드를 그대로 읽으므로 제품 소스를 수정할 필요가 없습니다.

실제 run을 확인할 때는 저장소 루트에서 `<run-directory>`를 선택한 디렉터리명으로 바꾼 뒤 다음 PowerShell을 실행합니다. 같은 이름의 루트 파일이 하나라도 있으면 아무것도 덮어쓰지 않고 중단합니다.

```powershell
$Run = 'ShipAutonomySim\Saved\ShipCaptures\<run-directory>'
$SourceFiles = @(
    Get-Item -LiteralPath (Join-Path $Run 'manifest.json')
    Get-ChildItem -LiteralPath $Run -File -Filter 'color_*.png'
    Get-ChildItem -LiteralPath $Run -File -Filter 'depth_*.png'
)
if (@($SourceFiles | Where-Object Name -Like 'color_*.png').Count -eq 0 -or
    @($SourceFiles | Where-Object Name -Like 'depth_*.png').Count -eq 0) {
    throw '선택한 run에 컬러 또는 깊이 프레임이 없습니다.'
}
$Destination = (Resolve-Path '.').Path
$Collisions = @($SourceFiles | Where-Object {
    Test-Path -LiteralPath (Join-Path $Destination $_.Name)
})
if ($Collisions.Count -ne 0) {
    throw "루트에 같은 이름의 파일이 있습니다: $($Collisions.Name -join ', ')"
}
$CopiedPaths = @($SourceFiles | ForEach-Object {
    $Target = Join-Path $Destination $_.Name
    Copy-Item -LiteralPath $_.FullName -Destination $Target
    $Target
})
python -m http.server 8000
```

브라우저에서 `http://localhost:8000`을 열어 재생, 프레임 이동, 컬러와 깊이의 동기 index와 시간, 깊이 원본과 컬러맵 전환을 확인합니다. 확인이 끝나면 서버 터미널에서 `Ctrl+C`를 누르고, 같은 PowerShell 세션에서 이번에 복사한 파일만 정리합니다. `Saved`의 원본 run은 삭제하지 않습니다.

```powershell
$CopiedPaths | ForEach-Object {
    Remove-Item -LiteralPath $_
}
```
