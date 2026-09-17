# KBDHybridBridge v0.8 (ASE ArkApi)

v0.7 expands the proven Argentjara bridge into a data-driven parent system.

## Proven behavior retained

Argentjara:
- Argentavis -> `Buff_ValyrianReins_Argent_C`
- Tapejara -> `Buff_ValyrianReins_Tapejara_C`

Both were manually proven on the live server:
- appear automatically with actual Valyrian Reins equipped;
- disappear after Reins are removed.

## What v0.7 adds

The config now contains:

- `ParentProfiles`: KBD Reins parent categories.
- `Mappings`: Sid hybrid -> documented vanilla parents.
- aliases for likely/internal Sid class naming differences.
- lazy buff discovery for parent Reins classes that have not been manually verified yet.
- on-demand diagnostic commands.

The bridge NEVER hard-codes stamina/speed multipliers. It injects KBD's actual Reins buff class.

## Sid hybrids currently mapped

- Argentjara -> Argent + Tapejara
- Serpentmoloch -> Argent
- Tropeocoatlus -> Quetz + Tropeognathus
- Colossodraco -> Quetz
- Gigadeisopteryx / Gigadeusopteryx -> Quetz
- Wolf Wyvern -> Wyvern
- Necro Wyvern -> Wyvern
- Meteor Wyvern -> Wyvern
- Plasma Wyvern -> Wyvern
- Neoavirhinus -> Tropeognathus
- Frostvarg -> Managarmr
- Pelagosuchus -> Pelagornis
- Pelosornis -> Pteranodon
- Phalainasuchus -> Lymantria/Moth
- Nyctavenator -> Wyvern
- Vulcanotavis -> Argent
- Magsuredrake -> Rock Drake + Wyvern
- Glacialisvenator -> Managarmr + Snow Owl
- Caelumlator -> Moth
- Spinojalosaurus -> Tapejara
- Aracnoptera -> Moth
- Gigaplolophosaura -> Tapejara

Only the Argent and Tapejara exact KBD buff class names are currently manually verified.
All other profiles first try likely KBD class names and then search loaded KBD
`Buff_ValyrianReins_*` classes by profile token. If a profile cannot be resolved,
it safely skips that parent's buff and writes one `[UNRESOLVED]` line.

## One command that should save a lot of manual testing

In the in-game console:

    KBDHybridBridge.DumpReinsBuffs

This scans the Unreal object table ONCE, on demand, and writes every currently loaded:

    Buff_ValyrianReins_*_C

class to:

    ArkApi/Plugins/KBDHybridBridge/KBDHybridBridge.log

Run that once after hot-loading v0.7. The output will let us replace the remaining
candidate guesses with exact class names in config.json.

## See which live Sid creatures matched

    KBDHybridBridge.DumpMatchedHybrids

This writes each matched live creature class and the parent profiles assigned to it.

That is useful if Sid's internal blueprint class name differs from the public hybrid name.
Adding an alias is config-only; no DLL rebuild is needed.

## Performance

- Same 5-second world scan that was lag-free in v0.6.
- KBD class discovery happens only while a parent profile is first resolved and is cached.
- The expensive global dump runs ONLY when you explicitly run `DumpReinsBuffs`.
- No per-scan debug spam.

## Hot update

Compile v0.7 and upload the new DLL as:

    KBDHybridBridge.dll.ArkApi

ArkAPI automatic reloading will replace the live plugin without a full Fjordur restart.

## Important: mounted weapons and other creature-side abilities

This release handles the actual KBD Reins buffs.

It does NOT yet force blueprint-level capabilities that the buff alone cannot provide.
The known example is Tapejara's "rider can use mounted weaponry": Argentjara gets
`Buff_ValyrianReins_Tapejara_C`, but Sid's mount still blocks weapon use.

That capability needs a separate safe rider/mount hook and is intentionally not faked in v0.7.

## Non-Reins KBD base buffs

v0.7 does NOT blindly inject normal KBD creature buffs such as Raptor, Yuty, Rock Golem,
etc. Those often contain species-specific blueprint assumptions. We should identify and
test them individually before adding an `Always` parent-buff layer.


## v0.8 — in-game command feedback

The diagnostic commands now reply directly to the player who runs them using ArkAPI server messages. You no longer need the plugin log just to see command output.

Run these in the in-game console (Tab), with NO `cheat` prefix:

    KBDHybridBridge.Status
    KBDHybridBridge.Reload
    KBDHybridBridge.Scan
    KBDHybridBridge.DumpReinsBuffs
    KBDHybridBridge.DumpMatchedHybrids

`DumpReinsBuffs` prints the loaded `Buff_ValyrianReins_*_C` class names directly in game.

`DumpMatchedHybrids` prints each live Sid hybrid class that matched and the KBD parent profiles assigned to it.

`Status` prints the plugin version, enabled state, scan interval, and loaded mapping counts.

The log is still retained as a backup, but it is no longer required for normal diagnostics.
