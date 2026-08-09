#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"

struct FShipCaptureBundleAsset
{
    FString Path;
    FString MediaType;
    TArray64<uint8> Bytes;
};

bool BuildShipCaptureBundle(
    const FString& ManifestJson,
    const TArray<FShipCaptureBundleAsset>& Assets,
    TArray64<uint8>& OutBundleBytes);
