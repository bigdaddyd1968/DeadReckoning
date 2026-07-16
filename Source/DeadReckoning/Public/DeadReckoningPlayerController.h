// Copyright 2026 by M. Duane Forkner Jr, Speed Demon Games and Vegas Visual Design, LLP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DeadReckoningPlayerController.generated.h"

class UInputMappingContext;

/**
 * Abstract Player Controller class for the Dead Reckoning framework.
 * Handles input mapping contexts and gameplay setup.
 */
UCLASS(abstract)
class DEADRECKONING_API ADeadReckoningPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:
	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;
};
