# KBDHybridBridge v1.5 (ASE ArkApi)

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

The bridge handles the KBD Reins buffs and the Tapejara parent profile's mounted
weapon capability. Other blueprint-level creature abilities may still require
separate compatibility handling.

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


## v0.9 — chat commands + stale-config protection

The live test showed:

    [CONFIG] loaded 0 parent profiles and 0 hybrid mappings

That happened because only the DLL had been hot-updated while the server still had an older
pre-v0.7 config schema. v0.9 compiles the current parent profiles and hybrid mappings into
the DLL as defaults. If the external config is old or incomplete, the plugin keeps the
external Enabled/Debug/ScanEverySeconds values but falls back to the built-in mappings.

v0.9 also adds in-game CHAT commands, because the client console commands were being echoed
but were not reliably hitting the ArkApi callback on this setup.

Open normal ARK chat and type:

    /kbdstatus
    /kbdreload
    /kbdscan
    /kbddump
    /kbdhybrids

`/kbdstatus` should answer immediately with something like:

    KBDHybridBridge: v0.9 | Enabled=true | Scan=5s | Parents=13 | Hybrids=22

`/kbddump` prints every currently loaded `Buff_ValyrianReins_*_C` class to your in-game chat.

`/kbdhybrids` prints the live Sid hybrid classes that matched a mapping.

The old console commands are still registered as a secondary route, but chat is the
recommended diagnostic interface for this server.


## v1.0 — fixes literal `%s` in chat

v0.9 used printf-style `%s` with ArkApi's `SendChatMessage`.

ArkApi does not use printf formatting there; it uses `FString::Format` / fmt-style formatting.
That is why the game literally displayed:

    KBDHybridBridge: %s

v1.0 passes the prepared `FString` directly to `SendChatMessage`, matching ArkApi's own plugin examples.

After hot-loading v1.0, normal ARK chat commands should display their real contents:

    /kbdstatus
    /kbdreload
    /kbdscan
    /kbddump
    /kbdhybrids

Recommended first test:

    /kbdstatus

Expected roughly:

    KBDHybridBridge: v1.0 | Enabled=true | Scan=5s | Parents=13 | Hybrids=22


## v1.2 — direct Tab-console output

The previous attempt hooked `AdminCheat` / `Cheat`. Those hooks installed successfully,
but typing `cheat kbd status` did not traverse that server path on this setup.

ArkApi itself processes in-game console commands by hooking:

    APlayerController.ConsoleCommand

v1.2 hooks that exact function with its actual ASE signature and returns the diagnostic
text as the console command's FString result. This means the result should appear in the
same Tab console where the command is entered.

Use these DIRECTLY in the Tab console — NO `cheat` prefix:

    kbd status
    kbd reload
    kbd scan
    kbd dump
    kbd hybrids

Long aliases are also accepted:

    KBDHybridBridge.Status
    KBDHybridBridge.Reload
    KBDHybridBridge.Scan
    KBDHybridBridge.DumpReinsBuffs
    KBDHybridBridge.DumpMatchedHybrids

First test:

    kbd status

Expected console result:

    KBDHybridBridge v1.2 | Enabled=true | Scan=5s | Parents=13 | Hybrids=22

The server log should also show:

    [HOOK] APlayerController.ConsoleCommand=OK
    [TERMINAL] kbd status

## v1.3 — dedicated-server terminal routing

`APlayerController.ConsoleCommand` is normally executed on the client and does not
reliably reach a dedicated server. v1.3 handles commands on the authenticated server
RPC paths instead:

- `ABasePlayerController.ServerCheat_Implementation`
- `AShooterPlayerController.ServerGlobalCommand_Implementation`

Enable cheats normally, then use the Tab terminal:

    cheat kbd status
    cheat kbd reload
    cheat kbd scan
    cheat kbd dump
    cheat kbd hybrids

`admincheat` and the long `KBDHybridBridge.*` aliases are also accepted. Commands are
restricted to authenticated server admins, and results are returned through
`ClientMessage` so they are written to the player's console.

## v1.4 — Tapejara-parent rider weaponry

While Valyrian Reins are equipped, mapped hybrids with the `Tapejara` parent profile
now have `APrimalDinoCharacter.bAllowMountedWeaponry` enabled. The change is forced
to replicate so the rider can equip and fire handheld weapons.

When the Reins are removed, the bridge restores the dino's original mounted-weapon
setting. The original setting is also restored if the mapping is removed, the plugin
is disabled, or the plugin unloads.

## v1.5 — clear Sid's rider-weapon blocks

Argentjara can retain `bPreventAllRiderWeapons` and
`bPreventAllRiderWeaponsOnReequip` even after mounted weaponry is allowed. While
Tapejara Reins support is active, v1.5 now enables `bAllowMountedWeaponry` and clears
both blocking flags. All three original values are restored when support is removed.
