# Hitori (14주차) 엔진 리팩토링 & 기능 이식 마스터 플랜

이 문서는 AI 코딩 에이전트(Claude Code 등)가 **13-2주차 엔진(`Kurisu`)의 우수한 구조·기능을 14주차 엔진(`Hitori`)에 안전하게 이식·리팩토링**하기 위한 실행 명세서입니다.

---

## 0. 저장소 경로 & 빌드 규칙

- **작업 대상 (Target, 14주차 Hitori)**: `C:\Users\JUNGLE\Desktop\Jungle\JG_Week14`
- **참조 원본 (Reference, 13-2주차 Kurisu, 읽기 전용)**: `C:\Users\JUNGLE\Desktop\Jungle\JG_Week13-2\Kurisu\Kurisu`
  - **절대 `Kurisu` 폴더의 파일은 수정하지 말 것.**
- **프로젝트 재생성 (새 `.h`/`.cpp`/`.hlsl` 추가·삭제 시 필수)**:
  ```bat
  SlnGenerator.bat --no-pause
  ```
- **빌드 대상**: `Hitori.slnx` (Configuration: `Debug` / Platform: `x64`)
  - 주요 프로젝트 의존 순서: `HitoriHeaderTool` → `HitoriEngine` → `HitoriEditorCore` → `HitoriEditor` (`HitoriGame`, `Shaders`)
  - 새 `UCLASS` / `USTRUCT` / `UENUM` / `UPROPERTY` 추가 시 `HitoriHeaderTool`이 `*.generated.h`를 생성함.

---

## 1. 절대 깨뜨리면 안 되는 14주차 고유 기능 & 제약사항 (Hard Constraints)

이식 시 아래 14주차(`Hitori`) 고유 구조와 기능은 **반드시 보존**해야 합니다. (13-2주차 코드를 그대로 복사해 덮어쓰지 말 것)

1. **Deferred Rendering & 14주차 렌더링 구조**
   - `ERenderingPath::Deferred` (`StaticMeshGBufferShader.hlsl`, `DeferredLightingShader.hlsl`)
   - `FSceneViewFamily`, `FSceneTextures` (`Source/Runtime/Render/SceneRenderer.h`)
   - `SkyLightEnvironment` / `TextureCube` IBL, `GPUOcclusion` / `SoftwareOcclusion`, `TiledLightCulling25D`, `ShadowMaskVisualize`, `TireMarkShader`
   - `UStaticMesh::AdditionalLODs` (다중 LOD 리소스)
2. **기존 씬/에셋 파일 하위 호환성**
   - `Source/Runtime/Render/RenderResourceManager.h`의 `EPSOType` 기존 열거값 순서(특히 `Reserved_13`, `DeferredPointLighting` 등)는 **절대 밀리지 않게 유지**.
   - 기존 `HitoriProject` 씬/에셋이 정상 로드되어야 함.
3. **14주차 에디터 고유 시스템**
   - `FEditorModeTools` (`Source/Editor/EditorModeManager.h`) + `FGizmo(FEditorModeTools*)`
   - `USelection` (`Source/Editor/Selection.h`) — `UEditorEngine::GetSelectedActors()` / `GetSelectedComponents()` 구조 유지
   - `Sequencer` (`MovieScene`, `LevelSequence`), `BlueprintEditor`, `InputEditor`, `WorldSettingsPanel`, `ActorClipboard`
   - 14주차 확장 파티클 모듈(`AccelerationDrag`, `Light`, `Rotation`, `RotationRate`, `RotationRateMultiplyLife`, `SizeScaleBySpeed`)
4. **네이밍 규칙**
   - 13-2주차의 `Kurisu` / `KRS_` / `KURISU_` 명칭은 모두 `Hitori` / `HTR_` / `HITORI_`로 치환하여 이식.

---

## 2. 작업 진행 규칙 (Agent Execution Protocol)

- **반드시 한 번에 하나의 Phase만 수행**하고, 각 Phase 완료 후 아래를 검증한 뒤 체크박스를 `[x]`로 갱신할 것:
  1. `SlnGenerator.bat --no-pause` 실행 (파일 추가/삭제 시)
  2. `HitoriEditor` (`Debug|x64`) 및 `HitoriGame` (`Debug|x64`) 빌드 에러 0개 확인
  3. 셰이더 변경 시 `Shaders` / 런타임 셰이더 컴파일 에러 없음 확인
  4. 변경 파일 목록과 남은 리스크를 짧게 보고
- 참조 파일을 읽지 않고 추측 구현하지 말 것. 반드시 `Kurisu` 원본과 `Hitori` 대상 파일을 둘 다 읽고 차이점을 파악한 뒤 병합할 것.

---

## 3. 단계별 실행 계획 (Phase 0 ~ Phase 6)

### Phase 0. 기반 정비 & 리스크 없는 코드 분할
**목표**: 기능 변경 없이 코어 유틸 추가, 대형 파일 분할, PhysX 락 안전망 확보.

- [x] **Core 유틸 추가**
  - 참조: `Kurisu\Source\Runtime\Core\FrameTime.h`, `Kurisu\Source\Runtime\Core\Misc\Version.h`
  - 대상: `Hitori\Source\Runtime\Core\FrameTime.h`, `Hitori\Source\Runtime\Core\Misc\Version.h` (`HITORI_VERSION_*`)
  - 결과: `Version.h`는 이미 존재해 유지. `FFrameNumber`/`FFrameTime`/`FFrameRate`/`FQualifiedFrameTime`을 `MovieSceneFrameTime.h`에서 `Core/FrameTime.h`로 이동(`FMovieSceneFrameRange`는 MovieScene에 유지)
- [x] **`LuaBindings.cpp` 도메인별 분할**
  - 참조 구조: `Kurisu\Source\Runtime\Script\LuaBindings*.{h,cpp}`
  - 대상: 58.5KB 단일 `Hitori\Source\Runtime\Script\LuaBindings.cpp`를 `LuaBindings.cpp`(코어), `LuaBindingsActor.cpp`, `LuaBindingsAnimation.cpp`, `LuaBindingsInput.cpp`, `LuaBindingsInternal.h`로 분할 (14주차 기존 Lua 바인딩 기능 100% 유지)
- [x] **PhysX RAII 락 & 통계 헤더 추가**
  - 참조: `Kurisu\Source\Runtime\Physics\PhysicsLock.h`, `PhysicsStats.h`
  - 대상: `Hitori\Source\Runtime\Physics\PhysicsLock.h` (`HTR_PHYSX_*`), `PhysicsStats.h`
  - 결과: 기존 `PxSceneRead/WriteLock` 30곳을 `SCOPED_SCENE_READ/WRITE_LOCK`으로 교체. Simulate/SyncActors/Notifies/Ragdoll 구간에 `SCOPE_CYCLE_COUNTER` 연결
- [x] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드 통과 (HitoriEditor/HitoriGame 오류 0)

---

### Phase 1. ShowFlags·설정 단일화 & 에디터 체감 UX
**목표**: 중복 ShowFlag 정리, 기즈모 스냅, 콘솔 자동완성, 하단 드로어 바, 테마 시스템 도입.

- [x] **X-Macro 기반 `FEngineShowFlags` 단일화**
  - 참조: `Kurisu\Source\Runtime\Render\ShowFlags.h`, `ShowFlags.cpp`, `ShowFlagsValues.h`, `Kurisu\Source\Editor\Settings\EditorSettings.{h,cpp}`, `Kurisu\Source\Editor\Viewports\ViewportShowMenu.cpp`
  - 대상: `Hitori\Source\Runtime\Render\ShowFlags.h`, `ShowFlags.cpp`(신규), `ShowFlagsValues.h`(신규), `Hitori\Source\Editor\Settings\SettingsPanel.{h,cpp}`
  - 주의: 14주차 고유 플래그(`VisualizeDOF`, `Constraints`, `ShadowMask`, `Deferred` 관련 옵션 등)를 빠짐없이 `ShowFlagsValues.h` 또는 `FEditorSettings`에 보존하고, `SettingsPanel` UI가 깨지지 않게 연동.
- [x] **트랜스폼 기즈모 스냅 추가 (Location / Rotation / Scale)**
  - 참조: `Kurisu\Source\Editor\Settings\EditorSettings.h` (`bLocationSnap`, `LocationSnapSize`, `bRotationSnap`, `RotationSnapDegrees`, `bScaleSnap`, `ScaleSnapSize`), `Kurisu\Source\Editor\Gizmo\Gizmo.cpp`
  - 대상: `Hitori\Source\Editor\Settings\SettingsPanel.{h,cpp}`, `Hitori\Source\Editor\Gizmo\Gizmo.{h,cpp}`, `Hitori\Source\Editor\Viewports\EditorViewportToolbar.{h,cpp}`
- [x] **Output Log 고도화**
  - 참조: `Kurisu\Source\Editor\OutputLog\OutputLogPanel.{h,cpp}`
  - 대상: `Hitori\Source\Editor\OutputLog\OutputLogPanel.{h,cpp}`
  - 기능: 콘솔/CVar 자동완성 팝업(`FConsoleSuggestion`), Verbosity 필터(Message/Warning/Error/Command), 필터 로그 파일 저장, 고정폭 폰트(`LoadLogFont`)
- [x] **UE5식 하단 드로어 바 (`FBottomDrawerBar`)**
  - 참조: `Kurisu\Source\Editor\EditorUI\BottomDrawerBar.{h,cpp}`
  - 대상: `Hitori\Source\Editor\EditorUI\BottomDrawerBar.{h,cpp}` 및 `LevelEditorTab` / `EditorEngine` 연동
- [x] **에디터 테마/팔레트 시스템 (`EditorStyle.h`)**
  - 참조: `Kurisu\Source\Editor\EditorUI\EditorStyle.h`
  - 대상: `Hitori\Source\Editor\EditorUI\EditorStyle.h` (Dark/Mid/Light/Hitori 테마, 기본 Dark)
- [x] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드 + ShowFlags INI 저장/복원·스냅·자동완성·하단 드로어 동작 확인

---

### Phase 2. 렌더링 아키텍처 핵심 — `FVertexFactory` & `UMaterial`
**목표**: 정점 스트림/셰이더 변형을 `FVertexFactory`로 추상화하고, `UMaterial`을 Domain/BlendMode/ShadingModel + `.mat` 에셋 기반으로 고도화.

- [x] **`FVertexFactory` C++ & HLSL 도입**
  - 참조:
    - C++: `Kurisu\Source\Runtime\Render\VertexFactory.{h,cpp}`, `MeshPassProcessor.{h,cpp}`, `RenderResourceManager.{h,cpp}`, `Renderer.{h,cpp}`, `Mesh.{h,cpp}`, `SkeletalMesh.{h,cpp}`, `SkeletalMeshObject.{h,cpp}`
    - HLSL: `Kurisu\EngineResources\Shaders\VertexFactory.hlsli`, `VF_Local.hlsli`, `VF_GpuSkin.hlsli`, `VF_MeshParticle.hlsli`, `UberLit.hlsl`, `DepthPrepass.hlsl`, `ShadowDepth.hlsl`, `DecalShader.hlsl`
  - 대상:
    - `Hitori\Source\Runtime\Render\VertexFactory.{h,cpp}` 신규 추가
    - `Hitori\Source\Runtime\Render\Mesh.h`: `AdditionalLODs` 각 LOD별 `FLocalVertexFactory` 지원
    - `Hitori\Source\Runtime\Render\MeshPassProcessor.{h,cpp}`: `FMeshDrawBatch`의 raw `VertexBuffer`/`IndexBuffer`/`BoneBuffer`/`MeshParticleInstances`를 `const FVertexFactory* VertexFactory` + `uint32 InstanceCount`로 통합
    - `Hitori\Source\Runtime\Render\RenderResourceManager.{h,cpp}`: `ResolveShader(BaseShader, EVertexFactoryType)` 구현
    - `Hitori\EngineResources\Shaders\StaticMeshGBufferShader.hlsl` (**14주차 Deferred GBuffer**) 포함 모든 메시 셰이더에 VF 적용
- [x] **`UMaterial` 추상화 & `MaterialFile` (`.mat`) 도입**
  - 참조: `Kurisu\Source\Runtime\Render\Material.{h,cpp}`, `MaterialFile.{h,cpp}`
  - 대상: `Hitori\Source\Runtime\Render\Material.{h,cpp}`, `MaterialFile.{h,cpp}`(신규)
  - 기능: `EMaterialDomain`, `EMaterialBlendMode`(Opaque/**Masked**+`OpacityMaskClipValue`/Translucent/**Additive**), `EMaterialShadingModel`(DefaultLit/Unlit), `SyncLegacyStates()`, `bRenderBeforeDOF` 보존, DepthPrepass & GBuffer Masked 알파 클립 지원
  - 주의: `EPSOType` 기존 번호 순서 유지.
- [x] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드 + **Forward 및 Deferred 양쪽 경로**에서 StaticMesh(LOD 포함)/SkeletalMesh(Cloth 포함)/MeshParticle/Sprite/Trail/Decal/Shadow 정상 렌더링 확인

---

### Phase 3. HDR Post-Process 파이프라인 통합 & 신규 효과
**목표**: `SceneRenderer.cpp` 인라인 DOF 분리, `FPostProcessSettings` 도입, HDR 공간 통일 + 단일 ToneMap, LensFlare/AutoExposure 추가.

- [x] **`FPostProcessSettings` 도입**
  - 참조: `Kurisu\Source\Runtime\Engine\PostProcessSettings.{h,cpp}`
  - 대상: `Hitori\Source\Runtime\Engine\PostProcessSettings.{h,cpp}`(신규) + `SceneView` / `CameraComponent` 연동
- [x] **인라인 DOF → `FDepthOfFieldRenderer` 추출**
  - 참조: `Kurisu\Source\Runtime\Render\PostProcess\DepthOfFieldRenderer.{h,cpp}`, `Kurisu\EngineResources\Shaders\DepthOfField*.hlsl`, `DepthCommon.hlsli`
  - 대상: `Hitori\Source\Runtime\Render\PostProcess\DepthOfFieldRenderer.{h,cpp}`(신규), `Hitori\Source\Runtime\Render\SceneRenderer.cpp` (L536~L700 인라인 코드 제거), 다각형 보케(`BladeCount`) 지원
- [x] **HDR 파이프라인 통일 & 단일 `AddToneMapPasses`**
  - 참조: `Kurisu\Source\Runtime\Render\SceneRenderer.cpp` (`AddToneMapPasses`), `ToneMapData.h`, `ToneMapShader.hlsl`
  - 대상: `Hitori\Source\Runtime\Render\SceneRenderer.{h,cpp}`
  - 변경: `HeightFog`와 `TranslucentBeforeDOF`/`TranslucentAfterDOF`를 모두 `LightingHDR`에서 처리한 뒤 마지막에 단일 `AddToneMapPasses` 실행 (물리 노출 + 광학 비네팅 포함)
- [x] **LensFlare & ExposureMeter (Auto Exposure) 추가**
  - 참조:
    - `Kurisu\Source\Runtime\Render\PostProcess\LensFlareRenderer.{h,cpp}`, `ExposureMeterRenderer.{h,cpp}`
    - `Kurisu\EngineResources\Shaders\LensFlare*.hlsl`, `ExposureMeter.hlsl`
  - 대상: `Hitori\Source\Runtime\Render\PostProcess\` 및 `Renderer.{h,cpp}`, `SceneRenderer.{h,cpp}`
- [x] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드 + Deferred/Forward 양쪽에서 DOF, Fog, 반투명, LensFlare, AutoExposure, ToneMap 정상 동작 확인

---

### Phase 4. 에디터 선택·Undo 아키텍처 & 3D 썸네일
**목표**: 다중 액터 프로퍼티 편집, 커맨드 패턴 Undo/Redo, 콘텐츠 드로어 3D 썸네일 렌더러.

- [x] **선택 시스템 델리게이트 & `DetailsPanel` 다중 선택 편집**
  - 참조: `Kurisu\Source\Editor\KurisuEd\EditorSelection.{h,cpp}`, `Kurisu\Source\Editor\Details\DetailsPanel.{h,cpp}`
  - 대상: `Hitori\Source\Editor\Selection.h` (14주차 `USelection` 유지 + `OnChanged`/배치 가드 보강 또는 `UEditorEngine::NoteSelectionChange` 연동), `Hitori\Source\Editor\Details\DetailsPanel.{h,cpp}` (`SetTargets` 다중 선택 공통 프로퍼티 편집)
- [x] **커맨드 기반 Undo/Redo (`FEditorUndoHistory`) 전환**
  - 참조: `Kurisu\Source\Editor\KurisuEd\EditorUndo.{h,cpp}`
  - 대상: `Hitori\Source\Editor\HitoriEd\EditorUndo.{h,cpp}`(신규), `EditorEngine.{h,cpp}`, `DetailsPanel.{h,cpp}`, `OutlinerPanel.{h,cpp}`, `Gizmo`
  - 전략: Transform / Property / Spawn·Delete·Duplicate / Component 편집은 `IEditorUndoCommand`(`FName` 기반)로 처리하고, 미지원 복합 편집용 범용 스냅샷 폴백 커맨드를 함께 두어 `FLevelUndoHistory`를 안전하게 대체. `SavedStateId` 기반 Dirty 추적 포함.
- [x] **콘텐츠 드로어 3D 오프스크린 썸네일 (`FMeshThumbnailRenderer`)**
  - 참조: `Kurisu\Source\Editor\ContentDrawer\MeshThumbnailRenderer.{h,cpp}`, `Kurisu\EngineResources\Shaders\ThumbnailCheckerShader.hlsl`, `Kurisu\Source\Editor\ContentDrawer\ContentDrawerPanel.cpp`
  - 대상: `Hitori\Source\Editor\ContentDrawer\MeshThumbnailRenderer.{h,cpp}`(신규), `ContentDrawerPanel.{h,cpp}`, `EditorEngine.cpp` (`RenderPending` 프레임당 1개 렌더)
- [x] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드 + 다중 선택 편집, Undo/Redo(삭제 후 복원 포함), 3D 썸네일 렌더링 확인

---

### Phase 5. 에셋 에디터 고도화 (5-A / 5-B 2회 분할 권장)

#### Phase 5-A: Material / PhysicalMaterial / StaticMesh 충돌 편집기
- [ ] **Material Editor**
  - 참조: `Kurisu\Source\Editor\MaterialEditor\**`
  - 대상: `Hitori\Source\Editor\MaterialEditor\**` + `EditorManager` / `ContentDrawerPanel` `.mat` 더블클릭 연결
- [ ] **Physical Material Editor**
  - 참조: `Kurisu\Source\Editor\PhysicalMaterialEditor\**`
  - 대상: `Hitori\Source\Editor\PhysicalMaterialEditor\**` + `.pmat` 연결
- [ ] **StaticMesh Editor 충돌 편집기 (`StaticMeshDetailsPanel`)**
  - 참조: `Kurisu\Source\Editor\StaticMeshEditor\Panels\StaticMeshDetailsPanel.cpp`, `Kurisu\Source\Editor\StaticMeshEditor\StaticMeshEditor.{h,cpp}`, `Kurisu\Source\Runtime\Render\Mesh.{h,cpp}` (`.bodysetup` 저장/복원/Undo)
  - 대상: `Hitori\Source\Editor\StaticMeshEditor\**`, `Hitori\Source\Runtime\Render\Mesh.{h,cpp}` (14주차 LOD 유지하면서 Auto-Fit Box/Sphere/Capsule/Convex & `.bodysetup` 저장 추가)
- [ ] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드

#### Phase 5-B: PhysicsDebug / Skeletal / AnimTimeline / ParticleCurve UX 보강
- [x] **PhysicsAssetEditor 3D 셰이딩 디버그 렌더러**
  - 참조: `Kurisu\Source\Editor\PhysicsAssetEditor\PhysicsAssetDebugRenderer.{h,cpp}`, `Kurisu\EngineResources\Shaders\PhysicsDebugShader.hlsl`
  - 대상: `Hitori\Source\Editor\PhysicsAssetEditor\**`
- [ ] **SkeletalMeshEditor 보강**
  - 참조: `Kurisu\Source\Editor\SkeletalMeshEditor\SkeletalMeshEditor.{h,cpp}`, `Panels\SkeletalMeshClothingPanel.{h,cpp}`, `Kurisu\Source\Runtime\Render\SceneRenderer.cpp` (본 가중치 히트맵 와이어 오버레이)
  - 대상: `Hitori\Source\Editor\SkeletalMeshEditor\**` (본 포즈 Undo/Redo, 본 가중치 히트맵, Cloth Paint Brush/Fill/Smooth + Undo/Redo)
- [x] **AnimTimelinePanel 보강**
  - 참조: `Kurisu\Source\Editor\AnimSequenceEditor\Panels\AnimTimelinePanel.{h,cpp}`
  - 대상: `Hitori\Source\Editor\AnimSequenceEditor\Panels\AnimTimelinePanel.{h,cpp}` (트랙 필터, 아이콘 트랜스포트 바, Notify Record 팝업)
- [x] **ParticleCurveEditor 보강**
  - 참조: `Kurisu\Source\Runtime\Curves\CurveKeyUtils.{h,cpp}`, `Kurisu\Source\Editor\ParticleSystemEditor\Panels\ParticleSystemCurveEditorPanel.{h,cpp}`
  - 대상: `Hitori\Source\Runtime\Curves\CurveKeyUtils.{h,cpp}`(신규), `Hitori\Source\Editor\ParticleEditor\Panels\ParticleEditorPanels.{h,cpp}` (Arrive/Leave 탄젠트 핸들, 다중 키 선택, 트랙 숨김 토글)
- [ ] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` 빌드

---

### Phase 6. 런타임 모듈 구조 정리 (Physics Profile / Clothing / Audio)
**목표**: 런타임 서브시스템 경계 정리 및 에셋화 마무리.

- [ ] **Physics `CollisionProfile` & `PhysicalMaterialFile`**
  - 참조: `Kurisu\Source\Runtime\Physics\CollisionProfile.{h,cpp}`, `PhysicsFiltering.{h,cpp}`, `PhysicalMaterialFile.{h,cpp}`, `BodySetupCore.{h,cpp}`
  - 대상: `Hitori\Source\Runtime\Physics\` (14주차 `PhysicsSettings`, `PhysicsHandle`, `Collision.h` 유지하면서 UE식 `ECollisionPreset` + 채널 응답 매트릭스 + `.pmat` 파일화 통합)
- [ ] **`Runtime/Clothing` 모듈 독립화**
  - 참조: `Kurisu\Source\Runtime\Clothing\**`
  - 대상: `Hitori\Source\Runtime\Clothing\**` 신규 구성 + 14주차 `SkeletalMeshData` / `SkeletalMeshComponent`에 분산된 NvCloth 시뮬레이션 로직을 `FClothingSimulationNv` / `UClothingAssetCommon`으로 캡슐화
- [ ] **`Runtime/Audio` UObject 에셋화**
  - 참조: `Kurisu\Source\Runtime\Audio\SoundBase.h`, `SoundWave.{h,cpp}`
  - 대상: `Hitori\Source\Runtime\Audio\SoundBase.h`, `SoundWave.{h,cpp}`
- [ ] **검증**: `SlnGenerator.bat --no-pause` + `Debug|x64` & `Release|x64` 최종 빌드 통과

---

## 4. Claude에게 단계별로 내릴 명령어 (복붙용)

> 사용법: `JG_Week14` 폴더에서 Claude Code를 실행한 뒤, 한 Phase씩 아래 프롬프트를 그대로 붙여넣으세요. Phase가 끝나고 커밋한 뒤 `/compact` 또는 새 세션으로 다음 Phase를 진행하면 가장 안정적입니다.

### Phase 0
```text
UPGRADE_PLAN.md를 읽고 규칙(0~2절)을 숙지한 뒤 Phase 0만 진행해줘.
작업 전 새 브랜치(refactor/week14-upgrade)를 만들고, 완료 후 SlnGenerator.bat --no-pause 실행 및 Debug|x64 빌드 통과까지 확인한 뒤 UPGRADE_PLAN.md 체크박스를 업데이트해줘.
```

### Phase 1
```text
UPGRADE_PLAN.md를 읽고 Phase 1만 진행해줘.
- 14주차 기존 SettingsPanel과 툴바 기능은 유지하면서 ShowFlagsValues.h로 통합
- 기본 에디터 테마는 Dark
- 스냅 기본값은 14주차 월드 단위에 맞춰 설정
완료 후 SlnGenerator.bat --no-pause 및 Debug|x64 빌드 확인하고 체크박스 업데이트해줘.
```

### Phase 2
```text
UPGRADE_PLAN.md를 읽고 Phase 2만 진행해줘.
14주차의 StaticMesh LOD, Deferred GBuffer(StaticMeshGBufferShader.hlsl), Cloth 스키닝, 기존 EPSOType 숫자 호환성(Reserved_13 등)을 절대 깨뜨리지 말고 FVertexFactory + UMaterial/MaterialFile을 통합해줘.
완료 후 프로젝트 재생성 및 Debug|x64 빌드 통과까지 확인하고 체크박스 업데이트해줘.
```

### Phase 3
```text
UPGRADE_PLAN.md를 읽고 Phase 3만 진행해줘.
SceneRenderer의 인라인 DOF를 FDepthOfFieldRenderer로 분리하고, Fog와 반투명(BeforeDOF/AfterDOF 포함)을 HDR(LightingHDR)에서 처리한 뒤 단일 AddToneMapPasses로 통합해줘.
LensFlareRenderer와 ExposureMeterRenderer(Auto Exposure)도 추가하되, 에디터 뷰포트의 Auto Exposure 기본값은 OFF로 해줘.
완료 후 프로젝트 재생성 및 Debug|x64 빌드 통과까지 확인하고 체크박스 업데이트해줘.
```

### Phase 4
```text
UPGRADE_PLAN.md를 읽고 Phase 4만 진행해줘.
14주차 USelection 구조는 유지하면서 델리게이트와 DetailsPanel 다중 선택 편집을 붙이고, Undo는 FEditorUndoHistory(커맨드 패턴 + 미지원 편집용 스냅샷 폴백 커맨드)로 전환해줘.
ContentDrawer에 FMeshThumbnailRenderer 3D 오프스크린 썸네일(128x128, 프레임당 1개)도 연동하고 빌드 검증 후 체크박스 업데이트해줘.
```

### Phase 5-A
```text
UPGRADE_PLAN.md를 읽고 Phase 5-A만 진행해줘.
MaterialEditor, PhysicalMaterialEditor, StaticMeshDetailsPanel(Auto-Fit Box/Sphere/Capsule/Convex + .bodysetup 저장/복원/Undo)을 구현하고 ContentDrawer 더블클릭으로 열리게 연결해줘.
14주차 StaticMesh LOD는 유지하고, 프로젝트 재생성 및 Debug|x64 빌드 통과까지 확인 후 체크박스 업데이트해줘.
```

### Phase 5-B
```text
UPGRADE_PLAN.md를 읽고 Phase 5-B만 진행해줘.
PhysicsAssetDebugRenderer(3D 셰이딩), SkeletalMeshEditor(본 포즈 Undo·가중치 히트맵·Cloth Paint 툴), AnimTimelinePanel(필터·아이콘·Record 팝업), ParticleCurveEditor(탄젠트 핸들·다중 키·트랙 숨김)를 보강해줘.
프로젝트 재생성 및 Debug|x64 빌드 통과까지 확인 후 체크박스 업데이트해줘.
```

### Phase 6
```text
UPGRADE_PLAN.md를 읽고 Phase 6만 진행해줘.
CollisionProfile 프리셋 + PhysicalMaterialFile을 14주차 PhysX 필터와 연결하고(기존 PhysicsSettings/PhysicsHandle 유지), NvCloth 로직을 Runtime/Clothing 모듈로 분리, Audio에 USoundBase/USoundWave를 추가해줘.
프로젝트 재생성 후 Debug|x64와 Release|x64 모두 빌드 통과 확인하고 체크박스 업데이트해줘.
```
