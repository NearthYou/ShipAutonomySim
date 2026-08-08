#include "ShipPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "ShipMovement.h"
#include "UObject/ConstructorHelpers.h"

AShipPawn::AShipPawn()
{
	PrimaryActorTick.bCanEverTick = false;

    CollisionRoot =
        CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionRoot"));
    SetRootComponent(CollisionRoot);
    CollisionRoot->InitBoxExtent(FVector(100.0, 50.0, 50.0));
    CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CollisionRoot->SetCollisionObjectType(ECC_Pawn);
    CollisionRoot->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionRoot->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    CollisionRoot->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    CollisionRoot->SetSimulatePhysics(false);
    CollisionRoot->SetEnableGravity(false);

    VisualMesh =
        CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
    VisualMesh->SetupAttachment(CollisionRoot);
    VisualMesh->SetRelativeScale3D(FVector(2.0, 1.0, 1.0));
    VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisualMesh->SetSimulatePhysics(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded())
    {
        VisualMesh->SetStaticMesh(Cube.Object);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Engine cube mesh load failed"));
    }

    ShipMovement =
        CreateDefaultSubobject<UShipMovement>(TEXT("ShipMovement"));
    CameraBoom =
        CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(CollisionRoot);
    CameraBoom->bUsePawnControlRotation = false;
    CameraBoom->bInheritYaw = true;
    CameraBoom->bInheritPitch = false;
    CameraBoom->bInheritRoll = false;
    CameraBoom->bDoCollisionTest = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;
}

void AShipPawn::BeginPlay()
{
    Super::BeginPlay();
    CameraBoom->TargetArmLength = CameraArmLengthCm;
    CameraBoom->SetRelativeRotation(FRotator(CameraPitchDegrees, 0.0, 0.0));
    CameraBoom->SocketOffset =
        FVector(0.0, 0.0, CameraSocketHeightCm);
}

void AShipPawn::BuildManualInputObjects()
{
    if (IsValid(ThrottleAction))
    {
        return;
    }

    ThrottleAction = NewObject<UInputAction>(this, TEXT("ThrottleAction"));
    SteerAction = NewObject<UInputAction>(this, TEXT("SteerAction"));
    ManualControlMapping = NewObject<UInputMappingContext>(
        this,
        TEXT("ManualControlMapping"));
    for (UInputAction* Action : {ThrottleAction.Get(), SteerAction.Get()})
    {
        Action->ValueType = EInputActionValueType::Axis1D;
        Action->AccumulationBehavior =
            EInputActionAccumulationBehavior::Cumulative;
    }

    ManualControlMapping->MapKey(ThrottleAction, EKeys::W);
    FEnhancedActionKeyMapping& S =
        ManualControlMapping->MapKey(ThrottleAction, EKeys::S);
    S.Modifiers.Add(NewObject<UInputModifierNegate>(ManualControlMapping));
    ManualControlMapping->MapKey(SteerAction, EKeys::D);
    FEnhancedActionKeyMapping& A =
        ManualControlMapping->MapKey(SteerAction, EKeys::A);
    A.Modifiers.Add(NewObject<UInputModifierNegate>(ManualControlMapping));
}

void AShipPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    DeactivateManualInput();
    BuildManualInputObjects();

    UEnhancedInputComponent* Enhanced =
        Cast<UEnhancedInputComponent>(PlayerInputComponent);
    APlayerController* PlayerController =
        Cast<APlayerController>(GetController());
    ULocalPlayer* LocalPlayer = IsValid(PlayerController)
        ? PlayerController->GetLocalPlayer()
        : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = IsValid(LocalPlayer)
        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
            LocalPlayer)
        : nullptr;
    if (!IsValid(Enhanced) || !IsValid(Subsystem))
    {
        UE_LOG(LogTemp, Error, TEXT("Manual Enhanced Input is unavailable"));
        return;
    }

    Enhanced->BindAction(
        ThrottleAction,
        ETriggerEvent::Triggered,
        this,
        &AShipPawn::HandleThrottle);
    Enhanced->BindAction(
        ThrottleAction,
        ETriggerEvent::Completed,
        this,
        &AShipPawn::HandleThrottleCompleted);
    Enhanced->BindAction(
        ThrottleAction,
        ETriggerEvent::Canceled,
        this,
        &AShipPawn::HandleThrottleCanceled);
    Enhanced->BindAction(
        SteerAction,
        ETriggerEvent::Triggered,
        this,
        &AShipPawn::HandleSteer);
    Enhanced->BindAction(
        SteerAction,
        ETriggerEvent::Completed,
        this,
        &AShipPawn::HandleSteerCompleted);
    Enhanced->BindAction(
        SteerAction,
        ETriggerEvent::Canceled,
        this,
        &AShipPawn::HandleSteerCanceled);
    Subsystem->AddMappingContext(ManualControlMapping, 0);
    RegisteredInputSubsystem = Subsystem;
    bManualMappingRegistered = true;
    bManualInputActive = true;
}

void AShipPawn::ResetManualInput()
{
    ShipMovement->SetThrottle(0.0f);
    ShipMovement->SetSteer(0.0f);
}

void AShipPawn::DeactivateManualInput()
{
    ResetManualInput();
    bManualInputActive = false;
    if (bManualMappingRegistered && RegisteredInputSubsystem.IsValid() &&
        IsValid(ManualControlMapping))
    {
        RegisteredInputSubsystem->RemoveMappingContext(ManualControlMapping);
#if WITH_DEV_AUTOMATION_TESTS
        ++TestManualMappingRemovalCount;
#endif
    }
    bManualMappingRegistered = false;
    RegisteredInputSubsystem.Reset();
}

void AShipPawn::LockManualInputForAutonomy()
{
    bAutonomyInputLocked = true;
    DeactivateManualInput();
    if (ShipMovement != nullptr)
    {
        ShipMovement->SetThrottle(0.0f);
        ShipMovement->SetSteer(0.0f);
    }
}

void AShipPawn::HandleThrottle(const FInputActionValue& Value)
{
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }

    ShipMovement->SetThrottle(
        FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f));
}

void AShipPawn::HandleSteer(const FInputActionValue& Value)
{
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }


    ShipMovement->SetSteer(
        FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f));
}

void AShipPawn::HandleThrottleCompleted()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestThrottleCompletedCount;
#endif
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }
    HandleThrottleReleased();
}

void AShipPawn::HandleThrottleCanceled()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestThrottleCanceledCount;
#endif
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }
    HandleThrottleReleased();
}

void AShipPawn::HandleSteerCompleted()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestSteerCompletedCount;
#endif
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }
    HandleSteerReleased();
}

void AShipPawn::HandleSteerCanceled()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestSteerCanceledCount;
#endif
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }
    HandleSteerReleased();
}

void AShipPawn::HandleThrottleReleased()
{
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }

    ShipMovement->SetThrottle(0.0f);
}

void AShipPawn::HandleSteerReleased()
{
    if (bAutonomyInputLocked || !bManualInputActive)
    {
        return;
    }

    ShipMovement->SetSteer(0.0f);
}

void AShipPawn::UnPossessed()
{
    DeactivateManualInput();
    Super::UnPossessed();
}

void AShipPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DeactivateManualInput();
    Super::EndPlay(EndPlayReason);
}
