#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SKSEMenuFramework.h"

namespace
{
    struct VendorResult
    {
        RE::FormID vendorFactionID{ 0 };
        RE::FormID merchantRefID{ 0 };
        RE::FormID containerRefID{ 0 };

        std::string vendorName;
        std::string locationName;

        std::int32_t quantity{ 0 };
    };

    struct ItemStock
    {
        RE::FormID itemFormID{ 0 };
        std::string itemName;

        std::vector<VendorResult> vendors;
    };

    using FactionActorMap =
        std::unordered_map<
        RE::FormID,
        std::vector<RE::Actor*>>;

    using StockIndex =
        std::unordered_map<
        RE::FormID,
        ItemStock>;

    StockIndex g_stockIndex;

    // UI state is intentionally stored independently from ItemStock objects.
    // The full stock dive rebuilds g_stockIndex from scratch, so the selected
    // item is retained only by FormID.
    char g_searchBuffer[256]{};
    RE::FormID g_selectedItemFormID{ 0 };
    std::string g_submittedSearch;

    std::string NormaliseSearchText(std::string text)
    {
        const auto first =
            std::ranges::find_if(
                text,
                [](unsigned char ch)
                {
                    return !std::isspace(ch);
                });

        const auto last =
            std::ranges::find_if(
                text | std::views::reverse,
                [](unsigned char ch)
                {
                    return !std::isspace(ch);
                }).base();

        if (first >= last) {
            return {};
        }

        std::string result(first, last);

        std::ranges::transform(
            result,
            result.begin(),
            [](unsigned char ch)
            {
                return static_cast<char>(
                    std::tolower(ch));
            });

        return result;
    }


    // ================================================================
    // SKYRIM ROLE FACTIONS
    // ================================================================

    // Skyrim.esm local FormID:
    // CurrentFollowerFaction [FACT:0005C84E]
    constexpr RE::FormID kCurrentFollowerFactionID =
        0x0005C84E;

    // Skyrim.esm local FormID:
    // Khajiit Traders [FACT:0010CEE9]
    //
    // Previous runtime diagnostic proved that Ahkari, Ma'dran and
    // Ri'saad are all active members of this faction.
    constexpr RE::FormID kKhajiitTradersFactionID =
        0x0010CEE9;

    // ================================================================
    // ROLE FACTION LOOKUP
    // ================================================================

    RE::TESFaction* LookupSkyrimFaction(
        RE::FormID localFormID)
    {
        auto* dataHandler =
            RE::TESDataHandler::GetSingleton();

        if (!dataHandler) {
            return nullptr;
        }

        return dataHandler->LookupForm<RE::TESFaction>(
            localFormID,
            "Skyrim.esm");
    }

    // ================================================================
    // LOCATION
    // ================================================================

    std::string ResolveActorLocation(
        RE::Actor* actor,
        RE::TESFaction* khajiitTradersFaction)
    {
        if (!actor) {
            return {};
        }

        auto* parentCell =
            actor->GetParentCell();

        if (!parentCell) {
            // A recognised caravan trader with no usable current
            // parent cell is considered to be travelling.
            if (khajiitTradersFaction &&
                actor->IsInFaction(
                    khajiitTradersFaction)) {

                return "Travelling";
            }

            return {};
        }

        // Prefer the current BGSLocation display name.
        //
        // For caravan merchants at a recognised stop this gives
        // locations such as Riften or Solitude.
        if (auto* location = parentCell->GetLocation()) {
            const char* locationName =
                location->GetName();

            if (locationName &&
                locationName[0] != '\0') {

                return locationName;
            }
        }

        if (khajiitTradersFaction &&
            actor->IsInFaction(
                khajiitTradersFaction)) {

            return "Travelling";
        }

        const char* cellName =
            parentCell->GetName();

        if (cellName &&
            cellName[0] != '\0') {

            return cellName;
        }

        return {};
    }

    // ================================================================
    // GLOBAL ACTOR / FACTION INDEX
    // ================================================================

    FactionActorMap BuildFactionActorIndex()
    {
        FactionActorMap factionActors;

        auto [allForms, allFormsLock] =
            RE::TESForm::GetAllForms();

        if (!allForms) {
            SKSE::log::error(
                "[StockChecker] GetAllForms() returned nullptr");

            return factionActors;
        }

        std::size_t totalForms = 0;
        std::size_t actorCount = 0;
        std::size_t actorsWithBase = 0;
        std::size_t activeMemberships = 0;

        {
            const RE::BSReadLockGuard lock{
                allFormsLock.get()
            };

            for (const auto& [formID, form] : *allForms) {
                (void)formID;

                ++totalForms;

                if (!form) {
                    continue;
                }

                auto* actor =
                    form->As<RE::Actor>();

                if (!actor) {
                    continue;
                }

                ++actorCount;

                if (!actor->GetActorBase()) {
                    continue;
                }

                ++actorsWithBase;

                actor->VisitFactions(
                    [&](RE::TESFaction* faction,
                        std::int8_t rank) -> bool
                    {
                        if (!faction) {
                            return false;
                        }

                        if (rank <= -1) {
                            return false;
                        }

                        factionActors[
                            faction->GetFormID()]
                            .push_back(actor);

                        ++activeMemberships;

                        return false;
                    });
            }
        }

        SKSE::log::info(
            "[StockChecker] Global forms = {}",
            totalForms);

        SKSE::log::info(
            "[StockChecker] Global actors = {}",
            actorCount);

        SKSE::log::info(
            "[StockChecker] Actors with base = {}",
            actorsWithBase);

        SKSE::log::info(
            "[StockChecker] Active faction memberships = {}",
            activeMemberships);

        SKSE::log::info(
            "[StockChecker] Factions with active actors = {}",
            factionActors.size());

        return factionActors;
    }

    // ================================================================
    // FULL STOCK DIVE
    // ================================================================

    void RunFullStockDive()
    {
        SKSE::log::info(
            "============================================================");

        SKSE::log::info(
            "[StockChecker] FULL STOCK DIVE START");

        auto* dataHandler =
            RE::TESDataHandler::GetSingleton();

        if (!dataHandler) {
            SKSE::log::error(
                "[StockChecker] TESDataHandler unavailable");

            return;
        }

        g_stockIndex.clear();

        auto* currentFollowerFaction =
            LookupSkyrimFaction(
                kCurrentFollowerFactionID);

        auto* khajiitTradersFaction =
            LookupSkyrimFaction(
                kKhajiitTradersFactionID);

        if (currentFollowerFaction) {
            SKSE::log::info(
                "[StockChecker] CurrentFollowerFaction resolved [{:08X}]",
                currentFollowerFaction->GetFormID());
        }
        else {
            SKSE::log::error(
                "[StockChecker] CurrentFollowerFaction lookup FAILED");
        }

        if (khajiitTradersFaction) {
            SKSE::log::info(
                "[StockChecker] Khajiit Traders faction resolved [{:08X}]",
                khajiitTradersFaction->GetFormID());
        }
        else {
            SKSE::log::error(
                "[StockChecker] Khajiit Traders faction lookup FAILED");
        }

        SKSE::log::info(
            "[StockChecker] Building current actor/faction index...");

        auto factionActors =
            BuildFactionActorIndex();

        const auto& factions =
            dataHandler->GetFormArray<RE::TESFaction>();

        std::size_t vendorFactionsWithContainers = 0;
        std::size_t resolvedVendorServices = 0;
        std::size_t unresolvedVendorServices = 0;

        std::size_t deadMerchantActorsExcluded = 0;
        std::size_t followerMerchantActorsExcluded = 0;

        std::size_t inventoryEntriesVisited = 0;
        std::size_t positiveStockEntries = 0;

        std::unordered_set<RE::FormID>
            uniqueResolvedActors;

        for (auto* faction : factions) {
            if (!faction) {
                continue;
            }

            auto* merchantContainer =
                faction->vendorData.merchantContainer;

            if (!merchantContainer) {
                continue;
            }

            ++vendorFactionsWithContainers;

            const auto factionID =
                faction->GetFormID();

            const auto actorIt =
                factionActors.find(factionID);

            if (actorIt == factionActors.end() ||
                actorIt->second.empty()) {

                ++unresolvedVendorServices;
                continue;
            }

            ++resolvedVendorServices;

            const auto inventoryCounts =
                merchantContainer->GetInventoryCounts();

            if (inventoryCounts.empty()) {
                continue;
            }

            for (auto* actor : actorIt->second) {
                if (!actor) {
                    continue;
                }

                if (!actor->IsInFaction(faction)) {
                    continue;
                }

                if (actor->IsDead()) {
                    ++deadMerchantActorsExcluded;

                    const char* rawName =
                        actor->GetName();

                    SKSE::log::info(
                        "[StockChecker] EXCLUDED DEAD: {} [{:08X}]",
                        rawName &&
                        rawName[0] != '\0' ?
                        rawName :
                        "<unnamed>",
                        actor->GetFormID());

                    continue;
                }

                if (currentFollowerFaction &&
                    actor->IsInFaction(
                        currentFollowerFaction)) {

                    ++followerMerchantActorsExcluded;

                    const char* rawName =
                        actor->GetName();

                    SKSE::log::info(
                        "[StockChecker] EXCLUDED FOLLOWER: {} [{:08X}]",
                        rawName &&
                        rawName[0] != '\0' ?
                        rawName :
                        "<unnamed>",
                        actor->GetFormID());

                    continue;
                }

                const auto actorRefID =
                    actor->GetFormID();

                uniqueResolvedActors.insert(
                    actorRefID);

                const char* rawVendorName =
                    actor->GetName();

                std::string vendorName =
                    rawVendorName ?
                    rawVendorName :
                    "";

                if (vendorName.empty()) {
                    vendorName = "<unnamed>";
                }

                std::string locationName =
                    ResolveActorLocation(
                        actor,
                        khajiitTradersFaction);

                if (locationName.empty()) {
                    locationName = "<unknown>";
                }

                for (const auto& [item, quantity] :
                    inventoryCounts) {

                    ++inventoryEntriesVisited;

                    if (!item) {
                        continue;
                    }

                    if (quantity <= 0) {
                        continue;
                    }

                    ++positiveStockEntries;

                    const auto itemFormID =
                        item->GetFormID();

                    const char* rawItemName =
                        item->GetName();

                    if (!rawItemName ||
                        rawItemName[0] == '\0') {

                        continue;
                    }

                    auto& itemStock =
                        g_stockIndex[itemFormID];

                    if (itemStock.itemFormID == 0) {
                        itemStock.itemFormID =
                            itemFormID;

                        itemStock.itemName =
                            rawItemName;
                    }

                    VendorResult result;

                    result.vendorFactionID =
                        factionID;

                    result.merchantRefID =
                        actorRefID;

                    result.containerRefID =
                        merchantContainer->GetFormID();

                    result.vendorName =
                        vendorName;

                    result.locationName =
                        locationName;

                    result.quantity =
                        quantity;

                    itemStock.vendors.push_back(
                        std::move(result));
                }
            }
        }

        SKSE::log::info(
            "============================================================");

        SKSE::log::info(
            "[StockChecker] FULL STOCK DIVE SUMMARY");

        SKSE::log::info(
            "[StockChecker] Vendor factions with containers = {}",
            vendorFactionsWithContainers);

        SKSE::log::info(
            "[StockChecker] Resolved vendor services = {}",
            resolvedVendorServices);

        SKSE::log::info(
            "[StockChecker] Unresolved vendor services excluded = {}",
            unresolvedVendorServices);

        SKSE::log::info(
            "[StockChecker] Dead merchant actors excluded = {}",
            deadMerchantActorsExcluded);

        SKSE::log::info(
            "[StockChecker] Follower merchant actors excluded = {}",
            followerMerchantActorsExcluded);

        SKSE::log::info(
            "[StockChecker] Unique resolved actors = {}",
            uniqueResolvedActors.size());

        SKSE::log::info(
            "[StockChecker] Inventory entries visited = {}",
            inventoryEntriesVisited);

        SKSE::log::info(
            "[StockChecker] Positive stock entries encountered = {}",
            positiveStockEntries);

        SKSE::log::info(
            "[StockChecker] Searchable unique items = {}",
            g_stockIndex.size());

        SKSE::log::info(
            "============================================================");

        SKSE::log::info(
            "[StockChecker] FULL STOCK DIVE COMPLETE");

        SKSE::log::info(
            "============================================================");
    }


    // ================================================================
    // SMF UI
    // ================================================================

    bool ItemMatchesQuery(
        const ItemStock& itemStock,
        const std::string& query)
    {
        if (query.empty()) {
            return true;
        }

        std::string itemName =
            itemStock.itemName;

        std::ranges::transform(
            itemName,
            itemName.begin(),
            [](unsigned char ch)
            {
                return static_cast<char>(
                    std::tolower(ch));
            });

        return itemName.find(query) !=
            std::string::npos;
    }

    std::vector<const ItemStock*> BuildMatchingItems(
        const std::string& query)
    {
        std::vector<const ItemStock*> items;

        items.reserve(
            g_stockIndex.size());

        for (const auto& [formID, itemStock] :
            g_stockIndex) {

            (void)formID;

            if (ItemMatchesQuery(
                itemStock,
                query)) {

                items.push_back(
                    std::addressof(itemStock));
            }
        }

        std::ranges::sort(
            items,
            [](const ItemStock* left,
                const ItemStock* right)
            {
                if (!left || !right) {
                    return left != nullptr;
                }

                if (left->itemName !=
                    right->itemName) {

                    return left->itemName <
                        right->itemName;
                }

                return left->itemFormID <
                    right->itemFormID;
            });

        return items;
    }

    void RenderVendorTable(
        const ItemStock& itemStock,
        const char* tableID)
    {
        auto vendors =
            itemStock.vendors;

        std::ranges::sort(
            vendors,
            [](const VendorResult& left,
                const VendorResult& right)
            {
                if (left.vendorName !=
                    right.vendorName) {

                    return left.vendorName <
                        right.vendorName;
                }

                if (left.locationName !=
                    right.locationName) {

                    return left.locationName <
                        right.locationName;
                }

                if (left.quantity !=
                    right.quantity) {

                    return left.quantity >
                        right.quantity;
                }

                if (left.merchantRefID !=
                    right.merchantRefID) {

                    return left.merchantRefID <
                        right.merchantRefID;
                }

                return left.vendorFactionID <
                    right.vendorFactionID;
            });

        const ImGuiMCP::ImGuiTableFlags tableFlags =
            ImGuiMCP::ImGuiTableFlags_Resizable |
            ImGuiMCP::ImGuiTableFlags_RowBg |
            ImGuiMCP::ImGuiTableFlags_BordersOuter;

        if (ImGuiMCP::BeginTable(
            tableID,
            3,
            tableFlags)) {

            ImGuiMCP::TableSetupColumn(
                "Vendor");

            ImGuiMCP::TableSetupColumn(
                "Location");

            ImGuiMCP::TableSetupColumn(
                "Qty");

            ImGuiMCP::TableHeadersRow();

            for (const auto& vendor :
                vendors) {

                ImGuiMCP::TableNextRow();

                ImGuiMCP::TableSetColumnIndex(
                    0);

                ImGuiMCP::Text(
                    "%s",
                    vendor.vendorName.c_str());

                ImGuiMCP::TableSetColumnIndex(
                    1);

                ImGuiMCP::Text(
                    "%s",
                    vendor.locationName.c_str());

                ImGuiMCP::TableSetColumnIndex(
                    2);

                ImGuiMCP::Text(
                    "%d",
                    vendor.quantity);
            }

            ImGuiMCP::EndTable();
        }
    }

    void __stdcall RenderStockCheckerPage()
    {
        if (g_selectedItemFormID != 0 &&
            !g_stockIndex.contains(
                g_selectedItemFormID)) {

            g_selectedItemFormID = 0;
        }

        ImGuiMCP::Text(
            "Search");

        const bool searchSubmitted =
            ImGuiMCP::InputText(
                "##StockCheckerSearch",
                g_searchBuffer,
                sizeof(g_searchBuffer),
                ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);

        const std::string liveQuery =
            NormaliseSearchText(
                g_searchBuffer);

        if (searchSubmitted) {
            g_submittedSearch =
                liveQuery;

            g_selectedItemFormID = 0;
        }

        const auto filteredItems =
            BuildMatchingItems(
                liveQuery);

        ImGuiMCP::Text(
            "Item");

        if (filteredItems.empty()) {
            ImGuiMCP::Text(
                "No matching items.");
        }
        else {
            std::vector<const char*> itemNames;

            itemNames.reserve(
                filteredItems.size());

            int selectedIndex = -1;

            for (std::size_t i = 0;
                i < filteredItems.size();
                ++i) {

                const auto* item =
                    filteredItems[i];

                itemNames.push_back(
                    item->itemName.c_str());

                if (item->itemFormID ==
                    g_selectedItemFormID) {

                    selectedIndex =
                        static_cast<int>(i);
                }
            }

            int comboIndex =
                selectedIndex;

            if (ImGuiMCP::Combo(
                "##StockCheckerItem",
                &comboIndex,
                itemNames.data(),
                static_cast<int>(
                    itemNames.size()))) {

                if (comboIndex >= 0 &&
                    comboIndex <
                    static_cast<int>(
                        filteredItems.size())) {

                    g_selectedItemFormID =
                        filteredItems[
                            static_cast<std::size_t>(
                                comboIndex)]
                            ->itemFormID;

                    g_searchBuffer[0] = '\0';
                    g_submittedSearch.clear();
                }
            }
        }

        ImGuiMCP::Separator();

        if (g_selectedItemFormID != 0) {
            const auto selectedIt =
                g_stockIndex.find(
                    g_selectedItemFormID);

            if (selectedIt ==
                g_stockIndex.end()) {

                g_selectedItemFormID = 0;

                ImGuiMCP::Text(
                    "Select an item or submit a search.");

                return;
            }

            const auto& selectedItem =
                selectedIt->second;

            ImGuiMCP::Text(
                "%s",
                selectedItem.itemName.c_str());

            const std::string tableID =
                "##StockCheckerSingle_" +
                std::to_string(
                    selectedItem.itemFormID);

            RenderVendorTable(
                selectedItem,
                tableID.c_str());

            return;
        }

        if (!g_submittedSearch.empty()) {
            const auto resultItems =
                BuildMatchingItems(
                    g_submittedSearch);

            if (resultItems.empty()) {
                ImGuiMCP::Text(
                    "No stocked items match that search.");

                return;
            }

            for (const auto* itemStock :
                resultItems) {

                if (!itemStock) {
                    continue;
                }

                ImGuiMCP::Text(
                    "%s",
                    itemStock->itemName.c_str());

                const std::string tableID =
                    "##StockCheckerSearch_" +
                    std::to_string(
                        itemStock->itemFormID);

                RenderVendorTable(
                    *itemStock,
                    tableID.c_str());

                ImGuiMCP::Separator();
            }

            return;
        }

        ImGuiMCP::Text(
            "Select an item or type a search and press Enter.");
    }

    void RegisterStockCheckerUI()
    {
        static bool registered = false;

        if (registered) {
            return;
        }

        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::warn(
                "[StockChecker] Skyrim Menu Framework not detected; "
                "SMF page not registered");

            return;
        }

        SKSEMenuFramework::SetSection(
            "Stock Checker");

        SKSEMenuFramework::AddSectionItem(
            "Stock Checker - Search",
            RenderStockCheckerPage);

        registered = true;

        SKSE::log::info(
            "[StockChecker] SMF page registered");
    }

    // ================================================================
    // BARTER MENU
    // ================================================================

    class MenuEventSink :
        public RE::BSTEventSink<
        RE::MenuOpenCloseEvent>
    {
    public:
        static MenuEventSink* GetSingleton()
        {
            static MenuEventSink singleton;

            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<
            RE::MenuOpenCloseEvent>*) override
        {
            if (!event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (event->menuName !=
                RE::BarterMenu::MENU_NAME) {

                return RE::BSEventNotifyControl::kContinue;
            }

            if (event->opening) {
                SKSE::log::info(
                    "[StockChecker] Barter menu opened");

                return RE::BSEventNotifyControl::kContinue;
            }

            SKSE::log::info(
                "[StockChecker] Barter menu closed");

            SKSE::log::info(
                "[StockChecker] Running FULL refresh");

            RunFullStockDive();

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        MenuEventSink() = default;
    };

    // ================================================================
    // SKSE MESSAGES
    // ================================================================

    void OnSKSEMessage(
        SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            SKSE::log::info(
                "[StockChecker] kPostLoad");

            RegisterStockCheckerUI();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
        {
            SKSE::log::info(
                "[StockChecker] kDataLoaded");

            auto* ui =
                RE::UI::GetSingleton();

            if (!ui) {
                SKSE::log::error(
                    "[StockChecker] UI singleton unavailable");

                break;
            }

            ui->AddEventSink(
                MenuEventSink::GetSingleton());

            SKSE::log::info(
                "[StockChecker] Barter menu event sink registered");

            break;
        }

        case SKSE::MessagingInterface::kPostLoadGame:
            SKSE::log::info(
                "[StockChecker] kPostLoadGame");

            RunFullStockDive();
            break;

        case SKSE::MessagingInterface::kNewGame:
            SKSE::log::info(
                "[StockChecker] kNewGame");

            RunFullStockDive();
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    SKSE::log::info(
        "[StockChecker] Plugin loaded");

    auto* messaging =
        SKSE::GetMessagingInterface();

    if (!messaging) {
        SKSE::log::error(
            "[StockChecker] Messaging interface unavailable");

        return false;
    }

    messaging->RegisterListener(
        OnSKSEMessage);

    SKSE::log::info(
        "[StockChecker] Messaging listener registered");

    return true;
}