#include "SimGameMode.h"

#include "ShipPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ShipNavigationSimulation.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimGameMode, Log, All);

ASimGameMode::ASimGameMode()
{
	DefaultPawnClass = nullptr;
	ShipPawnClass = AShipPawn::StaticClass();
	TestShipSpawnTransform = FTransform(FRotator::ZeroRotator, FVector::ZeroVector);
}

void ASimGameMode::ReportRuntimeCalculationError(
    EShipRuntimeCalculationError Error)
{
    if (LatchRuntimeCalculationError(Error, RuntimeErrorState))
    {
        UE_LOG(
            LogSimGameMode,
            Error,
            TEXT("Stage4RuntimeCalculationError error=%d"),
            static_cast<int32>(Error));
    }
}

void ASimGameMode::BeginPlay()
{
	Super::BeginPlay();

#if WITH_DEV_AUTOMATION_TESTS
	++TestBeginPlayInvocationCount;
#endif
	EnsureTestShipForFirstPlayer();
}

void ASimGameMode::EnsureTestShipForFirstPlayer()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!IsValid(PC))
	{
		UE_LOG(LogTemp, Error, TEXT("Stage 3 ship spawn requires a player controller"));
		return;
	}
	if (IsValid(Cast<AShipPawn>(PC->GetPawn())))
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AShipPawn* Ship = GetWorld()->SpawnActor<AShipPawn>(
		ShipPawnClass, TestShipSpawnTransform, Params);
	if (!IsValid(Ship))
	{
		UE_LOG(LogTemp, Error, TEXT("Stage 3 ship spawn failed"));
		return;
	}
	PC->Possess(Ship);
}
