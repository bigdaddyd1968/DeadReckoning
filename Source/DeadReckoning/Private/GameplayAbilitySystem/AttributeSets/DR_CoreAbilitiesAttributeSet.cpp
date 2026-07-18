// Copyright 2026 by M. Duane Forkner Jr, Speed Demon Games and Vegas Visual Design, LLP. All rights reserved.


#include "GameplayAbilitySystem/AttributeSets/DR_CoreAbilitiesAttributeSet.h"
#include "Net/UnrealNetwork.h"

UDR_CoreAbilitiesAttributeSet::UDR_CoreAbilitiesAttributeSet()
{
	Strength = 100.0f;
	Dexterity = 100.0f;
	Intelligence = 100.0f;
	Constitution = 100.0f;
	Charisma = 100.0f;
	Wisdom = 100.0f;
	Luck = 100.0f;
}

void UDR_CoreAbilitiesAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Strength);
	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Dexterity);
	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Intelligence);
	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Constitution);
	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Charisma);
	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Wisdom);
	DOREPLIFETIME(UDR_CoreAbilitiesAttributeSet, Luck);
}
