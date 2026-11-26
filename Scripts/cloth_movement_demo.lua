local CharacterSpeed = 1.6
local CameraSpeed = 2.8
local LookDegreesPerPixel = 0.18
local TurnDegreesPerSecond = 240
local StrafeAngle = 25
local Character

-- 반대 방향 키를 함께 누르면 상쇄되는 입력축
local function ReadAxis(PositiveKey, NegativeKey)
    return (IsInputKeyDown(PositiveKey) and 1 or 0) - (IsInputKeyDown(NegativeKey) and 1 or 0)
end

-- 우클릭 중 카메라 전방·우측 이동과 월드 높이 조절
local function MoveFreeCamera(DeltaTime, ForwardInput, RightInput)
    local MouseX, MouseY = GetInputMouseDelta()
    local Rotation = obj.Rotation
    if MouseX ~= 0 or MouseY ~= 0 then
        Rotation = FRotator(math.max(-85, math.min(85, Rotation.pitch + MouseY * LookDegreesPerPixel)),
            Rotation.yaw + MouseX * LookDegreesPerPixel, 0)
        obj.Rotation = Rotation
    end

    local UpInput = ReadAxis("E", "Q")
    if ForwardInput == 0 and RightInput == 0 and UpInput == 0 then
        return
    end
    local Yaw, Pitch = math.rad(Rotation.yaw), math.rad(Rotation.pitch)
    local Forward = FVector(math.cos(Yaw) * math.cos(Pitch), math.sin(Yaw) * math.cos(Pitch), -math.sin(Pitch))
    local Right = FVector(-math.sin(Yaw), math.cos(Yaw), 0)
    local Direction = Forward * ForwardInput + Right * RightInput + FVector(0, 0, UpInput)
    obj.Location = obj.Location + Direction:Normalize() * (CameraSpeed * DeltaTime)
end

-- 시점 기준 보행과 캐릭터 이동량을 따르는 카메라
local function MoveCharacter(DeltaTime, ForwardInput, RightInput)
    if ForwardInput == 0 and RightInput == 0 then
        return
    end
    if not Character or not Character:IsValid() then
        Character = GetActorByName("DKM Cloak")
        if not Character then
            return
        end
    end

    local CameraYaw = obj.Rotation.yaw
    local Yaw = math.rad(CameraYaw)
    local Forward = FVector(math.cos(Yaw), math.sin(Yaw), 0)
    local Right = FVector(-math.sin(Yaw), math.cos(Yaw), 0)
    local Direction = (Forward * ForwardInput + Right * RightInput):Normalize()
    local Displacement = Direction * (CharacterSpeed * DeltaTime)
    Character.Location = Character.Location + Displacement
    obj.Location = obj.Location + Displacement

    -- 뒤로 걸을 때 등을 돌리지 않고 옆걸음에만 작은 회전 적용
    local CharacterYaw = Character.Rotation.yaw
    local YawDifference = (CameraYaw + RightInput * StrafeAngle - CharacterYaw + 180) % 360 - 180
    local MaxTurn = TurnDegreesPerSecond * DeltaTime
    Character.Rotation = FRotator(0, CharacterYaw + math.max(-MaxTurn, math.min(MaxTurn, YawDifference)), 0)
end

function BeginPlay()
    print("[Cloth] WASD: move character | Hold RMB: look and fly with WASD, Q down / E up")
end

function Tick(DeltaTime)
    local ForwardInput = ReadAxis("W", "S")
    local RightInput = ReadAxis("D", "A")
    if IsInputKeyDown("RightMouseButton") then
        MoveFreeCamera(DeltaTime, ForwardInput, RightInput)
    else
        MoveCharacter(DeltaTime, ForwardInput, RightInput)
    end
end
