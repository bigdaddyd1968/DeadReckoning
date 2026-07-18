// Copyright 2026 by M. Duane Forkner Jr, Speed Demon Games and Vegas Visual Design, LLP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "DR_CoreAbilitiesAttributeSet.generated.h"

/**
 * @class UDR_CoreAbilitiesAttributeSet
 * @brief A class for managing core abilities and attributes in Unreal Engine.
 *
 * The UDR_CoreAbilitiesAttributeSet serves as a base class for defining and managing
 * a collection of core ability-related attributes for game characters or entities. These
 * attributes typically represent character stats such as health, stamina, mana, and
 * other values essential to gameplay mechanics.
 *
 * This class is intended to be used in the context of Unreal Engine's Gameplay Ability
 * System (GAS). It works in conjunction with gameplay effects and abilities to modify
 * and use these attributes dynamically during gameplay.
 *
 * Key elements of this class are attribute replication, event binding, and ensuring
 * synchronization across the server and clients.
 *
 * Design Notes:
 * - Attributes in this class are typically float values representing numerical character
 *   stats.
 * - Changes to these attributes can trigger relevant gameplay events.
 * - Use GameplayEffects to reliably modify attributes within this system.
 *
 * Common Examples of Attributes:
 * - Health
 * - Stamina/Energy
 * - Mana
 * - Strength and Dexterity
 * - Magic Power
 *
 * Key Features:
 * - Provides a structured way to organize and manage gameplay-relevant attributes.
 * - Integrates seamlessly with Unreal Engine's Gameplay Ability System.
 * - Supports server-client synchronization to ensure all players see the same attribute values.
 * - Enables customization and extension for game-specific use cases.
 *
 * Intended Usage:
 * - Extend this class to define your game's specific core attributes.
 * - Add member properties for the attributes you wish to manage.
 * - Leverage Unreal Engine's replication and binding features to synchronize
 *   attributes and listen to changes.
 */
UCLASS()
class DEADRECKONING_API UDR_CoreAbilitiesAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
	
public:
	// Constructor
	UDR_CoreAbilitiesAttributeSet();
	
	// Core Ability Attributes
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Strength;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Strength)
	
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Dexterity;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Dexterity)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Intelligence;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Intelligence)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Constitution;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Constitution)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Charisma;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Charisma)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Wisdom;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Wisdom)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Core Abilities")
	FGameplayAttributeData Luck;
	ATTRIBUTE_ACCESSORS_BASIC(UDR_CoreAbilitiesAttributeSet, Luck)
	
public:
	UFUNCTION()
	void OnRep_Strength(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Strength, OldValue)
	}
	
	UFUNCTION()
	void OnRep_Dexterity(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Dexterity, OldValue)
	}
	
	UFUNCTION()
	void OnRep_Intelligence(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Intelligence, OldValue)
	}
	
	UFUNCTION()
	void OnRep_Constitution(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Constitution, OldValue)
	}
	
	UFUNCTION()
	void OnRep_Charisma(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Charisma, OldValue)
	}
	
	UFUNCTION()
	void OnRep_Wisdom(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Wisdom, OldValue)
	}
	
	UFUNCTION()
	void OnRep_Luck(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UDR_CoreAbilitiesAttributeSet, Luck, OldValue)
	}
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
