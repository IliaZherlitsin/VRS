// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ValueAdverts.h"
#include "VRSTestObject.generated.h"

/**
 * Embedded struct used by the automation tests to exercise nested
 * (member-of-member) value registration via TInnerValueAdvert.
 */
USTRUCT()
struct FVRSTestSettings
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Level = 0;
};

/**
 * Minimal UObject owner used by the VRS automation tests.
 *
 * Exposes one member per supported registration shape: a float and an int32
 * for direct adverts, an FVector for non-scalar types, and an embedded struct
 * for nested adverts. Default values are asserted by the tests — change them
 * together with the expectations in VRSTests.cpp.
 */
UCLASS()
class UVRSTestObject : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	float Health = 100.f;

	UPROPERTY()
	int32 Armor = 10;

	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	UPROPERTY()
	FVRSTestSettings Settings;
};

/** Statically-allocated adverts shared by every automation test. */
namespace FVRSTestObjectAdverts
{
	inline TValueAdvert TestHealth(TEXT("Health"), &UVRSTestObject::Health);
	inline TValueAdvert TestArmor(TEXT("Armor"), &UVRSTestObject::Armor);
	inline TValueAdvert TestPosition(TEXT("Position"), &UVRSTestObject::Position);
	inline TInnerValueAdvert TestLevel(TEXT("Level"), &UVRSTestObject::Settings, &FVRSTestSettings::Level);
}
