-- T 키로 래그돌을 켜고 끈다. R 키는 demo_physics_reset.lua와 같이 처음 상태로 되돌린다.
-- 액터 하나에 스크립트가 하나뿐이라 리셋 코드를 여기에도 둔다.

local Saved = nil

function BeginPlay()
    local Comp = GetComponentByName("USkeletalMeshComponent")
    if Comp then
        Saved = { Comp = Comp, Location = Comp.Location, Rotation = Comp.Rotation, Simulate = Comp.SimulatePhysics }
    end
end

function Tick(dt)
    if IsKeyPressed("T") then
        obj:SetSimulatePhysics(not obj:IsSimulatingPhysics())
    end
    if IsKeyPressed("R") and Saved then
        Saved.Comp.SimulatePhysics = Saved.Simulate
        Saved.Comp:ResetPose()
        Saved.Comp.Location = Saved.Location
        Saved.Comp.Rotation = Saved.Rotation
        Saved.Comp:RecreatePhysicsState()
        Saved.Comp:ForceClothNextUpdateTeleportAndReset()
    end
end
