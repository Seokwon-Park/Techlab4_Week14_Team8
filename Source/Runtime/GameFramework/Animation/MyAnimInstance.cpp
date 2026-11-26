#include "EnginePCH.h"
#include "MyAnimInstance.h"
#include "Animation/AnimSequenceFile.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFrameWork/Pawn.h"

// 諛쒖냼由?Walk/Jog??PlaySound)? 怨듦꺽 ?먯젙(Punch_Jab??AttackWindow) Notify??.anim.bin????λ뤌 ?덈떎.
// AnimSequence ?먮뵒?곗쓽 ??꾨씪?몄뿉???몄쭛?쒕떎.

void UMyAnimInstance::NativeInitializeAnimation()
{
	USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(GetOuter());

	//AnimNode ?ㅼ젙
	int32 Loco = StateMachine.AddState("Locomotion", &LocomotionPlayer);
	// Hit ?곹깭瑜?鍮좎졇?섍????쒓컙 ?쇨꺽 ?뚮옒洹몃? ?대┛??
	FAnimStateDesc HitState;
	HitState.Name = "Hit";
	HitState.Pose.SetLinkNode(&HitPlayer);
	HitState.OnExit = [this](UAnimInstance*) { bIsHit = false; };
	int32 Hit = StateMachine.AddState(HitState);

	// Attack ?곹깭瑜?鍮좎졇?섍????쒓컙(?뺤긽 醫낅즺??Hit???딄린?? 怨듦꺽 ?뚮옒洹몃? ?대┛??
	FAnimStateDesc AttackState;
	AttackState.Name = "Attack";
	AttackState.Pose.SetLinkNode(&AttackPlayer);
	AttackState.OnExit = [this](UAnimInstance*) { bIsAttacking = false; };
	int32 Attack = StateMachine.AddState(AttackState);

	//TEMP : BlendSpace ?앹꽦 諛??ㅼ젙
	auto LoadSequence = [&](const char* Path) -> UAnimSequence*
		{
			auto* Sequence = NewObject<UAnimSequence>(SkeletalMesh);
			FString Error;

			if (!AnimSequenceFile::Read(Path, *Sequence, Error))
			{
				HTR_LOG(Error, "Animation load failed: {} ({})", Path, Error);
				delete Sequence;
				return nullptr;
			}

			return Sequence;
		};

	auto* Idle = LoadSequence(
		"Content/Models/UAL1_Standard/UAL1_Standard_Armature_Idle_Loop.anim.bin");
	auto* Walk = LoadSequence(
		"Content/Models/UAL1_Standard/UAL1_Standard_Armature_Walk_Loop.anim.bin");
	auto* Running = LoadSequence(
		"Content/Models/UAL1_Standard/UAL1_Standard_Armature_Jog_Fwd_Loop.anim.bin");

	auto* BlendSpace = NewObject<UBlendSpace1D>();

	BlendSpace->AddSample(Idle, FVector(0, 0, 0));
	BlendSpace->AddSample(Walk, FVector(200, 0, 0));
	BlendSpace->AddSample(Running, FVector(550, 0, 0));
	LocomotionPlayer.BlendSpace = BlendSpace;
	LocomotionPlayer.X = 0.f;

	AttackPlayer.Sequence = LoadSequence(
		"Content/Models/UAL1_Standard/UAL1_Standard_Armature_Punch_Jab.anim.bin");
	AttackPlayer.bLooping = false;

	HitPlayer.Sequence = LoadSequence(
		"Content/Models/UAL1_Standard/UAL1_Standard_Armature_Hit_Chest.anim.bin");
	HitPlayer.bLooping = false;

	StateMachine.SetInitialState(Loco);

	FAnimTransitionDesc LocoToAttack;
	LocoToAttack.From = Loco;
	LocoToAttack.To = Attack;
	LocoToAttack.Rule = [this](const FAnimTransitionContext&) { return bIsAttacking; };
	StateMachine.AddTransition(LocoToAttack);

	FAnimTransitionDesc AttackToLoco;
	AttackToLoco.From = Attack;
	AttackToLoco.To = Loco;
	AttackToLoco.Rule = [this](const FAnimTransitionContext&) { return AttackPlayer.IsFinished(); };
	StateMachine.AddTransition(AttackToLoco);

	//AnyTransition?쇰줈 Hit 異붽?
	FAnimTransitionDesc AnyToHit;
	AnyToHit.From = INDEX_NONE;
	AnyToHit.To = Hit;
	AnyToHit.Priority = -1;
	AnyToHit.CrossfadeDuration = 0.1f;
	AnyToHit.Rule = [this](const FAnimTransitionContext&) { return bIsHit; };
	StateMachine.AddTransition(AnyToHit);

	// Hit ?대┰???앸굹硫?Locomotion?쇰줈 蹂듦? (OnExit?먯꽌 bIsHit = false)
	FAnimTransitionDesc HitToLoco;
	HitToLoco.From = Hit;
	HitToLoco.To = Loco;
	HitToLoco.Rule = [this](const FAnimTransitionContext&) { return HitPlayer.IsFinished(); };
	StateMachine.AddTransition(HitToLoco);

	RootNode = &StateMachine;
}

void UMyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (APawn* Pawn = TryGetPawnOwner())
	{
		// UE GroundSpeed: ?섑룊 ?띾룄留? cm/s ?⑥쐞 (BlendSpace ?섑뵆??UE 媛?湲곗?)
		const FVector Velocity = Pawn->GetVelocity();
		Speed = FVector(Velocity.X, Velocity.Y, 0.0f).Size() * WorldToCm;
	}

	LocomotionPlayer.X = Speed;                     // UE ?먯꽌??? 諛붿씤?⑹씠 ??좏븳??
}
