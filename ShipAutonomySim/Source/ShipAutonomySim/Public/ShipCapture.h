#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipCapture.generated.h"

struct FShipCaptureFrameRecord
{
	int32 Index = INDEX_NONE;
	FString ColorLeafName;
	FString DepthLeafName;
	int64 TimeMs = 0;
};

UCLASS()
class SHIPAUTONOMYSIM_API UShipCapture : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipCapture();
};
