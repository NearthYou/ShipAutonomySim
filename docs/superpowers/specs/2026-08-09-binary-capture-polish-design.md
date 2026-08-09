# 단일 바이너리 캡처와 제출 완성도 개선 설계

## 목표

과제 2c 가산점인 단일 바이너리 이미지 시퀀스를 구현하고, 실제 데이터로 깊이 범위를 보정하며, 자동화 측정의 `-500cm` 첫 케이스 이상값을 제거한다. 웹 뷰어에는 데이터 출처와 깊이 범위, 순차 및 무작위 접근 성능을 명확히 표시하고 최종 한국어 보고서에는 측정 결과와 한계를 읽기 쉬운 본문으로 정리한다.

사용자는 현실적인 항목만 판단해 바로 반영하도록 위임했다. 이 문서는 그 위임에 따른 승인 설계이며 별도 중간 승인 없이 구현 계획으로 진행한다.

## 범위

포함한다.

- UE가 정상 캡처 종료 시 `sequence.siv` 한 파일을 추가 생성한다.
- 웹 뷰어가 URL의 `bundle` query로 `.siv`를 읽고 기존 manifest 방식도 유지한다.
- 바이너리 모드에서 순차 및 고정 seed 무작위 이미지 접근 평균을 측정한다.
- 깊이 far를 실제 캡처 분포에 근거해 2500cm로 조정한다.
- Stage 5 실제 월드 자동화의 첫 케이스도 명시적인 fresh world에서 관찰한다.
- 웹 화면에 source, frame count, interval, depth range, benchmark 결과를 표시하고 정보 구조와 반응형 레이아웃을 다듬는다.
- `REPORT.md`를 제출용 한국어 보고서로 작성하고 `REPORT_DRAFT.md`는 근거 자료로 남긴다.

포함하지 않는다.

- 캡처 중 GPU readback, PNG encode, 파일 저장의 비동기화
- 전체 프레임 선로딩 구조의 대규모 재작성
- PCG, Niagara, 외부 asset, 서드파티 압축 라이브러리
- 3인칭 영상 stream, 별도 SceneCapture, 새 gameplay
- 기존 PNG와 manifest 출력 제거
- Water, MainLevel, Config 수정

## 확인된 데이터와 판단

실제 성공 run 178프레임, 컬러와 깊이 PNG 합계 42,973,748 bytes를 분석했다.

- PNG 각각에 zlib을 다시 적용한 크기는 42,852,589 bytes로 0.282% 감소했다.
- 전체 PNG를 한 덩어리로 zlib 처리해도 42,835,349 bytes로 0.322% 감소했다.
- 이득이 작고 브라우저 압축 해제 경로와 오류 경계만 늘어나므로 이중 압축은 하지 않는다.
- bundle payload는 UE ImageWrapper가 만든 PNG compressed bitstream을 그대로 사용한다. 따라서 한 파일 안의 이미지 데이터는 이미 무손실 압축된 상태다.

같은 run의 깊이 PNG 46,661,632 pixels를 분석했다.

- 0은 far clipping 또는 invalid이며 전체의 46.549%다.
- 0을 제외한 유효 픽셀의 92.656%가 10m 안쪽, 96.857%가 20m 안쪽, 97.818%가 25m 안쪽이다.
- 기존 5000cm는 G8 한 단계가 약 19.6cm다.
- 2500cm는 코스 길이 20m에 5m 여유를 두고 유효 픽셀의 97.8%를 보존하며 한 단계가 약 9.8cm다.
- near를 255로 두면 가까운 장애물이 밝게 강조되고 기존 heat colormap에서 높은 값이 따뜻한 색으로 연결된다. 위험 요소를 먼저 식별하는 뷰어 목적과 맞는다.

`-500cm` capture-off의 약 493cm 값은 다른 모든 run의 약 162cm와 달랐다. 반복 로그에서 오직 첫 case만 같은 형태로 벗어났고 이후 fresh world case와 capture-on `-500cm`는 정상 곡선에 맞았다. 최초 게임 월드는 Automation latent command가 붙기 전에 이미 주행을 시작하므로 벽 최근접 구간 관찰을 놓치는 측정 결함으로 본다. 제품 주행 수식은 바꾸지 않고 첫 case도 같은 option으로 한 번 fresh reload한 뒤 측정한다.

## 단일 바이너리 형식

파일명은 `sequence.siv`, version은 1이다.

파일 배치는 다음과 같다.

| 구간 | 형식 | 설명 |
| --- | --- | --- |
| magic | ASCII 8 bytes | `SIVPACK1` |
| header length | uint32 little-endian | UTF-8 header JSON byte 수 |
| header | UTF-8 JSON | format, version, 원본 manifest JSON, asset index |
| payload | bytes | color와 depth PNG payload 연속 저장 |

asset index 항목은 `path`, `offset`, `length`, `media_type`을 가진다. offset은 payload 시작 기준이며 color, depth 순서로 frame index가 증가한다. 경로는 원본 manifest의 leaf name과 정확히 일치하고 중복을 허용하지 않는다.

UE는 finalize 시 이미 게시된 PNG를 읽어 bundle temp 파일을 만들고, 전체 크기와 index를 검증한 뒤 final 파일로 rename한다. 캡처 중에는 bundle을 만들지 않아 자율주행 timing 측정에 추가 영향을 주지 않는다. 기존 PNG와 manifest는 과제 필수 경로이므로 그대로 남긴다. bundle은 파생 가산점 산출물이며 bundle 생성 실패가 기존 PNG run을 삭제하지 않는다. 실패는 명시적으로 로그에 남긴다.

## 웹 로딩과 성능 측정

기본 URL은 기존처럼 `manifest.json`을 읽는다. `?bundle=sequence.siv`가 있으면 한 번의 fetch로 bundle을 읽는다.

bundle parser는 다음을 검증한다.

- magic과 version
- header length와 파일 경계
- JSON 구조와 원본 manifest 계약
- asset 경로 중복, 정수 offset과 length, payload 범위
- manifest에 있는 모든 color와 depth path의 index 존재
- 각 payload의 PNG signature

검증 뒤 각 asset을 Blob URL로 만들고 기존 typed manifest와 preload, player 경로를 그대로 사용한다. 새 런타임 의존성은 추가하지 않는다.

성능 측정은 bundle index에서 한 이미지의 compressed PNG bytes를 복사해 접근하는 시간을 대상으로 한다. image decode와 canvas render는 제외하고 UI와 보고서에 이 정의를 함께 표시한다. 순차 순서는 index 순서이며 무작위 순서는 고정 seed Fisher-Yates 순서다. 전체 asset을 한 번 warm-up한 후 세 pass 평균을 계산하고 dead-code 제거를 막기 위한 checksum을 누적한다. 결과는 평균 ms/image와 sample count로 표시한다.

## 웹 시각 개선

- 상단 제목을 선박 자율주행 캡처 뷰어로 구체화한다.
- 현재 source를 `SIV BINARY` 또는 `MANIFEST + PNG` badge로 표시한다.
- frame count, capture interval, depth near/far를 summary strip에 표시한다.
- 깊이 panel에 near 255, far 0 방향을 보여주는 작은 scale legend를 추가한다.
- 바이너리 mode에서 benchmark button과 순차, 무작위 결과 card를 표시한다.
- 기존 재생, 탐색, 속도, grayscale/colormap, 오류 상태와 접근성 계약은 유지한다.

UE 장면 자체의 장식 변경은 하지 않는다. 현재 제출 핵심은 카메라 데이터와 자율주행이며 ship mesh나 Niagara를 바꾸면 collision과 capture 시야, 성능 검증 범위가 함께 커진다. 시각 완성도 개선은 평가자가 직접 사용하는 웹 뷰어에 집중한다.

## 테스트와 검증

TDD 순서는 다음과 같다.

1. UE pure test에서 hand-built 두 asset의 exact magic, header, offsets, payload와 malformed 입력 실패를 먼저 RED로 만든다.
2. 기존 actual-world test에 첫 `-500cm`가 정상 범위에 들어오는 검증을 추가해 RED를 확인한다.
3. 웹 unit test에서 bundle parser, 경계 검증, deterministic random order, benchmark 집계를 RED로 만든다.
4. 각 제품 코드를 최소 구현해 표적 GREEN을 만든다.
5. UE Build, ShipCapture unit, Navigation actual-world, 전체 relevant Automation을 실행한다.
6. `-500cm` actual-world sweep을 독립 실행 세 번 반복해 min wall distance 재현성을 확인한다.
7. 새 2500cm run의 depth PNG histogram을 다시 계산한다.
8. 실제 Chromium에서 manifest와 bundle 두 mode, benchmark, desktop과 mobile layout, console을 확인한다.
9. npm test, Python tests, compileall, generated JS syntax, runtime dependency 0, git diff check를 확인한다.

## 보고서 반영

`REPORT.md` 본문은 다음 순서로 작성한다.

1. 구현 결과 요약
2. 클래스 책임과 데이터 흐름
3. 직접 이동 모델과 자율주행 판단
4. 컬러, 깊이 캡처와 단일 바이너리 형식
5. 11개 wall slide와 capture-on/off 수치
6. 순차 및 무작위 bundle 접근 성능
7. 깊이 범위의 실제 histogram 근거
8. 동기 캡처 비용과 비동기화를 미룬 이유
9. 빌드, 실행, 웹 재생 절차
10. 확인한 한계와 후속 개선

SHA 대조, 근거 등급, 긴 로그 경로는 부록으로 보내고 본문은 무엇을 만들었고 왜 그렇게 판단했으며 어떤 결과가 나왔는지가 먼저 읽히게 한다.
