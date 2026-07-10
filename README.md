# VRS — Value Resolution System

A priority-based, layered value modification framework for Unreal Engine 5.

VRS lets many independent systems modify the same value safely and predictably. Instead of
each system writing to a variable directly — and fighting over the result — every change is
queued, sorted by priority and operation across discrete layers, and flushed into the live
value on demand. In non-shipping builds every modification is traceable: the debug subsystem
records who changed what, when, and how, to a JSON Lines log.

---

## Why

Imagine a character's `MaxHealth`. Armor adds `+50`, a blessing multiplies by `1.5`, poison
subtracts `30`. Three systems, none aware of the others, all wanting to change one number.

Doing this by hand is fragile: the order matters, sources overwrite each other, and nothing
keeps them isolated or traceable. VRS solves this — each source registers its change, VRS
applies them in a deterministic order (layer, then priority, then operation), every stored
change stays addressable through its key, and retained changes can be removed from future
flushes with a single call.

```
MaxHealth = 100 (base)
  + 50   (armor,     layer 1)
  * 1.5  (blessing,  layer 2)
  - 30   (poison,    layer 2, higher priority)
```

### Design note: in-place mutation, not reversible stat recomputation

VRS mutates the live value in place. A `Flush` applies each queued change directly on top of
the current value — there is no separate base value and no recomputation from a set of active
modifiers, the way GAS-style attribute systems work.

Because of that, **`RemoveChange` is not a rollback**. It deletes the stored change and stops
it from applying on future flushes, but it does not undo what has already been applied to the
value:

- A **transient** change already applied once and is gone — removal rarely matters.
- A **`Static`** change stops compounding once removed, but whatever it already added stays.
- A **`Static | Consume`** change (applied exactly once) is unaffected by removal at all — it
  only retires the bookkeeping so the key/change stop being addressable.

If you need the value to actually revert — e.g. unequipping armor should visibly lower
`MaxHealth` back down — don't rely on `RemoveChange` for that. Queue an explicit inverse change
instead (a matching `Sub` for an `Add`, etc.). That reverts correctly as long as no other
operation touched the same value in between; if operations interact (an `Add` and a `Mul` on
the same stack), a manual inverse can't reproduce the value that would exist without the
original change, because the operations don't commute.

A full recompute-from-base stat layer — one that always derives the value from the base plus
every still-active modifier — would remove this limitation entirely. It isn't part of VRS
today; it could be built on top of the resolver if that guarantee becomes necessary.

---

## Features

- **Layered priority resolution** — changes live in compile-time layers. Layers apply in
  index order; within a layer changes are sorted by priority, and ties are broken by operation
  order (`Set` → `Add` → `Sub` → `Mul` → `Div`). Coarse ordering via layers, fine ordering via
  priority and operation.
- **Five operations** — `Set`, `Add`, `Sub`, `Mul`, `Div` per change.
- **Transient, static and consume changes** — transient changes apply once and are removed;
  static changes persist and re-apply on every flush; `Static | Consume` applies once and stays
  addressable for later removal. Removal never reverts a value already applied — see the
  design note above.
- **Atomic batches** — group value registrations (`FValueBatch`) or changes (`FChangeBatch`)
  and submit them in one call.
- **Direct and nested members** — expose a UObject member directly, or follow two pointers
  (`Owner -> Struct -> Field`) for nested values.
- **Type-safe** — `TChangeTyped<T>` specialisations cover all common engine types (integers,
  float/double, bool, strings, vectors, rotators, transforms, colors, gameplay tags, object
  pointers). Adding a new type is a single specialisation.
- **Compile-time bounds** — max changes per layer and max layers are template parameters;
  per-layer storage uses a fixed allocator, so there is no hidden heap traffic.
- **Automatic cleanup** — the resolver periodically purges aggregators and keys whose owning
  object has been garbage collected, and drops the pending-change bookkeeping that pointed at
  them. Nothing dangles when an object dies.
- **Debug tracing stripped from shipping** — change metadata, recording, JSON logging and
  type validation live behind `#if !UE_BUILD_SHIPPING`. Shipping keeps only the runtime path;
  none of the tracing cost ships.
- **JSON Lines debug log** — every flushed change is appended to a timestamped file under
  `<ProjectSaved>/VRS/`, ready to inspect or post-process.
- **Console commands** — `VRS.Debug.Enable`, `VRS.Debug.Disable`, `VRS.Debug.Record`,
  `VRS.Debug.SetRecordRate <seconds>`.

---

## Requirements

- Unreal Engine 5.x (developed and tested on UE 5.6).
- A C++20 toolchain (VRS uses concepts / `requires`).

All dependencies are first-party Epic modules (`Core`, `CoreUObject`, `GameplayTags`, and —
privately — `Engine`, `Json`, `JsonUtilities`). No third-party libraries.

---

## Architecture

VRS is built from a few small, single-responsibility pieces:

| Piece | Role |
|-------|------|
| **`FResolver`** | The entry point. Owns registered objects, values and pending changes; applies them on `Flush`. |
| **`TValueAdvert` / `TInnerValueAdvert`** | Compile-time descriptors of *which* member of *which* class to modify. Statically allocated, never store a live object. |
| **`FTargetKey`** | Unique key returned by `RegisterObject`, identifying one specific object instance. |
| **`FValueType`** | Pairs an advert with a target key — the handle used to address a value in every call. |
| **`IValueAggregator`** | Per-value engine: stacks changes across layers and produces the final value. Type-erased so the resolver can hold every value type in one map. |
| **`TLayerPackage`** | Per-layer container of pending changes, with fixed-size storage. |
| **`FChangeSettings` / `EChangeFlags`** | Priority, operation and retention behaviour of a single change. |
| **`FValueKey`** | A convenience handle to one value: bind it once, then mutate the value with plain operators (`+=`, `*=`, …) and read it with `Get()`. Weakly references the resolver, so it stays safe if the value or its owner goes away. |
| **`FResolverDebug`** | Non-shipping recorder that serialises flushed changes to JSON Lines. |

A guiding constraint throughout is to keep the runtime path cheap: fixed-size storage, no
per-call allocations, and all tracing compiled out of shipping.

---

## Quick start

`FResolver` has a virtual destructor and is meant to live for as long as the systems that use
it — typically held as a member of a component or subsystem, or subclassed, rather than created
as a local variable. It is shown locally here only to keep the example short.

```cpp
#include "Resolver.h"
#include "ValueAdverts.h"

// 1. Declare static adverts for the members you want to expose (once, in a header).
//    A direct member:
inline TValueAdvert MaxHealthAdvert{ TEXT("MaxHealth"), &AMyCharacter::MaxHealth };
//    A nested member (Owner -> Struct -> Field):
inline TInnerValueAdvert MoveSpeedAdvert{ TEXT("MoveSpeed"), &AMyCharacter::Stats, &FStats::MoveSpeed };

// 2. Create a resolver and register the object you want to mutate.
FResolver Resolver(OwningObject, /*ClearInterval*/ 30.0);
const FTargetKey Target = Resolver.RegisterObject(MyCharacter, TEXT("Player"));

// 3. Build a value handle and register the value.
const FValueType MaxHealth(&MaxHealthAdvert, Target);
Resolver.RegisterValue<AMyCharacter, float, FValueSettings{10, 5}>(MaxHealth);

// A nested value is registered the same way, with the inner struct type in the middle:
const FValueType MoveSpeed(&MoveSpeedAdvert, Target);
Resolver.RegisterValue<AMyCharacter, FStats, float, FValueSettings{10, 5}>(MoveSpeed);

// 4. Queue a change. Nothing is applied yet — the change waits in its layer.
//    Static|Consume applies exactly once, then stays addressable so it can be
//    withdrawn later. Withdrawing it does not lower Health back down — see the
//    design note above for why, and what to do if you need a real reversal.
FChangeContext Ctx(this, /*Layer*/ 1);
FChangeKey ArmorKey;
Resolver.AddChange(
    MaxHealth,
    /*Value*/ 25.f,
    FChangeSettings(/*Priority*/ 10, EValueOperation::Add, EChangeFlags::Static | EChangeFlags::Consume),
    Ctx,
    &ArmorKey);

// 5. Flush to apply every pending change.
Resolver.Flush(/*bWithForce*/ false);   // MaxHealth is now 125

// 6. Later — withdraw the armor bonus. This retires the change's bookkeeping; it does
//    not lower MaxHealth back down (see the design note above).
Resolver.RemoveChange(ArmorKey);
Resolver.Flush(false);                   // no pending changes — MaxHealth stays 125
```

### The `FValueKey` shorthand

For values you touch often, `MakeKey` returns a bound handle with natural operator syntax:

```cpp
auto Health = Resolver.MakeKey<float>(MaxHealth, /*Priority*/ 10, FChangeContext(this, 1));

*Health += 25.f;        // queue an Add
*Health *= 1.5f;        // queue a Mul
Health->Flush(false);   // apply

// Get() returns a pointer — null if the value or its owner is gone. Check before dereferencing:
if (const float* Current = Health->Get())
{
    const float Value = *Current;
    // ... use Value ...
}
```

### Lifetime safety

`FValueKey` only weakly references the value inside the resolver. If the owning object is
destroyed, the key does not dangle: `Get()` returns `nullptr`, and every mutating operator
becomes a logged no-op instead of a crash. On the resolver side, the periodic cleanup removes
the dead aggregator and its bookkeeping automatically, so a destroyed object leaves nothing
behind. You never have to manually unregister a value when its object dies.

---

## Change flags

| Flags | Behaviour |
|-------|-----------|
| *(none)* | Transient — applied by the next `Flush`, then removed. |
| `Static` | Persists in its layer; re-applied on every `Flush` until removed or force-flushed. |
| `Static \| Consume` | Applied exactly once; stays in its layer and addressable. `RemoveChange` retires it — the value it already applied is not rolled back. |
| `Consume` | Applied once, then removed by the flush that applied it (like a transient). |

---

## Testing

VRS ships with an automation test suite (`Source/VRS/Private/Tests/VRSTests.cpp`), stripped
from shipping builds. It covers:

- **Operations & flushing** — all five operations, division by zero, queued-but-not-yet-applied
  semantics.
- **Change lifecycle** — transient, static, `Consume`, `Static | Consume`, forced flush,
  withdrawal via `RemoveChange`.
- **Priorities & layers** — in-layer priority/operation ordering, layer-by-index ordering,
  layer overflow, out-of-range layers.
- **Object targeting** — isolation between multiple registered objects of the same class.
- **Nested values** — member-of-member registration and mutation.
- **Bookkeeping** — `IsRegistered`, `HasChanges`, `IsKeyValid`, idempotent re-registration,
  operations against unregistered/invalid values.
- **Cleanup & lifetime** — aggregator purge after the owner is garbage collected; `FValueKey`
  behaviour (`Bind`, both `operator()` overloads, `SetDefault`) and its safety when the target
  dies mid-use.
- **Batches** — `FValueBatch` and `FChangeBatch`, including partial removal before submission
  and input validation.
- **Development-only checks** — type-mismatch detection, the debug recorder's validation and
  end-to-end JSON Lines logging.

Run them from the editor via **Window → Test Automation** (filter by `VRS`), or from the
console with `Automation RunTests VRS`.

---

## License

This project is released under a **source-available, non-commercial** license. Personal,
educational and portfolio use is permitted, including distributing compiled non-commercial
projects that use VRS. Commercial use of any kind requires a separate commercial license
(via Fab or directly from the author). See [LICENSE](LICENSE) for full terms.

---

## Author

Ilia Zherlitsin
