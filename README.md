# KBDHybridBridge v0.5 (ASE ArkApi)

This build is a performance cleanup plus a costume-slot detection fix.

## Why v0.4 could lag

The old build did three expensive things every second:

1. Scanned every live dino in the world.
2. Searched the entire Unreal object table again for each configured buff.
3. Wrote repeated "Reins found" messages to disk.

v0.5 changes that:

- Default scan interval is 5 seconds.
- Buff and dino UClass pointers are resolved once at config load/reload and cached.
- Mapped dino matching uses a hash lookup.
- Repeated item-detection logging is removed.
- Debug logging defaults to false.

## Costume-slot detection

ASE exposes InventoryItems, EquippedItems, and ItemSlots separately.

v0.5 checks:
- EquippedItems
- ItemSlots
- InventoryItems

For InventoryItems, a direct Valyrian Reins item only counts when ARK marks it as equipped (`bEquippedItem`) or it is skinned onto another item. This is intended to catch the dino Costume slot without treating loose Reins in inventory as active.

## Current mapping

Argentjara receives both proven parent-specific KBD Reins buffs while Reins are equipped:

- Buff_ValyrianReins_Argent_C
- Buff_ValyrianReins_Tapejara_C

## Hot reload

With ArkAPI automatic plugin reloading already enabled:

1. Build the new DLL.
2. Upload it next to the live plugin as:

   KBDHybridBridge.dll.ArkApi

3. Wait for ArkAPI to consume the `.ArkApi` replacement.
4. No full server restart is required.

## Test

With Valyrian Reins in the Argentjara Costume slot, wait up to 5 seconds then:

    cheat ListMyBuffs

Expected:
- Buff_ValyrianReins_Argent_C_#
- Buff_ValyrianReins_Tapejara_C_#

Remove the Reins, wait up to 5 seconds, and those two injected buffs should disappear.

## Immediate rollback if a test build causes lag

Edit plugin config.json:

    "Enabled": false

Then run:

    KBDHybridBridge.Reload

The timer remains registered, but the plugin immediately stops world scans and buff processing. No full server restart is needed.

## Tuning

After detection is confirmed, `ScanEverySeconds` can be raised to 10 or 15 for even lower overhead.

A later version can move to inventory/equip hooks so no world polling is required at all.
