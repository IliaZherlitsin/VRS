// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Resolver.h"
#include "ValueBatch.h"
#include "ChangeBatch.h"
#include "Misc/AutomationTest.h"
#include "Tests/VRSTestObject.h"

/** Storage bounds shared by every test that does not probe the bounds themselves. */
inline constexpr FValueSettings GTestSettings{10, 5};

#define VRS_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ===========================================================================
// Operations and basic flushing
// ===========================================================================

/**
 * Transient change lifecycle: a change is applied exactly once by Flush,
 * after which its key is retired and can no longer be removed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSBaseChangeTest, "VRS.Resolver.BaseChangeTest", VRS_TEST_FLAGS)

bool FVRSBaseChangeTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	FChangeKey Key;
	const FChangeContext Ctx(TEXT("Test"), 0);

	TestTrue(TEXT("First change accepted"), Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add), Ctx, &Key));
	Resolver.Flush(false);

	TestTrue(TEXT("Second change accepted"), Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add), Ctx, &Key));
	Resolver.Flush(false);

	TestEqual(TEXT("Health after two transient adds"), Obj->Health, 160.f);
	TestFalse(TEXT("Transient key is retired after flush"), Resolver.IsKeyValid(Key));
	TestFalse(TEXT("RemoveChange fails for a retired key"), Resolver.RemoveChange(Key));
	return true;
}

/**
 * Every arithmetic operation produces the documented result.
 * Applied sequentially so each step also re-verifies the running value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSOperationsTest, "VRS.Resolver.OperationsTest", VRS_TEST_FLAGS)

bool FVRSOperationsTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);
	const auto Apply = [&](const float Value, const EValueOperation Op)
	{
		Resolver.AddChange(HealthType, Value, FChangeSettings(0, Op), Ctx);
		Resolver.Flush(false);
	};

	Apply(50.f, EValueOperation::Set);
	TestEqual(TEXT("Set overwrites the value"), Obj->Health, 50.f);

	Apply(25.f, EValueOperation::Add);
	TestEqual(TEXT("Add increases the value"), Obj->Health, 75.f);

	Apply(5.f, EValueOperation::Sub);
	TestEqual(TEXT("Sub decreases the value"), Obj->Health, 70.f);

	Apply(2.f, EValueOperation::Mul);
	TestEqual(TEXT("Mul scales the value"), Obj->Health, 140.f);

	Apply(2.f, EValueOperation::Div);
	TestEqual(TEXT("Div divides the value"), Obj->Health, 70.f);
	return true;
}

/**
 * Division by zero must be rejected at flush time: the value stays untouched
 * and the failure is reported through the error log, not silently swallowed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSDivByZeroTest, "VRS.Resolver.DivByZeroTest", VRS_TEST_FLAGS)

bool FVRSDivByZeroTest::RunTest(const FString& Parameters)
{
	// The layer logs an error for the failed flush — expect it so the test
	// framework does not treat the intentional error as a test failure.
	AddExpectedError(TEXT("Failed to flush change of advert"), EAutomationExpectedErrorFlags::Contains, 1);

	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	FChangeKey Key;
	const FChangeContext Ctx(TEXT("Test"), 0);
	TestTrue(TEXT("Div-by-zero change is queued (rejected only at flush)"),
		Resolver.AddChange(HealthType, 0.f, FChangeSettings(0, EValueOperation::Div), Ctx, &Key));

	Resolver.Flush(false);

	TestEqual(TEXT("Value untouched by the failed division"), Obj->Health, 100.f);
	// The failed transient change still leaves the layer — its key must be retired.
	TestFalse(TEXT("Failed transient key is retired after flush"), Resolver.IsKeyValid(Key));
	return true;
}

// ===========================================================================
// Static change lifecycle
// ===========================================================================

/**
 * A static change survives Flush and is re-applied on every subsequent Flush,
 * and its key stays valid so the change remains addressable.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSStaticChangeTest, "VRS.Resolver.StaticChangeTest", VRS_TEST_FLAGS)

bool FVRSStaticChangeTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	FChangeKey Key;
	const FChangeContext Ctx(TEXT("Test"), 0);
	TestTrue(TEXT("Static change accepted"), Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add, EChangeFlags::Static), Ctx, &Key));

	Resolver.Flush(false);
	Resolver.Flush(false);

	TestEqual(TEXT("Static change re-applied on every flush"), Obj->Health, 160.f);
	TestTrue(TEXT("Static key still valid after flushes"), Resolver.IsKeyValid(Key));
	return true;
}

/**
 * The Consume flag limits a change to a single application.
 *
 * Static|Consume is the equipment pattern: the bonus is applied exactly once,
 * survives any number of subsequent flushes without compounding, stays
 * addressable through its key, and is withdrawn via RemoveChange.
 * A Consume-only change behaves like a transient: applied once, then removed
 * by the flush that applied it, with its key retired.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSConsumeChangeTest, "VRS.Resolver.ConsumeChangeTest", VRS_TEST_FLAGS)

bool FVRSConsumeChangeTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	// --- Static | Consume: apply once, persist, stay removable -------------

	FChangeKey EquipKey;
	TestTrue(TEXT("Static|Consume change accepted"),
		Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add, EChangeFlags::Static | EChangeFlags::Consume), Ctx, &EquipKey));

	Resolver.Flush(false);
	Resolver.Flush(false);
	Resolver.Flush(false);

	TestEqual(TEXT("Static|Consume applied exactly once across three flushes"), Obj->Health, 130.f);
	TestTrue(TEXT("Static|Consume key still valid — the change stays in its layer"), Resolver.IsKeyValid(EquipKey));
	TestTrue(TEXT("Static|Consume change can be withdrawn"), Resolver.RemoveChange(EquipKey));

	Resolver.Flush(false);
	TestEqual(TEXT("Withdrawn change stays inert"), Obj->Health, 130.f);

	// --- Consume without Static: behaves like a transient ------------------

	FChangeKey OnceKey;
	TestTrue(TEXT("Consume-only change accepted"),
		Resolver.AddChange(HealthType, 10.f, FChangeSettings(1, EValueOperation::Add, EChangeFlags::Consume), Ctx, &OnceKey));

	Resolver.Flush(false);
	Resolver.Flush(false);

	TestEqual(TEXT("Consume-only applied exactly once"), Obj->Health, 140.f);
	// The change left the layer on the flush that applied it — its key must be
	// retired, keeping the resolver's bookkeeping consistent.
	TestFalse(TEXT("Consume-only key retired after flush"), Resolver.IsKeyValid(OnceKey));
	return true;
}

/**
 * A forced flush applies static changes one last time and then clears them:
 * further flushes must not touch the value again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSForceFlushTest, "VRS.Resolver.ForceFlushTest", VRS_TEST_FLAGS)

bool FVRSForceFlushTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	FChangeKey Key;
	const FChangeContext Ctx(TEXT("Test"), 0);
	TestTrue(TEXT("Static change accepted"), Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add, EChangeFlags::Static), Ctx, &Key));

	Resolver.Flush(false);  // 100 -> 130, static stays
	Resolver.Flush(true);   // 130 -> 160, static cleared
	Resolver.Flush(false);  // nothing left to apply

	TestEqual(TEXT("Force flush applies the static change once more, then clears it"), Obj->Health, 160.f);
	TestFalse(TEXT("Static key retired by the forced flush"), Resolver.IsKeyValid(Key));
	return true;
}

/**
 * A flushed static change can be withdrawn via RemoveChange, after which
 * subsequent flushes no longer re-apply it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSRemoveChangeTest, "VRS.Resolver.RemoveChangeTest", VRS_TEST_FLAGS)

bool FVRSRemoveChangeTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	FChangeKey Key;
	const FChangeContext Ctx(TEXT("Test"), 0);
	TestTrue(TEXT("Static change accepted"), Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add, EChangeFlags::Static), Ctx, &Key));
	Resolver.Flush(false);
	TestEqual(TEXT("Static change applied"), Obj->Health, 130.f);

	TestTrue(TEXT("RemoveChange succeeds for a flushed static change"), Resolver.RemoveChange(Key));
	TestFalse(TEXT("Key retired after removal"), Resolver.IsKeyValid(Key));

	// The removed change must not come back on the next flush.
	Resolver.Flush(false);
	TestEqual(TEXT("Removed static change no longer re-applies"), Obj->Health, 130.f);
	return true;
}

// ===========================================================================
// Priorities and layers
// ===========================================================================

/**
 * Within one layer, changes apply in ascending Priority order:
 * Add (priority 0) lands before Mul (priority 1), so (100 + 30) * 3 = 390.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSPriorityTest, "VRS.Resolver.PriorityTest", VRS_TEST_FLAGS)

bool FVRSPriorityTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	// Queued in reverse priority order on purpose — the sort must fix it.
	Resolver.AddChange(HealthType, 3.f, FChangeSettings(1, EValueOperation::Mul), Ctx);
	Resolver.AddChange(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx);

	Resolver.Flush(false);

	TestEqual(TEXT("Priorities order the operations: (100 + 30) * 3"), Obj->Health, 390.f);
	return true;
}

/**
 * Layers apply strictly by index, never by cross-layer priority. The priorities
 * are deliberately inverted relative to the layer order: if the implementation
 * ever sorted changes globally by Priority, the result would be 250 instead of 300.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSLayerTest, "VRS.Resolver.LayerTest", VRS_TEST_FLAGS)

bool FVRSLayerTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext CtxLayer0(TEXT("Test"), 0);
	const FChangeContext CtxLayer1(TEXT("Test"), 1);

	// Layer 1 holds Mul with the LOW priority, layer 0 holds Add with the HIGH one.
	Resolver.AddChange(HealthType, 2.f, FChangeSettings(0, EValueOperation::Mul), CtxLayer1);
	Resolver.AddChange(HealthType, 50.f, FChangeSettings(5, EValueOperation::Add), CtxLayer0);

	Resolver.Flush(false);

	// Global priority sort would give (100 * 2) + 50 = 250.
	// Layer-by-index gives (100 + 50) * 2 = 300.
	TestEqual(TEXT("Layers apply by index, not by cross-layer priority"), Obj->Health, 300.f);
	return true;
}

/**
 * A layer holds at most MaxChanges queued changes — the excess one is rejected
 * up front by AddChange instead of silently evicting something.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSLayerOverflowTest, "VRS.Resolver.LayerOverflowTest", VRS_TEST_FLAGS)

bool FVRSLayerOverflowTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	// Deliberately tiny bounds: two changes per layer, two layers.
	TestTrue(TEXT("Value registered"), (Resolver.RegisterValue<UVRSTestObject, float, FValueSettings{2, 2}>(HealthType)));

	const FChangeContext Ctx(TEXT("Test"), 0);
	const FChangeSettings Settings(0, EValueOperation::Add);

	TestTrue(TEXT("First change fits"), Resolver.AddChange(HealthType, 1.f, Settings, Ctx));
	TestTrue(TEXT("Second change fits"), Resolver.AddChange(HealthType, 1.f, Settings, Ctx));
	TestFalse(TEXT("Third change overflows the layer and is rejected"), Resolver.AddChange(HealthType, 1.f, Settings, Ctx));

	// The other layer has its own independent capacity.
	const FChangeContext CtxLayer1(TEXT("Test"), 1);
	TestTrue(TEXT("Other layer still accepts changes"), Resolver.AddChange(HealthType, 1.f, Settings, CtxLayer1));
	return true;
}

/**
 * A context that targets a layer beyond MaxLayers must be rejected —
 * otherwise it would index out of the fixed layer array.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSInvalidLayerTest, "VRS.Resolver.InvalidLayerTest", VRS_TEST_FLAGS)

bool FVRSInvalidLayerTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), (Resolver.RegisterValue<UVRSTestObject, float, FValueSettings{2, 2}>(HealthType)));

	// Layer 2 does not exist when MaxLayers == 2 (valid indices are 0 and 1).
	const FChangeContext BadCtx(TEXT("Test"), 2);
	TestFalse(TEXT("Change targeting a non-existent layer is rejected"),
		Resolver.AddChange(HealthType, 1.f, FChangeSettings(0, EValueOperation::Add), BadCtx));
	return true;
}

// ===========================================================================
// Object targeting (target keys)
// ===========================================================================

/**
 * Two objects of the same class registered under different target keys are
 * fully isolated: a change addressed to one must never leak to the other.
 * Transient changes are used so the first object's value stays frozen while
 * the second one is mutated.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSTargetTest, "VRS.Resolver.TargetTest", VRS_TEST_FLAGS)

bool FVRSTargetTest::RunTest(const FString& Parameters)
{
	auto* ObjOne = NewObject<UVRSTestObject>();
	auto* ObjTwo = NewObject<UVRSTestObject>();

	FResolver Resolver(*ObjOne, 30.0);
	const auto KeyOne = Resolver.RegisterObject(ObjOne);
	const auto KeyTwo = Resolver.RegisterObject(ObjTwo);

	const FValueType OneHealthType(&FVRSTestObjectAdverts::TestHealth, KeyOne);
	const FValueType TwoHealthType(&FVRSTestObjectAdverts::TestHealth, KeyTwo);

	TestTrue(TEXT("ObjOne value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(OneHealthType));
	TestTrue(TEXT("ObjTwo value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(TwoHealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	// Mutate ONLY ObjOne — the asymmetry is what proves the isolation.
	TestTrue(TEXT("Change for ObjOne accepted"), Resolver.AddChange(OneHealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx));
	Resolver.Flush(false);

	TestEqual(TEXT("ObjOne changed"), ObjOne->Health, 130.f);
	TestEqual(TEXT("ObjTwo untouched"), ObjTwo->Health, 100.f);

	// Now mutate ObjTwo — ObjOne must stay exactly where it was.
	TestTrue(TEXT("Change for ObjTwo accepted"),
		Resolver.AddChange(TwoHealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx));
	Resolver.Flush(false);

	TestEqual(TEXT("ObjOne unaffected by ObjTwo's change"), ObjOne->Health, 130.f);
	TestEqual(TEXT("ObjTwo changed independently"), ObjTwo->Health, 130.f);
	return true;
}

// ===========================================================================
// Nested (member-of-member) values
// ===========================================================================

/**
 * A value nested inside an embedded struct is registered through
 * TInnerValueAdvert and mutated exactly like a direct member.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSInnerValueTest, "VRS.Resolver.InnerValueTest", VRS_TEST_FLAGS)

bool FVRSInnerValueTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType LevelType(&FVRSTestObjectAdverts::TestLevel, Target);
	TestTrue(TEXT("Nested value registered"), (Resolver.RegisterValue<UVRSTestObject, FVRSTestSettings, int32, GTestSettings>(LevelType)));

	const FChangeContext Ctx(TEXT("Test"), 0);
	TestTrue(TEXT("Change accepted"), Resolver.AddChange(LevelType, 7, FChangeSettings(0, EValueOperation::Add), Ctx));
	Resolver.Flush(false);

	TestEqual(TEXT("Nested member mutated through the resolver"), Obj->Settings.Level, 7);
	return true;
}

// ===========================================================================
// Getters and bookkeeping
// ===========================================================================

/**
 * GetValue reads the live object, GetDefaultValue reads the CDO, and neither
 * reflects a queued change until Flush actually applies it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSValueGettersTest, "VRS.Resolver.ValueGettersTest", VRS_TEST_FLAGS)

bool FVRSValueGettersTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	const auto* DefaultValue = Resolver.GetDefaultValue<float>(HealthType);
	TestNotNull(TEXT("Default value exists"), DefaultValue);
	if (DefaultValue) TestEqual(TEXT("Default value comes from the CDO"), *DefaultValue, 100.f);

	Resolver.AddChange(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx);

	// AddChange only queues — the live value must not move until Flush.
	const auto* PreFlushValue = Resolver.GetValue<float>(HealthType);
	TestNotNull(TEXT("Pre-flush value exists"), PreFlushValue);
	if (PreFlushValue) TestEqual(TEXT("Queued change is not applied yet"), *PreFlushValue, 100.f);

	Resolver.Flush(false);

	const auto* PostFlushValue = Resolver.GetValue<float>(HealthType);
	TestNotNull(TEXT("Post-flush value exists"), PostFlushValue);
	if (PostFlushValue) TestEqual(TEXT("Flush applied the change"), *PostFlushValue, 130.f);
	return true;
}

/**
 * Bookkeeping queries (IsRegistered / HasChanges / IsKeyValid) track the
 * pending-change state across the add-then-flush cycle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSUtilityTest, "VRS.Resolver.UtilityTest", VRS_TEST_FLAGS)

bool FVRSUtilityTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	TestTrue(TEXT("Value type reports as registered"), Resolver.IsRegistered(HealthType));
	TestFalse(TEXT("No pending changes initially"), Resolver.HasChanges());

	FChangeKey Key;
	const FChangeContext Ctx(TEXT("Test"), 0);
	Resolver.AddChange(HealthType, 30.f, FChangeSettings(1, EValueOperation::Add), Ctx, &Key);

	TestTrue(TEXT("Resolver has pending changes"), Resolver.HasChanges());
	TestTrue(TEXT("Value has pending changes"), Resolver.HasChanges(HealthType));
	TestTrue(TEXT("Layer 0 has pending changes"), Resolver.HasChanges(HealthType, 0));
	TestFalse(TEXT("Layer 1 has no pending changes"), Resolver.HasChanges(HealthType, 1));
	TestTrue(TEXT("Key valid while the change is pending"), Resolver.IsKeyValid(Key));

	Resolver.Flush(false);

	TestFalse(TEXT("No pending changes after flush"), Resolver.HasChanges(HealthType));
	TestFalse(TEXT("Transient key retired after flush"), Resolver.IsKeyValid(Key));
	return true;
}

/**
 * Operations addressed to a value that was never registered (or to an invalid
 * value type) fail cleanly instead of touching anything.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSUnregisteredValueTest, "VRS.Resolver.UnregisteredValueTest", VRS_TEST_FLAGS)

bool FVRSUnregisteredValueTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	// The object is registered, but no value is — every query below must fail cleanly.

	const FValueType ArmorType(&FVRSTestObjectAdverts::TestArmor, Target);
	const FChangeContext Ctx(TEXT("Test"), 0);

	TestFalse(TEXT("AddChange fails for an unregistered value"),
		Resolver.AddChange(ArmorType, 5, FChangeSettings(0, EValueOperation::Add), Ctx));
	TestFalse(TEXT("Flush fails for an unregistered value"), Resolver.Flush(ArmorType, false));
	TestFalse(TEXT("HasChanges is false for an unregistered value"), Resolver.HasChanges(ArmorType));
	TestFalse(TEXT("IsRegistered is false for an unregistered value"), Resolver.IsRegistered(ArmorType));

	const FValueType Invalid;
	TestFalse(TEXT("AddChange fails for an invalid value type"),
		Resolver.AddChange(Invalid, 5, FChangeSettings(0, EValueOperation::Add), Ctx));

	TestEqual(TEXT("Armor was never touched"), Obj->Armor, 10);
	return true;
}

/**
 * Registering the same value twice is idempotent — both calls succeed and
 * the second one does not disturb the existing aggregator.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSRegisterTwiceTest, "VRS.Resolver.RegisterTwiceTest", VRS_TEST_FLAGS)

bool FVRSRegisterTwiceTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);

	TestTrue(TEXT("First registration succeeds"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	// Queue a change, then register again — the pending change must survive.
	const FChangeContext Ctx(TEXT("Test"), 0);
	Resolver.AddChange(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx);

	TestTrue(TEXT("Second registration is an idempotent no-op"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));
	TestTrue(TEXT("Pending change survived the re-registration"), Resolver.HasChanges(HealthType));

	Resolver.Flush(false);
	TestEqual(TEXT("Change applied normally"), Obj->Health, 130.f);
	return true;
}

/**
 * When the owner object dies, the aggregator turns invalid and ClearResolver
 * purges it. Cleanup is invoked directly (via friend access) instead of
 * waiting real seconds for the periodic timer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSCleanupTest, "VRS.Resolver.CleanupTest", VRS_TEST_FLAGS)

bool FVRSCleanupTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	TestTrue(TEXT("Registered while owner alive"), Resolver.IsRegistered(HealthType));

	// Simulate the owner being destroyed elsewhere in the game (e.g. EndPlay)
	// without the resolver's knowledge — the weak pointer goes stale at once.
	Obj->MarkAsGarbage();

	TestTrue(TEXT("Aggregator entry still present before cleanup"), Resolver.IsRegistered(HealthType));
	TestNull(TEXT("GetValue fails once the owner is dead"), Resolver.GetValue<float>(HealthType));

	Resolver.ClearResolver();

	TestFalse(TEXT("Aggregator purged by cleanup"), Resolver.IsRegistered(HealthType));
	return true;
}

// ===========================================================================
// Value keys (FValueKey)
// ===========================================================================

/**
 * The user-facing FValueKey handle: MakeKey binds it to a registered value,
 * operators queue changes with the stored priority/context, and Get / GetDefault
 * read through to the live object and the CDO.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSValueKeyTest, "VRS.Resolver.ValueKeyTest", VRS_TEST_FLAGS)

bool FVRSValueKeyTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const auto Key = Resolver.MakeKey<float>(HealthType, /*Priority*/ 0, FChangeContext(TEXT("Test"), 0));
	TestTrue(TEXT("MakeKey returns a bound key"), Key.IsValid());
	if (!Key.IsValid()) return true;

	TestTrue(TEXT("Key reports valid"), Key->IsValid());
	TestFalse(TEXT("No pending changes yet"), Key->HasChanges());

	// Operators queue transient changes with the key's stored priority/context.
	(*Key) += 25.f;
	TestTrue(TEXT("Operator queued a change"), Key->HasChanges());
	TestTrue(TEXT("Flush through the key succeeds"), Key->Flush(false));

	const auto Current = Key->Get();
	TestNotNull(TEXT("Get returns the live value"), Current);
	if (Current) TestEqual(TEXT("Live value reflects the queued add"), *Current, 125.f);

	const auto Default = Key->GetDefault();
	TestNotNull(TEXT("GetDefault returns the CDO value"), Default);
	if (Default) TestEqual(TEXT("CDO value is unaffected"), *Default, 100.f);
	return true;
}

/**
 * Every operation on an unbound key is a safe, logged no-op — never a crash.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSUnboundValueKeyTest, "VRS.Resolver.UnboundValueKeyTest", VRS_TEST_FLAGS)

bool FVRSUnboundValueKeyTest::RunTest(const FString& Parameters)
{
	// Each failed operation logs through VALUE_KEY_LOG_INVALID — expect them all.
	AddExpectedError(TEXT("Failed to change value"), EAutomationExpectedErrorFlags::Contains, 3);

	FValueKey<float> Unbound;

	TestFalse(TEXT("Named operation fails on an unbound key"), Unbound.Add(5.f));
	TestNull(TEXT("Get returns null on an unbound key"), Unbound.Get());
	TestFalse(TEXT("Flush fails on an unbound key"), Unbound.Flush(false));
	return true;
}

/**
 * The rest of the FValueKey surface: Bind() rebinds a key to a fresh internal key (or
 * copies one from another key, sharing target/priority/context), operator() enqueues a
 * change with explicit settings and — optionally — an explicit context override instead
 * of the key's own stored one, and SetDefault() resets the value to its CDO default
 * through the normal change pipeline.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSValueKeyBindTest, "VRS.Resolver.ValueKeyBindTest", VRS_TEST_FLAGS)

bool FVRSValueKeyBindTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	// Bind manually via MakeInternalKey, instead of the ready-bound MakeKey convenience.
	FValueKey<float> Key;
	Key.Bind(Resolver.MakeInternalKey<float>(HealthType), /*Priority*/ 0, FChangeContext(TEXT("Test"), 0));
	TestTrue(TEXT("Bind produced a valid key"), Key.IsValid());

	// operator(Value, Settings) — uses the key's own stored context (layer 0).
	TestTrue(TEXT("operator(Value, Settings) queues a change"),
		Key(30.f, FChangeSettings(0, EValueOperation::Add)));
	Key.Flush(false);
	TestEqual(TEXT("Change via operator(Value, Settings) applied"), Obj->Health, 130.f);

	// operator(Value, Settings, Context) — overrides the stored context (layer 1 instead of 0).
	TestTrue(TEXT("operator(Value, Settings, Context) queues into the overridden layer"),
		Key(10.f, FChangeSettings(0, EValueOperation::Add), FChangeContext(TEXT("Test"), 1)));
	Key.Flush(false);
	TestEqual(TEXT("Change via the context override applied"), Obj->Health, 140.f);

	// A key bound from another one shares the same target, priority and context.
	FValueKey<float> KeyTwo;
	KeyTwo.Bind(Key);
	KeyTwo -= 5.f;
	KeyTwo.Flush(false);
	TestEqual(TEXT("A key rebound via Bind(Other) mutates the same value"), Obj->Health, 135.f);

	// SetDefault queues a Set back to the CDO default (100), regardless of the current value.
	TestTrue(TEXT("SetDefault queues a reset change"), Key.SetDefault());
	Key.Flush(false);
	TestEqual(TEXT("SetDefault reset the value to the CDO default"), Obj->Health, 100.f);
	return true;
}

/**
 * When the owner object dies, FValueKey must never dereference dead memory. Get() and
 * every mutating operation detect the dead target through the resolver's internal key
 * and fail cleanly — this holds even before the periodic ClearResolver cleanup has run.
 *
 * GetDefault() is deliberately unaffected: it reads the class CDO, not the destroyed
 * instance, so it keeps working after the target is gone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSValueKeyTargetDeathTest, "VRS.Resolver.ValueKeyTargetDeathTest", VRS_TEST_FLAGS)

bool FVRSValueKeyTargetDeathTest::RunTest(const FString& Parameters)
{
	// The outer FValueKeyBase::IsValid() only checks that the internal key is still
	// alive in the resolver — it stays true even after the target dies. Get() reaches
	// its failure branch and logs once; every other call fails silently underneath
	// (TInternalValueKey's own IsValid() guard does not log).
	AddExpectedError(TEXT("Failed to get current value"), EAutomationExpectedErrorFlags::Contains, 1);

	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const auto Key = Resolver.MakeKey<float>(HealthType, 0, FChangeContext(TEXT("Test"), 0));
	TestTrue(TEXT("MakeKey returns a bound key"), Key.IsValid());
	if (!Key.IsValid()) return true;

	*Key += 25.f;
	Key->Flush(false);
	TestNotNull(TEXT("Get succeeds while the owner is alive"), Key->Get());

	// Simulate the owner being destroyed elsewhere in the game (e.g. EndPlay), without
	// the resolver's knowledge. The weak pointer inside the internal key goes stale at once.
	Obj->MarkAsGarbage();

	
	TestNull(TEXT("Get returns null once the owner is dead"), Key->Get());
	TestFalse(TEXT("Add fails once the owner is dead"), Key->Add(5.f));
	TestFalse(TEXT("Flush fails once the owner is dead"), Key->Flush(false));
	TestFalse(TEXT("HasChanges fails once the owner is dead"), Key->HasChanges());
	TestFalse(TEXT("RemoveChange fails once the owner is dead"), Key->RemoveChange(FChangeKey::New()));

	const float* Default = Key->GetDefault();
	TestNotNull(TEXT("GetDefault still succeeds — it reads the CDO, not the dead instance"), Default);
	if (Default) TestEqual(TEXT("CDO default is unaffected by the instance's death"), *Default, 100.f);
	return true;
}

// ===========================================================================
// Value batches (deferred registration)
// ===========================================================================

/**
 * FValueBatch defers registrations and applies them in one AddValueBatch call.
 * Covers one direct member and one nested member end to end.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSValueBatchApplyTest, "VRS.Resolver.ValueBatchApplyTest", VRS_TEST_FLAGS)

bool FVRSValueBatchApplyTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);

	// The batch only records intent — nothing reaches the resolver yet.
	FValueBatch Batch;
	const auto HealthType = Batch.Add<UVRSTestObject, float, GTestSettings>(FVRSTestObjectAdverts::TestHealth, Target);
	const auto LevelType = Batch.Add<UVRSTestObject, FVRSTestSettings, int32, GTestSettings>(FVRSTestObjectAdverts::TestLevel, Target);

	TestEqual(TEXT("Batch holds both queued registrations"), Batch.Num(), 2);
	TestTrue(TEXT("Batch contains Health"), Batch.Contains(HealthType));
	TestTrue(TEXT("Batch contains Level"), Batch.Contains(LevelType));
	TestFalse(TEXT("Health not registered before AddValueBatch"), Resolver.IsRegistered(HealthType));

	const int32 Applied = Resolver.AddValueBatch(Batch);

	TestEqual(TEXT("Both registrations applied"), Applied, 2);
	TestTrue(TEXT("Batch emptied after AddValueBatch"), Batch.IsEmpty());
	TestTrue(TEXT("Health registered after AddValueBatch"), Resolver.IsRegistered(HealthType));
	TestTrue(TEXT("Level registered after AddValueBatch"), Resolver.IsRegistered(LevelType));

	// Prove both values actually work end to end, not just that they registered.
	const FChangeContext Ctx(TEXT("Test"), 0);
	Resolver.AddChange(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx);
	Resolver.AddChange(LevelType, 5, FChangeSettings(0, EValueOperation::Add), Ctx);
	Resolver.Flush(false);

	TestEqual(TEXT("Health changed via batch-registered value"), Obj->Health, 130.f);
	TestEqual(TEXT("Nested Level changed via batch-registered value"), Obj->Settings.Level, 5);
	return true;
}

/**
 * FValueBatch::Remove excludes exactly the removed entry — the survivor is
 * still registered, the removed one never reaches the resolver.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSValueBatchRemoveTest, "VRS.Resolver.ValueBatchRemoveTest", VRS_TEST_FLAGS)

bool FVRSValueBatchRemoveTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);

	FValueBatch Batch;
	const auto HealthType = Batch.Add<UVRSTestObject, float, GTestSettings>(FVRSTestObjectAdverts::TestHealth, Target);
	const auto ArmorType = Batch.Add<UVRSTestObject, int32, GTestSettings>(FVRSTestObjectAdverts::TestArmor, Target);

	TestEqual(TEXT("Batch starts with two entries"), Batch.Num(), 2);

	TestTrue(TEXT("Remove succeeds for a queued entry"), Batch.Remove(HealthType));
	TestEqual(TEXT("Batch has one entry left"), Batch.Num(), 1);
	TestFalse(TEXT("Removed entry no longer queued"), Batch.Contains(HealthType));

	const int32 Applied = Resolver.AddValueBatch(Batch);

	TestEqual(TEXT("Only the remaining entry was applied"), Applied, 1);
	TestFalse(TEXT("Removed value was never registered"), Resolver.IsRegistered(HealthType));
	TestTrue(TEXT("Kept value was registered"), Resolver.IsRegistered(ArmorType));
	return true;
}

// ===========================================================================
// Change batches (grouped changes)
// ===========================================================================

/**
 * FChangeBatch groups changes and submits them in one AddBatch call.
 * Exercises both Add overloads (typed convenience and pre-built raw change)
 * and verifies the batched changes obey the normal priority rules.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSChangeBatchApplyTest, "VRS.Resolver.ChangeBatchApplyTest", VRS_TEST_FLAGS)

bool FVRSChangeBatchApplyTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	FChangeBatch Batch;
	FChangeKey AddKey;
	FChangeKey MulKey;

	// Convenience overload — builds the TChangeTyped<T> internally.
	const auto bAddQueued = Batch.Add<float>(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx, &AddKey);

	// Raw overload — the caller builds the change itself.
	auto MulChange = MakeUnique<TChangeTyped<float>>(2.f, FChangeKey::New(), FChangeSettings(1, EValueOperation::Mul));
	const auto bMulQueued = Batch.Add(HealthType, MoveTemp(MulChange), Ctx, &MulKey);

	TestTrue(TEXT("Typed change queued"), bAddQueued);
	TestTrue(TEXT("Raw change queued"), bMulQueued);
	TestEqual(TEXT("Batch holds two changes"), Batch.Num(), 2);
	TestTrue(TEXT("Batch contains the Add key"), Batch.Contains(AddKey));
	TestTrue(TEXT("Batch contains the Mul key"), Batch.Contains(MulKey));

	// Nothing reaches the resolver until AddBatch — the batch only holds intent.
	TestFalse(TEXT("Resolver has no pending changes before AddBatch"), Resolver.HasChanges(HealthType));

	const auto Applied = Resolver.AddBatch(Batch);

	TestEqual(TEXT("Both changes accepted by the resolver"), Applied, 2);
	TestTrue(TEXT("Batch emptied after AddBatch"), Batch.IsEmpty());
	TestTrue(TEXT("Resolver now has pending changes"), Resolver.HasChanges(HealthType));

	Resolver.Flush(false);

	// Add has the lower priority (0), so it applies first: (100 + 30) * 2 = 260.
	TestEqual(TEXT("Batched changes applied in priority order"), Obj->Health, 260.f);
	return true;
}

/**
 * FChangeBatch::Remove excludes exactly the removed change — the final value
 * proves the survivor applied and the removed one never did.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSChangeBatchRemoveTest, "VRS.Resolver.ChangeBatchRemoveTest", VRS_TEST_FLAGS)

bool FVRSChangeBatchRemoveTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	FChangeBatch Batch;
	FChangeKey AddKey;
	FChangeKey MulKey;
	Batch.Add<float>(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx, &AddKey);
	Batch.Add<float>(HealthType, 2.f, FChangeSettings(1, EValueOperation::Mul), Ctx, &MulKey);

	TestEqual(TEXT("Batch starts with two changes"), Batch.Num(), 2);

	TestTrue(TEXT("Remove succeeds for a queued change"), Batch.Remove(MulKey));
	TestEqual(TEXT("Batch has one change left"), Batch.Num(), 1);
	TestFalse(TEXT("Removed change no longer queued"), Batch.Contains(MulKey));

	const auto Applied = Resolver.AddBatch(Batch);
	TestEqual(TEXT("Only the surviving change was accepted"), Applied, 1);

	Resolver.Flush(false);

	// Only +30 landed — the removed Mul never touched the value.
	TestEqual(TEXT("Value reflects only the surviving change"), Obj->Health, 130.f);
	return true;
}

/**
 * FChangeBatch::Add validates its input up front: a change with no operation
 * is rejected instead of being queued as a silent no-op.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSChangeBatchValidationTest, "VRS.Resolver.ChangeBatchValidationTest", VRS_TEST_FLAGS)

bool FVRSChangeBatchValidationTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("Test"), 0);

	FChangeBatch Batch;

	// EValueOperation::None makes Settings.IsValid() false — Add must reject it.
	const auto bAccepted = Batch.Add<float>(HealthType, 30.f, FChangeSettings(0, EValueOperation::None), Ctx);

	TestFalse(TEXT("Add rejects an invalid operation"), bAccepted);
	TestTrue(TEXT("Batch stays empty after a rejected Add"), Batch.IsEmpty());
	return true;
}

// ===========================================================================
// Development-only behaviour (type checks and the debug recorder)
// ===========================================================================

#if !UE_BUILD_SHIPPING

/**
 * Non-shipping type validation: reading a value through the wrong template
 * type is caught by the typeid check and returns null instead of casting
 * the pointer to garbage.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSTypeMismatchTest, "VRS.Resolver.TypeMismatchTest", VRS_TEST_FLAGS)

bool FVRSTypeMismatchTest::RunTest(const FString& Parameters)
{
	// "MakeKey type mismatch for" contains the shorter "Type mismatch for" substring,
	// so the more specific pattern must be registered first — expected-error matching
	// consumes each logged line greedily against patterns in declaration order.
	AddExpectedError(TEXT("MakeKey type mismatch for"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Type mismatch for"), EAutomationExpectedErrorFlags::Contains, 1);

	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj);
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	// Health is a float — asking for int32 must fail loudly, not reinterpret.
	TestNull(TEXT("GetValue with the wrong type returns null"), Resolver.GetValue<int32>(HealthType));
	TestFalse(TEXT("MakeKey with the wrong type returns null"), Resolver.MakeKey<int32>(HealthType).IsValid());
	return true;
}

/**
 * FChangeEntry::IsValid checks every required field — each one is knocked out
 * individually to prove none of them is skipped.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSDebugEntryValidationTest, "VRS.Debug.EntryValidationTest", VRS_TEST_FLAGS)

bool FVRSDebugEntryValidationTest::RunTest(const FString& Parameters)
{
	const FDateTime Now = FDateTime::Now();

	const FChangeEntry Valid(TEXT("Health"), TEXT("Resolver Test"), TEXT("TestObj"),
		TEXT("TestChanger"), TEXT("Add"), Now, Now, TEXT("100"), TEXT("130"), 0, false);
	TestTrue(TEXT("Fully populated entry is valid"), Valid.IsValid());

	const FChangeEntry NullAdvert(nullptr, TEXT("Resolver Test"), TEXT("TestObj"),
		TEXT("TestChanger"), TEXT("Add"), Now, Now, TEXT("100"), TEXT("130"), 0, false);
	TestFalse(TEXT("Null advert name is invalid"), NullAdvert.IsValid());

	const FChangeEntry EmptyOld(TEXT("Health"), TEXT("Resolver Test"), TEXT("TestObj"),
		TEXT("TestChanger"), TEXT("Add"), Now, Now, TEXT(""), TEXT("130"), 0, false);
	TestFalse(TEXT("Empty OldValue is invalid"), EmptyOld.IsValid());

	const FChangeEntry EmptyNew(TEXT("Health"), TEXT("Resolver Test"), TEXT("TestObj"),
		TEXT("TestChanger"), TEXT("Add"), Now, Now, TEXT("100"), TEXT(""), 0, false);
	TestFalse(TEXT("Empty NewValue is invalid"), EmptyNew.IsValid());
	return true;
}

/**
 * FResolverDebug::RecordChange guards its buffer: invalid entries are rejected
 * at the entry point, valid ones are accepted.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSDebugRecordChangeTest, "VRS.Debug.RecordChangeTest", VRS_TEST_FLAGS)

bool FVRSDebugRecordChangeTest::RunTest(const FString& Parameters)
{
	auto& DebugSystem = FResolverDebug::Get();
	const FDateTime Now = FDateTime::Now();

	const FChangeEntry Invalid(TEXT("Health"), TEXT("Resolver Test"), TEXT("TestObj"),
		TEXT("TestChanger"), TEXT("Add"), Now, Now, TEXT(""), TEXT("130"), 0, false);
	TestFalse(TEXT("RecordChange rejects an invalid entry"), DebugSystem.RecordChange(Invalid));

	const FChangeEntry Valid(TEXT("Health"), TEXT("Resolver Test"), TEXT("TestObj"),
		TEXT("TestChanger"), TEXT("Add"), Now, Now, TEXT("100"), TEXT("130"), 0, false);
	TestTrue(TEXT("RecordChange accepts a valid entry"), DebugSystem.RecordChange(Valid));
	return true;
}

/**
 * End-to-end debug recording: a normal Resolver.Flush must land a correctly
 * populated FChangeEntry in the debug singleton's buffer.
 *
 * The buffer is global and may hold entries from other tests in the same run —
 * Last() is still deterministically ours because Flush is synchronous and the
 * changer name below is unique to this test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRSDebugFlushIntegrationTest, "VRS.Debug.FlushIntegrationTest", VRS_TEST_FLAGS)

bool FVRSDebugFlushIntegrationTest::RunTest(const FString& Parameters)
{
	auto* Obj = NewObject<UVRSTestObject>();

	FResolver Resolver(*Obj, 30.0);
	const auto Target = Resolver.RegisterObject(Obj, TEXT("TestTargetName"));
	const FValueType HealthType(&FVRSTestObjectAdverts::TestHealth, Target);
	TestTrue(TEXT("Value registered"), Resolver.RegisterValue<UVRSTestObject, float, GTestSettings>(HealthType));

	const FChangeContext Ctx(TEXT("DebugIntegrationChanger"), 0);
	Resolver.AddChange(HealthType, 30.f, FChangeSettings(0, EValueOperation::Add), Ctx);
	Resolver.Flush(false);

	auto& DebugSystem = FResolverDebug::Get();
	TestFalse(TEXT("Debug buffer received an entry"), DebugSystem.ChangeBuffer.IsEmpty());
	if (DebugSystem.ChangeBuffer.IsEmpty()) return true;

	const FChangeEntry& LastEntry = DebugSystem.ChangeBuffer.Last();
	TestEqual(TEXT("Recorded advert name"), FString(LastEntry.AdvertName), FString(TEXT("Health")));
	TestEqual(TEXT("Recorded resolver name"), FString(LastEntry.ResolverName), FString("Resolver ") + Obj->GetName());
	TestEqual(TEXT("Recorded target name"), FString(LastEntry.TargetName), FString(TEXT("TestTargetName")));
	TestEqual(TEXT("Recorded changer name"), LastEntry.ChangerName, FString(TEXT("DebugIntegrationChanger")));
	TestEqual(TEXT("Recorded operation name"), LastEntry.OperationName, FString(TEXT("Add")));
	TestTrue(TEXT("Old value string mentions 100"), LastEntry.OldValue.Contains(TEXT("100")));
	TestTrue(TEXT("New value string mentions 130"), LastEntry.NewValue.Contains(TEXT("130")));
	TestNotEqual(TEXT("Old and new value strings differ"), LastEntry.OldValue, LastEntry.NewValue);
	return true;
}

#endif // !UE_BUILD_SHIPPING

#undef VRS_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
