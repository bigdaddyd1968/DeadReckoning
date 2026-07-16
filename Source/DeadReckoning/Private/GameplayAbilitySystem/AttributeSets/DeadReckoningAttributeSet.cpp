// Copyright 2026 by M. Duane Forkner Jr, Speed Demon Games and Vegas Visual Design, LLP. All rights reserved.


#include "GameplayAbilitySystem/AttributeSets/DeadReckoningAttributeSet.h"
#include "Net/UnrealNetwork.h"

UDeadReckoningAttributeSet::UDeadReckoningAttributeSet()
{
	Health = 100.f;
	MaxHealth = 100.f;
	Stamina = 100.f;
	MaxStamina = 100.f;
}

void UDeadReckoningAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UDeadReckoningAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDeadReckoningAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDeadReckoningAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDeadReckoningAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
}


