local ComparisonTime = 0.5
local Elapsed = 0
local Continued = false
local ReplayHeld = false

function Tick(DeltaTime)
    -- 키를 놓을 때 한 번만 재시작
    if IsInputKeyDown("R") then
        ReplayHeld = true
    elseif ReplayHeld then
        ReplayHeld = false
        OpenLevel("ClothGridComparison")
        return
    end

    if Continued then
        return
    end

    -- 물리의 프레임당 최대 진행량(60Hz 여섯 스텝)에 맞춘 비교 시각
    Elapsed = Elapsed + math.min(DeltaTime, 6 / 60)
    if Elapsed + 0.000001 < ComparisonTime then
        return
    end

    -- 구를 감싼 순간의 관찰과 재생 재개
    if IsInputKeyDown("Space") then
        Continued = true
        StartHitStop(0, 1)
    else
        StartHitStop(1, 0)
    end
end
