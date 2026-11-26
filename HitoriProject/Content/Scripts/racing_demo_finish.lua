-- The car starts behind the line. Its first crossing starts the lap.
local gates = {
    { axis = "x", direction =  1, center = -46 }, -- start
    { axis = "y", direction =  1, center = 128 }, -- east curve
    { axis = "x", direction = -1, center =  46 }, -- back straight
    { axis = "y", direction = -1, center = -128 }, -- west curve
    { axis = "x", direction =  1, center = -46 }, -- finish
}

local halfStraight = 82
local halfRoad = 7
local vehicle
local previous
local nextGate = 1

local function isOnCourse(position)
    -- Leave room for the curb and runoff, but reject cuts through the infield.
    local beyondStraight = math.max(0, math.abs(position.x) - halfStraight)
    local radiusSquared = beyondStraight^2 + position.y^2
    return radiusSquared >= 36^2 and radiusSquared <= 58^2
end

local function crossesGate(from, to, gate)
    local alongX = gate.axis == "x"
    local before = (alongX and from.x or from.y) * gate.direction
    local after = (alongX and to.x or to.y) * gate.direction
    if before >= 0 or after < 0 then return false end

    -- Interpolate at the crossing so a fast car cannot skip a thin gate.
    local alpha = -before / (after - before)
    local lateralBefore = alongX and from.y or from.x
    local lateralAfter = alongX and to.y or to.x
    local lateral = lateralBefore + (lateralAfter - lateralBefore) * alpha
    return math.abs(lateral - gate.center) <= halfRoad
end

function BeginPlay()
    vehicle = assert(GetActorByName("RacingDemoVehicle"), "Racing demo vehicle is missing")
    previous = vehicle.Location
end

function Tick()
    local gate = gates[nextGate]
    if not gate or not vehicle then return end

    local current = vehicle.Location
    if not isOnCourse(current) then
        nextGate = 1
    elseif crossesGate(previous, current, gate) then
        nextGate = nextGate + 1
        if nextGate > #gates then
            obj:PlaySequence()
            assert(obj:IsSequencePlaying(), "Racing demo finish sequence did not start")
            print("Racing demo lap complete")
        end
    end
    previous = current
end
