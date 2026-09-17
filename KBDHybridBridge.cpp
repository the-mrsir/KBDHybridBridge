#include <API/ARK/Ark.h>
#include <json.hpp>

#include <fstream>
#include <string>
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
        bool warned = false;
    };

    struct Rule
    {
        std::string trigger;
        std::vector<BuffSpec> buffs;
    };

    struct Mapping
    {
        std::string dino_class_name;
        std::vector<Rule> rules;
    };

    static const std::string plugin_name = "KBDHybridBridge";

    static bool enabled = true;
    static bool debug = false;
    static int scan_every_seconds = 5;
    static int seconds_since_scan = 0;
    static std::vector<Mapping> mappings;

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

    /*
      v0.6 FIX:
      Older builds required the matched class object's metaclass name to equal
      exactly "Class". KBD BlueprintGeneratedClass objects do not satisfy that
      assumption on this server, which is why the log repeatedly said:
        Buff class not loaded/found: Buff_ValyrianReins_Argent_C

      We now:
        1) accept the exact class object name when found, OR
        2) use the ClassField of any live instance whose class has that name.
      Resolution is lazy and then cached; it is NOT done every scan.
    */
    UClass* FindLoadedClass(const std::string& short_name)
    {
        auto& objects = Globals::GUObjectArray()();

        // First pass: exact generated class object.
        for (int i = 0; i < objects.ObjObjects.NumElements; ++i)
        {
            auto* item = objects.ObjObjects.GetObjectPtr(i);
            if (!item || !item->Object)
                continue;

            UObject* obj = item->Object;
            if (ObjectName(obj) == short_name)
                return reinterpret_cast<UClass*>(obj);
        }

        // Second pass: live/default instance whose ClassField is the class wanted.
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

    bool IsValyrianReinsItem(UPrimalItem* item)
    {
        return item && ClassName(item).find("ValyrianReins") != std::string::npos;
    }

    /*
      The user's v0.4 log proved the Costume-slot Reins are exposed by ASE as:
        [REINS] found directly in EquippedItems:
        PrimalItemCostume_ValyrianReins_C

      So v0.6 intentionally uses the proven path first and avoids scanning
      all ordinary inventory items.
    */
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

        // Keep ItemSlots as a secondary compatibility path for unusual slot layouts.
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

    UClass* ResolveBuff(BuffSpec& spec)
    {
        if (spec.cls)
            return spec.cls;

        spec.cls = FindLoadedClass(spec.name);

        if (spec.cls)
        {
            WriteLog("[RESOLVE] " + spec.name, true);
            spec.warned = false;
            return spec.cls;
        }

        if (!spec.warned)
        {
            WriteLog("[WARN] Buff class not loaded/found: " + spec.name, true);
            spec.warned = true;
        }

        return nullptr;
    }

    APrimalBuff* GetBuff(APrimalDinoCharacter* dino, UClass* buff_class)
    {
        if (!dino || !buff_class)
            return nullptr;

        return dino->GetBuff(TSubclassOf<APrimalBuff>(buff_class));
    }

    bool ApplyBuff(APrimalDinoCharacter* dino, BuffSpec& buff, UPrimalItem* associated_item)
    {
        UClass* cls = ResolveBuff(buff);
        if (!cls)
            return false;

        if (GetBuff(dino, cls))
            return true;

        APrimalBuff* added = APrimalBuff::StaticAddBuff(
            TSubclassOf<APrimalBuff>(cls),
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

    void RemoveBuff(APrimalDinoCharacter* dino, BuffSpec& buff)
    {
        UClass* cls = buff.cls;
        if (!cls)
            return; // don't force a global lookup just to remove something never added

        APrimalBuff* active = GetBuff(dino, cls);
        if (!active)
            return;

        WriteLog("[REMOVE] " + ObjectName(dino) + " <- " + buff.name, true);
        active->Deactivate();
    }

    void ProcessRule(APrimalDinoCharacter* dino, Rule& rule)
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

        for (auto& buff : rule.buffs)
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

        const std::string actual_class = ObjectName(dino->ClassField());

        for (auto& mapping : mappings)
        {
            // v0.6: do NOT require the Sid UClass to exist when the plugin first loads.
            // Match against the actual live dino class instead.
            if (mapping.dino_class_name != actual_class)
                continue;

            for (auto& rule : mapping.rules)
                ProcessRule(dino, rule);

            return;
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

            for (const auto& jrule : jmap.value("Rules", json::array()))
            {
                Rule rule;
                rule.trigger = jrule.value("Trigger", "Always");

                for (const auto& jbuff : jrule.value("Buffs", json::array()))
                {
                    BuffSpec spec;
                    spec.name = jbuff.get<std::string>();
                    rule.buffs.emplace_back(std::move(spec));
                }

                if (!rule.buffs.empty())
                    mapping.rules.emplace_back(std::move(rule));
            }

            if (!mapping.rules.empty())
                new_mappings.emplace_back(std::move(mapping));
        }

        mappings = std::move(new_mappings);
        seconds_since_scan = 0;

        WriteLog("[CONFIG] loaded " + std::to_string(mappings.size()) + " hybrid mappings", true);
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

        ArkApi::GetCommands().AddOnTimerCallback("KBDHybridBridge.Timer", &Timer);
        ArkApi::GetCommands().AddConsoleCommand("KBDHybridBridge.Reload", &ReloadCommand);
        ArkApi::GetCommands().AddConsoleCommand("KBDHybridBridge.Scan", &ScanCommand);

        WriteLog("[LOAD] KBDHybridBridge v0.6 loaded", true);
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
