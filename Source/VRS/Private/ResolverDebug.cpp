// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#include "ResolverDebug.h"

FResolverDebug::FResolverDebug()
{
	UpdateFilePath();
	Enable();
	PreExitHandle = FCoreDelegates::OnPreExit.AddLambda([this]
	{
		RecordChanges(0.f);
	});
	
#if WITH_EDITOR
	EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool)
	{
		RecordChanges(0.f);
	});
#endif
}

FResolverDebug::~FResolverDebug()
{
	Disable();
	FCoreDelegates::OnPreExit.Remove(PreExitHandle);
#if WITH_EDITOR
	FEditorDelegates::EndPIE.Remove(EndPIEHandle);
#endif
	RecordChanges(0.f);
}

TSharedPtr<FJsonObject> FResolverDebug::CreateJSONObject(const FChangeEntry& Entry)
{
	auto Object = MakeShared<FJsonObject>();
	
	Object->SetStringField(TEXT("AdvertName"), FString(Entry.AdvertName));
	Object->SetStringField(TEXT("ResolverName"),Entry.ResolverName);
	Object->SetStringField(TEXT("TargetName"), Entry.TargetName);
	Object->SetStringField(TEXT("ChangerName"), Entry.ChangerName);
	Object->SetStringField(TEXT("OperationName"), Entry.OperationName);
	
	Object->SetStringField(TEXT("ChangeTime"), Entry.ChangeTime.ToString());
	Object->SetStringField(TEXT("FlushTime"), Entry.FlushTime.ToString());
	
	Object->SetStringField(TEXT("OldValue"), Entry.OldValue);
	Object->SetStringField(TEXT("NewValue"), Entry.NewValue);
	Object->SetNumberField(TEXT("Layer"), Entry.Layer);
	Object->SetBoolField(TEXT("ChangeRemoved"), Entry.bChangeRemoved);
	return Object;
}

void FResolverDebug::UpdateFilePath()
{
	const auto Dir = FPaths::ProjectSavedDir() / TEXT("VRS");
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*Dir);
    
	const auto FileName = FString::Printf(TEXT("VRS_%s.jsonl"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	FilePath = Dir / FileName;
}

bool FResolverDebug::RecordChanges(float Unused)
{
	if (ChangeBuffer.IsEmpty() || FilePath.IsEmpty()) return true;

	FString Payload;
	int32 WrittenCount = 0;
	for (const auto& Entry : ChangeBuffer)
	{
		FString Line;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		FJsonSerializer::Serialize(CreateJSONObject(Entry).ToSharedRef(), Writer);

		Payload += Line;
		Payload += LINE_TERMINATOR;
		++WrittenCount;
	}

	
	if (RecordCount >= MaxRecordAmount)
	{
		UpdateFilePath();
		FFileHelper::SaveStringToFile(Payload, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8, &IFileManager::Get());
		// The fresh file already contains this payload — start counting from it.
		RecordCount = WrittenCount;
	}
	else
	{
		FFileHelper::SaveStringToFile(Payload, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8, 
		&IFileManager::Get(), FILEWRITE_Append);
	}
	ChangeBuffer.Reset();
	return true;
}

static FAutoConsoleCommand FCmdVRSDebugEnable(
	TEXT("VRS.Debug.Enable"),
	TEXT("EnableVRS debug"),
	FConsoleCommandDelegate::CreateStatic([]
	{
		auto& DebugSystem = FResolverDebug::Get();
		DebugSystem.Enable();
	})
);

static FAutoConsoleCommand FCmdVRSDebugDisable(
	TEXT("VRS.Debug.Disable"),
	TEXT("Disable VRS debug"),
	FConsoleCommandDelegate::CreateStatic([]
	{
		auto& DebugSystem = FResolverDebug::Get();
		DebugSystem.Disable();
	})
);

static FAutoConsoleCommand FCmdVRSDebugRecord(
	TEXT("VRS.Debug.Record"),
	TEXT("Record to the file all new changes"),
	FConsoleCommandDelegate::CreateStatic([]
	{
		auto& DebugSystem = FResolverDebug::Get();
		DebugSystem.RecordChanges();
	})
);

static FAutoConsoleCommand FCmdVRSDebugSetRecordRate(
	TEXT("VRS.Debug.SetRecordRate"),
	TEXT("Change record rate of the changes to the file"),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.IsEmpty()) return;
		auto& DebugSystem = FResolverDebug::Get();
		DebugSystem.SetRecordRate(FCString::Atof(*Args[0]));
	})
);
