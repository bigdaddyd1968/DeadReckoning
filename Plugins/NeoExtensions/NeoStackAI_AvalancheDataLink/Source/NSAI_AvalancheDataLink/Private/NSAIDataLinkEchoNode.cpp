#include "NSAIDataLinkEchoNode.h"

#include "DataLinkCoreTypes.h"
#include "DataLinkExecutor.h"
#include "DataLinkInputDataViewer.h"
#include "DataLinkNames.h"
#include "DataLinkNodeInstance.h"
#include "DataLinkOutputDataViewer.h"
#include "DataLinkPinBuilder.h"
#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"

FName UNSAIDataLinkEchoNode::GetInputPinName()
{
	return TEXT("Input");
}

void UNSAIDataLinkEchoNode::OnBuildPins(FDataLinkPinBuilder& Inputs, FDataLinkPinBuilder& Outputs) const
{
	Inputs.Add(GetInputPinName())
		.SetStruct<FDataLinkString>();

	Outputs.Add(UE::DataLink::OutputDefault)
		.SetStruct<FDataLinkString>();
}

EDataLinkExecutionReply UNSAIDataLinkEchoNode::OnExecute(FDataLinkExecutor& InExecutor) const
{
	const FDataLinkNodeInstance& NodeInstance = InExecutor.GetNodeInstance(this);
	const FDataLinkString& InputString = NodeInstance.GetInputDataViewer().Get<FDataLinkString>(GetInputPinName());
	FDataLinkString& OutputString = NodeInstance.GetOutputDataViewer().Get<FDataLinkString>(UE::DataLink::OutputDefault);
	OutputString.Value = Prefix + InputString.Value;
	InExecutor.Next(this);
	return EDataLinkExecutionReply::Handled;
}

FName UNSAIDataLinkPersonSummaryNode::GetInputPinName()
{
	return TEXT("Person");
}

void UNSAIDataLinkPersonSummaryNode::OnBuildPins(FDataLinkPinBuilder& Inputs, FDataLinkPinBuilder& Outputs) const
{
	Inputs.Add(GetInputPinName())
		.SetStruct<FNSAIDataLinkPerson>();

	Outputs.Add(UE::DataLink::OutputDefault)
		.SetStruct<FDataLinkString>();
}

EDataLinkExecutionReply UNSAIDataLinkPersonSummaryNode::OnExecute(FDataLinkExecutor& InExecutor) const
{
	const FDataLinkNodeInstance& NodeInstance = InExecutor.GetNodeInstance(this);
	const FNSAIDataLinkPerson& Person = NodeInstance.GetInputDataViewer().Get<FNSAIDataLinkPerson>(GetInputPinName());
	FDataLinkString& OutputString = NodeInstance.GetOutputDataViewer().Get<FDataLinkString>(UE::DataLink::OutputDefault);
	OutputString.Value = FString::Printf(TEXT("%s:%d"), *Person.Name, Person.Score);
	InExecutor.Next(this);
	return EDataLinkExecutionReply::Handled;
}

void UNSAIDataLinkScriptEchoNode::ProcessEvent(UFunction* Function, void* Parms)
{
	if (!Function || Function->GetFName() != TEXT("OnExecute"))
	{
		Super::ProcessEvent(Function, Parms);
		return;
	}

	struct FGetInputDataParams
	{
		FInstancedStruct InputData;
		FName InputName;
		bool ReturnValue = false;
	};

	FGetInputDataParams GetInputParams;
	GetInputParams.InputName = TEXT("Input");
	if (UFunction* GetInputFunction = FindFunction(TEXT("GetInputData")))
	{
		Super::ProcessEvent(GetInputFunction, &GetInputParams);
	}

	if (!GetInputParams.ReturnValue || GetInputParams.InputData.GetScriptStruct() != FDataLinkString::StaticStruct())
	{
		if (UFunction* FailFunction = FindFunction(TEXT("Fail")))
		{
			Super::ProcessEvent(FailFunction, nullptr);
		}
		return;
	}

	const FDataLinkString& InputString = GetInputParams.InputData.Get<FDataLinkString>();
	FDataLinkString OutputString;
	OutputString.Value = TEXT("Script Echo: ") + InputString.Value;

	struct FSucceedParams
	{
		FInstancedStruct OutputData;
		bool bPersistExecution = false;
		bool ReturnValue = false;
	};

	FSucceedParams SucceedParams;
	SucceedParams.OutputData = FInstancedStruct::Make(OutputString);
	if (UFunction* SucceedFunction = FindFunction(TEXT("Succeed")))
	{
		Super::ProcessEvent(SucceedFunction, &SucceedParams);
	}
}

UWorld* UNSAIDataLinkScriptEchoNode::GetWorld() const
{
	return GWorld;
}
