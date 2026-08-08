#include "SimGameMode.h"

#include "ShipPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ASimGameMode::ASimGameMode()
{
	DefaultPawnClass = nullptr;
	ShipPawnClass = AShipPawn::StaticClass();
	TestShipSpawnTransform = FTransform(FRotator::ZeroRotator, FVector::ZeroVector);
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
