# KBDHybridBridge v0.6 (ASE ArkApi)

## What the server log proved

The uploaded log exposed two separate bugs in the older builds.

### v0.4 actually DID detect the Reins correctly

The log contained:

    [REINS] found directly in EquippedItems: PrimalItemCostume_ValyrianReins_C

So the Argentjara Costume slot is visible through `EquippedItems`.
The problem was never the costume-slot detection.

### v0.4 failed to resolve the KBD generated classes

It repeatedly logged:

    [WARN] Buff class not loaded/found: Buff_ValyrianReins_Argent_C
    [WARN] Buff class not loaded/found: Buff_ValyrianReins_Tapejara_C

The old `FindLoadedClass` assumed the generated class's own meta-class had the literal name
`Class`. That assumption is invalid for KBD's generated blueprint classes on this server.

### v0.5 introduced a second load-order bug

Its final startup log was:

    [WARN] Dino class not loaded/found: Argentjara_Character_BP_C
    [CONFIG] loaded 0 hybrid mappings
    [LOAD] KBDHybridBridge v0.5 loaded

v0.5 tried to resolve the Sid hybrid UClass at plugin startup. At that point in the server
startup sequence, the Argentjara class was not available to that lookup, so v0.5 threw away
the mapping entirely.

## v0.6 fixes

- Never throws away a mapping just because the Sid class is unavailable at plugin startup.
- Matches mappings against the actual class of live dinos.
- Lazily resolves KBD buff classes only when a matching hybrid with Reins actually needs one.
- Class resolution accepts the exact generated-class object OR a live instance's ClassField.
- Once a buff UClass is resolved, it is cached.
- KBD class lookup warnings are emitted once, not on every scan.
- Uses the proven `EquippedItems` Costume-slot path first.
- Keeps the lower-overhead 5-second scan interval.

## Test

Hot-reload the compiled v0.6 DLL.

With Reins already in the Argentjara Costume slot, wait up to 5 seconds, then:

    cheat ListMyBuffs

Expected:

    Buff_ValyrianReins_Argent_C_#
    Buff_ValyrianReins_Tapejara_C_#

The plugin log should show:

    [CONFIG] loaded 1 hybrid mappings
    [LOAD] KBDHybridBridge v0.6 loaded
    [RESOLVE] Buff_ValyrianReins_Argent_C
    [RESOLVE] Buff_ValyrianReins_Tapejara_C
    [ADD] ... <- Buff_ValyrianReins_Argent_C
    [ADD] ... <- Buff_ValyrianReins_Tapejara_C

If it still cannot resolve a KBD class, send only the new tail of KBDHybridBridge.log.
