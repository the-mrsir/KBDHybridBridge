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

    struct Rule
    {
        std::string trigger;              // "Always" or "ValyrianReins"
        std::vector<std::string> buffs;   // e.g. Buff_ValyrianReins_Argent_C
    };

    struct Mapping
    {
        std::string dino_class;           // e.g. Argentjara_Character_BP_C
        std::vector<Rule> rules;
    };

    static bool enabled = true;
    static bool debug = true;
    static int scan_every_seconds = 1;
    static bool inventory_reins_fallback = false;
    static int seconds_since_scan = 0;
    static std::vector<Mapping> mappings;

    static const std::string plugin_name = "KBDHybridBridge";

    std::string PluginDir()
    {
        return ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + plugin_name;
    }

    void WriteLog(const std::string& msg)
    {
        if (!debug)
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

    // KBD's buff classes are already loaded whenever KBD is active.
    // Find the UClass object by its short Unreal class name.
    UClass* FindLoadedClass(const std::string& short_name)
    {
        auto& objects = Globals::GUObjectArray()();

        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;

            // Class objects have the requested class as their own object name.
            if (ObjectName(obj) != short_name)
                continue;

            // Avoid returning a normal instance/CDO that happens to share text.
            // A UClass object is itself an instance of the Unreal meta-class "Class".
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

    UPrimalItem* MatchReins(UPrimalItem* item, const char* source)
    {
        if (!item)
            return nullptr;

        if (IsValyrianReinsItem(item))
        {
            WriteLog(std::string("[REINS] found directly in ") + source + ": " + ClassName(item));
            return item;
        }

        UPrimalItem* skin = item->MyItemSkinField();
        if (IsValyrianReinsItem(skin))
        {
            WriteLog(std::string("[REINS] found as skin in ") + source + ": " + ClassName(skin));
            return skin;
        }

        return nullptr;
    }

    // ASE keeps equipment, slots/costumes, and ordinary inventory in separate arrays.
    // v0.3 only looked at EquippedItems, which can miss creature costume-slot items.
    UPrimalItem* FindValyrianReins(APrimalDinoCharacter* dino)
    {
        if (!dino)
            return nullptr;

        auto* inv = dino->MyInventoryComponentField();
        if (!inv)
            return nullptr;

        // Normal equipped items (saddles, armor, etc.)
        auto equipped = inv->EquippedItemsField();
        for (UPrimalItem* item : equipped)
        {
            if (auto* found = MatchReins(item, "EquippedItems"))
                return found;
        }

        // Creature costume/slot items can live here instead of EquippedItems.
        auto slots = inv->ItemSlotsField();
        for (UPrimalItem* item : slots)
        {
            if (auto* found = MatchReins(item, "ItemSlots"))
                return found;
        }

        // Diagnostic/fallback path. Disabled by default because a loose Reins item
        // sitting in normal inventory should not count as equipped.
        auto inventory = inv->InventoryItemsField();
        for (UPrimalItem* item : inventory)
        {
            if (!item)
                continue;

            if (IsValyrianReinsItem(item))
            {
                WriteLog("[REINS] Reins exists in InventoryItems: " + ClassName(item));
                if (inventory_reins_fallback)
                    return item;
            }

            UPrimalItem* skin = item->MyItemSkinField();
            if (IsValyrianReinsItem(skin))
            {
                WriteLog("[REINS] Reins skin exists on InventoryItems item: " + ClassName(item));
                if (inventory_reins_fallback)
                    return skin;
            }
        }

        return nullptr;
    }

    APrimalBuff* GetBuff(APrimalDinoCharacter* dino, UClass* buff_class)
    {
        if (!dino || !buff_class)
            return nullptr;

        return dino->GetBuff(TSubclassOf<APrimalBuff>(buff_class));
    }

    bool ApplyBuff(APrimalDinoCharacter* dino, const std::string& buff_name, UPrimalItem* associated_item)
    {
        UClass* buff_class = FindLoadedClass(buff_name);
        if (!buff_class)
        {
            WriteLog("[WARN] Buff class not loaded/found: " + buff_name);
            return false;
        }

        if (GetBuff(dino, buff_class))
            return true;

        // Passing the actual Valyrian Reins item is intentional:
        // KBD can read the associated item's durability/quality rather than
        // us hard-coding KBD's movement/stamina math.
        APrimalBuff* added = APrimalBuff::StaticAddBuff(
            TSubclassOf<APrimalBuff>(buff_class),
            dino,
            associated_item,
            dino,
            false
        );

        if (added)
        {
            WriteLog("[ADD] " + ObjectName(dino) + " <- " + buff_name);
            return true;
        }

        WriteLog("[WARN] StaticAddBuff failed: " + ObjectName(dino) + " <- " + buff_name);
        return false;
    }

    void RemoveBuff(APrimalDinoCharacter* dino, const std::string& buff_name)
    {
        UClass* buff_class = FindLoadedClass(buff_name);
        if (!buff_class)
            return;

        APrimalBuff* buff = GetBuff(dino, buff_class);
        if (buff)
        {
            WriteLog("[REMOVE] " + ObjectName(dino) + " <- " + buff_name);
            buff->Deactivate();
        }
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
            WriteLog("[WARN] Unknown trigger: " + rule.trigger);
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
        if (!dino || !dino->ClassField())
            return;

        const std::string dino_class = ObjectName(dino->ClassField());

        for (const auto& mapping : mappings)
        {
            if (mapping.dino_class != dino_class)
                continue;

            for (const auto& rule : mapping.rules)
                ProcessRule(dino, rule);

            break;
        }
    }

    void Scan()
    {
        if (!enabled)
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
        debug = cfg.value("Debug", true);
        scan_every_seconds = std::max(1, cfg.value("ScanEverySeconds", 1));
        inventory_reins_fallback = cfg.value("InventoryReinsFallback", false);

        std::vector<Mapping> new_mappings;

        for (const auto& jmap : cfg.value("Mappings", json::array()))
        {
            Mapping mapping;
            mapping.dino_class = jmap.value("DinoClass", "");

            for (const auto& jrule : jmap.value("Rules", json::array()))
            {
                Rule rule;
                rule.trigger = jrule.value("Trigger", "Always");

                for (const auto& jbuff : jrule.value("Buffs", json::array()))
                    rule.buffs.emplace_back(jbuff.get<std::string>());

                if (!rule.buffs.empty())
                    mapping.rules.emplace_back(std::move(rule));
            }

            if (!mapping.dino_class.empty() && !mapping.rules.empty())
                new_mappings.emplace_back(std::move(mapping));
        }

        mappings = std::move(new_mappings);
        WriteLog("[CONFIG] loaded " + std::to_string(mappings.size()) + " hybrid mappings");
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
            WriteLog(std::string("[ERROR] reload failed: ") + e.what());
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

        WriteLog("[LOAD] KBDHybridBridge loaded");
    }

    void Unload()
    {
        ArkApi::GetCommands().RemoveOnTimerCallback("KBDHybridBridge.Timer");
        ArkApi::GetCommands().RemoveConsoleCommand("KBDHybridBridge.Reload");
        ArkApi::GetCommands().RemoveConsoleCommand("KBDHybridBridge.Scan");

        WriteLog("[UNLOAD] KBDHybridBridge unloaded");
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
