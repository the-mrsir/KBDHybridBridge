#include <API/ARK/Ark.h>
#include <json.hpp>

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cctype>
#include <sstream>

#pragma comment(lib, "ArkApi.lib")

namespace KBDHybridBridge
{
    using json = nlohmann::json;

    DECLARE_HOOK(
        AShooterPlayerController_AdminCheat,
        void,
        AShooterPlayerController*,
        FString*
    );

    DECLARE_HOOK(
        AShooterPlayerController_Cheat,
        void,
        AShooterPlayerController*,
        FString*
    );

    struct ParentProfile
    {
        std::string name;
        std::vector<std::string> buff_candidates;
        std::vector<std::string> discovery_tokens;

        UClass* resolved_class = nullptr;
        std::string resolved_name;
        bool unresolved_warned = false;
    };

    struct HybridMapping
    {
        std::string name;
        std::vector<std::string> aliases;
        std::vector<std::string> aliases_lower;
        std::vector<std::string> parents;
    };

    static const std::string plugin_name = "KBDHybridBridge";

    static bool enabled = true;
    static bool debug = false;
    static int scan_every_seconds = 5;
    static int seconds_since_scan = 0;

    static std::unordered_map<std::string, ParentProfile> parent_profiles;
    static std::vector<HybridMapping> mappings;

    std::string PluginDir()
    {
        return ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + plugin_name;
    }

    void WriteLog(const std::string& msg, bool force = false)
    {
        if (!debug && !force)
            return;

        std::ofstream f(PluginDir() + "/KBDHybridBridge.log", std::ios::app);
        if (f.is_open())
            f << msg << "\n";
    }

    void Reply(APlayerController* controller, const std::string& msg, bool error = false)
    {
        if (!controller)
            return;

        auto* shooter = static_cast<AShooterPlayerController*>(controller);

        ArkApi::GetApiUtils().SendServerMessage(
            shooter,
            error ? FColorList::Red : FColorList::Green,
            msg.c_str()
        );
    }

    void ChatReply(AShooterPlayerController* controller, const std::string& msg)
    {
        if (!controller)
            return;

        FString sender("KBDHybridBridge");
        FString text(msg.c_str());

        // ArkApi uses fmt-style formatting, not printf-style "%s".
        // Passing the FString directly avoids formatting entirely.
        ArkApi::GetApiUtils().SendChatMessage(
            controller,
            sender,
            *text
        );
    }

    std::string ObjectName(UObjectBase* obj)
    {
        if (!obj)
            return {};

        try
        {
            return obj->NameField().ToString().ToString();
        }
        catch (...)
        {
            return {};
        }
    }

    std::string ClassName(UObjectBase* obj)
    {
        if (!obj || !obj->ClassField())
            return {};
        return ObjectName(obj->ClassField());
    }

    std::string Lower(std::string s)
    {
        std::transform(
            s.begin(),
            s.end(),
            s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
        );
        return s;
    }

    std::string FStringToUtf8(FString* value)
    {
        if (!value)
            return {};

        return ArkApi::Tools::Utf8Encode(std::wstring(**value));
    }

    std::string Trim(std::string s)
    {
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};

        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    void TerminalReply(AShooterPlayerController* controller, const std::string& msg)
    {
        if (!controller)
            return;

        ArkApi::GetApiUtils().SendServerMessage(
            controller,
            FColorList::Green,
            msg.c_str()
        );
    }

    void RconReply(
        RCONClientConnection* connection,
        RCONPacket* packet,
        const std::string& msg
    )
    {
        if (!connection || !packet)
            return;

        const std::wstring wide = ArkApi::Tools::Utf8Decode(msg);
        FString out(wide.c_str());

        connection->SendMessageW(packet->Id, 0, &out);
    }

    bool StartsWith(const std::string& s, const std::string& prefix)
    {
        return s.size() >= prefix.size() &&
               std::equal(prefix.begin(), prefix.end(), s.begin());
    }

    bool EndsWith(const std::string& s, const std::string& suffix)
    {
        return s.size() >= suffix.size() &&
               std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
    }

    // Same lazy generated-class resolver that was proven in v0.6.
    UClass* FindLoadedClass(const std::string& short_name)
    {
        auto& objects = Globals::GUObjectArray()();

        // Exact class object name.
        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;
            if (ObjectName(obj) == short_name)
                return reinterpret_cast<UClass*>(obj);
        }

        // Live/default instance whose ClassField is the requested generated class.
        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;
            UClass* cls = obj->ClassField();

            if (cls && ObjectName(cls) == short_name)
                return cls;
        }

        return nullptr;
    }

    // Tries to discover a real loaded KBD Reins buff by profile token.
    // This only runs while resolving a profile and is cached afterwards.
    UClass* DiscoverReinsClass(const ParentProfile& profile, std::string& discovered_name)
    {
        const std::string prefix = "buff_valyrianreins_";
        auto& objects = Globals::GUObjectArray()();

        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;

            // Most reliable path: look at the class of live/default objects.
            if (obj->ClassField())
            {
                const std::string cls_name = ObjectName(obj->ClassField());
                const std::string cls_lower = Lower(cls_name);

                if (StartsWith(cls_lower, prefix))
                {
                    for (const auto& token : profile.discovery_tokens)
                    {
                        if (cls_lower.find(Lower(token)) != std::string::npos)
                        {
                            discovered_name = cls_name;
                            return obj->ClassField();
                        }
                    }
                }
            }

            // Also allow the generated-class object itself.
            const std::string obj_name = ObjectName(obj);
            const std::string obj_lower = Lower(obj_name);

            if (StartsWith(obj_lower, prefix) && EndsWith(obj_name, "_C"))
            {
                for (const auto& token : profile.discovery_tokens)
                {
                    if (obj_lower.find(Lower(token)) != std::string::npos)
                    {
                        discovered_name = obj_name;
                        return reinterpret_cast<UClass*>(obj);
                    }
                }
            }
        }

        return nullptr;
    }

    UClass* ResolveParentProfile(ParentProfile& profile)
    {
        if (profile.resolved_class)
            return profile.resolved_class;

        // First try known/expected exact names.
        for (const auto& candidate : profile.buff_candidates)
        {
            UClass* cls = FindLoadedClass(candidate);
            if (cls)
            {
                profile.resolved_class = cls;
                profile.resolved_name = candidate;
                profile.unresolved_warned = false;

                WriteLog(
                    "[RESOLVE] parent=" + profile.name +
                    " buff=" + candidate,
                    true
                );
                return cls;
            }
        }

        // Then discover by tokens among loaded KBD Reins classes.
        std::string discovered;
        UClass* discovered_class = DiscoverReinsClass(profile, discovered);

        if (discovered_class)
        {
            profile.resolved_class = discovered_class;
            profile.resolved_name = discovered;
            profile.unresolved_warned = false;

            WriteLog(
                "[DISCOVER] parent=" + profile.name +
                " buff=" + discovered,
                true
            );
            return discovered_class;
        }

        if (!profile.unresolved_warned)
        {
            std::ostringstream ss;
            ss << "[UNRESOLVED] parent=" << profile.name << " candidates=";

            for (size_t i = 0; i < profile.buff_candidates.size(); ++i)
            {
                if (i)
                    ss << ",";
                ss << profile.buff_candidates[i];
            }

            WriteLog(ss.str(), true);
            profile.unresolved_warned = true;
        }

        return nullptr;
    }

    bool IsValyrianReinsItem(UPrimalItem* item)
    {
        return item && ClassName(item).find("ValyrianReins") != std::string::npos;
    }

    // v0.6 proved that Sid hybrid Costume-slot Reins are in EquippedItems.
    UPrimalItem* FindValyrianReins(APrimalDinoCharacter* dino)
    {
        if (!dino)
            return nullptr;

        auto* inv = dino->MyInventoryComponentField();
        if (!inv)
            return nullptr;

        for (UPrimalItem* item : inv->EquippedItemsField())
        {
            if (!item)
                continue;

            if (IsValyrianReinsItem(item))
                return item;

            UPrimalItem* skin = item->MyItemSkinField();
            if (IsValyrianReinsItem(skin))
                return skin;
        }

        // Keep ItemSlots as a secondary compatibility path.
        for (UPrimalItem* item : inv->ItemSlotsField())
        {
            if (!item)
                continue;

            if (IsValyrianReinsItem(item))
                return item;

            UPrimalItem* skin = item->MyItemSkinField();
            if (IsValyrianReinsItem(skin))
                return skin;
        }

        return nullptr;
    }

    APrimalBuff* GetBuff(APrimalDinoCharacter* dino, UClass* buff_class)
    {
        if (!dino || !buff_class)
            return nullptr;

        return dino->GetBuff(TSubclassOf<APrimalBuff>(buff_class));
    }

    void ApplyParentReinsBuff(
        APrimalDinoCharacter* dino,
        ParentProfile& profile,
        UPrimalItem* reins
    )
    {
        UClass* cls = ResolveParentProfile(profile);
        if (!cls)
            return;

        if (GetBuff(dino, cls))
            return;

        APrimalBuff* added = APrimalBuff::StaticAddBuff(
            TSubclassOf<APrimalBuff>(cls),
            dino,
            reins,
            dino,
            false
        );

        if (added)
        {
            WriteLog(
                "[ADD] dino=" + ClassName(dino) +
                " parent=" + profile.name +
                " buff=" + profile.resolved_name,
                true
            );
        }
        else
        {
            WriteLog(
                "[WARN] StaticAddBuff failed dino=" + ClassName(dino) +
                " parent=" + profile.name +
                " buff=" + profile.resolved_name,
                true
            );
        }
    }

    void RemoveParentReinsBuff(
        APrimalDinoCharacter* dino,
        ParentProfile& profile
    )
    {
        // Resolve here too. This prevents a hot reload from leaving an old
        // bridge-injected buff stuck on a hybrid whose Reins were removed.
        UClass* cls = ResolveParentProfile(profile);
        if (!cls)
            return;

        APrimalBuff* active = GetBuff(dino, cls);
        if (!active)
            return;

        WriteLog(
            "[REMOVE] dino=" + ClassName(dino) +
            " parent=" + profile.name +
            " buff=" + profile.resolved_name,
            true
        );

        active->Deactivate();
    }

    HybridMapping* MatchHybrid(const std::string& actual_class)
    {
        const std::string actual_lower = Lower(actual_class);

        for (auto& mapping : mappings)
        {
            for (const auto& alias : mapping.aliases_lower)
            {
                if (!alias.empty() && actual_lower.find(alias) != std::string::npos)
                    return &mapping;
            }
        }

        return nullptr;
    }

    void ProcessDino(APrimalDinoCharacter* dino)
    {
        if (!dino || !dino->ClassField())
            return;

        HybridMapping* mapping = MatchHybrid(ObjectName(dino->ClassField()));
        if (!mapping)
            return;

        UPrimalItem* reins = FindValyrianReins(dino);
        const bool reins_equipped = reins != nullptr;

        for (const auto& parent_name : mapping->parents)
        {
            auto it = parent_profiles.find(parent_name);
            if (it == parent_profiles.end())
                continue;

            ParentProfile& profile = it->second;

            if (reins_equipped)
                ApplyParentReinsBuff(dino, profile, reins);
            else
                RemoveParentReinsBuff(dino, profile);
        }
    }

    void Scan()
    {
        if (!enabled || mappings.empty())
            return;

        UWorld* world = ArkApi::GetApiUtils().GetWorld();
        if (!world)
            return;

        TArray<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(
            reinterpret_cast<UObject*>(world),
            APrimalDinoCharacter::GetPrivateStaticClass(),
            &actors
        );

        for (AActor* actor : actors)
        {
            if (actor)
                ProcessDino(static_cast<APrimalDinoCharacter*>(actor));
        }
    }

    void Timer()
    {
        if (!enabled)
            return;

        if (++seconds_since_scan < scan_every_seconds)
            return;

        seconds_since_scan = 0;
        Scan();
    }

    void DumpReinsBuffsCommand(APlayerController* controller, FString*, bool)
    {
        std::set<std::string> found;
        const std::string prefix = "buff_valyrianreins_";

        auto& objects = Globals::GUObjectArray()();

        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;

            if (obj->ClassField())
            {
                const std::string cls_name = ObjectName(obj->ClassField());
                if (StartsWith(Lower(cls_name), prefix))
                    found.insert(cls_name);
            }

            const std::string obj_name = ObjectName(obj);
            if (StartsWith(Lower(obj_name), prefix) && EndsWith(obj_name, "_C"))
                found.insert(obj_name);
        }

        const std::string header =
            "KBDHybridBridge v1.0: found " +
            std::to_string(found.size()) +
            " loaded Valyrian Reins buff classes.";

        Reply(controller, header);
        WriteLog("[DUMP] " + header, true);

        if (found.empty())
        {
            Reply(controller, "No loaded Buff_ValyrianReins_* classes found.", true);
            return;
        }

        for (const auto& name : found)
        {
            Reply(controller, name);
            WriteLog("[DUMP] " + name, true);
        }
    }

    void DumpMatchedHybridsCommand(APlayerController* controller, FString*, bool)
    {
        UWorld* world = ArkApi::GetApiUtils().GetWorld();
        if (!world)
        {
            Reply(controller, "KBDHybridBridge: world is not available.", true);
            return;
        }

        TArray<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(
            reinterpret_cast<UObject*>(world),
            APrimalDinoCharacter::GetPrivateStaticClass(),
            &actors
        );

        std::set<std::string> logged;
        int matches = 0;

        for (AActor* actor : actors)
        {
            if (!actor)
                continue;

            auto* dino = static_cast<APrimalDinoCharacter*>(actor);
            const std::string cls = ClassName(dino);

            HybridMapping* mapping = MatchHybrid(cls);
            if (!mapping)
                continue;

            const std::string key = cls + "|" + mapping->name;
            if (!logged.insert(key).second)
                continue;

            ++matches;

            std::ostringstream ss;
            ss << cls << " -> " << mapping->name << " [";

            for (size_t i = 0; i < mapping->parents.size(); ++i)
            {
                if (i)
                    ss << ", ";
                ss << mapping->parents[i];
            }

            ss << "]";

            Reply(controller, ss.str());
            WriteLog("[MATCH] " + ss.str(), true);
        }

        if (matches == 0)
            Reply(controller, "KBDHybridBridge: no live mapped Sid hybrids found.", true);
        else
            Reply(controller, "KBDHybridBridge: matched " + std::to_string(matches) + " hybrid class(es).");
    }

    json BuiltInConfig()
    {
        static const char* kBuiltIn = R"JSON(
{
  "Enabled": true,
  "Debug": false,
  "ScanEverySeconds": 5,
  "ParentProfiles": {
    "Argent": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Argent_C"
      ],
      "DiscoveryTokens": [
        "argent"
      ],
      "Verified": true
    },
    "Tapejara": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Tapejara_C"
      ],
      "DiscoveryTokens": [
        "tapejara"
      ],
      "Verified": true
    },
    "Ptera": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Ptera_C",
        "Buff_ValyrianReins_Ptero_C"
      ],
      "DiscoveryTokens": [
        "ptera",
        "ptero"
      ],
      "Verified": false
    },
    "Quetz": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Quetz_C"
      ],
      "DiscoveryTokens": [
        "quetz"
      ],
      "Verified": false
    },
    "Moth": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Moth_C"
      ],
      "DiscoveryTokens": [
        "moth",
        "lymantria"
      ],
      "Verified": false
    },
    "Pela": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Pela_C",
        "Buff_ValyrianReins_Pelagornis_C"
      ],
      "DiscoveryTokens": [
        "pela",
        "pelagornis"
      ],
      "Verified": false
    },
    "RockDrake": {
      "BuffCandidates": [
        "Buff_ValyrianReins_RockDrake_C",
        "Buff_ValyrianReins_Drake_C"
      ],
      "DiscoveryTokens": [
        "rockdrake",
        "drake"
      ],
      "Verified": false
    },
    "Griffin": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Griffin_C"
      ],
      "DiscoveryTokens": [
        "griffin"
      ],
      "Verified": false
    },
    "Owl": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Owl_C",
        "Buff_ValyrianReins_SnowOwl_C"
      ],
      "DiscoveryTokens": [
        "owl",
        "snowowl"
      ],
      "Verified": false
    },
    "Managarmr": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Managarmr_C",
        "Buff_ValyrianReins_Mana_C"
      ],
      "DiscoveryTokens": [
        "managarmr",
        "mana"
      ],
      "Verified": false
    },
    "GasBags": {
      "BuffCandidates": [
        "Buff_ValyrianReins_GasBags_C",
        "Buff_ValyrianReins_Gasbag_C"
      ],
      "DiscoveryTokens": [
        "gasbags",
        "gasbag"
      ],
      "Verified": false
    },
    "Tropeognathus": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Tropeognathus_C",
        "Buff_ValyrianReins_Tropeo_C"
      ],
      "DiscoveryTokens": [
        "tropeognathus",
        "tropeo"
      ],
      "Verified": false
    },
    "Wyvern": {
      "BuffCandidates": [
        "Buff_ValyrianReins_Wyvern_C"
      ],
      "DiscoveryTokens": [
        "wyvern"
      ],
      "Verified": false
    }
  },
  "Mappings": [
    {
      "Name": "Argentjara",
      "Aliases": [
        "Argentjara",
        "TapeArgent"
      ],
      "Parents": [
        "Argent",
        "Tapejara"
      ]
    },
    {
      "Name": "Serpentmoloch",
      "Aliases": [
        "Serpentmoloch",
        "Flyboa"
      ],
      "Parents": [
        "Argent"
      ]
    },
    {
      "Name": "Tropeocoatlus",
      "Aliases": [
        "Tropeocoatlus",
        "QuetzTrop"
      ],
      "Parents": [
        "Quetz",
        "Tropeognathus"
      ]
    },
    {
      "Name": "Colossodraco",
      "Aliases": [
        "Colossodraco",
        "RockQuetz"
      ],
      "Parents": [
        "Quetz"
      ]
    },
    {
      "Name": "Gigadeisopteryx",
      "Aliases": [
        "Gigadeisopteryx",
        "Gigadeusopteryx",
        "QuetzGiga"
      ],
      "Parents": [
        "Quetz"
      ]
    },
    {
      "Name": "Wolf Wyvern",
      "Aliases": [
        "WolfWyvern",
        "Wolf_Wyvern",
        "Wolfwyv"
      ],
      "Parents": [
        "Wyvern"
      ]
    },
    {
      "Name": "Necro Wyvern",
      "Aliases": [
        "NecroWyvern",
        "Necro_Wyvern"
      ],
      "Parents": [
        "Wyvern"
      ]
    },
    {
      "Name": "Meteor Wyvern",
      "Aliases": [
        "MeteorWyvern",
        "Meteor_Wyvern"
      ],
      "Parents": [
        "Wyvern"
      ]
    },
    {
      "Name": "Plasma Wyvern",
      "Aliases": [
        "PlasmaWyvern",
        "Plasma_Wyvern"
      ],
      "Parents": [
        "Wyvern"
      ]
    },
    {
      "Name": "Neoavirhinus",
      "Aliases": [
        "Neoavirhinus"
      ],
      "Parents": [
        "Tropeognathus"
      ]
    },
    {
      "Name": "Frostvarg",
      "Aliases": [
        "Frostvarg",
        "Frovarg"
      ],
      "Parents": [
        "Managarmr"
      ]
    },
    {
      "Name": "Pelagosuchus",
      "Aliases": [
        "Pelagosuchus"
      ],
      "Parents": [
        "Pela"
      ]
    },
    {
      "Name": "Pelosornis",
      "Aliases": [
        "Pelosornis"
      ],
      "Parents": [
        "Ptera"
      ]
    },
    {
      "Name": "Phalainasuchus",
      "Aliases": [
        "Phalainasuchus"
      ],
      "Parents": [
        "Moth"
      ]
    },
    {
      "Name": "Nyctavenator",
      "Aliases": [
        "Nyctavenator"
      ],
      "Parents": [
        "Wyvern"
      ]
    },
    {
      "Name": "Vulcanotavis",
      "Aliases": [
        "Vulcanotavis",
        "ArgentMagma"
      ],
      "Parents": [
        "Argent"
      ]
    },
    {
      "Name": "Magsuredrake",
      "Aliases": [
        "Magsuredrake",
        "Magsure"
      ],
      "Parents": [
        "RockDrake",
        "Wyvern"
      ]
    },
    {
      "Name": "Glacialisvenator",
      "Aliases": [
        "Glacialisvenator",
        "ManaYO"
      ],
      "Parents": [
        "Managarmr",
        "Owl"
      ]
    },
    {
      "Name": "Caelumlator",
      "Aliases": [
        "Caelumlator",
        "Yomoth"
      ],
      "Parents": [
        "Moth"
      ]
    },
    {
      "Name": "Spinojalosaurus",
      "Aliases": [
        "Spinojalosaurus",
        "Spinotape"
      ],
      "Parents": [
        "Tapejara"
      ]
    },
    {
      "Name": "Aracnoptera",
      "Aliases": [
        "Aracnoptera",
        "SpiderMoth"
      ],
      "Parents": [
        "Moth"
      ]
    },
    {
      "Name": "Gigaplolophosaura",
      "Aliases": [
        "Gigaplolophosaura",
        "TapajaraHyb"
      ],
      "Parents": [
        "Tapejara"
      ]
    }
  ]
}
)JSON";

        return json::parse(kBuiltIn);
    }

    void ReadConfig()
    {
        std::ifstream f(PluginDir() + "/config.json");
        if (!f.is_open())
            throw std::runtime_error("KBDHybridBridge: could not open config.json");

        json external_cfg;
        f >> external_cfg;

        // v0.9: keep working even if the server still has an older v0.6 config.json.
        // Runtime toggles come from the external file, while missing/new schema sections
        // fall back to defaults compiled into the DLL.
        json cfg = BuiltInConfig();

        cfg["Enabled"] = external_cfg.value("Enabled", cfg.value("Enabled", true));
        cfg["Debug"] = external_cfg.value("Debug", cfg.value("Debug", false));
        cfg["ScanEverySeconds"] =
            external_cfg.value("ScanEverySeconds", cfg.value("ScanEverySeconds", 5));

        const bool has_new_profiles =
            external_cfg.contains("ParentProfiles") &&
            external_cfg["ParentProfiles"].is_object() &&
            !external_cfg["ParentProfiles"].empty();

        const bool has_new_mappings =
            external_cfg.contains("Mappings") &&
            external_cfg["Mappings"].is_array() &&
            !external_cfg["Mappings"].empty() &&
            external_cfg["Mappings"][0].contains("Name");

        if (has_new_profiles)
            cfg["ParentProfiles"] = external_cfg["ParentProfiles"];

        if (has_new_mappings)
            cfg["Mappings"] = external_cfg["Mappings"];

        enabled = cfg.value("Enabled", true);
        debug = cfg.value("Debug", false);
        scan_every_seconds = std::max(1, cfg.value("ScanEverySeconds", 5));

        if (!has_new_profiles || !has_new_mappings)
        {
            WriteLog(
                "[CONFIG] older/incomplete config schema detected; using v0.9 built-in parent/mapping defaults",
                true
            );
        }

        std::unordered_map<std::string, ParentProfile> new_profiles;

        if (cfg.contains("ParentProfiles") && cfg["ParentProfiles"].is_object())
        {
            for (auto it = cfg["ParentProfiles"].begin();
                 it != cfg["ParentProfiles"].end();
                 ++it)
            {
                ParentProfile profile;
                profile.name = it.key();

                const auto& value = it.value();

                for (const auto& c : value.value("BuffCandidates", json::array()))
                    profile.buff_candidates.emplace_back(c.get<std::string>());

                for (const auto& t : value.value("DiscoveryTokens", json::array()))
                    profile.discovery_tokens.emplace_back(t.get<std::string>());

                new_profiles.emplace(profile.name, std::move(profile));
            }
        }

        std::vector<HybridMapping> new_mappings;

        for (const auto& jm : cfg.value("Mappings", json::array()))
        {
            HybridMapping mapping;
            mapping.name = jm.value("Name", "");

            for (const auto& a : jm.value("Aliases", json::array()))
            {
                const std::string alias = a.get<std::string>();
                mapping.aliases.emplace_back(alias);
                mapping.aliases_lower.emplace_back(Lower(alias));
            }

            for (const auto& p : jm.value("Parents", json::array()))
                mapping.parents.emplace_back(p.get<std::string>());

            if (!mapping.name.empty() &&
                !mapping.aliases.empty() &&
                !mapping.parents.empty())
            {
                new_mappings.emplace_back(std::move(mapping));
            }
        }

        parent_profiles = std::move(new_profiles);
        mappings = std::move(new_mappings);
        seconds_since_scan = 0;

        WriteLog(
            "[CONFIG] loaded " +
            std::to_string(parent_profiles.size()) +
            " parent profiles and " +
            std::to_string(mappings.size()) +
            " hybrid mappings",
            true
        );
    }


    std::string StatusText()
    {
        return
            std::string("KBDHybridBridge v1.1 | Enabled=") +
            (enabled ? "true" : "false") +
            " | Scan=" +
            std::to_string(scan_every_seconds) +
            "s | Parents=" +
            std::to_string(parent_profiles.size()) +
            " | Hybrids=" +
            std::to_string(mappings.size());
    }

    std::vector<std::string> DumpReinsBuffNames()
    {
        std::set<std::string> found;
        const std::string prefix = "buff_valyrianreins_";

        auto& objects = Globals::GUObjectArray()();

        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;

            if (obj->ClassField())
            {
                const std::string cls_name = ObjectName(obj->ClassField());
                if (StartsWith(Lower(cls_name), prefix))
                    found.insert(cls_name);
            }

            const std::string obj_name = ObjectName(obj);
            if (StartsWith(Lower(obj_name), prefix) && EndsWith(obj_name, "_C"))
                found.insert(obj_name);
        }

        return std::vector<std::string>(found.begin(), found.end());
    }

    std::vector<std::string> MatchedHybridLines()
    {
        std::vector<std::string> output;

        UWorld* world = ArkApi::GetApiUtils().GetWorld();
        if (!world)
        {
            output.emplace_back("World is not available.");
            return output;
        }

        TArray<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(
            reinterpret_cast<UObject*>(world),
            APrimalDinoCharacter::GetPrivateStaticClass(),
            &actors
        );

        std::set<std::string> logged;

        for (AActor* actor : actors)
        {
            if (!actor)
                continue;

            auto* dino = static_cast<APrimalDinoCharacter*>(actor);
            const std::string cls = ClassName(dino);

            HybridMapping* mapping = MatchHybrid(cls);
            if (!mapping)
                continue;

            const std::string key = cls + "|" + mapping->name;
            if (!logged.insert(key).second)
                continue;

            std::ostringstream ss;
            ss << cls << " -> " << mapping->name << " [";

            for (size_t i = 0; i < mapping->parents.size(); ++i)
            {
                if (i)
                    ss << ",";
                ss << mapping->parents[i];
            }

            ss << "]";
            output.emplace_back(ss.str());
        }

        if (output.empty())
            output.emplace_back("No live mapped Sid hybrids found.");

        return output;
    }

    bool HandleKbdTerminalCommand(
        AShooterPlayerController* controller,
        const std::string& raw
    )
    {
        std::string cmd = Lower(Trim(raw));

        // Accept both "kbd status" and "kbd.status" styles after cheat/admincheat.
        if (cmd == "kbd" || cmd == "kbd help")
        {
            TerminalReply(
                controller,
                "KBD commands: cheat kbd status | reload | scan | dump | hybrids"
            );
            return true;
        }

        if (cmd == "kbd status" || cmd == "kbd.status" ||
            cmd == "kbdhybridbridge.status")
        {
            TerminalReply(controller, StatusText());
            WriteLog("[TERMINAL] status", true);
            return true;
        }

        if (cmd == "kbd reload" || cmd == "kbd.reload" ||
            cmd == "kbdhybridbridge.reload")
        {
            try
            {
                ReadConfig();
                Scan();

                TerminalReply(
                    controller,
                    "KBDHybridBridge reloaded. Parents=" +
                    std::to_string(parent_profiles.size()) +
                    " Hybrids=" +
                    std::to_string(mappings.size())
                );

                WriteLog("[TERMINAL] reload", true);
            }
            catch (const std::exception& e)
            {
                TerminalReply(
                    controller,
                    std::string("KBD reload failed: ") + e.what()
                );
            }

            return true;
        }

        if (cmd == "kbd scan" || cmd == "kbd.scan" ||
            cmd == "kbdhybridbridge.scan")
        {
            Scan();
            TerminalReply(controller, "KBDHybridBridge manual scan complete.");
            WriteLog("[TERMINAL] scan", true);
            return true;
        }

        if (cmd == "kbd dump" || cmd == "kbd.dump" ||
            cmd == "kbdhybridbridge.dumpreinsbuffs")
        {
            const auto names = DumpReinsBuffNames();

            TerminalReply(
                controller,
                "Loaded KBD Reins buffs: " + std::to_string(names.size())
            );

            for (const auto& name : names)
            {
                TerminalReply(controller, name);
                WriteLog("[DUMP] " + name, true);
            }

            return true;
        }

        if (cmd == "kbd hybrids" || cmd == "kbd.hybrids" ||
            cmd == "kbdhybridbridge.dumpmatchedhybrids")
        {
            const auto lines = MatchedHybridLines();

            for (const auto& line : lines)
            {
                TerminalReply(controller, line);
                WriteLog("[MATCH] " + line, true);
            }

            return true;
        }

        return false;
    }

    void Hook_AShooterPlayerController_AdminCheat(
        AShooterPlayerController* controller,
        FString* msg
    )
    {
        if (HandleKbdTerminalCommand(controller, FStringToUtf8(msg)))
            return;

        AShooterPlayerController_AdminCheat_original(controller, msg);
    }

    void Hook_AShooterPlayerController_Cheat(
        AShooterPlayerController* controller,
        FString* msg
    )
    {
        if (HandleKbdTerminalCommand(controller, FStringToUtf8(msg)))
            return;

        AShooterPlayerController_Cheat_original(controller, msg);
    }

    void RconStatus(
        RCONClientConnection* connection,
        RCONPacket* packet,
        UWorld*
    )
    {
        RconReply(connection, packet, StatusText());
    }

    void RconReload(
        RCONClientConnection* connection,
        RCONPacket* packet,
        UWorld*
    )
    {
        try
        {
            ReadConfig();
            Scan();

            RconReply(
                connection,
                packet,
                "Reloaded. Parents=" +
                std::to_string(parent_profiles.size()) +
                " Hybrids=" +
                std::to_string(mappings.size())
            );
        }
        catch (const std::exception& e)
        {
            RconReply(
                connection,
                packet,
                std::string("Reload failed: ") + e.what()
            );
        }
    }

    void RconScan(
        RCONClientConnection* connection,
        RCONPacket* packet,
        UWorld*
    )
    {
        Scan();
        RconReply(connection, packet, "Manual scan complete.");
    }

    void RconDump(
        RCONClientConnection* connection,
        RCONPacket* packet,
        UWorld*
    )
    {
        const auto names = DumpReinsBuffNames();

        RconReply(
            connection,
            packet,
            "Loaded KBD Reins buffs: " + std::to_string(names.size())
        );

        for (const auto& name : names)
            RconReply(connection, packet, name);
    }

    void RconHybrids(
        RCONClientConnection* connection,
        RCONPacket* packet,
        UWorld*
    )
    {
        const auto lines = MatchedHybridLines();

        for (const auto& line : lines)
            RconReply(connection, packet, line);
    }

    void ReloadCommand(APlayerController* controller, FString*, bool)
    {
        try
        {
            ReadConfig();
            Scan();

            const std::string msg =
                "KBDHybridBridge v1.0 reloaded: " +
                std::to_string(parent_profiles.size()) +
                " parent profiles, " +
                std::to_string(mappings.size()) +
                " hybrid mappings.";

            WriteLog("[RELOAD] " + msg, true);
            Reply(controller, msg);
        }
        catch (const std::exception& e)
        {
            const std::string msg =
                std::string("KBDHybridBridge reload failed: ") + e.what();

            WriteLog("[ERROR] " + msg, true);
            Reply(controller, msg, true);
        }
    }

    void ScanCommand(APlayerController* controller, FString*, bool)
    {
        Scan();
        WriteLog("[SCAN] manual scan complete", true);
        Reply(controller, "KBDHybridBridge: manual scan complete.");
    }

    void StatusCommand(APlayerController* controller, FString*, bool)
    {
        const std::string status =
            std::string("KBDHybridBridge v1.0 | Enabled=") +
            (enabled ? "true" : "false") +
            " | ScanEverySeconds=" +
            std::to_string(scan_every_seconds) +
            " | ParentProfiles=" +
            std::to_string(parent_profiles.size()) +
            " | HybridMappings=" +
            std::to_string(mappings.size());

        Reply(controller, status);
    }


    void ChatStatus(
        AShooterPlayerController* controller,
        FString*,
        EChatSendMode::Type
    )
    {
        const std::string status =
            std::string("v1.0 | Enabled=") +
            (enabled ? "true" : "false") +
            " | Scan=" + std::to_string(scan_every_seconds) +
            "s | Parents=" + std::to_string(parent_profiles.size()) +
            " | Hybrids=" + std::to_string(mappings.size());

        ChatReply(controller, status);
    }

    void ChatReload(
        AShooterPlayerController* controller,
        FString*,
        EChatSendMode::Type
    )
    {
        try
        {
            ReadConfig();
            Scan();

            ChatReply(
                controller,
                "Reloaded. Parents=" +
                std::to_string(parent_profiles.size()) +
                " Hybrids=" +
                std::to_string(mappings.size())
            );
        }
        catch (const std::exception& e)
        {
            ChatReply(controller, std::string("Reload failed: ") + e.what());
        }
    }

    void ChatScan(
        AShooterPlayerController* controller,
        FString*,
        EChatSendMode::Type
    )
    {
        Scan();
        ChatReply(controller, "Manual hybrid scan complete.");
    }

    void ChatDumpReins(
        AShooterPlayerController* controller,
        FString*,
        EChatSendMode::Type
    )
    {
        std::set<std::string> found;
        const std::string prefix = "buff_valyrianreins_";

        auto& objects = Globals::GUObjectArray()();

        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;

            if (obj->ClassField())
            {
                const std::string cls_name = ObjectName(obj->ClassField());
                if (StartsWith(Lower(cls_name), prefix))
                    found.insert(cls_name);
            }

            const std::string obj_name = ObjectName(obj);
            if (StartsWith(Lower(obj_name), prefix) && EndsWith(obj_name, "_C"))
                found.insert(obj_name);
        }

        ChatReply(
            controller,
            "Loaded Valyrian Reins buff classes: " +
            std::to_string(found.size())
        );

        for (const auto& name : found)
            ChatReply(controller, name);
    }

    void ChatDumpHybrids(
        AShooterPlayerController* controller,
        FString*,
        EChatSendMode::Type
    )
    {
        UWorld* world = ArkApi::GetApiUtils().GetWorld();
        if (!world)
        {
            ChatReply(controller, "World is not available.");
            return;
        }

        TArray<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(
            reinterpret_cast<UObject*>(world),
            APrimalDinoCharacter::GetPrivateStaticClass(),
            &actors
        );

        std::set<std::string> logged;
        int matches = 0;

        for (AActor* actor : actors)
        {
            if (!actor)
                continue;

            auto* dino = static_cast<APrimalDinoCharacter*>(actor);
            const std::string cls = ClassName(dino);

            HybridMapping* mapping = MatchHybrid(cls);
            if (!mapping)
                continue;

            const std::string key = cls + "|" + mapping->name;
            if (!logged.insert(key).second)
                continue;

            ++matches;

            std::ostringstream ss;
            ss << cls << " -> " << mapping->name << " [";

            for (size_t i = 0; i < mapping->parents.size(); ++i)
            {
                if (i)
                    ss << ",";
                ss << mapping->parents[i];
            }

            ss << "]";
            ChatReply(controller, ss.str());
        }

        if (matches == 0)
            ChatReply(controller, "No live mapped Sid hybrids found.");
    }

    void Load()
    {
        ReadConfig();

        auto& hooks = ArkApi::GetHooks();

        const bool admin_cheat_hook = hooks.SetHook(
            "AShooterPlayerController.AdminCheat",
            &Hook_AShooterPlayerController_AdminCheat,
            &AShooterPlayerController_AdminCheat_original
        );

        const bool cheat_hook = hooks.SetHook(
            "AShooterPlayerController.Cheat",
            &Hook_AShooterPlayerController_Cheat,
            &AShooterPlayerController_Cheat_original
        );

        WriteLog(
            std::string("[HOOK] AdminCheat=") +
            (admin_cheat_hook ? "OK" : "FAIL") +
            " Cheat=" +
            (cheat_hook ? "OK" : "FAIL"),
            true
        );

        ArkApi::GetCommands().AddRconCommand("kbd.status", &RconStatus);
        ArkApi::GetCommands().AddRconCommand("kbd.reload", &RconReload);
        ArkApi::GetCommands().AddRconCommand("kbd.scan", &RconScan);
        ArkApi::GetCommands().AddRconCommand("kbd.dump", &RconDump);
        ArkApi::GetCommands().AddRconCommand("kbd.hybrids", &RconHybrids);

        ArkApi::GetCommands().AddOnTimerCallback(
            "KBDHybridBridge.Timer",
            &Timer
        );

        ArkApi::GetCommands().AddConsoleCommand(
            "KBDHybridBridge.Reload",
            &ReloadCommand
        );

        ArkApi::GetCommands().AddConsoleCommand(
            "KBDHybridBridge.Scan",
            &ScanCommand
        );

        ArkApi::GetCommands().AddConsoleCommand(
            "KBDHybridBridge.DumpReinsBuffs",
            &DumpReinsBuffsCommand
        );

        ArkApi::GetCommands().AddConsoleCommand(
            "KBDHybridBridge.DumpMatchedHybrids",
            &DumpMatchedHybridsCommand
        );

        ArkApi::GetCommands().AddConsoleCommand(
            "KBDHybridBridge.Status",
            &StatusCommand
        );

        ArkApi::GetCommands().AddChatCommand("/kbdstatus", &ChatStatus);
        ArkApi::GetCommands().AddChatCommand("/kbdreload", &ChatReload);
        ArkApi::GetCommands().AddChatCommand("/kbdscan", &ChatScan);
        ArkApi::GetCommands().AddChatCommand("/kbddump", &ChatDumpReins);
        ArkApi::GetCommands().AddChatCommand("/kbdhybrids", &ChatDumpHybrids);

        WriteLog("[LOAD] KBDHybridBridge v1.1 loaded", true);
    }

    void Unload()
    {
        ArkApi::GetCommands().RemoveOnTimerCallback(
            "KBDHybridBridge.Timer"
        );

        ArkApi::GetCommands().RemoveConsoleCommand(
            "KBDHybridBridge.Reload"
        );

        ArkApi::GetCommands().RemoveConsoleCommand(
            "KBDHybridBridge.Scan"
        );

        ArkApi::GetCommands().RemoveConsoleCommand(
            "KBDHybridBridge.DumpReinsBuffs"
        );

        ArkApi::GetCommands().RemoveConsoleCommand(
            "KBDHybridBridge.DumpMatchedHybrids"
        );

        ArkApi::GetCommands().RemoveConsoleCommand(
            "KBDHybridBridge.Status"
        );

        ArkApi::GetCommands().RemoveChatCommand("/kbdstatus");
        ArkApi::GetCommands().RemoveChatCommand("/kbdreload");
        ArkApi::GetCommands().RemoveChatCommand("/kbdscan");
        ArkApi::GetCommands().RemoveChatCommand("/kbddump");
        ArkApi::GetCommands().RemoveChatCommand("/kbdhybrids");

        ArkApi::GetCommands().RemoveRconCommand("kbd.status");
        ArkApi::GetCommands().RemoveRconCommand("kbd.reload");
        ArkApi::GetCommands().RemoveRconCommand("kbd.scan");
        ArkApi::GetCommands().RemoveRconCommand("kbd.dump");
        ArkApi::GetCommands().RemoveRconCommand("kbd.hybrids");

        ArkApi::GetHooks().DisableHook(
            "AShooterPlayerController.AdminCheat",
            &Hook_AShooterPlayerController_AdminCheat
        );

        ArkApi::GetHooks().DisableHook(
            "AShooterPlayerController.Cheat",
            &Hook_AShooterPlayerController_Cheat
        );

        WriteLog("[UNLOAD] KBDHybridBridge unloaded", true);
    }
}

extern "C" __declspec(dllexport) void Plugin_Init()
{
    KBDHybridBridge::Load();
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
    KBDHybridBridge::Unload();
}
