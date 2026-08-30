#include "bot_chat_knowledge.h"
#include "bot_chat_config.h"
#include "bot_chat_social.h"
#include "bot_chat_util.h"
#include "Log.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "CreatureData.h"
#include "ItemTemplate.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Map.h"
#include "MapMgr.h"
#include "SharedDefines.h"
#include "TravelMgr.h"
#include "Random.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    struct IndexedName
    {
        uint32 id = 0;
        std::string name;
        std::string nameLower;
    };

    struct SpawnLoc
    {
        uint32 mapId = 0;
        uint32 zoneId = 0;
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        std::string formatted;
    };

    struct ServiceNpc
    {
        uint32 entry = 0;
        uint32 zoneId = 0;
        uint32 flags = 0;
        std::string name;
        std::string loc;
    };

    std::mutex g_IndexMutex;
    bool g_IndexesReady = false;

    std::vector<IndexedName> g_Creatures;

    std::unordered_map<std::string, std::vector<uint32>> g_QuestWords;
    std::unordered_map<std::string, std::vector<uint32>> g_CreatureWords;
    std::unordered_map<std::string, std::vector<uint32>> g_ItemWords;
    std::unordered_map<std::string, std::vector<uint32>> g_ZoneWords;
    std::unordered_map<uint32, std::string> g_QuestLowerById;
    std::unordered_map<uint32, std::string> g_CreatureLowerById;
    std::unordered_map<uint32, std::string> g_ItemLowerById;
    std::unordered_map<uint32, std::string> g_ZoneLowerById;

    std::unordered_map<uint32, std::vector<SpawnLoc>> g_CreatureSpawns;
    std::unordered_map<uint32, std::vector<uint32>> g_QuestStarters; // quest -> creature/go entries (positive creature)
    std::unordered_map<uint32, std::vector<uint32>> g_QuestEnders;
    std::unordered_map<uint32, uint32> g_CreatureNpcFlags;
    std::vector<ServiceNpc> g_ServiceNpcs;

    struct TaxiStop
    {
        uint32 id = 0;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        bool alliance = false;
        bool horde = false;
        std::string name;
        std::string zoneName;
    };

    struct MapHop
    {
        uint32 fromMap = 0;
        uint32 toMap = 0;
        std::string fromPlace;
        std::string toPlace;
    };

    std::vector<TaxiStop> g_TaxiStops;
    std::vector<MapHop> g_MapHops;

    const std::unordered_set<std::string> kStopwords = {
        "the", "a", "an", "of", "to", "in", "for", "and", "or", "is", "are", "was", "were",
        "where", "how", "what", "who", "when", "why", "which", "do", "does", "did", "can",
        "i", "im", "i'm", "you", "u", "my", "me", "this", "that", "anyone", "someone",
        "know", "get", "go", "at", "on", "from", "with", "just", "like", "some", "any",
        "need", "help", "pls", "please", "thx", "thanks", "looking", "wanna", "want",
        "there", "here", "about", "have", "has", "got", "still", "also", "then", "than",
        "its", "it's", "your", "ur", "yea", "yeah", "nah", "hey", "hi", "yo",
        "quest", "quests", "coords", "coord", "loc", "location", "locations"
    };

    const std::unordered_set<std::string> kGenericTokens = {
        "boot", "boots", "gear", "item", "items", "sword", "bow", "staff", "axe",
        "helm", "helmet", "glove", "gloves", "chest", "legs", "leg", "shoulder",
        "shoulders", "cloak", "ring", "trinket", "neck", "belt", "bracer",
        "drop", "drops", "loot", "score", "epic", "rare", "blue", "green",
        "pack", "set", "piece", "stat", "stats", "ilvl", "dps", "heal", "tank",
        "raid", "heroic", "normal", "boss", "mob", "mobs", "npc", "guy", "dude",
        "thing", "stuff", "nice", "new", "last", "one", "ones", "those", "these",
        "good", "best", "better", "lucky", "congrats", "grats", "gz"
    };

    bool HasDistinctiveTokens(const std::vector<std::string>& tokens)
    {
        for (const auto& token : tokens)
        {
            if (!kGenericTokens.count(token))
                return true;
        }
        return false;
    }

    std::string ToLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string StripPunct(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (unsigned char c : text)
        {
            if (std::isalnum(c) || c == ' ' || c == '\'')
                out.push_back(static_cast<char>(c));
            else
                out.push_back(' ');
        }
        return out;
    }

    std::vector<std::string> Tokenize(const std::string& text, bool dropStopwords)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(StripPunct(ToLowerCopy(text)));
        std::string token;
        while (ss >> token)
        {
            if (token.size() < 2)
                continue;
            if (dropStopwords && kStopwords.count(token))
                continue;
            tokens.push_back(token);
        }
        return tokens;
    }

    bool ShouldSkipCreatureName(const std::string& name)
    {
        if (name.empty() || name[0] == '[' || name[0] == '(')
            return true;
        std::string lower = ToLowerCopy(name);
        static const char* junk[] = {
            "trigger", "waypoint", "credit", "bunny", "controller", "spawner",
            "invisible", "dummy", "target", "stalker", "[dnd]", "[ph]", "zzold"
        };
        for (const char* word : junk)
        {
            if (lower.find(word) != std::string::npos)
                return true;
        }
        return false;
    }

    void AddWordIndex(std::unordered_map<std::string, std::vector<uint32>>& index, const std::string& name, uint32 id)
    {
        auto tokens = Tokenize(name, true);
        std::unordered_set<std::string> seen;
        for (const auto& token : tokens)
        {
            if (!seen.insert(token).second)
                continue;
            index[token].push_back(id);
        }
    }

    std::string FormatZoneLoc(uint32 mapId, float x, float y, float z, uint32 knownZoneId = 0, bool allowMapLookup = false);

    void BuildIndexes()
    {
        g_Creatures.clear();
        g_QuestWords.clear();
        g_CreatureWords.clear();
        g_ItemWords.clear();
        g_ZoneWords.clear();
        g_QuestLowerById.clear();
        g_CreatureLowerById.clear();
        g_ItemLowerById.clear();
        g_ZoneLowerById.clear();
        g_CreatureSpawns.clear();
        g_QuestStarters.clear();
        g_QuestEnders.clear();
        g_CreatureNpcFlags.clear();
        g_ServiceNpcs.clear();
        g_TaxiStops.clear();
        g_MapHops.clear();

        for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
        {
            if (!quest || quest->GetTitle().empty())
                continue;
            g_QuestLowerById[questId] = ToLowerCopy(quest->GetTitle());
            AddWordIndex(g_QuestWords, quest->GetTitle(), questId);
        }

        if (CreatureTemplateContainer const* creatures = sObjectMgr->GetCreatureTemplates())
        {
            for (auto const& [entry, tmpl] : *creatures)
            {
                if (ShouldSkipCreatureName(tmpl.Name))
                    continue;
                IndexedName named{entry, tmpl.Name, ToLowerCopy(tmpl.Name)};
                g_Creatures.push_back(named);
                g_CreatureLowerById[entry] = named.nameLower;
                AddWordIndex(g_CreatureWords, tmpl.Name, entry);
                g_CreatureNpcFlags[entry] = tmpl.npcflag;
            }
        }

        if (ItemTemplateContainer const* items = sObjectMgr->GetItemTemplateStore())
        {
            for (auto const& [entry, tmpl] : *items)
            {
                if (tmpl.Name1.empty())
                    continue;
                std::string lower = ToLowerCopy(tmpl.Name1);
                if (lower.rfind("zzold", 0) == 0)
                    continue;
                g_ItemLowerById[entry] = lower;
                AddWordIndex(g_ItemWords, tmpl.Name1, entry);
            }
        }

        for (uint32 i = 0; i < sAreaTableStore.GetNumRows(); ++i)
        {
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(i);
            if (!area || !area->area_name[0] || !area->area_name[0][0])
                continue;
            if (area->zone != 0)
                continue; // zones only, not subzones
            g_ZoneLowerById[area->ID] = ToLowerCopy(area->area_name[0]);
            AddWordIndex(g_ZoneWords, area->area_name[0], area->ID);
        }

        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            auto& locs = g_CreatureSpawns[data.id];
            if (locs.size() >= 3)
                continue;
            SpawnLoc loc;
            loc.mapId = data.mapid;
            loc.x = data.posX;
            loc.y = data.posY;
            loc.z = data.posZ;
            loc.zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, data.mapid, data.posX, data.posY, data.posZ);
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(loc.zoneId))
            {
                if (area->zone)
                    loc.zoneId = area->zone;
            }
            loc.formatted = FormatZoneLoc(data.mapid, data.posX, data.posY, data.posZ, 0, true);
            locs.push_back(loc);
        }

        if (QuestRelations* starters = sObjectMgr->GetCreatureQuestRelationMap())
        {
            for (auto const& pair : *starters)
                g_QuestStarters[pair.second].push_back(pair.first);
        }
        if (QuestRelations* enders = sObjectMgr->GetCreatureQuestInvolvedRelationMap())
        {
            for (auto const& pair : *enders)
                g_QuestEnders[pair.second].push_back(pair.first);
        }

        const uint32 serviceMask =
            UNIT_NPC_FLAG_FLIGHTMASTER | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_TRAINER_CLASS |
            UNIT_NPC_FLAG_TRAINER_PROFESSION | UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_REPAIR |
            UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_AUCTIONEER |
            UNIT_NPC_FLAG_MAILBOX | UNIT_NPC_FLAG_STABLEMASTER;

        for (const auto& named : g_Creatures)
        {
            auto fIt = g_CreatureNpcFlags.find(named.id);
            if (fIt == g_CreatureNpcFlags.end() || (fIt->second & serviceMask) == 0)
                continue;
            auto sIt = g_CreatureSpawns.find(named.id);
            if (sIt == g_CreatureSpawns.end() || sIt->second.empty())
                continue;
            const SpawnLoc& spawn = sIt->second.front();
            ServiceNpc npc;
            npc.entry = named.id;
            npc.zoneId = spawn.zoneId;
            npc.flags = fIt->second;
            npc.name = named.name;
            npc.loc = spawn.formatted;
            g_ServiceNpcs.push_back(npc);
        }

        for (uint32 i = 1; i < sTaxiNodesStore.GetNumRows(); ++i)
        {
            TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(i);
            if (!node || !node->name[0] || !node->name[0][0])
                continue;
            if (std::fabs(node->x) < 0.01f && std::fabs(node->y) < 0.01f)
                continue;

            TaxiStop stop;
            stop.id = node->ID;
            stop.mapId = node->map_id;
            stop.x = node->x;
            stop.y = node->y;
            stop.z = node->z;
            stop.name = node->name[0];
            stop.alliance = node->MountCreatureID[0] != 0;
            stop.horde = node->MountCreatureID[1] != 0;
            if (!stop.alliance && !stop.horde)
            {
                stop.alliance = true;
                stop.horde = true;
            }
            stop.zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, stop.mapId, stop.x, stop.y, stop.z);
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(stop.zoneId))
            {
                if (area->zone)
                {
                    if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
                    {
                        stop.zoneId = parent->ID;
                        if (parent->area_name[0])
                            stop.zoneName = parent->area_name[0];
                    }
                }
                else if (area->area_name[0])
                    stop.zoneName = area->area_name[0];
            }
            g_TaxiStops.push_back(stop);
        }

        std::unordered_set<uint64_t> hopSeen;
        for (auto& [maps, transfers] : TravelMgr::instance().mapTransfersMap)
        {
            if (transfers.empty())
                continue;
            uint64_t key = (static_cast<uint64_t>(maps.first) << 32) | maps.second;
            if (!hopSeen.insert(key).second)
                continue;
            mapTransfer& transfer = transfers.front();
            WorldPosition* from = transfer.getPointFrom();
            WorldPosition* to = transfer.getPointTo();
            if (!from || !to)
                continue;
            MapHop hop;
            hop.fromMap = maps.first;
            hop.toMap = maps.second;
            hop.fromPlace = FormatZoneLoc(from->GetMapId(), from->GetPositionX(), from->GetPositionY(), from->GetPositionZ(), 0, true);
            hop.toPlace = FormatZoneLoc(to->GetMapId(), to->GetPositionX(), to->GetPositionY(), to->GetPositionZ(), 0, true);
            if (hop.fromPlace.empty())
                hop.fromPlace = SafeFormat("map {}", hop.fromMap);
            if (hop.toPlace.empty())
                hop.toPlace = SafeFormat("map {}", hop.toMap);
            g_MapHops.push_back(hop);
        }

        g_IndexesReady = true;
        LOG_INFO("server.loading",
            "[Bot Chat] Game knowledge indexed: {} quests, {} creatures, {} items, {} zones, {} taxis, {} map links",
            g_QuestLowerById.size(), g_Creatures.size(), g_ItemLowerById.size(), g_ZoneLowerById.size(),
            g_TaxiStops.size(), g_MapHops.size());
    }

    void EnsureIndexes()
    {
        std::lock_guard<std::mutex> lock(g_IndexMutex);
        if (!g_IndexesReady)
            BuildIndexes();
    }

    float ScoreNameMatch(const std::string& nameLower, const std::vector<std::string>& queryTokens, const std::string& queryLower)
    {
        if (nameLower.empty())
            return 0.f;

        if (nameLower == queryLower)
            return 10.f;
        if (queryLower.size() >= 4 && nameLower.find(queryLower) != std::string::npos)
            return 6.f;

        if (queryTokens.empty())
            return 0.f;

        int hits = 0;
        for (const auto& token : queryTokens)
        {
            if (nameLower.find(token) != std::string::npos)
                ++hits;
        }
        if (hits == 0)
            return 0.f;

        float coverage = static_cast<float>(hits) / static_cast<float>(queryTokens.size());
        float score = coverage * 3.f + static_cast<float>(hits) * 0.35f;
        if (hits == static_cast<int>(queryTokens.size()) && queryTokens.size() >= 2)
            score += 2.f;
        return score;
    }

    template <typename WordMap>
    std::vector<std::pair<float, uint32>> SearchIndex(
        const WordMap& words,
        const std::unordered_map<uint32, std::string>& namesById,
        const std::vector<std::string>& queryTokens,
        const std::string& queryLower,
        size_t maxResults)
    {
        std::unordered_map<uint32, float> scores;

        for (const auto& token : queryTokens)
        {
            auto it = words.find(token);
            if (it == words.end())
                continue;
            for (uint32 id : it->second)
                scores[id] += 1.f;
        }

        for (auto& [id, score] : scores)
        {
            auto nIt = namesById.find(id);
            if (nIt == namesById.end())
                continue;
            float extra = ScoreNameMatch(nIt->second, queryTokens, queryLower);
            if (extra > score)
                score = extra;
        }

        std::vector<std::pair<float, uint32>> ranked;
        ranked.reserve(scores.size());
        for (auto const& [id, score] : scores)
        {
            if (score >= 1.2f)
                ranked.push_back({score, id});
        }
        std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
            return a.first > b.first;
        });
        if (ranked.size() > maxResults)
            ranked.resize(maxResults);
        return ranked;
    }

    std::string NpcRole(uint32 npcflag)
    {
        if (npcflag & UNIT_NPC_FLAG_FLIGHTMASTER)
            return "flight master";
        if (npcflag & UNIT_NPC_FLAG_TRAINER)
            return "trainer";
        if (npcflag & UNIT_NPC_FLAG_AUCTIONEER)
            return "auctioneer";
        if (npcflag & UNIT_NPC_FLAG_BANKER)
            return "banker";
        if (npcflag & UNIT_NPC_FLAG_INNKEEPER)
            return "innkeeper";
        if (npcflag & UNIT_NPC_FLAG_VENDOR)
            return "vendor";
        if (npcflag & UNIT_NPC_FLAG_QUESTGIVER)
            return "quest giver";
        return "";
    }

    std::string FormatZoneLoc(uint32 mapId, float x, float y, float z, uint32 knownZoneId, bool allowMapLookup)
    {
        uint32 zoneId = knownZoneId;
        if (!zoneId && allowMapLookup)
            zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, mapId, x, y, z);
        if (!zoneId)
            return "";

        std::string zoneName = "Unknown";
        uint32 coordZone = zoneId;

        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
        {
            if (area->zone)
            {
                if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
                {
                    if (parent->area_name[0])
                        zoneName = parent->area_name[0];
                    coordZone = area->zone;
                }
            }
            else if (area->area_name[0])
            {
                zoneName = area->area_name[0];
            }
        }

        float zx = x;
        float zy = y;
        Map2ZoneCoordinates(zx, zy, coordZone);
        if (zx >= -5.f && zx <= 105.f && zy >= -5.f && zy <= 105.f)
            return SafeFormat("{} ({:.0f}, {:.0f})", zoneName, zx, zy);
        return zoneName;
    }

    std::string FormatSpawn(uint32 entry)
    {
        auto it = g_CreatureSpawns.find(entry);
        if (it == g_CreatureSpawns.end() || it->second.empty())
            return "";
        return it->second.front().formatted;
    }

    std::string CreatureName(uint32 entry)
    {
        if (CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry))
            return tmpl->Name;
        return "unknown NPC";
    }

    bool IsJunkQuestTitle(const std::string& title)
    {
        std::string lower = ToLowerCopy(title);
        return lower.find("<nyi>") != std::string::npos
            || lower.find("<txt>") != std::string::npos
            || lower.find("[ph]") != std::string::npos
            || lower.find("unused") != std::string::npos
            || lower.find("deprecated") != std::string::npos
            || lower.find("do not use") != std::string::npos;
    }

    std::string PlayerZoneName(Player* player)
    {
        if (!player)
            return "";
        uint32 zoneId = player->GetZoneId();
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
        if (!area)
            return "";
        if (area->zone)
        {
            if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
            {
                if (parent->area_name[0])
                    return parent->area_name[0];
            }
        }
        return area->area_name[0] ? area->area_name[0] : "";
    }

    bool LooksLikeTravelQuery(const std::string& lower)
    {
        static const char* hints[] = {
            "how do i get", "how do you get", "how to get", "get to ", "get too ",
            "way to ", "road to ", "path to ", "directions to", "how do i go",
            "where is", "where are", "wheres ", "where's "
        };
        for (const char* hint : hints)
        {
            if (lower.find(hint) != std::string::npos)
                return true;
        }
        return false;
    }

    bool LooksLikeQuestQuery(const std::string& lower)
    {
        return lower.find("quest") != std::string::npos
            || lower.find("turn in") != std::string::npos
            || lower.find("turnin") != std::string::npos
            || lower.find("wanted") != std::string::npos;
    }

    bool TaxiUsable(const TaxiStop& stop, TeamId team)
    {
        if (team == TEAM_ALLIANCE)
            return stop.alliance;
        if (team == TEAM_HORDE)
            return stop.horde;
        return true;
    }

    const TaxiStop* NearestTaxi(uint32 mapId, float x, float y, TeamId team)
    {
        const TaxiStop* best = nullptr;
        float bestDist = 1e18f;
        for (const TaxiStop& stop : g_TaxiStops)
        {
            if (stop.mapId != mapId || !TaxiUsable(stop, team))
                continue;
            float dx = stop.x - x;
            float dy = stop.y - y;
            float dist = dx * dx + dy * dy;
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &stop;
            }
        }
        return best;
    }

    const TaxiStop* TaxiInZone(uint32 zoneId, TeamId team)
    {
        for (const TaxiStop& stop : g_TaxiStops)
        {
            if (stop.zoneId == zoneId && TaxiUsable(stop, team))
                return &stop;
        }
        return nullptr;
    }

    std::string DescribeMapHop(const MapHop& hop)
    {
        // Continent / instance IDs from Map.dbc, not destination-specific routes.
        if (hop.fromMap == 369 || hop.toMap == 369)
            return SafeFormat("Take the Deeprun Tram between {} and {}.", hop.fromPlace, hop.toPlace);
        if ((hop.fromMap == 0 && hop.toMap == 1) || (hop.fromMap == 1 && hop.toMap == 0))
            return SafeFormat("Take a boat or zeppelin from {} to {}.", hop.fromPlace, hop.toPlace);
        if (hop.fromMap == 530 || hop.toMap == 530)
            return SafeFormat("Use the Outland portal or flight from {} to {}.", hop.fromPlace, hop.toPlace);
        if (hop.fromMap == 571 || hop.toMap == 571)
            return SafeFormat("Use the Northrend boat or portal from {} to {}.", hop.fromPlace, hop.toPlace);
        return SafeFormat("Take the connection from {} to {}.", hop.fromPlace, hop.toPlace);
    }

    std::vector<MapHop> FindMapPath(uint32 startMap, uint32 goalMap)
    {
        std::vector<MapHop> path;
        if (startMap == goalMap)
            return path;

        std::unordered_map<uint32, MapHop> cameFrom;
        std::unordered_set<uint32> seen;
        std::queue<uint32> q;
        q.push(startMap);
        seen.insert(startMap);

        while (!q.empty())
        {
            uint32 cur = q.front();
            q.pop();
            for (const MapHop& hop : g_MapHops)
            {
                if (hop.fromMap != cur || !seen.insert(hop.toMap).second)
                    continue;
                cameFrom[hop.toMap] = hop;
                if (hop.toMap == goalMap)
                {
                    uint32 at = goalMap;
                    while (at != startMap)
                    {
                        const MapHop& step = cameFrom[at];
                        path.push_back(step);
                        at = step.fromMap;
                    }
                    std::reverse(path.begin(), path.end());
                    return path;
                }
                q.push(hop.toMap);
            }
        }
        return path;
    }

    std::vector<std::string> ZonesBetween(const TaxiStop& from, const TaxiStop& to)
    {
        std::vector<std::string> zones;
        float vx = to.x - from.x;
        float vy = to.y - from.y;
        float len2 = vx * vx + vy * vy;
        if (len2 < 1.f)
            return zones;

        std::unordered_set<uint32> seen;
        seen.insert(from.zoneId);
        seen.insert(to.zoneId);
        for (const TaxiStop& stop : g_TaxiStops)
        {
            if (stop.mapId != from.mapId || seen.count(stop.zoneId) || stop.zoneName.empty())
                continue;
            float wx = stop.x - from.x;
            float wy = stop.y - from.y;
            float t = (wx * vx + wy * vy) / len2;
            if (t <= 0.15f || t >= 0.85f)
                continue;
            seen.insert(stop.zoneId);
            zones.push_back(stop.zoneName);
            if (zones.size() >= 3)
                break;
        }
        return zones;
    }

    std::string BuildTravelAdvice(Player* asker, uint32 destZoneId)
    {
        if (!asker || !destZoneId)
            return "";

        AreaTableEntry const* destArea = sAreaTableStore.LookupEntry(destZoneId);
        if (!destArea || !destArea->area_name[0])
            return "";

        uint32 fromZoneId = asker->GetZoneId();
        if (AreaTableEntry const* fromArea = sAreaTableStore.LookupEntry(fromZoneId))
        {
            if (fromArea->zone)
                fromZoneId = fromArea->zone;
        }
        if (destArea->zone)
            destZoneId = destArea->zone;

        std::string destName = destArea->area_name[0];
        if (destArea->zone)
        {
            if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(destArea->zone))
            {
                if (parent->area_name[0])
                    destName = parent->area_name[0];
            }
        }
        std::string fromName = PlayerZoneName(asker);
        if (fromName.empty())
            fromName = "an unknown zone";

        if (fromZoneId == destZoneId)
            return SafeFormat("The asker is already in {}.", destName);

        uint32 fromMap = asker->GetMapId();
        uint32 destMap = destArea->mapid;
        TeamId team = asker->GetTeamId();
        float x = asker->GetPositionX();
        float y = asker->GetPositionY();

        std::ostringstream oss;
        oss << "The asker is in " << fromName << ". Destination is " << destName << ".";

        if (fromMap != destMap)
        {
            auto hops = FindMapPath(fromMap, destMap);
            if (hops.empty())
            {
                oss << " Those places are on different maps. Use a boat, zeppelin, tram, or city portal. Do not invent a portal that is not a known connection.";
            }
            else
            {
                for (const MapHop& hop : hops)
                    oss << " " << DescribeMapHop(hop);
            }
        }

        if (fromMap == destMap)
        {
            const TaxiStop* fromTaxi = NearestTaxi(fromMap, x, y, team);
            const TaxiStop* destTaxi = TaxiInZone(destZoneId, team);

            if (fromTaxi && destTaxi && fromTaxi->id != destTaxi->id)
            {
                oss << " Fly from " << fromTaxi->name << " to " << destTaxi->name << " if they have the flight path.";
                auto through = ZonesBetween(*fromTaxi, *destTaxi);
                if (!through.empty())
                {
                    oss << " Over land, head through";
                    for (size_t i = 0; i < through.size(); ++i)
                    {
                        oss << (i == 0 ? " " : ", ") << through[i];
                    }
                    oss << ".";
                }
            }
            else if (destTaxi)
            {
                oss << " The nearest flight path at the destination is " << destTaxi->name << ".";
            }
            oss << " Do not invent a portal or mage portal.";
        }

        return oss.str();
    }

    std::string FormatQuestFact(uint32 questId, Player* asker)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            return "";
        if (IsJunkQuestTitle(quest->GetTitle()))
            return "";

        std::ostringstream oss;
        oss << "Quest \"" << quest->GetTitle() << "\"";
        if (quest->GetQuestLevel() > 0)
            oss << " (level " << quest->GetQuestLevel() << ")";

        int32 zoneOrSort = quest->GetZoneOrSort();
        if (zoneOrSort > 0)
        {
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(static_cast<uint32>(zoneOrSort)))
            {
                if (area->area_name[0])
                    oss << " in " << area->area_name[0];
            }
        }

        auto appendNpc = [&](const char* label, const std::vector<uint32>& entries) {
            if (entries.empty())
                return;
            uint32 entry = entries.front();
            std::string loc = FormatSpawn(entry);
            oss << ". " << label << " " << CreatureName(entry);
            if (!loc.empty())
                oss << " at " << loc;
        };

        uint32 questZone = (quest->GetZoneOrSort() > 0) ? static_cast<uint32>(quest->GetZoneOrSort()) : 0;

        bool usedTravel = false;
        auto qIt = TravelMgr::instance().quests.find(questId);
        if (qIt != TravelMgr::instance().quests.end() && qIt->second)
        {
            auto addDest = [&](const char* label, const std::vector<QuestTravelDestination*>& dests) {
                if (dests.empty())
                    return;
                QuestTravelDestination* dest = dests.front();
                if (!dest)
                    return;
                auto points = dest->getPoints(true);
                oss << ". " << label << " " << dest->getTitle();
                if (!points.empty() && points.front())
                {
                    WorldPosition* pos = points.front();
                    oss << " at " << FormatZoneLoc(pos->GetMapId(), pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ(), questZone);
                }
                usedTravel = true;
            };
            addDest("Starts from", qIt->second->questGivers);
            addDest("Turn in at", qIt->second->questTakers);
            if (!qIt->second->questObjectives.empty() && qIt->second->questObjectives.front())
            {
                QuestTravelDestination* obj = qIt->second->questObjectives.front();
                auto points = obj->getPoints(true);
                oss << ". Objective: " << obj->getTitle();
                if (!points.empty() && points.front())
                {
                    WorldPosition* pos = points.front();
                    oss << " around " << FormatZoneLoc(pos->GetMapId(), pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ(), questZone);
                }
            }
        }

        if (!usedTravel)
        {
            auto sIt = g_QuestStarters.find(questId);
            if (sIt != g_QuestStarters.end())
                appendNpc("Starts from", sIt->second);
            auto eIt = g_QuestEnders.find(questId);
            if (eIt != g_QuestEnders.end())
                appendNpc("Turn in at", eIt->second);
        }

        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredNpcOrGo[i] > 0 && quest->RequiredNpcOrGoCount[i] > 0)
            {
                uint32 entry = static_cast<uint32>(quest->RequiredNpcOrGo[i]);
                oss << ". Kill " << quest->RequiredNpcOrGoCount[i] << "x " << CreatureName(entry);
                std::string loc = FormatSpawn(entry);
                if (!loc.empty())
                    oss << " around " << loc;
                break;
            }
        }

        if (asker)
        {
            QuestStatus status = asker->GetQuestStatus(questId);
            if (status == QUEST_STATUS_INCOMPLETE)
                oss << ". You currently have this quest";
            else if (status == QUEST_STATUS_COMPLETE)
                oss << ". You have this quest ready to turn in";
            else if (status == QUEST_STATUS_REWARDED)
                oss << ". You already finished this quest";
        }

        return oss.str();
    }

    std::string FormatCreatureFact(uint32 entry)
    {
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
        if (!tmpl)
            return "";

        std::ostringstream oss;
        oss << tmpl->Name;
        if (tmpl->minlevel)
        {
            if (tmpl->minlevel == tmpl->maxlevel)
                oss << " (level " << static_cast<int>(tmpl->minlevel) << ")";
            else
                oss << " (level " << static_cast<int>(tmpl->minlevel) << "-" << static_cast<int>(tmpl->maxlevel) << ")";
        }
        std::string role = NpcRole(tmpl->npcflag);
        if (!role.empty())
            oss << ", " << role;
        std::string loc = FormatSpawn(entry);
        if (!loc.empty())
            oss << " at " << loc;
        else
            oss << " (no spawn location found)";
        return oss.str();
    }

    std::string FormatItemFact(uint32 entry)
    {
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(entry);
        if (!item)
            return "";

        std::ostringstream oss;
        oss << "Item \"" << item->Name1 << "\"";
        if (item->RequiredLevel)
            oss << ", required level " << item->RequiredLevel;
        if (item->Class == ITEM_CLASS_QUEST)
            oss << ", quest item";
        return oss.str();
    }

    std::string FormatZoneFact(uint32 zoneId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
        if (!area || !area->area_name[0])
            return "";

        std::ostringstream oss;
        oss << "Zone " << area->area_name[0];
        if (area->area_level)
            oss << ", around level " << area->area_level;
        if (MapEntry const* map = sMapStore.LookupEntry(area->mapid))
        {
            if (map->name[0])
                oss << " on " << map->name[0];
        }
        return oss.str();
    }

    bool LooksLikeTrainerQuery(const std::string& lower)
    {
        return lower.find("trainer") != std::string::npos
            || lower.find("train") != std::string::npos;
    }

    std::string FindTrainerFact(const std::string& lower, const std::vector<std::string>& tokens)
    {
        static const char* classes[] = {
            "warrior", "paladin", "hunter", "rogue", "priest", "death knight", "shaman",
            "mage", "warlock", "druid"
        };
        static const char* professions[] = {
            "alchemy", "alchemist", "blacksmith", "blacksmithing", "enchanting", "enchanter",
            "engineering", "engineer", "herbalism", "herbalist", "inscription", "scribe",
            "jewelcrafting", "jewelcrafter", "leatherworking", "leatherworker", "mining", "miner",
            "skinning", "skinner", "tailoring", "tailor", "cooking", "cook", "first aid",
            "fishing", "fisherman"
        };

        std::string needle;
        for (const char* name : classes)
        {
            if (lower.find(name) != std::string::npos)
            {
                needle = name;
                break;
            }
        }
        if (needle.empty())
        {
            for (const char* name : professions)
            {
                if (lower.find(name) != std::string::npos)
                {
                    needle = name;
                    break;
                }
            }
        }
        if (needle.empty())
        {
            for (const auto& token : tokens)
            {
                if (token != "trainer" && token != "train")
                {
                    needle = token;
                    break;
                }
            }
        }
        if (needle.empty())
            return "";

        for (const auto& named : g_Creatures)
        {
            uint32 flags = 0;
            auto fIt = g_CreatureNpcFlags.find(named.id);
            if (fIt != g_CreatureNpcFlags.end())
                flags = fIt->second;
            if (!(flags & UNIT_NPC_FLAG_TRAINER))
                continue;
            if (named.nameLower.find(needle) == std::string::npos)
                continue;
            return FormatCreatureFact(named.id);
        }
        return "";
    }

    bool HasWord(const std::string& hay, const std::string& needle)
    {
        if (needle.empty())
            return false;
        size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos)
        {
            bool before = (pos == 0) || !std::isalnum(static_cast<unsigned char>(hay[pos - 1]));
            bool after = (pos + needle.size() >= hay.size()) ||
                         !std::isalnum(static_cast<unsigned char>(hay[pos + needle.size()]));
            if (before && after)
                return true;
            ++pos;
        }
        return false;
    }

    std::string AfterPhrase(const std::string& hay, const char* phrase)
    {
        size_t pos = hay.find(phrase);
        if (pos == std::string::npos)
            return "";
        std::string rest = hay.substr(pos + std::strlen(phrase));
        size_t start = rest.find_first_not_of(" \t");
        if (start == std::string::npos)
            return "";
        return rest.substr(start);
    }

    std::string ExpandPlaceAlias(std::string entity)
    {
        entity = ToLowerCopy(StripPunct(entity));
        while (!entity.empty() && (entity.back() == '?' || entity.back() == ' '))
            entity.pop_back();

        static const std::pair<const char*, const char*> aliases[] = {
            {"sw", "stormwind"}, {"ifg", "ironforge"}, {"if", "ironforge"},
            {"org", "orgrimmar"}, {"og", "orgrimmar"},
            {"uc", "undercity"}, {"tb", "thunder bluff"},
            {"darn", "darnassus"}, {"exo", "exodar"},
            {"shatt", "shattrath"}, {"shat", "shattrath"},
            {"dala", "dalaran"}, {"dal", "dalaran"},
            {"sm city", "silvermoon"},
            {"goldshire", "elwynn"}, {"thelsamar", "loch modan"},
            {"kharanos", "dun morogh"}, {"brill", "tirisfal"},
            {"xroads", "barrens"}, {"crossroads", "barrens"}
        };
        for (const auto& alias : aliases)
        {
            if (entity == alias.first)
                return alias.second;
        }
        return entity;
    }

    bool MatchService(const std::string& lower, uint32& flag, std::string& label)
    {
        struct Svc { const char* words[6]; uint32 flag; const char* label; };
        static const Svc services[] = {
            { {"fp", "flight", "gryphon", "wyvern", "hippogryph", "flightmaster"}, UNIT_NPC_FLAG_FLIGHTMASTER, "flight master" },
            { {"trainer", "train", nullptr, nullptr, nullptr, nullptr}, UNIT_NPC_FLAG_TRAINER, "trainer" },
            { {"vendor", "repair", "sell", "shop", nullptr, nullptr}, UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_REPAIR, "vendor / repair" },
            { {"inn", "innkeeper", "hearth", "hs", "bind", nullptr}, UNIT_NPC_FLAG_INNKEEPER, "innkeeper" },
            { {"ah", "auction", "auctioneer", nullptr, nullptr, nullptr}, UNIT_NPC_FLAG_AUCTIONEER, "auctioneer" },
            { {"bank", "banker", nullptr, nullptr, nullptr, nullptr}, UNIT_NPC_FLAG_BANKER, "banker" },
            { {"mail", "mailbox", "post", nullptr, nullptr, nullptr}, UNIT_NPC_FLAG_MAILBOX, "mailbox" },
            { {"stable", "stablemaster", nullptr, nullptr, nullptr, nullptr}, UNIT_NPC_FLAG_STABLEMASTER, "stable master" }
        };
        for (const Svc& svc : services)
        {
            for (const char* word : svc.words)
            {
                if (!word)
                    break;
                if (HasWord(lower, word))
                {
                    flag = svc.flag;
                    label = svc.label;
                    return true;
                }
            }
        }
        return false;
    }

    uint32 ResolvePlayerZoneId(Player* player)
    {
        if (!player)
            return 0;
        uint32 zoneId = player->GetZoneId();
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
        {
            if (area->zone)
                return area->zone;
        }
        return zoneId;
    }

    uint32 ResolveNamedZoneId(const std::string& name)
    {
        if (name.empty())
            return 0;
        auto tokens = Tokenize(name, true);
        auto hits = SearchIndex(g_ZoneWords, g_ZoneLowerById, tokens, ToLowerCopy(name), 1);
        if (hits.empty() || hits.front().first < 2.0f)
            return 0;
        return hits.front().second;
    }

    std::string FindServiceFact(Player* asker, uint32 flag, const std::string& label, const std::string& zoneHint)
    {
        uint32 zoneId = ResolveNamedZoneId(zoneHint);
        if (!zoneId)
            zoneId = ResolvePlayerZoneId(asker);

        std::string zoneName;
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
        {
            if (area->area_name[0])
                zoneName = area->area_name[0];
        }

        std::vector<const ServiceNpc*> hits;
        for (const ServiceNpc& npc : g_ServiceNpcs)
        {
            if ((npc.flags & flag) == 0)
                continue;
            if (zoneId && npc.zoneId != zoneId)
                continue;
            if (!zoneHint.empty())
            {
                std::string needle = ExpandPlaceAlias(zoneHint);
                // If they asked "mage trainer", keep NPCs whose name matches leftover words
                auto tokens = Tokenize(zoneHint, true);
                bool ok = true;
                for (const auto& token : tokens)
                {
                    if (kGenericTokens.count(token))
                        continue;
                    if (g_ZoneLowerById.count(zoneId) && g_ZoneLowerById[zoneId].find(token) != std::string::npos)
                        continue;
                    if (npc.name.find(token) == std::string::npos &&
                        ToLowerCopy(npc.name).find(token) == std::string::npos)
                    {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    continue;
            }
            hits.push_back(&npc);
            if (hits.size() >= 3)
                break;
        }

        if (hits.empty())
        {
            return SafeFormat("No {} found in {}.", label, zoneName.empty() ? "this zone" : zoneName);
        }

        std::ostringstream oss;
        oss << label << " in " << (zoneName.empty() ? "this zone" : zoneName) << ":";
        for (const ServiceNpc* npc : hits)
            oss << " " << npc->name << (npc->loc.empty() ? "" : " at " + npc->loc) << ".";
        return oss.str();
    }
}

namespace
{
    bool LooksLikeGroupChat(const std::string& lower)
    {
        if (HasWord(lower, "lfg") || HasWord(lower, "lfm") || HasWord(lower, "lft") ||
            HasWord(lower, "lf1m") || HasWord(lower, "lf2m") || HasWord(lower, "lf3m"))
            return true;
        if (lower.find("need a tank") != std::string::npos || lower.find("need tank") != std::string::npos ||
            lower.find("need a heal") != std::string::npos || lower.find("need heal") != std::string::npos ||
            lower.find("need dps") != std::string::npos || lower.find("need a dps") != std::string::npos)
            return true;
        if (lower.find("anyone want") != std::string::npos || lower.find("anyone wanna") != std::string::npos ||
            lower.find("anyone for") != std::string::npos || lower.find("anyone up") != std::string::npos ||
            lower.find("join my group") != std::string::npos || lower.find("looking for") != std::string::npos ||
            lower.find("want to group") != std::string::npos || lower.find("wanna group") != std::string::npos ||
            lower.find("group up") != std::string::npos || lower.find("group with") != std::string::npos ||
            lower.find("invite me") != std::string::npos || HasWord(lower, "inv"))
            return true;
        if (lower.find("going in") != std::string::npos || lower.find("wanna run") != std::string::npos ||
            lower.find("need 1") != std::string::npos || lower.find("need one") != std::string::npos)
            return true;
        if (lower.find("anyone else") != std::string::npos || lower.find("anyone on this") != std::string::npos)
            return true;
        return false;
    }

    bool LooksLikeTradeChat(const std::string& lower)
    {
        return HasWord(lower, "wts") || HasWord(lower, "wtb") || HasWord(lower, "wtt") ||
               (HasWord(lower, "pst") && (lower.find("sell") != std::string::npos || lower.find("buy") != std::string::npos));
    }
}

ChatQuery ParseChatQuery(const std::string& message)
{
    ChatQuery query;
    if (message.empty())
        return query;

    std::string lower = ToLowerCopy(message);

    if (DetectSocialAct(message) != SocialAct::None)
    {
        query.intent = ChatIntent::Social;
        query.topic = ChatTopic::Social;
        return query;
    }

    if (IsShortFollowUp(message))
    {
        query.intent = ChatIntent::Question;
        query.topic = ChatTopic::FollowUp;
        return query;
    }

    if (LooksLikeTradeChat(lower))
    {
        query.intent = ChatIntent::Social;
        query.topic = ChatTopic::Trade;
        return query;
    }

    if (LooksLikeGroupChat(lower) && lower.find("where") == std::string::npos)
    {
        query.intent = ChatIntent::Question;
        query.topic = ChatTopic::LookingForGroup;
        return query;
    }

    uint32 serviceFlag = 0;
    std::string serviceLabel;
    bool service = MatchService(lower, serviceFlag, serviceLabel);

    static const char* travelPhrases[] = {
        "how do i get to ", "how do you get to ", "how to get to ", "how do i go to ",
        "get to ", "way to ", "road to ", "path to ", "directions to "
    };
    for (const char* phrase : travelPhrases)
    {
        std::string rest = AfterPhrase(lower, phrase);
        if (rest.empty())
            continue;
        query.entity = ExpandPlaceAlias(rest);
        query.intent = ChatIntent::HelpRequest;
        query.topic = ChatTopic::TravelTo;
        return query;
    }

    if (service)
    {
        bool asking = lower.find("where") != std::string::npos
            || lower.find("need") != std::string::npos
            || lower.find("looking for") != std::string::npos;
        std::string stripped = StripPunct(lower);
        while (!stripped.empty() && stripped.back() == ' ')
            stripped.pop_back();
        bool shortAsk = (stripped == "fp" || stripped == "ah" || stripped == "vendor" || stripped == "inn"
            || stripped == "bank" || stripped == "trainer" || stripped == "mail" || stripped == "stable")
            && lower.find('?') != std::string::npos;
        // Answers like "fp's at thorgrum" are not new questions.
        bool givingAnswer = lower.find(" at ") != std::string::npos
            || lower.find(" in ") != std::string::npos
            || lower.find(" is ") != std::string::npos;
        if ((asking || shortAsk) && !givingAnswer)
        {
            query.intent = ChatIntent::HelpRequest;
            query.topic = ChatTopic::FindService;
            query.serviceFlag = serviceFlag;
            query.serviceLabel = serviceLabel;
            std::string rest = AfterPhrase(lower, "in ");
            query.entity = ExpandPlaceAlias(rest);
            return query;
        }
    }

    if (lower.find("turn in") != std::string::npos || lower.find("turnin") != std::string::npos ||
        (lower.find("where") != std::string::npos && lower.find("quest") != std::string::npos))
    {
        query.intent = ChatIntent::HelpRequest;
        query.topic = ChatTopic::QuestHelp;
        query.entity = ExpandPlaceAlias(AfterPhrase(lower, "quest "));
        if (query.entity.empty())
            query.entity = ExpandPlaceAlias(AfterPhrase(lower, "turn in "));
        return query;
    }

    static const char* wherePhrases[] = { "where is ", "where are ", "where's ", "wheres ", "where do i find ", "anyone know where " };
    for (const char* phrase : wherePhrases)
    {
        std::string rest = AfterPhrase(lower, phrase);
        if (rest.empty())
            continue;
        rest = ExpandPlaceAlias(rest);
        if (MatchService(rest, serviceFlag, serviceLabel) || service)
        {
            query.intent = ChatIntent::HelpRequest;
            query.topic = ChatTopic::FindService;
            query.serviceFlag = serviceFlag ? serviceFlag : query.serviceFlag;
            query.serviceLabel = serviceLabel.empty() ? query.serviceLabel : serviceLabel;
            return query;
        }
        auto zoneHits = SearchIndex(g_ZoneWords, g_ZoneLowerById, Tokenize(rest, true), rest, 1);
        if (!zoneHits.empty() && zoneHits.front().first >= 2.0f)
        {
            query.intent = ChatIntent::HelpRequest;
            query.topic = ChatTopic::TravelTo;
            query.entity = rest;
            return query;
        }
        query.intent = ChatIntent::HelpRequest;
        query.topic = ChatTopic::FindEntity;
        query.entity = rest;
        return query;
    }

    if (lower.find("what level") != std::string::npos || lower.find("what lvl") != std::string::npos ||
        lower.find("which zone") != std::string::npos)
    {
        query.intent = ChatIntent::HelpRequest;
        query.topic = ChatTopic::TravelTo;
        query.entity = ExpandPlaceAlias(AfterPhrase(lower, "is "));
        return query;
    }

    if (!message.empty() && message.back() == '?')
    {
        query.intent = ChatIntent::Question;
        return query;
    }

    static const char* questionStarts[] = {
        "who ", "what ", "when ", "why ", "which ", "does ", "do ", "is ", "are ",
        "can ", "anyone ", "anybody "
    };
    for (const char* start : questionStarts)
    {
        if (lower.rfind(start, 0) == 0)
        {
            query.intent = ChatIntent::Question;
            return query;
        }
    }

    return query;
}

ChatIntent ClassifyChatIntent(const std::string& message)
{
    return ParseChatQuery(message).intent;
}

bool IsShortFollowUp(const std::string& message)
{
    std::string lower = ToLowerCopy(StripPunct(message));
    while (!lower.empty() && lower.back() == ' ')
        lower.pop_back();
    while (!lower.empty() && lower.front() == ' ')
        lower.erase(lower.begin());

    // "??" / "..." strip to empty. Still a follow-up, not a new topic.
    if (lower.empty())
        return true;

    static const char* followUps[] = {
        "what u mean", "what you mean", "what do you mean", "wdym",
        "huh", "what", "wut", "wait", "where", "which", "why",
        "and then", "then what", "how", "lol what", "nani", "come again",
        "and", "go on", "then", "so", "oh", "ah", "right"
    };
    for (const char* follow : followUps)
    {
        if (lower == follow)
            return true;
    }
    return lower.size() <= 12 && (lower.find("mean") != std::string::npos || lower.find("huh") != std::string::npos);
}

bool IsAck(const std::string& message)
{
    std::string lower = ToLowerCopy(StripPunct(message));
    while (!lower.empty() && lower.back() == ' ')
        lower.pop_back();
    while (!lower.empty() && lower.front() == ' ')
        lower.erase(lower.begin());

    static const char* acks[] = {
        "ok", "k", "kk", "k k", "okay", "okey",
        "yea", "yeah", "yep", "yes", "ya",
        "sure", "cool", "nice", "alright", "aight",
        "lol ok", "ok lol", "true", "fair",
        "idc", "nvm", "meh", "whatever", "np"
    };
    for (char const* ack : acks)
    {
        if (lower == ack)
            return true;
    }
    return false;
}

std::string DescribeBotPlace(Player* bot)
{
    if (!bot)
        return "";
    if (Map const* map = bot->GetMap())
    {
        if (map->IsDungeon())
        {
            std::string name = map->GetMapName();
            return name.empty() ? "a dungeon" : name;
        }
    }
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->area_name[0] && zone->area_name[0][0])
            return zone->area_name[0];
    }
    return "";
}

void InitializeGameKnowledge()
{
    EnsureIndexes();
}

std::string LookupGameKnowledge(Player* asker, const std::string& message)
{
    if (!g_EnableGameKnowledge || message.empty())
        return "";

    EnsureIndexes();

    ChatQuery parsed = ParseChatQuery(message);
    ChatIntent intent = parsed.intent;
    std::string lower = ToLowerCopy(StripPunct(message));
    auto tokens = Tokenize(parsed.entity.empty() ? message : parsed.entity, true);

    if (parsed.topic == ChatTopic::LookingForGroup)
    {
        return "This is looking-for-group chat. Reply like a player: say if you can join, ask which quest or dungeon, or say you are busy. Do not invent lockouts or raid times.";
    }
    if (parsed.topic == ChatTopic::Trade || parsed.topic == ChatTopic::Social)
        return "";
    if (parsed.topic == ChatTopic::FollowUp)
        return "";

    if (parsed.topic == ChatTopic::FindService)
    {
        return SafeFormat("KNOWN FACTS:\n- {}", FindServiceFact(asker, parsed.serviceFlag, parsed.serviceLabel, parsed.entity));
    }

    if (!HasDistinctiveTokens(tokens) && parsed.topic != ChatTopic::FindService)
        return "";

    bool travelQuery = (parsed.topic == ChatTopic::TravelTo) || LooksLikeTravelQuery(lower);
    bool questQuery = (parsed.topic == ChatTopic::QuestHelp) || LooksLikeQuestQuery(lower);

    std::vector<std::string> facts;
    uint32_t maxFacts = g_GameKnowledgeMaxFacts > 0 ? g_GameKnowledgeMaxFacts : 4;

    auto addZoneAndTravel = [&]() {
        auto zoneHits = SearchIndex(g_ZoneWords, g_ZoneLowerById, tokens, lower, 2);
        uint32 destZoneId = 0;
        for (auto const& [score, id] : zoneHits)
        {
            if (score < 2.0f)
                continue;
            std::string fact = FormatZoneFact(id);
            if (fact.empty())
                continue;
            if (!destZoneId)
                destZoneId = id;
            facts.push_back(fact);
            if (facts.size() >= maxFacts)
                break;
        }
        if (destZoneId && asker)
        {
            std::string advice = BuildTravelAdvice(asker, destZoneId);
            if (!advice.empty())
                facts.insert(facts.begin(), std::string("TRAVEL: ") + advice);
        }
    };

    if (travelQuery)
        addZoneAndTravel();

    if (travelQuery && facts.empty())
    {
        auto creatureHits = SearchIndex(g_CreatureWords, g_CreatureLowerById, tokens, lower, 2);
        for (auto const& [score, id] : creatureHits)
        {
            if (score < 2.0f)
                continue;
            std::string fact = FormatCreatureFact(id);
            if (fact.empty())
                continue;
            facts.push_back(std::string("TRAVEL: Go to this NPC / mob. ") + fact);
            break;
        }
    }

    if (facts.size() < maxFacts && LooksLikeTrainerQuery(lower))
    {
        std::string trainer = FindTrainerFact(lower, tokens);
        if (!trainer.empty())
            facts.push_back(trainer);
    }

    if (facts.size() < maxFacts && !travelQuery)
    {
        auto creatureHits = SearchIndex(g_CreatureWords, g_CreatureLowerById, tokens, lower, 3);
        for (auto const& [score, id] : creatureHits)
        {
            if (score < 2.0f)
                continue;
            std::string fact = FormatCreatureFact(id);
            if (!fact.empty())
                facts.push_back(fact);
            if (facts.size() >= maxFacts)
                break;
        }
    }

    if (facts.size() < maxFacts && (questQuery || !travelQuery))
    {
        auto questHits = SearchIndex(g_QuestWords, g_QuestLowerById, tokens, lower, 3);
        for (auto const& [score, id] : questHits)
        {
            if (score < 2.5f)
                continue;
            std::string fact = FormatQuestFact(id, asker);
            if (!fact.empty())
                facts.push_back(fact);
            if (facts.size() >= maxFacts)
                break;
        }
    }

    if (facts.size() < maxFacts && !travelQuery)
    {
        auto zoneHits = SearchIndex(g_ZoneWords, g_ZoneLowerById, tokens, lower, 2);
        for (auto const& [score, id] : zoneHits)
        {
            if (score < 2.0f)
                continue;
            std::string fact = FormatZoneFact(id);
            if (!fact.empty())
                facts.push_back(fact);
            if (facts.size() >= maxFacts)
                break;
        }
    }

    bool itemQuery = lower.find("item") != std::string::npos
        || lower.find("ilvl") != std::string::npos
        || lower.find("recipe") != std::string::npos
        || lower.find("where do i get") != std::string::npos
        || lower.find("how do i get") != std::string::npos;

    if (facts.size() < maxFacts && !travelQuery && itemQuery)
    {
        auto itemHits = SearchIndex(g_ItemWords, g_ItemLowerById, tokens, lower, 2);
        for (auto const& [score, id] : itemHits)
        {
            if (score < 2.5f)
                continue;
            std::string fact = FormatItemFact(id);
            if (!fact.empty())
                facts.push_back(fact);
            if (facts.size() >= maxFacts)
                break;
        }
    }

    if (facts.empty())
    {
        if (intent != ChatIntent::HelpRequest)
            return "";

        std::string none = g_GameKnowledgeNoneTemplate.empty()
            ? "NO RELIABLE FACTS. If they asked for a location, say you don't remember the exact spot. Do not invent one."
            : g_GameKnowledgeNoneTemplate;
        return none;
    }

    std::string header = g_GameKnowledgePromptTemplate.empty()
        ? "KNOWN FACTS (use only these, do not invent numbers or locations):\n{facts}\nIf a fact is missing, say you don't remember the exact spot."
        : g_GameKnowledgePromptTemplate;

    std::ostringstream body;
    for (size_t i = 0; i < facts.size(); ++i)
    {
        body << "- " << facts[i];
        if (i + 1 < facts.size())
            body << "\n";
    }

    if (header.find("{facts}") != std::string::npos)
        return SafeFormat(header, fmt::arg("facts", body.str()));
    return header + "\n" + body.str();
}

namespace
{
    std::string ShortNpcName(std::string const& name)
    {
        if (name.size() <= 22)
            return name;
        size_t space = name.find(' ');
        if (space == std::string::npos)
            return name.substr(0, 22);
        size_t space2 = name.find(' ', space + 1);
        if (space2 == std::string::npos)
            return name;
        return name.substr(0, space2);
    }

    std::string IdkLine()
    {
        char const* lines[] = { "idk", "dont remember", "not sure" };
        return lines[urand(0, 2)];
    }

    std::string ShortServiceLabel(std::string const& label)
    {
        std::string lower = ToLowerCopy(label);
        if (lower.find("flight") != std::string::npos)
            return "fp";
        if (lower.find("trainer") != std::string::npos)
            return "trainer";
        if (lower.find("inn") != std::string::npos)
            return "inn";
        if (lower.find("auction") != std::string::npos || lower == "ah")
            return "ah";
        if (lower.find("bank") != std::string::npos)
            return "bank";
        if (lower.find("mail") != std::string::npos)
            return "mailbox";
        if (lower.find("stable") != std::string::npos)
            return "stable";
        if (lower.find("vendor") != std::string::npos || lower.find("repair") != std::string::npos)
            return "vendor";
        return label.empty() ? "npc" : label;
    }
}

std::string PickKnowledgeReply(Player* asker, const std::string& message)
{
    if (message.empty())
        return "";

    EnsureIndexes();
    ChatQuery parsed = ParseChatQuery(message);
    if (parsed.intent != ChatIntent::HelpRequest)
        return "";

    if (parsed.topic == ChatTopic::FindService)
    {
        std::string fact = FindServiceFact(asker, parsed.serviceFlag, parsed.serviceLabel, parsed.entity);
        std::string label = ShortServiceLabel(parsed.serviceLabel);
        if (fact.find("No ") == 0)
            return IdkLine();

        uint32 zoneId = ResolveNamedZoneId(parsed.entity);
        if (!zoneId)
            zoneId = ResolvePlayerZoneId(asker);
        for (ServiceNpc const& npc : g_ServiceNpcs)
        {
            if ((npc.flags & parsed.serviceFlag) == 0)
                continue;
            if (zoneId && npc.zoneId != zoneId)
                continue;
            std::string zoneName;
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(npc.zoneId))
            {
                if (area->area_name[0])
                    zoneName = area->area_name[0];
            }
            std::string line = label + " is " + ShortNpcName(npc.name);
            if (!zoneName.empty() && line.size() < 40)
                line += " in " + zoneName;
            if (line.size() > 60)
                line.resize(60);
            return line;
        }
        return IdkLine();
    }

    if (parsed.topic == ChatTopic::TravelTo)
    {
        uint32 destZoneId = ResolveNamedZoneId(parsed.entity);
        if (!destZoneId)
            return IdkLine();

        AreaTableEntry const* destArea = sAreaTableStore.LookupEntry(destZoneId);
        std::string destName = (destArea && destArea->area_name[0]) ? destArea->area_name[0] : parsed.entity;

        uint32 fromZoneId = ResolvePlayerZoneId(asker);
        if (destArea && destArea->zone)
            destZoneId = destArea->zone;
        if (fromZoneId == destZoneId)
            return "ur in it";

        uint32 fromMap = asker ? asker->GetMapId() : 0;
        uint32 destMap = destArea ? destArea->mapid : fromMap;
        if (fromMap != destMap)
        {
            auto hops = FindMapPath(fromMap, destMap);
            if (hops.empty())
                return "boat or zep";
            MapHop const& hop = hops.front();
            if (hop.fromMap == 369 || hop.toMap == 369)
                return "deeprun tram";
            if (hop.fromMap == 530 || hop.toMap == 530)
                return "outland portal in capital";
            if (hop.fromMap == 571 || hop.toMap == 571)
                return "northrend boat";
            return "boat or zep";
        }

        TeamId team = asker ? asker->GetTeamId() : TEAM_ALLIANCE;
        const TaxiStop* destTaxi = TaxiInZone(destZoneId, team);
        if (destTaxi && !destTaxi->name.empty())
            return "fly to " + ShortNpcName(destTaxi->name);
        return "run to " + destName;
    }

    if (parsed.topic == ChatTopic::FindEntity)
    {
        auto tokens = Tokenize(parsed.entity.empty() ? message : parsed.entity, true);
        auto hits = SearchIndex(g_CreatureWords, g_CreatureLowerById, tokens,
            ToLowerCopy(parsed.entity.empty() ? message : parsed.entity), 1);
        if (hits.empty() || hits.front().first < 2.0f)
            return IdkLine();
        uint32 entry = hits.front().second;
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
        if (!tmpl)
            return IdkLine();
        std::string line = ShortNpcName(tmpl->Name);
        auto sit = g_CreatureSpawns.find(entry);
        if (sit != g_CreatureSpawns.end() && !sit->second.empty())
        {
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(sit->second.front().zoneId))
            {
                if (area->area_name[0])
                    line += " in " + std::string(area->area_name[0]);
            }
        }
        return line;
    }

    if (parsed.topic == ChatTopic::QuestHelp)
    {
        auto tokens = Tokenize(parsed.entity.empty() ? message : parsed.entity, true);
        auto hits = SearchIndex(g_QuestWords, g_QuestLowerById, tokens,
            ToLowerCopy(parsed.entity.empty() ? message : parsed.entity), 1);
        if (hits.empty() || hits.front().first < 2.5f)
            return IdkLine();
        Quest const* quest = sObjectMgr->GetQuestTemplate(hits.front().second);
        if (!quest)
            return IdkLine();
        int32 zoneOrSort = quest->GetZoneOrSort();
        if (zoneOrSort > 0)
        {
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(static_cast<uint32>(zoneOrSort)))
            {
                if (area->area_name[0])
                    return std::string("that quest is in ") + area->area_name[0];
            }
        }
        return IdkLine();
    }

    return IdkLine();
}
