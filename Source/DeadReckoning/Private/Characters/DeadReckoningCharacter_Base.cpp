// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/DeadReckoningCharacter_Base.h"

// Sets default values
ADeadReckoningCharacter_Base::ADeadReckoningCharacter_Base()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ADeadReckoningCharacter_Base::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ADeadReckoningCharacter_Base::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ADeadReckoningCharacter_Base::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

