-- 망토 캐릭터가 월드 Y축으로 좌우 왕복하며 진행 방향을 바라본다. R 키로 처음 상태로 되돌린다.

local Amplitude = 1.0
local Period = 4.0
local Start
local Comp
local Elapsed = 0

function BeginPlay()
    Start = obj.Location
    Comp = GetComponentByName("USkeletalMeshComponent")
end

function Tick(dt)
    if IsKeyPressed("R") then
        Elapsed = 0
        obj.Location = Start
        obj.Rotation = FRotator(0, 90, 0)
        if Comp then Comp:ForceClothNextUpdateTeleportAndReset() end
        return
    end

    Elapsed = Elapsed + dt
    local Phase = 2 * math.pi * Elapsed / Period
    obj.Location = FVector(Start.x, Start.y + Amplitude * math.sin(Phase), Start.z)
    obj.Rotation = FRotator(0, math.cos(Phase) >= 0 and 90 or -90, 0)
end
