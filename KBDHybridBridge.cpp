#include <API/ARK/Ark.h>
#include <json.hpp>

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ArkApi.lib")

namespace KBDHybridBridge
{
    using json = nlohmann::json;

    struct BuffSpec
    {
        std::string name;
        UClass* cls = nullptr;
    };

    struct Rule
    {
        std::string trigger;
        std::vector<BuffSpec> buffs;
    };

    struct Mapping
    {
        std::string dino_class_name;
        UClass* dino_class = nullptr;
        std::vector<Rule> rules;
    };

    static const std::string plugin_name = "KBDHybridBridge";

    static bool enabled = true;
    static bool debug = false;
    static int scan_every_seconds = 5;
    static int seconds_since_scan = 0;

    static std::vector<Mapping> mappings;
    static std::unordered_map<UClass*, size_t> mapping_by_class;

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

    // EXPENSIVE lookup. v0.5 only calls this at config load/reload,
    // never once-per-dino / once-per-scan.
    UClass* FindLoadedClass(const std::string& short_name)
    {
        auto& objects = Globals::GUObjectArray()();

        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;

            if (ObjectName(obj) != short_name)
                continue;

            if (obj->ClassField() && ObjectName(obj->ClassField()) == "Class")
                return reinterpret_cast<UClass*>(obj);
        }

        return nullptr;
    }

    bool IsValyrianReinsItem(UPrimalItem* item)
    {
        if (!item)
            return false;

        const auto item_class = ClassName(item);
        return item_class.find("ValyrianReins") != std::string::npos;
    }

    UPrimalItem* MatchDirectOrSkin(UPrimalItem* item)
    {
        if (!item)
            return nullptr;

        if (IsValyrianReinsItem(item))
            return item;

        UPrimalItem* skin = item->MyItemSkinField();
        if (IsValyrianReinsItem(skin))
            return skin;

        return nullptr;
    }

    // v0.5:
    // - EquippedItems and ItemSlots first.
    // - Then InventoryItems, but ONLY if ARK marks the Reins item equipped,
    //   or the Reins is actually skinned onto another item.
    // This is intended to catch dino Costume-slot equipment without treating
    // a loose Reins item in inventory as active.
    UPrimalItem* FindValyrianReins(APrimalDinoCharacter* dino)
    {
        if (!dino)
            return nullptr;

        auto* inv = dino->MyInventoryComponentField();
        if (!inv)
            return nullptr;

        for (UPrimalItem* item : inv->EquippedItemsField())
        {
            if (auto* found = MatchDirectOrSkin(item))
                return found;
        }

        for (UPrimalItem* item : inv->ItemSlotsField())
        {
            if (auto* found = MatchDirectOrSkin(item))
                return found;
        }

        for (UPrimalItem* item : inv->InventoryItemsField())
        {
            if (!item)
                continue;

            if (IsValyrianReinsItem(item))
            {
                if (item->bEquippedItem().Get())
                    return item;
            }

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

    bool ApplyBuff(APrimalDinoCharacter* dino, const BuffSpec& buff, UPrimalItem* associated_item)
    {
        if (!buff.cls)
            return false;

        if (GetBuff(dino, buff.cls))
            return true;

        APrimalBuff* added = APrimalBuff::StaticAddBuff(
            TSubclassOf<APrimalBuff>(buff.cls),
            dino,
            associated_item,
            dino,
            false
        );

        if (added)
        {
            WriteLog("[ADD] " + ObjectName(dino) + " <- " + buff.name, true);
            return true;
        }

        WriteLog("[WARN] StaticAddBuff failed: " + ObjectName(dino) + " <- " + buff.name, true);
        return false;
    }

    void RemoveBuff(APrimalDinoCharacter* dino, const BuffSpec& buff)
    {
        if (!buff.cls)
            return;

        APrimalBuff* active = GetBuff(dino, buff.cls);
        if (!active)
            return;

        WriteLog("[REMOVE] " + ObjectName(dino) + " <- " + buff.name, true);
        active->Deactivate();
    }

    void ProcessRule(APrimalDinoCharacter* dino, const Rule& rule)
    {
        bool active = false;
        UPrimalItem* associated_item = nullptr;

        if (rule.trigger == "Always")
        {
            active = true;
        }
        else if (rule.trigger == "ValyrianReins")
        {
            associated_item = FindValyrianReins(dino);
            active = associated_item != nullptr;
        }
        else
        {
            return;
        }

        for (const auto& buff : rule.buffs)
        {
            if (active)
                ApplyBuff(dino, buff, associated_item);
            else
                RemoveBuff(dino, buff);
        }
    }

    void ProcessDino(APrimalDinoCharacter* dino)
    {
        if (!dino)
            return;

        auto it = mapping_by_class.find(dino->ClassField());
        if (it == mapping_by_class.end())
            return;

        Mapping& mapping = mappings[it->second];

        for (const auto& rule : mapping.rules)
            ProcessRule(dino, rule);
    }

    // Still a world scan, but:
    // - defaults to once every 5 seconds rather than every second;
    // - class matching is O(1);
    // - buff UClass lookups are cached;
    // - no repeated "reins found" disk logging.
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
            if (!actor)
                continue;

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

        std::vector<Mapping> new_mappings;

        for (const auto& jmap : cfg.value("Mappings", json::array()))
        {
            Mapping mapping;
            mapping.dino_class_name = jmap.value("DinoClass", "");
            if (mapping.dino_class_name.empty())
                continue;

            mapping.dino_class = FindLoadedClass(mapping.dino_class_name);

            if (!mapping.dino_class)
            {
                WriteLog("[WARN] Dino class not loaded/found: " + mapping.dino_class_name, true);
                continue;
            }

            for (const auto& jrule : jmap.value("Rules", json::array()))
            {
                Rule rule;
                rule.trigger = jrule.value("Trigger", "Always");

                for (const auto& jbuff : jrule.value("Buffs", json::array()))
                {
                    BuffSpec spec;
                    spec.name = jbuff.get<std::string>();
                    spec.cls = FindLoadedClass(spec.name);

                    if (!spec.cls)
                    {
                        WriteLog("[WARN] Buff class not loaded/found: " + spec.name, true);
                        continue;
                    }

                    rule.buffs.emplace_back(std::move(spec));
                }

                if (!rule.buffs.empty())
                    mapping.rules.emplace_back(std::move(rule));
            }

            if (!mapping.rules.empty())
                new_mappings.emplace_back(std::move(mapping));
        }

        mappings = std::move(new_mappings);

        mapping_by_class.clear();
        for (size_t i = 0; i < mappings.size(); ++i)
            mapping_by_class[mappings[i].dino_class] = i;

        seconds_since_scan = 0;
        WriteLog("[CONFIG] loaded " + std::to_string(mappings.size()) + " hybrid mappings", true);
    }

    void ReloadCommand(APlayerController*, FString*, bool)
    {
        try
        {
            ReadConfig();
            Scan();
        }
        catch (const std::exception& e)
        {
            WriteLog(std::string("[ERROR] reload failed: ") + e.what(), true);
        }
    }

    void ScanCommand(APlayerController*, FString*, bool)
    {
        Scan();
    }

    void Load()
    {
        ReadConfig();

        ArkApi::GetCommands().AddOnTimerCallback("KBDHybridBridge.Timer", &Timer);
        ArkApi::GetCommands().AddConsoleCommand("KBDHybridBridge.Reload", &ReloadCommand);
        ArkApi::GetCommands().AddConsoleCommand("KBDHybridBridge.Scan", &ScanCommand);

        WriteLog("[LOAD] KBDHybridBridge v0.5 loaded", true);
    }

    void Unload()
    {
        ArkApi::GetCommands().RemoveOnTimerCallback("KBDHybridBridge.Timer");
        ArkApi::GetCommands().RemoveConsoleCommand("KBDHybridBridge.Reload");
        ArkApi::GetCommands().RemoveConsoleCommand("KBDHybridBridge.Scan");

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
