#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimGameMode.generated.h"

class AShipPawn;

UCLASS()
class SHIPAUTONOMYSIM_API ASimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimGameMode();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditAnywhere, Category=Stage3Test)
	TSubclassOf<AShipPawn> ShipPawnClass;

	UPROPERTY(EditAnywhere, Category=Stage3Test)
	FTransform TestShipSpawnTransform = FTransform::Identity;

	void EnsureTestShipForFirstPlayer();

#if WITH_DEV_AUTOMATION_TESTS
	int32 TestBeginPlayInvocationCount = 0;
	friend struct FSimGameModeTestAccessor;
#endif
};
