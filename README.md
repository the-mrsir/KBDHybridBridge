# KBDHybridBridge v0.3 (ASE ArkApi)

This is a server-side compatibility bridge for:

- Kraken's Better Dinos (KBD) — Workshop 1565015734
- Sid's Hybrids — Workshop 2212177129

## What this version does

The plugin scans live dinos and applies **real KBD buff classes** to configured Sid hybrids.

It does **not** fake KBD with permanent speed/stamina multipliers.

For Valyrian Reins rules it finds the actual Reins item (either directly equipped as a costume or used as the skin on the equipped saddle) and passes that item into `APrimalBuff::StaticAddBuff`. This is important because KBD can continue to use the Reins item itself for durability/quality-dependent behavior.

When the Reins are removed, the injected KBD Reins buff is deactivated.

The first mapping is the one we have already proven manually:

    Argentjara_Character_BP_C
      -> Buff_ValyrianReins_Argent_C

Manual test already confirmed that `ForceGiveBuff Buff_ValyrianReins_Argent 1`
on Argentjara adds the KBD buff and increases stamina.

## Mixed-parent support

The engine is intentionally generic. A hybrid may receive multiple buffs under one rule:

```json
{
  "DinoClass": "Argentjara_Character_BP_C",
  "Parents": ["Argentavis", "Tapejara"],
  "Rules": [
    {
      "Trigger": "ValyrianReins",
      "Buffs": [
        "Buff_ValyrianReins_Argent_C",
        "PUT_THE_CONFIRMED_TAPEJARA_BUFF_CLASS_HERE"
      ]
    }
  ]
}
```

The same format can be used for Vulcanotavis, Nanogryphus, and the rest of Sid's hybrids.

Supported triggers in v0.1:

- `ValyrianReins` — buff is active only while actual Valyrian Reins are equipped/applied to saddle.
- `Always` — buff is kept active on that hybrid. Use this only for a confirmed KBD parent/base buff that is supposed to be intrinsic.

## Install on server after building

ArkApi expects:

    ShooterGame/Binaries/Win64/ArkApi/Plugins/KBDHybridBridge/
        KBDHybridBridge.dll
        PluginInfo.json
        config.json

The folder name and DLL name must match.

Restart the server after installing.

### Useful server console commands

    KBDHybridBridge.Reload
    KBDHybridBridge.Scan

Debug output is written to:

    ArkApi/Plugins/KBDHybridBridge/KBDHybridBridge.log

## First test

1. Remove the manually-forced Reins buff first:

       cheat ForceGiveBuff Buff_ValyrianReins_Argent 0

2. Install/restart with this plugin.
3. Put Valyrian Reins on an Argentjara.
4. Wait 1–2 seconds.
5. Ride it and run:

       cheat ListMyBuffs

Expected:

    Buff_ValyrianReins_Argent_C_#

and KBD may also create its BD manager buff, as your manual test did.

6. Remove the Reins.
7. Wait 1–2 seconds and run `ListMyBuffs` again. The injected Argent Reins buff should be gone.

## Important current limitation

This package contains the **bridge engine**, but only the exact Argentavis Reins buff is populated because that is the only KBD parent-specific buff asset name we have positively identified and tested so far.

For each KBD parent we want to support, get the exact buff names from a normal KBD creature using `ListMyBuffs`, then add those names to `config.json`. No DLL recompile is needed to add mappings/buffs.

For Argentjara's Tapejara half, put Valyrian Reins on a normal Tapejara and run:

    cheat ListMyBuffs

Then add the Tapejara-specific Reins buff class to the same `Buffs` array and run:

    KBDHybridBridge.Reload

## Building

This source targets the ASE Ark Server API, not ASA.

The project expects the official AseApi repository at:

    extern/AseApi/

with:
- headers under `extern/AseApi/version/Core/Public`
- `ArkApi.lib` under `extern/AseApi/out_lib`

The included GitHub Actions workflow checks out AseApi and builds the x64 Release DLL automatically.

If building locally, use Visual Studio 2022 with the C++ desktop workload, clone ArkServerApi/AseApi into `extern/AseApi`, then build `KBDHybridBridge.sln` as `Release | x64`.

## Safety

This is an experimental compatibility plugin. Back up the world before first use. The v0.1 config deliberately targets only Argentjara until more KBD buff asset names are verified.


## v0.2 verified Argentjara parent mapping

Observed on a normal KBD Argentavis:
- `BD_BuffManager_C`
- `Buff_Flyers_C`
- `Buff_ValyrianReins_Argent_C`

Observed on a normal KBD Tapejara:
- `BD_BuffManager_C`
- `Buff_Flyers_Tapejara_C`
- `Buff_ValyrianReins_Tapejara_C`

The Argentjara mapping now applies:
- `Buff_Flyers_C` always
- `Buff_Flyers_Tapejara_C` always
- `Buff_ValyrianReins_Argent_C` only while actual Valyrian Reins are equipped
- `Buff_ValyrianReins_Tapejara_C` only while actual Valyrian Reins are equipped

The bridge deliberately does NOT inject `BD_BuffManager_C` directly. The parent KBD buffs should create/use the manager themselves, as seen in the manual Argentjara test.

### Recommended manual dual-parent proof before relying on the plugin

While riding Argentjara:

    cheat ForceGiveBuff Buff_Flyers 1
    cheat ForceGiveBuff Buff_Flyers_Tapejara 1
    cheat ForceGiveBuff Buff_ValyrianReins_Argent 1
    cheat ForceGiveBuff Buff_ValyrianReins_Tapejara 1
    cheat ListMyBuffs

Expected KBD-related entries include:

    BD_BuffManager_C_#
    Buff_Flyers_C_#
    Buff_Flyers_Tapejara_C_#
    Buff_ValyrianReins_Argent_C_#
    Buff_ValyrianReins_Tapejara_C_#

If any behavior is obviously broken with both base flyer buffs together, remove the relevant manual test buff and disable it from the `Always` rule in config.json before deploying broadly.


## v0.3 test result

Manual dual-parent test on Argentjara succeeded for the two Valyrian Reins buffs simultaneously:

- `BD_BuffManager_C`
- `Buff_ValyrianReins_Argent_C`
- `Buff_ValyrianReins_Tapejara_C`

The generic/base flyer buffs:

- `Buff_Flyers_C`
- `Buff_Flyers_Tapejara_C`

did not remain on Argentjara during the test, so v0.3 no longer tries to inject them automatically.

This is intentional. v0.3's first goal is a narrow, proven behavior:

**Actual Valyrian Reins equipped -> apply all verified parent-specific KBD Reins buffs.  
Reins removed -> remove the injected Reins buffs.**

That gives Argentjara the union of the verified Argentavis + Tapejara Reins behavior without hard-coded stat multipliers.

### Current proven mapping

Argentjara:
- Argentavis -> `Buff_ValyrianReins_Argent_C`
- Tapejara -> `Buff_ValyrianReins_Tapejara_C`

### Expansion pattern

For another Sid hybrid:
1. Identify its vanilla parents.
2. Put Valyrian Reins on each corresponding normal KBD creature.
3. Run `cheat ListMyBuffs`.
4. Record the exact parent-specific `Buff_ValyrianReins_*_C` class.
5. Add those buff classes to that hybrid's `ValyrianReins` rule in `config.json`.
6. Reload the bridge.

No DLL recompile is required for new config-only mappings.
