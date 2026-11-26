local RayDirection = FVector(1, 0, 0)
local bMoved = false
local bCheckAfterMove = false

local function CheckRay(Label, Y, ExpectedX)
    -- 쿼리 위치와 기대 결과의 비교
    local Hit = Raycast(FVector(0, Y, 2), RayDirection)
    local bMatches = (Hit == nil and ExpectedX == nil)
        or (Hit ~= nil and ExpectedX ~= nil and math.abs(Hit.x - ExpectedX) < 0.01)
    local Actual = Hit and string.format("x=%.3f", Hit.x) or "miss"
    local Expected = ExpectedX and string.format("x=%.3f", ExpectedX) or "miss"
    print(string.format("[QueryVerify] %s %s actual=%s expected=%s",
        bMatches and "PASS" or "FAIL", Label, Actual, Expected))
end

local function CheckMovingCube()
    CheckRay("MovingCube original lane", -3, not bMoved and 4.5 or nil)
    CheckRay("MovingCube alternate lane", -5, bMoved and 4.5 or nil)
end

function BeginPlay()
    -- 고정 대상과 이동 전 기준 위치의 검사
    bMoved = false
    bCheckAfterMove = false
    CheckRay("FixedCube", 0, 4.5)
    CheckMovingCube()
    CheckRay("Empty lane", -8, nil)
    print("[QueryVerify] Press T in PIE to move MovingCube and test both lanes on the next tick.")
end

function Tick(dt)
    -- 월드 틱에서 렌더 BVH 갱신 후 두 위치의 검사
    if bCheckAfterMove then
        bCheckAfterMove = false
        CheckMovingCube()
    end

    if not IsKeyPressed("T") then
        return
    end

    local MovingCube = GetActorByName("MovingCube")
    if not MovingCube then
        print("[QueryVerify] FAIL MovingCube actor missing")
        return
    end

    -- 키네마틱 메시의 검증 위치 전환
    bMoved = not bMoved
    MovingCube.Location = FVector(5, bMoved and -5 or -3, 2)
    bCheckAfterMove = true
end
