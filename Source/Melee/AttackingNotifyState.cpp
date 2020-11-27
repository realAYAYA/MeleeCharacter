// Fill out your copyright notice in the Description page of Project Settings.


#include "AttackingNotifyState.h"
#include "Components/SkeletalMeshComponent.h"
#include "MeleeCharacter.h"

void UAttackingNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration)
{
	if (MeshComp) {
		AMeleeCharacter* Role = Cast<AMeleeCharacter>(MeshComp->GetOwner());
		if (Role) {
			Role->OnAttackEnableChanged(true);
		}
	}
}

void UAttackingNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (MeshComp) {
		AMeleeCharacter* Role = Cast<AMeleeCharacter>(MeshComp->GetOwner());
		if (Role) {
			Role->OnAttackEnableChanged(false);
		}
	}
}
