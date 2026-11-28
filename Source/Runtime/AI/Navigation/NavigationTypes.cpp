#include "EnginePCH.h"
#include "AI/Navigation/NavigationTypes.h"

#include "Core/ConfigFile.h"

namespace
{
	const FConfigFile& GetNavigationConfig()
	{
		static const FConfigFile Config = []
		{
			FConfigFile Result;
			Result.Load("Config/DefaultGame.ini");
			return Result;
		}();
		return Config;
	}

	float ReadNavigationFloat(const char* Key, float Default)
	{
		const FString Text = GetNavigationConfig().GetString("Navigation", Key, "");

		const char* Start = Text.c_str();
		char* End = nullptr;
		const float Value = std::strtof(Start, &End);
		
		if (End == Start || *End != '\0' || !std::isfinite(Value))
		{
			return Default;
		}
		return Value;
	}
}

const FNavAgentProperties& GetDefaultNavAgent()
{
	static const FNavAgentProperties Agent = []
	{
		FNavAgentProperties Result;
		const float Radius = ReadNavigationFloat("AgentRadius", Result.AgentRadius);
		const float Height = ReadNavigationFloat("AgentHeight", Result.AgentHeight);
		const float Slope = ReadNavigationFloat("AgentMaxSlope", Result.AgentMaxSlope);
		const float Step = ReadNavigationFloat("AgentStepHeight", Result.AgentStepHeight);

		if (Radius > 0.0f && Height > 2.0f * Radius)
		{
			Result.AgentRadius = Radius;
			Result.AgentHeight = Height;
		}
		if (Slope > 0.0f && Slope < 90.0f)
		{
			Result.AgentMaxSlope = Slope;
		}
		if (Step >= 0.0f)
		{
			Result.AgentStepHeight = Step;
		}
		return Result;
	}();
	return Agent;
}

const FNavMeshResolution& GetDefaultNavResolution()
{
	static const FNavMeshResolution Resolution = []
	{
		FNavMeshResolution Result;
		const float CellSize = ReadNavigationFloat("CellSize", Result.CellSize);
		const float CellHeight = ReadNavigationFloat("CellHeight", Result.CellHeight);

		if (CellSize > 0.0f)
		{
			Result.CellSize = CellSize;
		}
		if (CellHeight > 0.0f)
		{
			Result.CellHeight = CellHeight;
		}
		return Result;
	}();
	return Resolution;
}
