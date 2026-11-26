// 표시(Show) 플래그 단일 목록.
// 여기에 한 줄을 추가하면 FEngineShowFlags 멤버, 메타데이터, editor.ini 저장/불러오기가 함께 생긴다.
// 이 파일은 SHOWFLAG 매크로를 정의한 뒤에만 포함한다. #pragma once를 두지 않는다.
//
// SHOWFLAG(이름, 게임 기본값, 에디터 기본값, 분류, 아이콘, 메뉴 표시 이름)
//   Hidden 분류는 뷰 모드·코드가 매 프레임 정하므로 메뉴/저장에서 제외할 수 있다.

#ifndef SHOWFLAG
#error "SHOWFLAG(Name, bGameDefault, bEditorDefault, Category, Icon, DisplayName) must be defined before including ShowFlagsValues.h"
#endif

// ---------- Common ----------
SHOWFLAG(Rendering,       true,  true,  Common,      "ShowPrimitives", "Primitives / Rendering")
SHOWFLAG(Decals,          true,  true,  Common,      "ShowDecal",      "Decals")
SHOWFLAG(SkeletalMeshes,  true,  true,  Common,      "SkeletalMesh",   "Skeletal Meshes")
SHOWFLAG(Particles,       true,  true,  Common,      "Particle",       "Particles")
SHOWFLAG(Bounds,          false, false, Common,      "Bounds",         "Bounding Boxes")
SHOWFLAG(Grid,            false, true,  Common,      "Grid",           "Batch Line / Grid")

// ---------- Physics ----------
SHOWFLAG(Collision,       false, true,  Physics,     "Bounds",         "Collision (PIE)")
SHOWFLAG(PhysicsBodies,   false, false, Physics,     "Bounds",         "Physics Bodies")
SHOWFLAG(Constraints,     false, false, Physics,     "Bounds",         "Physics Constraints")

// ---------- Post Process ----------
SHOWFLAG(Fog,             true,  true,  PostProcess, "ShowFog",           "Height Fog")
SHOWFLAG(DepthOfField,    true,  true,  PostProcess, "Camera",            "Depth of Field")
SHOWFLAG(LensFlares,      true,  true,  PostProcess, "Camera",            "Lens Flares")
SHOWFLAG(EyeAdaptation,   true,  true,  PostProcess, "Camera",            "Eye Adaptation (Auto Exposure)")
SHOWFLAG(Vignette,        true,  true,  PostProcess, "ShowVignette",      "Vignette")
SHOWFLAG(CameraOverlay,   true,  true,  PostProcess, "ShowCameraOverlay", "Camera Fade / Letterbox")

// ---------- Developer ----------
SHOWFLAG(VisualizeDOF,    false, false, Developer,   "Camera",         "Visualize DOF")
SHOWFLAG(CascadeBounds,   false, false, Developer,   "ViewCascade",    "Cascade Bounds")
SHOWFLAG(ObjectUUID,      false, false, Developer,   "Pivot",          "Object UUID")
SHOWFLAG(RootBone,        false, false, Developer,   "SkeletalMesh",   "Root Bone (PIE)")

// ---------- Render ----------
SHOWFLAG(AntiAliasing,    false, false, Render,      "ShowAntiAliasing", "Anti-Aliasing (FXAA)")
