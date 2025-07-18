// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvaluationVM/Tasks/ExecuteProgram.h"

#include "EvaluationVM/EvaluationProgram.h"
#include "EvaluationVM/EvaluationVM.h"

FAnimNextExecuteProgramTask FAnimNextExecuteProgramTask::Make(TSharedPtr<const UE::AnimNext::FEvaluationProgram> Program)
{
	FAnimNextExecuteProgramTask Task;
	Task.Program = Program;
	return Task;
}

void FAnimNextExecuteProgramTask::Execute(UE::AnimNext::FEvaluationVM& VM) const
{
	if (Program.IsValid())
	{
		Program->Execute(VM);
	}
}
