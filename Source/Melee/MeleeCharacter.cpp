// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeleeCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"

//////////////////////////////////////////////////////////////////////////
// AMeleeCharacter
const float AMeleeCharacter::ExitReadyTime = 4;
AMeleeCharacter::AMeleeCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)

	// Melee
	static ConstructorHelpers::FObjectFinder<UDataTable>
		AttackMontageDataTableObject(TEXT("DataTable'/Game/DataTables/AttackMontageTable.AttackMontageTable'"));
	if (AttackMontageDataTableObject.Succeeded()) {
		AttackMontageDataTable = AttackMontageDataTableObject.Object;
	}

	bIsStanding = true;
	/*
	static ConstructorHelpers::FObjectFinder<UAnimMontage>
		PunchMontageObject(TEXT("AnimMontage'/Game/Combat/Animations/PunchMontage.PunchMontage'"));
	if (PunchMontageObject.Succeeded()){
		PunchMontage = PunchMontageObject.Object;
	}
	*/
	static ConstructorHelpers::FObjectFinder<USoundBase>
		AttackSoundObject(TEXT("SoundWave'/Game/Combat/Audios/AttackSound.AttackSound'"));
	if (AttackSoundObject.Succeeded()) {
		AttackSound = AttackSoundObject.Object;
	}

	AttackAudioComp = CreateDefaultSubobject<UAudioComponent>("AttackAudioComp");
	AttackAudioComp->SetupAttachment(RootComponent);
	AttackAudioComp->bAutoActivate = false;
	if (AttackSound) {
		AttackAudioComp->SetSound(AttackSound);
	}

	LeftAttackBox = CreateDefaultSubobject<UBoxComponent>("LeftAttackBox");
	LeftAttackBox->SetupAttachment(GetMesh(), "hand_lSocket");
	LeftAttackBox->SetCollisionProfileName("WeaponP");
	LeftAttackBox->SetNotifyRigidBodyCollision(true);

	RightAttackBox = CreateDefaultSubobject<UBoxComponent>("RightAttackBox");
	RightAttackBox->SetupAttachment(GetMesh(), "hand_rSocket");
	RightAttackBox->SetCollisionProfileName("WeaponP");
	RightAttackBox->SetNotifyRigidBodyCollision(true);

}

//////////////////////////////////////////////////////////////////////////
// Input

void AMeleeCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Punch", IE_Pressed, this, &AMeleeCharacter::Punch);
	PlayerInputComponent->BindAction("Kick", IE_Pressed, this, &AMeleeCharacter::Kick);
	PlayerInputComponent->BindAction("Crouch", IE_Pressed, this, &AMeleeCharacter::CrouchStart);

	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &AMeleeCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AMeleeCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AMeleeCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AMeleeCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &AMeleeCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &AMeleeCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &AMeleeCharacter::OnResetVR);
}


void AMeleeCharacter::OnAttackEnableChanged(bool Enable)
{
	if (Enable) {
		LeftAttackBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		RightAttackBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		bIsStanding = true;
		LeftAttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RightAttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AMeleeCharacter::BeginPlay()
{
	Super::BeginPlay();
	LeftAttackBox->OnComponentHit.AddDynamic(this, &AMeleeCharacter::OnAttackHit);
	RightAttackBox->OnComponentHit.AddDynamic(this, &AMeleeCharacter::OnAttackHit);
}

void AMeleeCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void AMeleeCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
	Jump();
}

void AMeleeCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
	StopJumping();
}

void AMeleeCharacter::CrouchStart()
{
	if (bIsCrouched) {
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void AMeleeCharacter::ReadyToFight()
{
	bReady = !bReady;
	UWorld* World = GetWorld();
	if (World) {
		World->GetTimerManager().ClearTimer(ExitReadyTimer);
		if (bReady) {
			World->GetTimerManager().SetTimer(ExitReadyTimer, this, &AMeleeCharacter::ExitReadyTimeout,ExitReadyTime);
		}
	}
}

void AMeleeCharacter::ExitReadyTimeout()
{
	bReady = false;
}

void AMeleeCharacter::Punch()
{
	Attack(EAttackType::PUNCH);
}

void AMeleeCharacter::Kick()
{
	Attack(EAttackType::KICK);
}

void AMeleeCharacter::Attack(EAttackType Type)
{
	if (!bReady) {
		ReadyToFight();
	}

	if (AttackMontageDataTable) {
		FName RowName;
		switch (Type)
		{
		case EAttackType::PUNCH:
			RowName = "Punch";
			break;
		case EAttackType::KICK:
			if (bIsStanding) {
				RowName = "Kick";
				bIsStanding = false;
			}
			break;
		default:
			break;
		}
		FAttackMontage* AttackMontage = AttackMontageDataTable->FindRow<FAttackMontage>(RowName, TEXT("Montage"));
		if (AttackMontage && AttackMontage->Montage) {
			int32 count = AttackMontage->Montage->CompositeSections.Num();
			if (count > 0) {
				int32 id = FMath::Rand() % count;
				PlayAnimMontage(AttackMontage->Montage, 1.0f, AttackMontage->Montage->CompositeSections[id].SectionName);
			}
		}
	}
}

void AMeleeCharacter::OnAttackHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (AttackAudioComp) {
		AttackAudioComp->SetPitchMultiplier(FMath::RandRange(.5f, 4.f));
		AttackAudioComp->Play();
	}
}

void AMeleeCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AMeleeCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AMeleeCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f) && bIsStanding)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void AMeleeCharacter::MoveRight(float Value)
{
	if ( (Controller != NULL) && (Value != 0.0f) && bIsStanding)
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}
