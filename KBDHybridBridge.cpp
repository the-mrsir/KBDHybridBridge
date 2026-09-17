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

    void DumpReinsBuffsCommand(APlayerController*, FString*, bool)
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

        WriteLog(
            "[DUMP] loaded Valyrian Reins buff classes: " +
            std::to_string(found.size()),
            true
        );

        for (const auto& name : found)
            WriteLog("[DUMP] " + name, true);
    }

    void DumpMatchedHybridsCommand(APlayerController*, FString*, bool)
    {
        UWorld* world = ArkApi::GetApiUtils().GetWorld();
        if (!world)
            return;

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
            ss << "[MATCH] class=" << cls
               << " hybrid=" << mapping->name
               << " parents=";

            for (size_t i = 0; i < mapping->parents.size(); ++i)
            {
                if (i)
                    ss << ",";
                ss << mapping->parents[i];
            }

            WriteLog(ss.str(), true);
        }
    }

    void ReadConfig()
    {
        std::ifstream f(PluginDir() + "/config.json");
        if (!f.is_open())
            throw std::runtime_error("KBDHybridBridge: could not open config.json");

        json cfg;
        f >> cfg;

        enabled = cfg.value("Enabled", true);
        debug = cfg.value("Debug", false);
        scan_every_seconds = std::max(1, cfg.value("ScanEverySeconds", 5));

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

    void ReloadCommand(APlayerController*, FString*, bool)
    {
        try
        {
            ReadConfig();
            Scan();
            WriteLog("[RELOAD] config reloaded", true);
        }
        catch (const std::exception& e)
        {
            WriteLog(std::string("[ERROR] reload failed: ") + e.what(), true);
        }
    }

    void ScanCommand(APlayerController*, FString*, bool)
    {
        Scan();
        WriteLog("[SCAN] manual scan complete", true);
    }

    void Load()
    {
        ReadConfig();

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

        WriteLog("[LOAD] KBDHybridBridge v0.7 loaded", true);
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
