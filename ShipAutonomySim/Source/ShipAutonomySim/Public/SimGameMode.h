#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShipNavigationTypes.h"
#include "SimGameMode.generated.h"

class AShipPawn;

UCLASS()
class SHIPAUTONOMYSIM_API ASimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimGameMode();
    void ReportRuntimeCalculationError(EShipRuntimeCalculationError Error);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditAnywhere, Category=Stage3Test)
	TSubclassOf<AShipPawn> ShipPawnClass;

	UPROPERTY(EditAnywhere, Category=Stage3Test)
	FTransform TestShipSpawnTransform = FTransform::Identity;

    FShipRuntimeErrorState RuntimeErrorState;

	void EnsureTestShipForFirstPlayer();

#if WITH_DEV_AUTOMATION_TESTS
	int32 TestBeginPlayInvocationCount = 0;
	friend struct FSimGameModeTestAccessor;
    friend struct FShipNavigationGameModeTestAccessor;
#endif
};
