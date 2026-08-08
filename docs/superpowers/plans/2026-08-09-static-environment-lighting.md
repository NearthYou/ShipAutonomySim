# 영구 환경 조명 구현 계획

> 구현자는 이 계획의 한 개 Task를 순서대로 실행하고 체크박스로 추적한다. 사용자 수동 확인은 이미 끝났으므로 승인 질문, 실행 선택 질문, 추가 에이전트 또는 별도 reviewer를 만들지 않는다.

Goal: `/Game/Maps/MainLevel`에 UE 5.5.4 내장 `DirectionalLight`, `SkyLight`, `SkyAtmosphere`를 각각 한 개 영구 배치해 Lit 상태의 맑고 중립적인 낮 장면을 만들고, 기존 Water와 Stage 4 동작을 보존한다.

Architecture: MainLevel이 세 환경 actor를 직접 소유한다. 배치와 검증은 Editor scripting API를 사용하되 Python binding 이름은 구현 시작 시 reflection으로 먼저 확인하고, actor 부재와 검은 Lit 렌더를 RED로 남긴 뒤 세 actor와 non-black Lit 렌더를 GREEN으로 확인할 때만 레벨을 한 번 저장한다. C++, Config, 플러그인 설정과 외부 asset은 바꾸지 않는다.

Tech stack: Unreal Engine 5.5.4, MainLevel binary map, Python Editor Script Plugin의 세션 한정 활성화, PowerShell, Build.bat, Unreal Automation.

## 현재 기준과 로컬 UE 5.5.4 근거

- 계획 조사 gate는 branch `feat/ship-autonomy-navigation`, HEAD `23f0b75a014248fb973b01b7615d3da6429fe42e`, clean 상태에서 통과했다. 구현 gate는 이 문서 커밋의 SHA를 PM이 `LIGHTING_PLAN_SHA`로 전달하고 현재 HEAD와 일치시킨다.
- 현재 MainLevel blob `e99163b343dd19db0c595f453b46d3fb0871b985`는 `adac6870f6a819c981f9010ecacc2f043cef4f5d`의 map blob과 같다. 바이너리 inventory에는 `WaterBodyOcean_1` 한 개와 `WaterZone_0` 한 개가 있고 세 조명 actor는 없다.
- 관련 map 이력은 `2cd743364e8ecaaf4455072b1acc948c27f704a9`의 최초 Water 레벨, `f847bce2746e3763932694ed7475738383a1bdf2`의 코스 영역 Water 복원, `0472a66f61f7b886d1c1070caffdb3e03b9d1cdc`의 WaterInfo mesh 직렬화 정합, `adac6870f6a819c981f9010ecacc2f043cef4f5d`의 Ocean spline hole 축소다. 이후 Stage 4 커밋은 MainLevel을 바꾸지 않았다.
- `ShipAutonomySim/SETUP.md`도 현재 영구 actor를 Water Body Ocean 한 개와 Water Zone 한 개로 기록한다. 구현은 이 두 actor, WaterInfo mesh, Ocean spline과 코스 영역을 이동, 삭제, 재생성 또는 재설정하지 않는다.
- 로컬 `Engine/Source/Editor/UnrealEd/Public/Subsystems/EditorActorSubsystem.h`에서 `GetAllLevelActors()`와 `SpawnActorFromClass(TSubclassOf<AActor>, FVector, FRotator, bool bTransient=false)`를 확인했다.
- 로컬 `Engine/Source/Editor/LevelEditor/Public/LevelEditorSubsystem.h`에서 `SaveCurrentLevel()`을 확인했다. 배치 script는 이 함수를 호출하지 않고 외부 GREEN gate 뒤의 별도 한 줄만 저장을 호출한다.
- 로컬 `Engine/Source/Runtime/Engine/Classes/Components/SceneComponent.h`에서 `SetMobility(EComponentMobility::Type)`, `LightComponent.h`에서 `SetIntensity(float)`, `SetLightColor(FLinearColor, bool)`, `SetUseTemperature(bool)`를 확인했다.
- 로컬 `DirectionalLightComponent.h`에서 `SetAtmosphereSunLight(bool)`을 확인했다. `DirectionalLightComponent.cpp`의 UE 5.5.4 기본값은 intensity `10`, atmosphere sun light `true`, source angle `0.5357`이다.
- 로컬 `SkyLightComponent.h`에서 `SourceType`, `bRealTimeCapture`, `SetRealTimeCapture(bool)`, `RecaptureSky()`를 확인했다. `SkyLightComponent.cpp`의 기본값은 intensity `1`, mobility `Stationary`, real-time capture `false`이고 `SLS_CapturedScene`은 enum의 첫 값이다.
- 로컬 `SkyAtmosphereComponent.h`에서 `TransformMode`와 `PlanetTopAtAbsoluteWorldOrigin`을 확인했다. `SkyAtmosphereComponent.cpp`의 Earth-like 기본값은 ground radius `6360 km`, atmosphere height `60 km`이며 별도 texture가 필요 없다.
- Python surface의 snake_case property 이름과 enum 노출은 저장소 계약이 아니다. 아래 reflection gate에서 class, method, component와 editor property를 전부 먼저 읽을 수 있어야 하며 하나라도 없으면 actor를 spawn하지 않는다.

## 고정 actor 값

| actor label | transform | component 값 |
| --- | --- | --- |
| `DirectionalLight` | location `(0, 0, 1000)`, rotation `(pitch=-45, yaw=-30, roll=0)`, scale `(1, 1, 1)` | mobility `Movable`, intensity `10.0`, light color `(255,255,255,255)`, use temperature `false`, atmosphere sun light `true` |
| `SkyLight` | location `(0, 0, 1000)`, rotation `(0, 0, 0)`, scale `(1, 1, 1)` | mobility `Movable`, source type `SLS_CapturedScene`, intensity `1.0`, real-time capture `false`, cubemap `None`; 세 actor 구성 뒤 `RecaptureSky()` 한 번 |
| `SkyAtmosphere` | location `(0, 0, 0)`, rotation `(0, 0, 0)`, scale `(1, 1, 1)` | transform mode `PlanetTopAtAbsoluteWorldOrigin`; UE 5.5.4 Earth-like scattering 기본값 유지 |

세 actor에는 tick, Blueprint, Sequencer, 시간 변화 또는 animation을 추가하지 않는다. `PostProcessVolume`, `ExponentialHeightFog`, `VolumetricCloud`, 외부 cubemap, Starter Content, Fab와 Marketplace asset을 추가하지 않는다.

## Global Constraints

- 구현 branch는 `feat/ship-autonomy-navigation` 하나이며 branch 전환, push, merge와 PR을 하지 않는다.
- 제품 변경 파일은 `ShipAutonomySim/Content/Maps/MainLevel.umap` 하나다. Config, Source, uproject, 문서와 다른 asset은 수정하지 않는다.
- UnrealEditor 계열 process가 0개일 때만 통제된 editor를 열어 MainLevel 수정을 시작한다. 기존 process를 kill하거나 닫지 않는다.
- Unreal이 MainLevel 외 tracked file을 만들면 즉시 No-Go다. reset, restore, checkout, clean, 삭제 또는 자동 복구를 하지 않는다.
- 배치 script는 기존 조명 actor가 하나라도 있으면 중복 실행으로 실패하고 아무것도 spawn하지 않는다.
- reflection, actor count, protected actor inventory, Lit GREEN 또는 사전 범위 gate가 실패하면 MainLevel을 저장하지 않고 editor를 저장 없이 닫는다.
- 저장 뒤 검증이 실패하면 commit하지 않고 MainLevel diff를 그대로 보존해 PM에 No-Go를 보고한다. 자동 복원은 하지 않는다.
- Stage 4 전체 unit suite는 source를 건드리지 않는 map-only 변경에 비례하지 않으므로 실행하지 않는다. Build.bat와 MainLevel을 실제 사용하는 `ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep` 한 test만 최소 회귀로 실행한다.
- 사람이 반복 확인하지 않는다. GREEN PNG는 구현 agent가 로컬 이미지로 직접 검사한다.

---

### Task 1: MainLevel에 영구 환경 조명을 배치하고 검증해 한 커밋으로 남긴다

Files:

- Modify: `ShipAutonomySim/Content/Maps/MainLevel.umap`
- Do not modify: `ShipAutonomySim/Config/` 아래 전체, `ShipAutonomySim/Source/` 아래 전체, `ShipAutonomySim/ShipAutonomySim.uproject`, `docs/` 아래 전체, 그 밖의 asset
- Transient evidence only: `ShipAutonomySim/Saved/LightingEvidence/`, `ShipAutonomySim/Saved/Logs/`, `ShipAutonomySim/Saved/Screenshots/` 아래 ignored 파일

Interfaces:

- Consumes: PM handoff의 40자리 `LIGHTING_PLAN_SHA`, `/Game/Maps/MainLevel`, UE 5.5.4 EditorActorSubsystem과 LevelEditorSubsystem, 현재 WaterBodyOcean과 WaterZone
- Produces: 고정 값의 세 영구 actor를 가진 MainLevel 한 파일과 제목 `feat: 영구 환경 조명 배치`인 구현 commit 한 개

- [ ] Step 1: branch, plan SHA, clean 상태와 UnrealEditor 0개 gate를 통과한다.

PowerShell에서 저장소 밖 값을 추론하지 말고 다음 gate를 그대로 실행한다.

```powershell
$Repo = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer'
$Project = Join-Path $Repo 'ShipAutonomySim\ShipAutonomySim.uproject'
$MapRelative = 'ShipAutonomySim/Content/Maps/MainLevel.umap'
$Map = Join-Path $Repo $MapRelative
$EngineRoot = 'C:\Program Files\Epic Games\UE_5.5'
$ExpectedBranch = 'feat/ship-autonomy-navigation'
$ApprovedPlanSha = $env:LIGHTING_PLAN_SHA

if ($ApprovedPlanSha -notmatch '^[0-9a-f]{40}$') {
    throw 'No-Go: LIGHTING_PLAN_SHA is not the 40-character PM handoff SHA'
}
if ((git -C $Repo branch --show-current).Trim() -ne $ExpectedBranch) {
    throw 'No-Go: unexpected branch'
}
if ((git -C $Repo rev-parse HEAD).Trim() -ne $ApprovedPlanSha) {
    throw 'No-Go: HEAD does not equal the approved plan commit'
}
$InitialStatus = @(git -C $Repo status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $InitialStatus.Count -ne 0) {
    throw "No-Go: worktree is not clean: $($InitialStatus -join ', ')"
}
$EditorProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'UnrealEditor*' })
if ($EditorProcesses.Count -ne 0) {
    throw "No-Go: UnrealEditor process count is $($EditorProcesses.Count); do not close or kill user processes"
}
if (-not (Test-Path -LiteralPath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -PathType Leaf)) {
    throw 'No-Go: local UE 5.5.4 editor is missing'
}
```

Expected: 정확한 branch와 plan SHA, clean worktree, UnrealEditor 계열 process 0개다. 하나라도 다르면 파일을 만들거나 수정하지 않고 No-Go로 끝낸다.

- [ ] Step 2: map, Config, Git과 기존 actor의 저장 전 기준을 기록한다.

```powershell
$Evidence = Join-Path $Repo 'ShipAutonomySim\Saved\LightingEvidence'
New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
$ConfigRoot = Join-Path $Repo 'ShipAutonomySim\Config'
function Get-ConfigSnapshot {
    @(
        Get-ChildItem -LiteralPath $ConfigRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
            [pscustomobject]@{
                Path = $_.FullName.Substring($ConfigRoot.Length + 1).Replace('\', '/')
                SHA256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
            }
        }
    )
}
$ConfigBefore = @(Get-ConfigSnapshot)
$MapBefore = [pscustomobject]@{
    SHA256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Map).Hash
    Length = (Get-Item -LiteralPath $Map).Length
    LastWriteTimeUtc = (Get-Item -LiteralPath $Map).LastWriteTimeUtc.ToString('o')
}
$GitBefore = @(git -C $Repo status --porcelain=v1 --untracked-files=all)
$ConfigBefore | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 -LiteralPath (Join-Path $Evidence 'config-before.json')
$MapBefore | ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath (Join-Path $Evidence 'map-before.json')
$GitBefore | Set-Content -Encoding utf8 -LiteralPath (Join-Path $Evidence 'git-before.txt')
```

Expected: Config file 집합과 각 SHA-256, MainLevel SHA-256, length와 mtime, 빈 Git 상태가 ignored `Saved` 아래에만 기록된다.

- [ ] Step 3: Lit black와 조명 actor 부재를 RED로 기록한다.

통제된 editor를 다음 명령으로 한 개만 연다. `PythonScriptPlugin`은 command line session에만 활성화하며 uproject에서 활성화하지 않는다.

```powershell
$Editor = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe"
$LightingLog = Join-Path $Repo 'ShipAutonomySim\Saved\Logs\StaticLighting-Editor.log'
$EditorProcess = Start-Process -FilePath $Editor -ArgumentList @(
    $Project,
    '/Game/Maps/MainLevel',
    '-NoP4',
    '-NoSplash',
    '-EnablePlugins=PythonScriptPlugin',
    "-abslog=$LightingLog"
) -PassThru
```

Editor가 MainLevel을 연 뒤 Python console에서 다음 카메라를 고정한다.

```python
import unreal
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor.set_level_viewport_camera_info(
    unreal.Vector(6000.0, -6000.0, 3500.0),
    unreal.Rotator(pitch=-22.4, yaw=135.0, roll=0.0))
```

Viewport console에는 `viewmode lit`만 입력하고 `HighResShot 1280x720`을 실행한다. 최신 PNG를 `ShipAutonomySim/Saved/LightingEvidence/red-lit.png`로 복사한다. World Outliner와 아래 배치 script의 첫 log가 DirectionalLight, SkyLight, SkyAtmosphere count `0,0,0`인지 확인한다. 로컬 `view_image`로 `red-lit.png`를 열어 Water와 코스 영역을 읽기 어려운 검은 Lit 장면인지 확인한다. 이 actor 부재와 black image가 RED다. `viewmode unlit`은 입력하지 않는다.

```powershell
$ScreenshotRoot = Join-Path $Repo 'ShipAutonomySim\Saved\Screenshots'
$RedSource = Get-ChildItem -LiteralPath $ScreenshotRoot -Recurse -File -Filter '*.png' |
    Sort-Object LastWriteTimeUtc | Select-Object -Last 1
if ($null -eq $RedSource) { throw 'No-Go: RED screenshot was not created' }
Copy-Item -LiteralPath $RedSource.FullName -Destination (Join-Path $Evidence 'red-lit.png') -Force
```

- [ ] Step 4: Python surface를 fail-fast 확인하고 세 actor를 unsaved 상태로 정확히 한 번 배치한다.

다음 내용을 ignored 경로 `ShipAutonomySim/Saved/LightingEvidence/static_lighting.py`에 만들고 Python console에서 `exec(compile(open(path, encoding='utf-8').read(), path, 'exec'))`로 실행한다. 이 script에는 저장 호출이 없다.

```python
import json
import os
import unreal

def need(owner, name):
    value = getattr(owner, name, None)
    if value is None:
        raise RuntimeError(f"No-Go: missing Python surface {owner}.{name}")
    return value

def need_callable(owner, name):
    value = need(owner, name)
    if not callable(value):
        raise RuntimeError(f"No-Go: non-callable Python surface {owner}.{name}")
    return value

def need_property(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception as exc:
        raise RuntimeError(f"No-Go: missing editor property {obj.get_class().get_name()}.{name}") from exc

classes = {
    name: need(unreal, name)
    for name in (
        "DirectionalLight", "DirectionalLightComponent",
        "SkyLight", "SkyLightComponent",
        "SkyAtmosphere", "SkyAtmosphereComponent",
        "WaterBodyOcean", "WaterZone", "ActorComponent")
}
actor_subsystem = unreal.get_editor_subsystem(need(unreal, "EditorActorSubsystem"))
need_callable(actor_subsystem, "get_all_level_actors")
need_callable(actor_subsystem, "spawn_actor_from_class")
need_callable(actor_subsystem, "destroy_actor")
level_subsystem = unreal.get_editor_subsystem(need(unreal, "LevelEditorSubsystem"))
need_callable(level_subsystem, "save_current_level")
need_callable(classes["DirectionalLightComponent"], "set_mobility")
need_callable(classes["DirectionalLightComponent"], "set_intensity")
need_callable(classes["DirectionalLightComponent"], "set_light_color")
need_callable(classes["DirectionalLightComponent"], "set_use_temperature")
need_callable(classes["DirectionalLightComponent"], "set_atmosphere_sun_light")
need_callable(classes["SkyLightComponent"], "set_mobility")
need_callable(classes["SkyLightComponent"], "set_intensity")
need_callable(classes["SkyLightComponent"], "set_real_time_capture")
need_callable(classes["SkyLightComponent"], "recapture_sky")

directional_cdo = unreal.get_default_object(classes["DirectionalLight"])
sky_cdo = unreal.get_default_object(classes["SkyLight"])
atmosphere_cdo = unreal.get_default_object(classes["SkyAtmosphere"])
directional_cdo_component = directional_cdo.get_component_by_class(classes["DirectionalLightComponent"])
sky_cdo_component = sky_cdo.get_component_by_class(classes["SkyLightComponent"])
atmosphere_cdo_component = atmosphere_cdo.get_component_by_class(classes["SkyAtmosphereComponent"])
for obj, names in (
    (directional_cdo_component, ("mobility", "intensity", "light_color", "use_temperature", "atmosphere_sun_light")),
    (sky_cdo_component, ("mobility", "intensity", "source_type", "real_time_capture", "cubemap")),
    (atmosphere_cdo_component, ("transform_mode",))):
    if obj is None:
        raise RuntimeError("No-Go: class default component lookup failed")
    for property_name in names:
        need_property(obj, property_name)

component_mobility = need(unreal, "ComponentMobility")
movable = need(component_mobility, "MOVABLE")
sky_source = need(need(unreal, "SkyLightSourceType"), "SLS_CAPTURED_SCENE")
atmosphere_mode = need(
    need(unreal, "SkyAtmosphereTransformMode"),
    "PLANET_TOP_AT_ABSOLUTE_WORLD_ORIGIN")

def exact(actors, actor_class):
    return [actor for actor in actors if actor.get_class() == actor_class.static_class()]

def rounded(values):
    return [round(float(value), 4) for value in values]

def signature(actor):
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    components = sorted(
        f"{component.get_class().get_path_name()}:{component.get_name()}"
        for component in actor.get_components_by_class(classes["ActorComponent"]))
    return {
        "name": actor.get_name(),
        "class": actor.get_class().get_path_name(),
        "location": rounded((location.x, location.y, location.z)),
        "rotation": rounded((rotation.pitch, rotation.yaw, rotation.roll)),
        "scale": rounded((scale.x, scale.y, scale.z)),
        "components": components,
    }

light_classes = (
    classes["DirectionalLight"],
    classes["SkyLight"],
    classes["SkyAtmosphere"])
actors_before = actor_subsystem.get_all_level_actors()
counts_before = [len(exact(actors_before, actor_class)) for actor_class in light_classes]
if counts_before != [0, 0, 0]:
    raise RuntimeError(f"No-Go: duplicate lighting run blocked, counts={counts_before}")
if len(exact(actors_before, classes["WaterBodyOcean"])) != 1:
    raise RuntimeError("No-Go: expected exactly one WaterBodyOcean")
if len(exact(actors_before, classes["WaterZone"])) != 1:
    raise RuntimeError("No-Go: expected exactly one WaterZone")

protected_before = sorted(
    (signature(actor) for actor in actors_before if actor.get_class() not in tuple(c.static_class() for c in light_classes)),
    key=lambda item: (item["class"], item["name"]))
evidence_dir = os.path.join(unreal.Paths.project_saved_dir(), "LightingEvidence")
os.makedirs(evidence_dir, exist_ok=True)
with open(os.path.join(evidence_dir, "actor-inventory-before.json"), "w", encoding="utf-8") as handle:
    json.dump(protected_before, handle, ensure_ascii=False, indent=2)
unreal.log("LIGHTING_RED actor_counts=0,0,0")

created = []
try:
    directional = actor_subsystem.spawn_actor_from_class(
        classes["DirectionalLight"], unreal.Vector(0.0, 0.0, 1000.0),
        unreal.Rotator(pitch=-45.0, yaw=-30.0, roll=0.0), False)
    created.append(directional)
    sky = actor_subsystem.spawn_actor_from_class(
        classes["SkyLight"], unreal.Vector(0.0, 0.0, 1000.0),
        unreal.Rotator(0.0, 0.0, 0.0), False)
    created.append(sky)
    atmosphere = actor_subsystem.spawn_actor_from_class(
        classes["SkyAtmosphere"], unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0), False)
    created.append(atmosphere)
    if any(actor is None for actor in created):
        raise RuntimeError("No-Go: actor spawn returned None")

    for actor, label in zip(created, ("DirectionalLight", "SkyLight", "SkyAtmosphere")):
        actor.set_actor_label(label, True)
        actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))

    directional_component = directional.get_component_by_class(classes["DirectionalLightComponent"])
    sky_component = sky.get_component_by_class(classes["SkyLightComponent"])
    atmosphere_component = atmosphere.get_component_by_class(classes["SkyAtmosphereComponent"])
    if None in (directional_component, sky_component, atmosphere_component):
        raise RuntimeError("No-Go: spawned actor component lookup failed")

    directional_component.set_mobility(movable)
    directional_component.set_intensity(10.0)
    directional_component.set_light_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0), False)
    directional_component.set_use_temperature(False)
    directional_component.set_atmosphere_sun_light(True)

    sky_component.set_mobility(movable)
    sky_component.set_intensity(1.0)
    sky_component.set_editor_property("source_type", sky_source)
    sky_component.set_real_time_capture(False)
    if sky_component.get_editor_property("cubemap") is not None:
        raise RuntimeError("No-Go: SkyLight has an external cubemap")
    atmosphere_component.set_editor_property("transform_mode", atmosphere_mode)
    sky_component.recapture_sky()

    actors_after = actor_subsystem.get_all_level_actors()
    counts_after = [len(exact(actors_after, actor_class)) for actor_class in light_classes]
    if counts_after != [1, 1, 1]:
        raise RuntimeError(f"No-Go: wrong lighting actor counts={counts_after}")
    protected_after = sorted(
        (signature(actor) for actor in actors_after if actor.get_class() not in tuple(c.static_class() for c in light_classes)),
        key=lambda item: (item["class"], item["name"]))
    if protected_after != protected_before:
        raise RuntimeError("No-Go: existing actor inventory or transform changed")
    for forbidden_name in ("PostProcessVolume", "ExponentialHeightFog", "VolumetricCloud"):
        forbidden_class = getattr(unreal, forbidden_name, None)
        if forbidden_class is not None and exact(actors_after, forbidden_class):
            raise RuntimeError(f"No-Go: forbidden actor exists: {forbidden_name}")
    with open(os.path.join(evidence_dir, "actor-inventory-after.json"), "w", encoding="utf-8") as handle:
        json.dump([signature(actor) for actor in actors_after], handle, ensure_ascii=False, indent=2)
    unreal.log("LIGHTING_PRE_SAVE_GREEN actor_counts=1,1,1 protected_unchanged=true")
except Exception:
    for actor in reversed(created):
        if actor is not None:
            actor_subsystem.destroy_actor(actor)
    unreal.log_error("LIGHTING_NO_SAVE cleanup_after_failure=true")
    raise
```

Expected: `LIGHTING_RED actor_counts=0,0,0` 뒤에 `LIGHTING_PRE_SAVE_GREEN actor_counts=1,1,1 protected_unchanged=true`가 한 번씩 나온다. reflection 또는 배치가 실패하면 `LIGHTING_NO_SAVE`가 나오며 editor를 저장 없이 닫고 No-Go로 끝낸다.

- [ ] Step 5: 같은 Lit 카메라에서 GREEN PNG를 만들고 저장 전 gate를 통과한다.

같은 camera, `viewmode lit`, `HighResShot 1280x720`로 최신 PNG를 `ShipAutonomySim/Saved/LightingEvidence/green-lit.png`에 복사한다. 로컬 `view_image`로 하늘, Water 표면과 코스 영역이 실제 렌더 영역에서 보이고 검은 화면이 아님을 확인한다. UI만 밝거나 actor icon만 보이는 이미지는 실패다.

```powershell
$GreenSource = Get-ChildItem -LiteralPath $ScreenshotRoot -Recurse -File -Filter '*.png' |
    Sort-Object LastWriteTimeUtc | Select-Object -Last 1
if ($null -eq $GreenSource -or $GreenSource.LastWriteTimeUtc -le $RedSource.LastWriteTimeUtc) {
    throw 'No-Go: fresh GREEN screenshot was not created'
}
Copy-Item -LiteralPath $GreenSource.FullName -Destination (Join-Path $Evidence 'green-lit.png') -Force
```

다음 pixel gate도 통과시킨다.

```powershell
Add-Type -AssemblyName System.Drawing
function Get-LitImageMetric([string]$Path) {
    $Bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $Count = 0
        $NonBlack = 0
        $Luma = [System.Collections.Generic.List[double]]::new()
        for ($Y = 0; $Y -lt $Bitmap.Height; $Y += 4) {
            for ($X = 0; $X -lt $Bitmap.Width; $X += 4) {
                $Color = $Bitmap.GetPixel($X, $Y)
                $Value = 0.2126 * $Color.R + 0.7152 * $Color.G + 0.0722 * $Color.B
                $Luma.Add($Value)
                if ([Math]::Max($Color.R, [Math]::Max($Color.G, $Color.B)) -gt 16) { $NonBlack++ }
                $Count++
            }
        }
        $Sorted = @($Luma | Sort-Object)
        [pscustomobject]@{
            NonBlackRatio = $NonBlack / $Count
            MeanLuma = ($Luma | Measure-Object -Average).Average
            P95Luma = $Sorted[[Math]::Floor(($Sorted.Count - 1) * 0.95)]
        }
    } finally {
        $Bitmap.Dispose()
    }
}
$GreenMetric = Get-LitImageMetric (Join-Path $Evidence 'green-lit.png')
if ($GreenMetric.NonBlackRatio -lt 0.10 -or $GreenMetric.MeanLuma -lt 15 -or $GreenMetric.P95Luma -lt 32) {
    throw "No-Go: GREEN render is still black: $($GreenMetric | ConvertTo-Json -Compress)"
}
if (Select-String -LiteralPath $LightingLog -SimpleMatch 'Set new viewmode: Unlit') {
    throw 'No-Go: viewmode unlit was used during lighting validation'
}
```

PowerShell에서 현재 Git 상태와 Config snapshot을 Step 2와 다시 비교한다. 아직 map을 저장하지 않았으므로 Git은 clean이고 Config 집합과 hash도 같아야 한다. 다르면 저장하지 않고 No-Go다.

```powershell
$ConfigPreSave = @(Get-ConfigSnapshot)
if ((ConvertTo-Json -InputObject $ConfigPreSave -Depth 4 -Compress) -ne
    (ConvertTo-Json -InputObject $ConfigBefore -Depth 4 -Compress)) {
    throw 'No-Go: Config set or hash changed before save'
}
$GitPreSave = @(git -C $Repo status --porcelain=v1 --untracked-files=all)
if ($GitPreSave.Count -ne 0) {
    throw "No-Go: worktree changed before authorized map save: $($GitPreSave -join ', ')"
}
```

- [ ] Step 6: GREEN 뒤 MainLevel을 정확히 한 번 저장하고 범위를 즉시 검사한다.

Python console에서 저장 호출을 별도로 실행한다.

```python
import unreal
saved = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
if not saved:
    raise RuntimeError("No-Go: SaveCurrentLevel returned false")
```

저장 직후 script의 actor count를 다시 읽어 `1,1,1`, protected inventory 동일, WaterBodyOcean 한 개와 WaterZone 한 개를 재확인한다. Editor를 정상 종료한 뒤 다음 범위 gate를 실행한다.

```python
actors = actor_subsystem.get_all_level_actors()
post_save_counts = [len(exact(actors, actor_class)) for actor_class in light_classes]
if post_save_counts != [1, 1, 1]:
    raise RuntimeError(f"No-Go: post-save lighting counts={post_save_counts}")
if len(exact(actors, classes["WaterBodyOcean"])) != 1:
    raise RuntimeError("No-Go: post-save WaterBodyOcean count changed")
if len(exact(actors, classes["WaterZone"])) != 1:
    raise RuntimeError("No-Go: post-save WaterZone count changed")
protected_post_save = sorted(
    (signature(actor) for actor in actors if actor.get_class() not in tuple(c.static_class() for c in light_classes)),
    key=lambda item: (item["class"], item["name"]))
if protected_post_save != protected_before:
    raise RuntimeError("No-Go: post-save protected actor inventory changed")
unreal.log("LIGHTING_POST_SAVE_GREEN actor_counts=1,1,1 protected_unchanged=true")
```

```powershell
$AfterSaveStatus = @(git -C $Repo status --porcelain=v1 --untracked-files=all)
$ExpectedStatus = " M $MapRelative"
if ($AfterSaveStatus.Count -ne 1 -or $AfterSaveStatus[0] -ne $ExpectedStatus) {
    throw "No-Go: unexpected tracked or untracked output: $($AfterSaveStatus -join ', ')"
}
$Changed = @(git -C $Repo diff --name-only)
if ($Changed.Count -ne 1 -or $Changed[0] -ne $MapRelative) {
    throw "No-Go: product diff is not MainLevel-only: $($Changed -join ', ')"
}
$ConfigAfterSave = @(Get-ConfigSnapshot)
if ((ConvertTo-Json -InputObject $ConfigAfterSave -Depth 4 -Compress) -ne
    (ConvertTo-Json -InputObject $ConfigBefore -Depth 4 -Compress)) {
    throw 'No-Go: Config set or hash changed after save'
}
```

Config 집합과 hash가 Step 2의 `$ConfigBefore`와 정확히 같아야 한다. `Source`, uproject, docs와 다른 asset diff는 0이어야 한다. MainLevel 외 항목이 있으면 수정하거나 삭제하지 않고 No-Go로 끝낸다.

- [ ] Step 7: Build.bat와 최소 Stage 4 actual-world 회귀를 실행한다.

UnrealEditor 계열 process가 다시 0개인지 확인한 뒤 Build.bat를 실행한다.

```powershell
$BuildLog = Join-Path $Repo 'ShipAutonomySim\Saved\Logs\StaticLighting-Build.log'
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" `
    ShipAutonomySimEditor Win64 Development `
    "-Project=$Project" -WaitMutex -NoHotReloadFromIDE 2>&1 |
    Tee-Object -FilePath $BuildLog
$BuildExit = $LASTEXITCODE
if ($BuildExit -ne 0) {
    throw "No-Go: ShipAutonomySimEditor build failed with exit $BuildExit"
}
```

이어 실제 MainLevel을 사용하는 한 test만 실행한다.

```powershell
$AutomationLog = Join-Path $Repo 'ShipAutonomySim\Saved\Logs\StaticLighting-Stage4-ActualWorld.log'
$EditorCmd = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Arguments = @(
    $Project,
    '/Game/Maps/MainLevel?Stage4Slide=-500',
    '-game',
    '-Unattended',
    '-NoSplash',
    '-NullRHI',
    '-NoAudio',
    '-NoPause',
    '-NoP4',
    '-nowrite',
    '-TestExit="Automation Test Queue Empty"',
    '-ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep;SoftQuit;"',
    "-abslog=$AutomationLog"
)
$AutomationProcess = Start-Process -FilePath $EditorCmd -ArgumentList $Arguments -PassThru -WindowStyle Hidden
if (-not $AutomationProcess.WaitForExit(900000)) {
    $AutomationProcess.Kill()
    throw 'No-Go: Stage 4 actual-world Automation exceeded 900 seconds'
}
if ($AutomationProcess.ExitCode -ne 0) {
    throw "No-Go: Stage 4 actual-world Automation exit $($AutomationProcess.ExitCode)"
}
$AutomationText = Get-Content -Raw -LiteralPath $AutomationLog
if ($AutomationText -notmatch 'Stage4SweepCounts success=11 collision=0 timeout=0 setup=0 runtime=0 cases=11') {
    throw 'No-Go: Stage 4 sweep counts are not 11/0/0/0/0/11'
}
if ([regex]::Matches($AutomationText, 'Stage4Terminal Success').Count -ne 11) {
    throw 'No-Go: Stage4Terminal Success count is not 11'
}
if ([regex]::Matches($AutomationText, 'Water state changed to ValidWaves').Count -lt 11) {
    throw 'No-Go: ValidWaves was not observed in every fresh world'
}
if ($AutomationText -match 'Stage4SetupFailure|Stage4RuntimeCalculationError|Stage4Terminal Collision|Stage4Terminal Timeout|Result=\{Fail\}|Unknown test|Ensure condition failed|Fatal error') {
    throw 'No-Go: Stage 4 failure, setup, runtime, collision, timeout or engine error marker found'
}
if ($AutomationText -notmatch 'TEST COMPLETE\. EXIT CODE: 0') {
    throw 'No-Go: successful Automation completion marker is missing'
}
```

Expected: Build exit 0, actual-world test 한 개, 11개 fresh world 모두 `Stage4Terminal Success`, `ValidWaves`, collision 0, timeout 0, setup 0, runtime calculation error 0이다. full Stage 3와 Stage 4 unit Automation은 이 map-only 변경에서 실행하지 않는다.

- [ ] Step 8: MainLevel no-write game load와 map, Config, Git 불변을 확인한다.

MapCheckCommandlet은 사용하지 않는다. 저장된 map, Config 전체와 현재 Git 상태를 snapshot한 다음 UE 5.5.4 no-write game load를 실행한다.

```powershell
$ProtectedBeforeNoWrite = @{
    Map = (Get-FileHash -Algorithm SHA256 -LiteralPath $Map).Hash
    Config = @(
        Get-ChildItem -LiteralPath $ConfigRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
            "{0}|{1}" -f $_.FullName.Substring($ConfigRoot.Length + 1).Replace('\', '/'), (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        })
    Git = @(git -C $Repo status --porcelain=v1 --untracked-files=all)
}
$NoWriteLog = Join-Path $Repo 'ShipAutonomySim\Saved\Logs\StaticLighting-MainLevel-NoWrite.log'
& $EditorCmd $Project /Game/Maps/MainLevel -game `
    -Unattended -NoSplash -NullRHI -NoAudio -NoPause -NoP4 -nowrite `
    -ExecCmds='QUIT_EDITOR' "-abslog=$NoWriteLog"
$NoWriteExit = $LASTEXITCODE
if ($NoWriteExit -ne 0) { throw "No-Go: MainLevel no-write exit $NoWriteExit" }
$NoWriteText = Get-Content -Raw -LiteralPath $NoWriteLog
if ($NoWriteText -notmatch 'Load map complete') { throw 'No-Go: Load map complete marker missing' }
$LoadErrors = [regex]::Matches($NoWriteText, '(?im)^.*LoadErrors.*(?:Error|Warning)').Count
$FatalErrors = [regex]::Matches($NoWriteText, '(?im)Fatal error:').Count
$MapCheckErrors = [regex]::Matches($NoWriteText, '(?im)MapCheck:\s*Error').Count
$CleanExit = [regex]::Matches($NoWriteText, '(?im)LogExit: (?:Exiting\.|Editor shut down)').Count
if ($LoadErrors -ne 0 -or $FatalErrors -ne 0 -or $MapCheckErrors -ne 0 -or $CleanExit -lt 1) {
    throw "No-Go: loadErrors=$LoadErrors fatal=$FatalErrors mapCheckErrors=$MapCheckErrors cleanExit=$CleanExit"
}
$ProtectedAfterNoWrite = @{
    Map = (Get-FileHash -Algorithm SHA256 -LiteralPath $Map).Hash
    Config = @(
        Get-ChildItem -LiteralPath $ConfigRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
            "{0}|{1}" -f $_.FullName.Substring($ConfigRoot.Length + 1).Replace('\', '/'), (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        })
    Git = @(git -C $Repo status --porcelain=v1 --untracked-files=all)
}
if ($ProtectedAfterNoWrite.Map -ne $ProtectedBeforeNoWrite.Map) { throw 'No-Go: no-write load changed MainLevel' }
if (($ProtectedAfterNoWrite.Config -join "`n") -ne ($ProtectedBeforeNoWrite.Config -join "`n")) { throw 'No-Go: no-write load changed Config set or hash' }
if (($ProtectedAfterNoWrite.Git -join "`n") -ne ($ProtectedBeforeNoWrite.Git -join "`n")) { throw 'No-Go: no-write load changed Git status' }
```

Expected: process exit 0, map load 완료, LoadErrors 0, Fatal 0, MapCheck Error 0, clean exit이며 map SHA-256, Config 집합과 hash, Git 상태가 전후 동일하다.

- [ ] Step 9: diff를 검사하고 MainLevel 한 파일만 구현 commit으로 만든다.

```powershell
git -C $Repo diff --check
if ($LASTEXITCODE -ne 0) { throw 'No-Go: git diff --check failed' }
$FinalChanged = @(git -C $Repo diff --name-only)
if ($FinalChanged.Count -ne 1 -or $FinalChanged[0] -ne $MapRelative) {
    throw "No-Go: final product diff is not MainLevel-only: $($FinalChanged -join ', ')"
}
git -C $Repo add -- $MapRelative
if ($LASTEXITCODE -ne 0) { throw 'No-Go: staging MainLevel failed' }
$Staged = @(git -C $Repo diff --cached --name-status)
if ($Staged.Count -ne 1 -or $Staged[0] -ne "M`t$MapRelative") {
    throw "No-Go: staged set is not exactly MainLevel: $($Staged -join ', ')"
}
```

Commit은 다음 제목과 본문 섹션을 그대로 사용한다.

```powershell
git -C $Repo commit `
    -m 'feat: 영구 환경 조명 배치' `
    -m "변경 이유`nLit view에서 MainLevel이 검게 보여 Unlit 전환에 의존하던 문제를 제거합니다." `
    -m "핵심 변경`nMainLevel에 DirectionalLight, SkyLight, SkyAtmosphere를 각각 한 개 영구 배치합니다." `
    -m "검증 방법`nLit GREEN 이미지, ShipAutonomySimEditor Build.bat, Stage 4 actual-world sweep와 MainLevel no-write load를 확인했습니다."
if ($LASTEXITCODE -ne 0) { throw 'No-Go: implementation commit failed' }
```

마지막으로 `git show --name-status --format=fuller HEAD`에서 제목과 MainLevel 한 파일만 확인하고, branch가 그대로인지, `git status --porcelain=v1 --untracked-files=all`이 비어 있는지 확인한다. push, merge, PR과 branch 전환은 하지 않는다.

## 자체 검토 gate

- 설계의 actor 세 종류, fixed transform과 property, Water와 Stage 4 보존, map-only 범위, 중복 실행 차단, no-save 실패 경로, Lit RED와 GREEN, Build, targeted actual-world, no-write load, Config와 Git 불변, 단일 commit 요구가 모두 Task 1에 연결돼 있는지 대조한다.
- 다음 token scan 결과는 0이어야 한다. 검색어 자체가 문서에 들어가지 않도록 문자열을 나눠 만든다.

```powershell
$Plan = Join-Path $Repo 'docs\superpowers\plans\2026-08-09-static-environment-lighting.md'
$ForbiddenTokens = @(
    [string]::Concat('T','B','D'),
    [string]::Concat('T','O','D','O'),
    [string]::Concat('place','holder'),
    [string][char]0x00B7)
foreach ($Token in $ForbiddenTokens) {
    if (Select-String -LiteralPath $Plan -SimpleMatch $Token) {
        throw "Plan contains a forbidden token with codepoints: $([string]::Join(',', [int[]][char[]]$Token))"
    }
}
```

- `git diff --check`를 통과시키고 구현 전 문서 commit 이후 worktree가 clean인지 확인한다.
- 이 계획 완료 뒤에는 실행 승인이나 방식 선택을 묻지 않고 PM handoff에 계획 경로, plan commit SHA, 핵심 실행 단계와 clean 상태만 전달한다.
