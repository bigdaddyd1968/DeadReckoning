#pragma once

#include "DataLinkNode.h"
#include "Nodes/Script/DataLinkScriptNode.h"

#include "NSAIDataLinkEchoNode.generated.h"

USTRUCT(BlueprintType)
struct FNSAIDataLinkPerson
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Link")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data Link")
	int32 Score = 0;
};

UCLASS()
class UNSAIDataLinkEchoNode : public UDataLinkNode
{
	GENERATED_BODY()

public:
	static FName GetInputPinName();

protected:
	virtual void OnBuildPins(FDataLinkPinBuilder& Inputs, FDataLinkPinBuilder& Outputs) const override;
	virtual EDataLinkExecutionReply OnExecute(FDataLinkExecutor& InExecutor) const override;

private:
	UPROPERTY(EditAnywhere, Category = "Data Link")
	FString Prefix = TEXT("Echo: ");
};

UCLASS()
class UNSAIDataLinkPersonSummaryNode : public UDataLinkNode
{
	GENERATED_BODY()

public:
	static FName GetInputPinName();

protected:
	virtual void OnBuildPins(FDataLinkPinBuilder& Inputs, FDataLinkPinBuilder& Outputs) const override;
	virtual EDataLinkExecutionReply OnExecute(FDataLinkExecutor& InExecutor) const override;
};

UCLASS()
class UNSAIDataLinkScriptEchoNode : public UDataLinkScriptNode
{
	GENERATED_BODY()

public:
	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

protected:
	virtual UWorld* GetWorld() const override;
};
