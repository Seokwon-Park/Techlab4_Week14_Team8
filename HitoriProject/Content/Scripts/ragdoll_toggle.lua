-- R 키로 래그돌을 켜고 끈다. 스켈레탈 메시 액터의 ScriptPath에 붙인다.
-- 켜면 걷던 자세 그대로 쓰러지고, 끄면 쓰러진 자리에서 애니메이션이 다시 본을 채운다.

function Tick(dt)
    if IsKeyPressed("R") then
        obj:SetSimulatePhysics(not obj:IsSimulatingPhysics())
    end
end
