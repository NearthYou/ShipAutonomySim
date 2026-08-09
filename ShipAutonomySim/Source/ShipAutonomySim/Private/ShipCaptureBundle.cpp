#include "ShipCaptureBundle.h"

#include "Containers/Set.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Math/NumericLimits.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr ANSICHAR BundleMagic[] = "SIVPACK1";
constexpr int32 BundleMagicSize = 8;
constexpr int32 BundlePrefixSize = BundleMagicSize + 4;
constexpr int32 BundleVersion = 1;
constexpr uint8 PngSignature[] = {
    0x89,
    0x50,
    0x4e,
    0x47,
    0x0d,
    0x0a,
    0x1a,
    0x0a};

bool HasPngSignature(const TArray64<uint8>& Bytes)
{
    if (Bytes.Num() < UE_ARRAY_COUNT(PngSignature))
    {
        return false;
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(PngSignature); ++Index)
    {
        if (Bytes[Index] != PngSignature[Index])
        {
            return false;
        }
    }
    return true;
}
}

bool BuildShipCaptureBundle(
    const FString& ManifestJson,
    const TArray<FShipCaptureBundleAsset>& Assets,
    TArray64<uint8>& OutBundleBytes)
{
    OutBundleBytes.Reset();
    if (ManifestJson.IsEmpty() || Assets.IsEmpty())
    {
        return false;
    }

    TSet<FString> SeenPaths;
    int64 PayloadSize = 0;
    TArray<TSharedPtr<FJsonValue>> AssetValues;
    AssetValues.Reserve(Assets.Num());
    for (const FShipCaptureBundleAsset& Asset : Assets)
    {
        if (Asset.Path.IsEmpty() ||
            Asset.MediaType != TEXT("image/png") ||
            SeenPaths.Contains(Asset.Path) ||
            !HasPngSignature(Asset.Bytes) ||
            Asset.Bytes.Num() > TNumericLimits<uint32>::Max() ||
            PayloadSize > TNumericLimits<int64>::Max() - Asset.Bytes.Num())
        {
            return false;
        }

        const TSharedRef<FJsonObject> AssetObject =
            MakeShared<FJsonObject>();
        AssetObject->SetStringField(TEXT("path"), Asset.Path);
        AssetObject->SetStringField(TEXT("media_type"), Asset.MediaType);
        AssetObject->SetNumberField(
            TEXT("offset"),
            static_cast<double>(PayloadSize));
        AssetObject->SetNumberField(
            TEXT("length"),
            static_cast<double>(Asset.Bytes.Num()));
        AssetValues.Add(MakeShared<FJsonValueObject>(AssetObject));
        SeenPaths.Add(Asset.Path);
        PayloadSize += Asset.Bytes.Num();
    }

    const TSharedRef<FJsonObject> HeaderObject = MakeShared<FJsonObject>();
    HeaderObject->SetStringField(
        TEXT("format"),
        TEXT("ship-image-sequence"));
    HeaderObject->SetNumberField(TEXT("version"), BundleVersion);
    HeaderObject->SetStringField(TEXT("manifest_json"), ManifestJson);
    HeaderObject->SetArrayField(TEXT("assets"), MoveTemp(AssetValues));

    FString HeaderJson;
    const TSharedRef<
        TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<
            TCHAR,
            TCondensedJsonPrintPolicy<TCHAR>>::Create(&HeaderJson);
    if (!FJsonSerializer::Serialize(HeaderObject, Writer) ||
        HeaderJson.IsEmpty())
    {
        return false;
    }

    const FTCHARToUTF8 HeaderUtf8(*HeaderJson);
    const int32 HeaderSize = HeaderUtf8.Length();
    if (HeaderSize <= 0 ||
        static_cast<uint64>(HeaderSize) >
            static_cast<uint64>(TNumericLimits<uint32>::Max()) ||
        PayloadSize >
            TNumericLimits<int64>::Max() - BundlePrefixSize - HeaderSize)
    {
        return false;
    }

    TArray64<uint8> Candidate;
    Candidate.Reserve(BundlePrefixSize + HeaderSize + PayloadSize);
    Candidate.Append(
        reinterpret_cast<const uint8*>(BundleMagic),
        BundleMagicSize);
    const uint32 HeaderSizeUnsigned = static_cast<uint32>(HeaderSize);
    Candidate.Add(static_cast<uint8>(HeaderSizeUnsigned & 0xff));
    Candidate.Add(static_cast<uint8>((HeaderSizeUnsigned >> 8) & 0xff));
    Candidate.Add(static_cast<uint8>((HeaderSizeUnsigned >> 16) & 0xff));
    Candidate.Add(static_cast<uint8>((HeaderSizeUnsigned >> 24) & 0xff));
    Candidate.Append(
        reinterpret_cast<const uint8*>(HeaderUtf8.Get()),
        HeaderSize);
    for (const FShipCaptureBundleAsset& Asset : Assets)
    {
        Candidate.Append(Asset.Bytes);
    }

    if (Candidate.Num() != BundlePrefixSize + HeaderSize + PayloadSize)
    {
        return false;
    }
    OutBundleBytes = MoveTemp(Candidate);
    return true;
}
