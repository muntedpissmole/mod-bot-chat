#include "Log.h"
#include "Language.h"
#include "Player.h"
#include "Chat.h"
#include "Channel.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Config.h"
#include "Common.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "World.h"
#include "AiFactory.h"
#include "ChannelMgr.h"
#include <sstream>
#include <vector>
#include <fmt/core.h>

#include <thread>
#include <exception>
#include <algorithm>
#include <random>
#include <cctype>
#include <cstring>
#include <chrono>
#include <ctime>
#include "DatabaseEnv.h"
#include "bot_chat_handler.h"
// ollama api removed
// personality removed
#include "bot_chat_config.h"
#include "bot_chat_util.h"
// sentiment removed
// rag removed
#include "bot_chat_thread.h"
#include "bot_chat_knowledge.h"
#include "bot_chat_social.h"
#include "bot_chat_llm.h"
#include <iomanip>
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "SharedDefines.h"
#include "Group.h"
#include "Creature.h"
#include "GameObject.h"
#include "TravelMgr.h"
#include "TravelNode.h"
#include "ObjectMgr.h"
#include "QuestDef.h"

// For AzerothCore range checks
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Map.h"
#include "GridNotifiers.h"

// Forward declarations for internal helper functions.
static bool IsBotEligibleForChatChannelLocal(Player* bot, Player* player,
                                             ChatChannelSourceLocal source, Channel* channel = nullptr, Player* receiver = nullptr);
[[maybe_unused]] static std::string GenerateBotPrompt(Player* bot, std::string playerMessage, Player* player,
                                     const std::string& threadKey, const std::string& gameKnowledge,
                                     ChatChannelSourceLocal sourceLocal);

// Helper function to format class name for any player
static std::string FormatPlayerClass(uint8_t classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:      return "Warrior";
        case CLASS_PALADIN:      return "Paladin";
        case CLASS_HUNTER:       return "Hunter";
        case CLASS_ROGUE:        return "Rogue";
        case CLASS_PRIEST:       return "Priest";
        case CLASS_DEATH_KNIGHT: return "Death Knight";
        case CLASS_SHAMAN:       return "Shaman";
        case CLASS_MAGE:         return "Mage";
        case CLASS_WARLOCK:      return "Warlock";
        case CLASS_DRUID:        return "Druid";
        default:                 return "Unknown";
    }
}

// Helper function to format race name for any player
static std::string FormatPlayerRace(uint8_t raceId)
{
    switch (raceId)
    {
        case RACE_HUMAN:         return "Human";
        case RACE_ORC:           return "Orc";
        case RACE_DWARF:         return "Dwarf";
        case RACE_NIGHTELF:      return "Night Elf";
        case RACE_UNDEAD_PLAYER: return "Undead";
        case RACE_TAUREN:        return "Tauren";
        case RACE_GNOME:         return "Gnome";
        case RACE_TROLL:         return "Troll";
        case RACE_BLOODELF:      return "Blood Elf";
        case RACE_DRAENEI:       return "Draenei";
        default:                 return "Unknown";
    }
}

const char* ChatChannelSourceLocalStr[] =
{
    "Undefined",  // 0
    "Say",        // 1
    "Party",      // 2
    "Raid",       // 3
    "Guild",      // 4
    "Officer",    // 5
    "Yell",       // 6
    "Whisper",    // 7
    "Unknown8",   // 8
    "Unknown9",   // 9
    "Unknown10",  // 10
    "Unknown11",  // 11
    "Unknown12",  // 12
    "Unknown13",  // 13
    "Unknown14",  // 14
    "Unknown15",  // 15
    "Unknown16",  // 16
    "General"     // 17
};

std::string GetConversationEntryKey(uint64_t botGuid, uint64_t playerGuid, const std::string& playerMessage, const std::string& botReply)
{
    // Use a combination that guarantees uniqueness
    return SafeFormat("{}:{}:{}:{}", botGuid, playerGuid, playerMessage, botReply);
}

std::string rtrim(const std::string& s)
{
    const std::string whitespace = " \t\n\r,.!?;:";
    size_t end = s.find_last_not_of(whitespace);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

ChatChannelSourceLocal GetChannelSourceLocal(uint32_t type)
{
    switch (type)
    {
        case CHAT_MSG_SAY:
            return SRC_SAY_LOCAL;
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
            return SRC_PARTY_LOCAL;
        case CHAT_MSG_RAID:
        case CHAT_MSG_RAID_LEADER:
        case CHAT_MSG_RAID_WARNING:
            return SRC_RAID_LOCAL;
        case CHAT_MSG_GUILD:
            return SRC_GUILD_LOCAL;
        case CHAT_MSG_OFFICER:
            return SRC_OFFICER_LOCAL;
        case CHAT_MSG_YELL:
            return SRC_YELL_LOCAL;
        case CHAT_MSG_WHISPER:
        case CHAT_MSG_WHISPER_FOREIGN:
        case CHAT_MSG_WHISPER_INFORM:
            return SRC_WHISPER_LOCAL;
        case CHAT_MSG_CHANNEL:
            return SRC_GENERAL_LOCAL;
        default:
            return SRC_UNDEFINED_LOCAL;
    }
}

Channel* GetValidChannel(uint32_t teamId, const std::string& channelName, Player* player)
{
    ChannelMgr* cMgr = ChannelMgr::forTeam(static_cast<TeamId>(teamId));
    Channel* channel = cMgr->GetChannel(channelName, player);
    if (!channel)
    {
        if(g_DebugEnabled)
        {
            LOG_ERROR("server.loading", "[Bot Chat] Channel '{}' not found for team {}", channelName, teamId);
        }
    }
    return channel;
}

bool BotChatHandler::OnPlayerCanUseChat(Player* player, uint32_t type, uint32_t lang, std::string& msg)
{
    if (!g_Enable)
        return true;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, nullptr, nullptr);
    return true;
}

bool BotChatHandler::OnPlayerCanUseChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Group* /*group*/)
{
    if (!g_Enable)
        return true;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, nullptr, nullptr);
    return true;
}

bool BotChatHandler::OnPlayerCanUseChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Guild* /*guild*/)
{
    if (!g_Enable)
        return true;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, nullptr, nullptr);
    return true;
}

bool BotChatHandler::OnPlayerCanUseChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Channel* channel)
{
    if (!g_Enable)
        return true;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, channel, nullptr);
    return true;
}

bool BotChatHandler::OnPlayerCanUseChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Player* receiver)
{
    // Only process if our module is enabled
    if (!g_Enable)
        return true;

    if (type == CHAT_MSG_WHISPER)
    {
        // Check if this is a valid whisper to a bot
        if (!receiver || !player || player == receiver)
            return true;

        // Check if sender is a bot - if so, don't trigger Ollama responses for bot-to-bot whispers
        PlayerbotAI* senderAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        if (senderAI && senderAI->IsBotAI())
        {
            return true;
        }

        PlayerbotAI* receiverAI = PlayerbotsMgr::instance().GetPlayerbotAI(receiver);
        if (!receiverAI || !receiverAI->IsBotAI())
            return true;
    }

    if (g_DebugEnabled)
    {
        LOG_INFO("server.loading", "[Bot Chat] OnPlayerCanUseChat called: player={}, type={}, receiver={}",
            player->GetName(), type, receiver ? receiver->GetName() : "null");
    }

    // Process the chat immediately in OnPlayerCanUseChat to prevent double processing
    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, nullptr, receiver);

    // Return false to prevent the message from being processed again in OnPlayerChat
    return true;
}

void AppendBotConversation(uint64_t botGuid, uint64_t playerGuid, const std::string& playerMessage, const std::string& botReply)
{
    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);
    auto& playerHistory = g_BotConversationHistory[botGuid][playerGuid];
    playerHistory.push_back({ playerMessage, botReply });
    while (playerHistory.size() > g_MaxConversationHistory)
    {
        playerHistory.pop_front();
    }

}

void SaveBotConversationHistoryToDB()
{
    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);

    for (const auto& [botGuid, playerMap] : g_BotConversationHistory) {
        for (const auto& [playerGuid, history] : playerMap) {
            for (const auto& pair : history) {
                const std::string& playerMessage = pair.first;
                const std::string& botReply = pair.second;

                std::string escPlayerMsg = playerMessage;
                CharacterDatabase.EscapeString(escPlayerMsg);

                std::string escBotReply = botReply;
                CharacterDatabase.EscapeString(escBotReply);

                CharacterDatabase.Execute(SafeFormat(
                    "INSERT IGNORE INTO mod_bot_chat_history (bot_guid, player_guid, timestamp, player_message, bot_reply) "
                    "VALUES ({}, {}, NOW(), '{}', '{}')",
                    botGuid, playerGuid, escPlayerMsg, escBotReply));
            }
        }
    }

    // Cleanup: keep only the N most recent entries per bot/player pair
    std::string cleanupQuery = R"SQL(
        WITH ranked_history AS (
            SELECT
                bot_guid,
                player_guid,
                timestamp,
                ROW_NUMBER() OVER (
                    PARTITION BY bot_guid, player_guid
                    ORDER BY timestamp DESC
                ) as rn
            FROM mod_bot_chat_history
        )
        DELETE FROM mod_bot_chat_history
        WHERE (bot_guid, player_guid, timestamp) IN (
            SELECT bot_guid, player_guid, timestamp
            FROM ranked_history
            WHERE rn > {}
        );
    )SQL";
    CharacterDatabase.Execute(SafeFormat(cleanupQuery, g_MaxConversationHistory));
}

// Called when a bot sends a message (random chatter or other bot-initiated messages)
// This triggers other bots to potentially reply
void ProcessBotChatMessage(Player* bot, const std::string& msg, ChatChannelSourceLocal sourceLocal, Channel* channel)
{
    if (!bot || msg.empty())
        return;
        
    // If channel is nullptr but this is a channel-type message, use this bot's live-zone General.
    if (!channel && sourceLocal == SRC_GENERAL_LOCAL)
    {
        channel = FindZoneGeneral(bot);
        if (g_DebugEnabled)
        {
            if (channel)
                LOG_INFO("server.loading", "[Bot Chat] ProcessBotChatMessage: Found General '{}' for bot {}",
                         channel->GetName(), bot->GetName());
            else
                LOG_ERROR("server.loading", "[Bot Chat] ProcessBotChatMessage: Could not find zone General for bot {}",
                          bot->GetName());
        }
    }
    
    // Validate that bot is actually in the relevant chat group before triggering replies
    bool canSendMessage = false;
    switch (sourceLocal)
    {
        case SRC_SAY_LOCAL:
        case SRC_YELL_LOCAL:
            // Distance checks will be applied during eligibility filtering
            canSendMessage = true;
            break;
            
        case SRC_GENERAL_LOCAL:
            // Must have a channel object
            canSendMessage = (channel != nullptr);
            if (!canSendMessage && g_DebugEnabled)
                LOG_ERROR("server.loading", "[Bot Chat] Bot {} cannot send to General - no channel found", bot->GetName());
            break;
            
        case SRC_GUILD_LOCAL:
        case SRC_OFFICER_LOCAL:
            // Must be in a guild with at least one real player online
            if (bot->GetGuildId() != 0)
            {
                Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
                if (guild)
                {
                    // Check if any real (non-bot) players are online in this guild
                    bool hasRealPlayer = false;
                    for (auto const& pair : ObjectAccessor::GetPlayers())
                    {
                        Player* member = pair.second;
                        if (member && member->GetGuildId() == bot->GetGuildId())
                        {
                            if (!PlayerbotsMgr::instance().GetPlayerbotAI(member))
                            {
                                hasRealPlayer = true;
                                break;
                            }
                        }
                    }
                    canSendMessage = hasRealPlayer;
                    if (!canSendMessage && g_DebugEnabled)
                        LOG_INFO("server.loading", "[Bot Chat] Bot {} cannot send to Guild - no real players online in guild", bot->GetName());
                }
                else
                {
                    canSendMessage = false;
                    if (g_DebugEnabled)
                        LOG_ERROR("server.loading", "[Bot Chat] Bot {} cannot send to Guild - guild not found", bot->GetName());
                }
            }
            else
            {
                canSendMessage = false;
                if (g_DebugEnabled)
                    LOG_ERROR("server.loading", "[Bot Chat] Bot {} cannot send to Guild - not in a guild", bot->GetName());
            }
            break;
            
        case SRC_PARTY_LOCAL:
        case SRC_RAID_LOCAL:
            // Must be in a group with at least one real player
            if (bot->GetGroup())
            {
                Group* group = bot->GetGroup();
                bool hasRealPlayer = false;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (member && !PlayerbotsMgr::instance().GetPlayerbotAI(member))
                    {
                        hasRealPlayer = true;
                        break;
                    }
                }
                canSendMessage = hasRealPlayer;
                if (!canSendMessage && g_DebugEnabled)
                    LOG_INFO("server.loading", "[Bot Chat] Bot {} cannot send to Party - no real players in group", bot->GetName());
            }
            else
            {
                canSendMessage = false;
                if (g_DebugEnabled)
                    LOG_ERROR("server.loading", "[Bot Chat] Bot {} cannot send to Party - not in a group", bot->GetName());
            }
            break;
            
        case SRC_WHISPER_LOCAL:
            // Whispers are handled separately
            canSendMessage = true;
            break;
            
        default:
            canSendMessage = true;
            break;
    }
    
    if (!canSendMessage)
    {
        if (g_DebugEnabled)
            LOG_ERROR("server.loading", "[Bot Chat] Bot {} cannot send message to {} - validation failed", 
                    bot->GetName(), ChatChannelSourceLocalStr[sourceLocal]);
        return;
    }
        
    // Convert ChatChannelSourceLocal back to chat type for ProcessChat
    uint32_t type = 0;
    switch (sourceLocal)
    {
        case SRC_SAY_LOCAL: type = CHAT_MSG_SAY; break;
        case SRC_YELL_LOCAL: type = CHAT_MSG_YELL; break;
        case SRC_PARTY_LOCAL: type = CHAT_MSG_PARTY; break;
        case SRC_RAID_LOCAL: type = CHAT_MSG_RAID; break;
        case SRC_GUILD_LOCAL: type = CHAT_MSG_GUILD; break;
        case SRC_OFFICER_LOCAL: type = CHAT_MSG_OFFICER; break;
        case SRC_WHISPER_LOCAL: type = CHAT_MSG_WHISPER; break;
        case SRC_GENERAL_LOCAL: type = CHAT_MSG_CHANNEL; break;
        default: type = CHAT_MSG_SAY; break;
    }
    
    std::string mutableMsg = msg; // ProcessChat takes non-const reference
    uint32_t lang = bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH;
    
    // Call the main ProcessChat function with bot as sender
    BotChatHandler::ProcessChat(bot, type, lang, mutableMsg, sourceLocal, channel, nullptr);
}

std::string GetBotHistoryPrompt(uint64_t botGuid, uint64_t playerGuid, std::string playerMessage)
{
    if(!g_EnableChatHistory)
    {
        return "";
    }
    
    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);

    std::string result;
    const auto botIt = g_BotConversationHistory.find(botGuid);
    if (botIt == g_BotConversationHistory.end())
        return result;
    const auto playerIt = botIt->second.find(playerGuid);
    if (playerIt == botIt->second.end())
        return result;

    Player* player = ObjectAccessor::FindPlayer(ObjectGuid(playerGuid));
    std::string playerName = player ? player->GetName() : "The player";

    result += SafeFormat(g_ChatHistoryHeaderTemplate, fmt::arg("player_name", playerName));

    for (const auto& entry : playerIt->second) {
        result += SafeFormat(g_ChatHistoryLineTemplate,
            fmt::arg("player_name", playerName),
            fmt::arg("player_message", entry.first),
            fmt::arg("bot_reply", entry.second)
        );
    }

    result += SafeFormat(g_ChatHistoryFooterTemplate,
        fmt::arg("player_name", playerName),
        fmt::arg("player_message", playerMessage)
    );

    return result;
}

// --- Helper: Spells ---
std::string ChatHandler_GetBotSpellInfo(Player* bot)
{
    // Map to store highest rank of each spell: spell name -> (spellId, rank, costText)
    std::map<std::string, std::tuple<uint32, uint32, std::string>> uniqueSpells;
    
    for (const auto& spellPair : bot->GetSpellMap())
    {
        uint32 spellId = spellPair.first;
        const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->Attributes & SPELL_ATTR0_PASSIVE)
            continue;
        if (spellInfo->SpellFamilyName == SPELLFAMILY_GENERIC)
            continue;
        if (bot->HasSpellCooldown(spellId))
            continue;
        
        const char* name = spellInfo->SpellName[0];
        if (!name || !*name)
            continue;
        
        std::string costText;
        if (spellInfo->ManaCost || spellInfo->ManaCostPercentage)
        {
            switch (spellInfo->PowerType)
            {
                case POWER_MANA: costText = std::to_string(spellInfo->ManaCost) + " mana"; break;
                case POWER_RAGE: costText = std::to_string(spellInfo->ManaCost) + " rage"; break;
                case POWER_FOCUS: costText = std::to_string(spellInfo->ManaCost) + " focus"; break;
                case POWER_ENERGY: costText = std::to_string(spellInfo->ManaCost) + " energy"; break;
                case POWER_RUNIC_POWER: costText = std::to_string(spellInfo->ManaCost) + " runic power"; break;
                default: costText = std::to_string(spellInfo->ManaCost) + " unknown resource"; break;
            }
        }
        else
        {
            costText = "no cost";
        }
        
        // Get base spell name (without rank)
        std::string spellName = name;
        uint32 rank = spellInfo->GetRank();
        
        // Check if we already have this spell, and if so, only keep the highest rank
        auto it = uniqueSpells.find(spellName);
        if (it == uniqueSpells.end())
        {
            // First time seeing this spell
            uniqueSpells[spellName] = std::make_tuple(spellId, rank, costText);
        }
        else
        {
            // We've seen this spell before, check if this is a higher rank
            uint32 existingRank = std::get<1>(it->second);
            if (rank > existingRank)
            {
                // Replace with higher rank
                uniqueSpells[spellName] = std::make_tuple(spellId, rank, costText);
            }
        }
    }
    
    // Build the output string from unique spells
    std::ostringstream spellSummary;
    for (const auto& [spellName, spellData] : uniqueSpells)
    {
        uint32 rank = std::get<1>(spellData);
        const std::string& costText = std::get<2>(spellData);
        
        spellSummary << "**" << spellName << "**";
        if (rank > 0)
        {
            spellSummary << " (Rank " << rank << ")";
        }
        spellSummary << " - Costs " << costText << "\n";
    }
    return spellSummary.str();
}

// --- Helper: Group info ---
std::vector<std::string> ChatHandler_GetGroupStatus(Player* bot)
{
    std::vector<std::string> info;
    if (!bot || !bot->GetGroup()) return info;
    Group* group = bot->GetGroup();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->GetMap()) continue;
        if(bot == member) continue;
        float dist = bot->GetDistance(member);
        std::string beingAttacked = "";
        if (Unit* attacker = member->GetVictim())
        {
            beingAttacked = " [Under Attack by " + attacker->GetName() +
                            ", Level: " + std::to_string(attacker->GetLevel()) + ", HP: " + std::to_string(attacker->GetHealth()) +
                            "/" + std::to_string(attacker->GetMaxHealth()) + ")]";
        }
        std::string className = FormatPlayerClass(member->getClass());
        std::string raceName = FormatPlayerRace(member->getRace());
        info.push_back(
            member->GetName() +
            " (Level: " + std::to_string(member->GetLevel()) +
            ", Class: " + className +
            ", Race: " + raceName +
            ", HP: " + std::to_string(member->GetHealth()) + "/" + std::to_string(member->GetMaxHealth()) +
            ", Dist: " + std::to_string(dist) + ")" + beingAttacked
        );

    }
    return info;
}

// --- Helper: Visible players ---
std::vector<std::string> ChatHandler_GetVisiblePlayers(Player* bot, float radius = 40.0f)
{
    std::vector<std::string> players;
    if (!bot || !bot->GetMap()) return players;
    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* player = pair.second;
        if (!player || player == bot) continue;
        if (!player->IsInWorld() || player->IsGameMaster()) continue;
        if (player->GetMap() != bot->GetMap()) continue;
        if (!bot->IsWithinDistInMap(player, radius)) continue;
        if (!bot->IsWithinLOS(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ())) continue;
        float dist = bot->GetDistance(player);
        std::string faction = (player->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
        std::string className = FormatPlayerClass(player->getClass());
        std::string raceName = FormatPlayerRace(player->getRace());
        players.push_back(
            "Player: " + player->GetName() +
            " (Level: " + std::to_string(player->GetLevel()) +
            ", Class: " + className +
            ", Race: " + raceName +
            ", Faction: " + faction +
            ", Distance: " + std::to_string(dist) + ")"
        );

    }
    return players;
}

// --- Helper: Visible locations/objects (creatures and gameobjects) ---
std::vector<std::string> ChatHandler_GetVisibleLocations(Player* bot, float radius = 40.0f)
{
    std::vector<std::string> visible;
    if (!bot || !bot->GetMap()) return visible;
    Map* map = bot->GetMap();
    for (auto const& pair : map->GetCreatureBySpawnIdStore())
    {
        Creature* c = pair.second;
        if (!c) continue;
        if (c->GetGUID() == bot->GetGUID()) continue;
        if (!bot->IsWithinDistInMap(c, radius)) continue;
        if (!bot->IsWithinLOS(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ())) continue;
        if (c->IsPet() || c->IsTotem()) continue;
        std::string type;
        if (c->isDead()) type = "DEAD";
        else if (c->IsHostileTo(bot)) type = "ENEMY";
        else if (c->IsFriendlyTo(bot)) type = "FRIENDLY";
        else type = "NEUTRAL";
        float dist = bot->GetDistance(c);
        visible.push_back(
            type + ": " + c->GetName() +
            ", Level: " + std::to_string(c->GetLevel()) +
            ", HP: " + std::to_string(c->GetHealth()) + "/" + std::to_string(c->GetMaxHealth()) +
            ", Distance: " + std::to_string(dist) + ")"
        );
    }
    for (auto const& pair : map->GetGameObjectBySpawnIdStore())
    {
        GameObject* go = pair.second;
        if (!go) continue;
        if (!bot->IsWithinDistInMap(go, radius)) continue;
        if (!bot->IsWithinLOS(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ())) continue;
        float dist = bot->GetDistance(go);
        visible.push_back(
            go->GetName() +
            ", Type: " + std::to_string(go->GetGoType()) +
            ", Distance: " + std::to_string(dist) + ")"
        );
    }
    return visible;
}

// --- Helper: Combat summary ---
std::string ChatHandler_GetCombatSummary(Player* bot)
{
    std::ostringstream oss;
    bool inCombat = bot->IsInCombat();
    Unit* victim = bot->GetVictim();

    // Class-specific resource reporting
    auto classId = bot->getClass();

    auto printResource = [&](std::ostringstream& oss) {
        switch (classId)
        {
            case CLASS_WARRIOR:
                oss << ", Rage: " << bot->GetPower(POWER_RAGE) << "/" << bot->GetMaxPower(POWER_RAGE);
                break;
            case CLASS_ROGUE:
                oss << ", Energy: " << bot->GetPower(POWER_ENERGY) << "/" << bot->GetMaxPower(POWER_ENERGY);
                break;
            case CLASS_DEATH_KNIGHT:
                oss << ", Runic Power: " << bot->GetPower(POWER_RUNIC_POWER) << "/" << bot->GetMaxPower(POWER_RUNIC_POWER);
                break;
            case CLASS_HUNTER:
                oss << ", Focus: " << bot->GetPower(POWER_FOCUS) << "/" << bot->GetMaxPower(POWER_FOCUS);
                break;
            default: // Mana classes
                if (bot->GetMaxPower(POWER_MANA) > 0)
                    oss << ", Mana: " << bot->GetPower(POWER_MANA) << "/" << bot->GetMaxPower(POWER_MANA);
                break;
        }
    };

    if (inCombat)
    {
        oss << "IN COMBAT: ";
        if (victim)
        {
            oss << "Target: " << victim->GetName()
                << ", Level: " << victim->GetLevel()
                << ", HP: " << victim->GetHealth() << "/" << victim->GetMaxHealth();
        }
        else
        {
            oss << "No current target";
        }
        oss << ". ";
        printResource(oss);
    }
    else
    {
        oss << "NOT IN COMBAT. ";
        printResource(oss);
    }
    return oss.str();
}


[[maybe_unused]] static std::string GenerateBotGameStateSnapshot(Player* bot)
{
    // Prepare each section
    std::string combat = ChatHandler_GetCombatSummary(bot);

    std::string group;
    std::vector<std::string> groupInfo = ChatHandler_GetGroupStatus(bot);
    if (!groupInfo.empty()) {
        group += "Group members:\n";
        for (const auto& entry : groupInfo) group += " - " + entry + "\n";
    }

    std::string spells = ChatHandler_GetBotSpellInfo(bot);

    std::string quests;
    for (auto const& [questId, qsd] : bot->getQuestStatusMap())
    {
        // look up the template
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        // get the English title as a fallback
        std::string title = quest->GetTitle();

        // then, if we have a locale record, overwrite it
        if (auto const* locale = sObjectMgr->GetQuestLocale(questId))
        {
            int locIdx = bot->GetSession()->GetSessionDbLocaleIndex();
            if (locIdx >= 0)
                ObjectMgr::GetLocaleString(locale->Title, locIdx, title);
        }

        // Convert quest status to readable string
        std::string statusText;
        switch (qsd.Status)
        {
            case QUEST_STATUS_NONE:       statusText = "not started"; break;
            case QUEST_STATUS_COMPLETE:   statusText = "complete (ready to turn in)"; break;
            case QUEST_STATUS_INCOMPLETE: statusText = "in progress"; break;
            case QUEST_STATUS_FAILED:     statusText = "failed"; break;
            case QUEST_STATUS_REWARDED:   statusText = "completed and rewarded"; break;
            default:                      statusText = "unknown"; break;
        }

        quests += "Quest \"" + title + "\" is " + statusText + "\n";
    }

    std::string los;
    std::vector<std::string> losLocs = ChatHandler_GetVisibleLocations(bot);
    if (!losLocs.empty()) {
        for (const auto& entry : losLocs) los += " - " + entry + "\n";
    }

    std::string players;
    std::vector<std::string> nearbyPlayers = ChatHandler_GetVisiblePlayers(bot);
    if (!nearbyPlayers.empty()) {
        for (const auto& entry : nearbyPlayers) players += " - " + entry + "\n";
    }

    // Use template
    return SafeFormat(
        g_ChatBotSnapshotTemplate,
        fmt::arg("combat", combat),
        fmt::arg("group", group),
        fmt::arg("spells", spells),
        fmt::arg("quests", quests),
        fmt::arg("los", los),
        fmt::arg("players", players)
    );
}

void SendBotChannelLine(Player* bot, std::string const& channelName, std::string const& line)
{
    if (!bot || channelName.empty() || line.empty())
        return;
    if (!ChannelBelongsToBotZone(bot, channelName))
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] {} skipped '{}' (not live-zone General)",
                     bot->GetName(), channelName);
        return;
    }
    ChannelMgr* cMgr = ChannelMgr::forTeam(bot->GetTeamId());
    if (!cMgr)
        return;
    Channel* channel = cMgr->GetChannel(channelName, bot, false);
    if (!channel)
        return;
    if (!bot->IsInChannel(channel))
        channel->JoinChannel(bot, "");
    if (bot->IsInChannel(channel))
        channel->Say(bot->GetGUID(), line, LANG_UNIVERSAL);
}

static void DeliverSocialReplies(std::vector<Player*> const& bots, Player* sender, std::string const& reply,
                                 ChatChannelSourceLocal sourceLocal, Channel* channel, SocialAct act = SocialAct::None,
                                 uint32 extraTypeChars = 0)
{
    if (!sender)
        return;

    Player* peer = nullptr;
    for (Player* bot : bots)
    {
        if (bot)
        {
            peer = bot;
            break;
        }
    }
    std::string const threadKey = MakeThreadKey(sender, sourceLocal, channel, peer);
    uint64 senderGuid = sender->GetGUID().GetRawValue();
    uint32 channelId = channel ? channel->GetChannelId() : 0;
    std::string channelName = channel ? channel->GetName() : "";

    for (Player* bot : bots)
    {
        if (!bot)
            continue;
        std::string line = reply;
        if (act != SocialAct::None)
            line = PickSocialReply(act, threadKey);
        if (line.empty())
            continue;
        // PickSocialReply already reserved the line. A second LineRecentlySpoken
        // check here dropped every hi/ty/gz because PickFrom just noted it.
        if (act == SocialAct::None)
        {
            if (LineTooSimilarToRecent(threadKey, line) || LineRecentlySpoken(line))
                continue;
            NoteSpokenLine(line);
        }
        AppendChannelThread(threadKey, bot->GetName(), bot->GetGUID().GetRawValue(), true, line);
        uint64 botGuid = bot->GetGUID().GetRawValue();
        std::thread([botGuid, senderGuid, line, sourceLocal, channelId, channelName, extraTypeChars]() {
            try
            {
                BotChatTypingSleep(line.length() + extraTypeChars);

                Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                if (!botPtr)
                    return;
                PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(botPtr);
                if (!botAI)
                    return;

                if (g_DebugEnabled)
                    LOG_INFO("server.loading", "[Bot Chat] Bot {} social reply: {}", botPtr->GetName(), line);

                if (channelId != 0 && !channelName.empty())
                {
                    SendBotChannelLine(botPtr, channelName, line);
                    ChannelMgr* cMgr = ChannelMgr::forTeam(botPtr->GetTeamId());
                    Channel* targetChannel = cMgr ? cMgr->GetChannel(channelName, botPtr, false) : nullptr;
                    ProcessBotChatMessage(botPtr, line, SRC_GENERAL_LOCAL, targetChannel);
                    return;
                }

                switch (sourceLocal)
                {
                    case SRC_GUILD_LOCAL:
                    case SRC_OFFICER_LOCAL:
                        botAI->SayToGuild(line);
                        ProcessBotChatMessage(botPtr, line, sourceLocal, nullptr);
                        break;
                    case SRC_PARTY_LOCAL:
                        botAI->SayToParty(line);
                        ProcessBotChatMessage(botPtr, line, SRC_PARTY_LOCAL, nullptr);
                        break;
                    case SRC_RAID_LOCAL:
                        botAI->SayToRaid(line);
                        ProcessBotChatMessage(botPtr, line, SRC_RAID_LOCAL, nullptr);
                        break;
                    case SRC_WHISPER_LOCAL:
                    {
                        Player* originalSender = ObjectAccessor::FindPlayer(ObjectGuid(senderGuid));
                        if (originalSender)
                            botAI->Whisper(line, originalSender->GetName());
                        break;
                    }
                    case SRC_YELL_LOCAL:
                        botAI->Yell(line);
                        ProcessBotChatMessage(botPtr, line, SRC_YELL_LOCAL, nullptr);
                        break;
                    default:
                        botAI->Say(line);
                        ProcessBotChatMessage(botPtr, line, SRC_SAY_LOCAL, nullptr);
                        break;
                }
            }
            catch (std::exception const& e)
            {
                LOG_ERROR("server.loading", "[Bot Chat] Social reply exception: {}", e.what());
            }
        }).detach();
    }
}

void DeliverBotChatReply(std::vector<Player*> const& bots, Player* sender, std::string const& reply,
                         ChatChannelSourceLocal sourceLocal, Channel* channel)
{
    DeliverSocialReplies(bots, sender, reply, sourceLocal, channel);
}

static Player* PickChimeBot(std::vector<Player*> const& pool, Player* primary, Player* sender)
{
    std::vector<Player*> candidates;
    for (Player* bot : pool)
    {
        if (!bot || bot == primary || bot == sender)
            continue;
        candidates.push_back(bot);
    }
    if (candidates.empty())
        return nullptr;
    return candidates[urand(0, candidates.size() - 1)];
}

static void MaybeChimeIn(std::vector<Player*> const& pool, Player* primary, Player* sender,
                         ChatChannelSourceLocal sourceLocal, Channel* channel,
                         std::string const& threadKey, bool groupTopic, std::string const& lastBotLine)
{
    if (!primary || !sender)
        return;
    if (sourceLocal != SRC_GUILD_LOCAL && sourceLocal != SRC_GENERAL_LOCAL)
        return;
    uint32 const chance = (sourceLocal == SRC_GUILD_LOCAL) ? 55 : 35;
    if (urand(0, 99) >= chance)
        return;
    Player* chime = PickChimeBot(pool, primary, sender);
    if (!chime)
        return;
    std::string line = groupTopic ? PickGroupChime(chime, threadKey)
                                  : PickContinueForLast(lastBotLine, threadKey);
    if (line.empty())
        line = PickSocialReply(SocialAct::Reaction, threadKey);
    if (line.empty())
        return;
    DeliverSocialReplies({ chime }, sender, line, sourceLocal, channel, SocialAct::None, urand(8, 18));
}

void BotChatHandler::ProcessChat(Player* player, uint32_t /*type*/, uint32_t lang, std::string& msg, ChatChannelSourceLocal sourceLocal, Channel* channel, Player* receiver)
{
    if (player == nullptr) {
        LOG_ERROR("server.loading", "[Bot Chat] ProcessChat: player is null");
        return;
    }
    if (msg.empty()) {
        return;
    }
    if (lang == LANG_ADDON) return;
    std::string chanName = (channel != nullptr) ? channel->GetName() : "Unknown";
    uint32_t channelId = (channel != nullptr) ? channel->GetChannelId() : 0;
    std::string receiverName = (receiver != nullptr) ? receiver->GetName() : "None";
    if(g_DebugEnabled)
    {
        LOG_INFO("server.loading",
                "[Bot Chat] Player {} sent msg: '{}' | Source: {} | Channel Name: {} | Channel ID: {} | Receiver: {}",
                player->GetName(), msg, (int)sourceLocal, chanName, channelId, receiverName);
    }


    auto startsWithWord = [](std::string const& text, std::string const& word)
    {
        if (word.empty() || text.size() < word.size())
            return false;
        for (size_t i = 0; i < word.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(text[i])) !=
                std::tolower(static_cast<unsigned char>(word[i])))
                return false;
        }
        return text.size() == word.size() || !std::isalnum(static_cast<unsigned char>(text[word.size()]));
    };

    // Questie (and similar) party announces look like "{rt1} Questie: 5/5 ...".
    // Strip leading {rtN}/{star} icons so a blacklist of "questie" still matches.
    auto stripLeadingIcons = [](std::string text)
    {
        while (!text.empty())
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.erase(text.begin());
            if (text.empty() || text.front() != '{')
                break;
            size_t const close = text.find('}');
            if (close == std::string::npos || close > 12)
                break;
            text.erase(0, close + 1);
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.erase(text.begin());
        return text;
    };

    std::string trimmedMsg = rtrim(msg);
    std::string const blacklistProbe = stripLeadingIcons(trimmedMsg);
    for (std::string const& blacklist : g_BlacklistCommands)
    {
        if (startsWithWord(trimmedMsg, blacklist) || startsWithWord(blacklistProbe, blacklist))
        {
            if (g_DebugEnabled)
                LOG_INFO("server.loading",
                         "[Bot Chat] Message starts with '{}' (blacklisted). Skipping bot responses.",
                         blacklist);
            return;
        }
    }
    if (LooksLikeAddonAnnounce(msg))
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] Skipping addon announce from {}", player->GetName());
        return;
    }
    if (channel && channel->GetName().find("Trade") != std::string::npos)
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] Skipping Trade board from {}", player->GetName());
        return;
    }
    
    // Check if this channel type is disabled
    if (sourceLocal == SRC_GENERAL_LOCAL && g_DisableForCustomChannels)
    {
        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Custom channels are disabled, skipping");
        }
        return;
    }
    
    if ((sourceLocal == SRC_SAY_LOCAL || sourceLocal == SRC_YELL_LOCAL) && g_DisableForSayYell)
    {
        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Say/Yell channels are disabled, skipping");
        }
        return;
    }
    
    if ((sourceLocal == SRC_GUILD_LOCAL || sourceLocal == SRC_OFFICER_LOCAL) && g_DisableForGuild)
    {
        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Guild channels are disabled, skipping");
        }
        return;
    }
    
    if ((sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL) && g_DisableForParty)
    {
        if (g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Party/Raid channels are disabled, skipping");
        }
        return;
    }
             
    PlayerbotAI* senderAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    bool senderIsBot = (senderAI && senderAI->IsBotAI());

    std::string threadKey = MakeThreadKey(player, sourceLocal, channel, receiver);
    if (g_EnableChannelThreads)
        AppendChannelThread(threadKey, player->GetName(), player->GetGUID().GetRawValue(), senderIsBot, msg);

    ChatQuery parsedQuery = ParseChatQuery(msg);
    ChatIntent chatIntent = parsedQuery.intent;

    std::string gameKnowledgeText;
    if (g_EnableGameKnowledge)
    {
        std::string knowledgeQuery;
        if (!senderIsBot && (chatIntent == ChatIntent::HelpRequest || parsedQuery.topic == ChatTopic::LookingForGroup))
            knowledgeQuery = msg;
        else
            knowledgeQuery = GetLastPlayerHelpQuery(threadKey);
        if (!knowledgeQuery.empty())
            gameKnowledgeText = LookupGameKnowledge(player, knowledgeQuery);
    }
    
    std::vector<Player*> eligibleBots;
    
    // Handle different chat sources differently
    if (sourceLocal == SRC_WHISPER_LOCAL && receiver != nullptr)
    {
        // Check if whisper replies are disabled
        if (!g_EnableWhisperReplies)
        {
            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[Bot Chat] Whisper replies are disabled, skipping");
            }
            return;
        }
        
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Processing whisper from {} to {}", 
                    player->GetName(), receiver->GetName());
        }
        
        // Skip bot-to-bot whispers to prevent Ollama responses
        if (senderIsBot)
        {
            return;
        }
        
        // For whispers, only the receiver bot can respond (if it's a bot)
        PlayerbotAI* receiverAI = PlayerbotsMgr::instance().GetPlayerbotAI(receiver);
        if (receiverAI && receiverAI->IsBotAI())
        {
            eligibleBots.push_back(receiver);
            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[Bot Chat] Found eligible bot {} for whisper", receiver->GetName());
            }
        }
        else if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Whisper target {} is not a bot or has no AI", receiver->GetName());
        }
    }
    else if (channel != nullptr)
    {
        // For channel chat, find all bots that are in the same channel instance
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Processing channel message in '{}' (ID: {})", 
                    channel->GetName(), channel->GetChannelId());
        }
        
        // Verify the original channel is valid before proceeding
        if (!channel)
        {
            if(g_DebugEnabled)
            {
                LOG_ERROR("server.loading", "[Bot Chat] Channel is null, cannot process channel message");
            }
            return;
        }
        
        // For channel chat, simply find all bots in the same zone as the player
        auto const& allPlayers = ObjectAccessor::GetPlayers();
        for (auto const& itr : allPlayers)
        {
            Player* candidate = itr.second;
            if (!candidate || candidate == player)
                continue;
                
            // Skip non-bots early
            PlayerbotAI* candidateAI = PlayerbotsMgr::instance().GetPlayerbotAI(candidate);
            if (!candidateAI || !candidateAI->IsBotAI())
                continue;
            
            std::string const chName = channel->GetName();
            bool const isGeneral = chName.rfind("General", 0) == 0 || chName.find("General -") != std::string::npos;
            bool const isTrade = chName.find("Trade") != std::string::npos;
            bool const isLocalDef = chName.find("LocalDefense") != std::string::npos;
            bool const isLocalChannel = isGeneral || isTrade || isLocalDef;
            bool const isGlobalChannel = chName.find("World") != std::string::npos ||
                                         chName.find("LookingForGroup") != std::string::npos;

            if (isTrade)
                continue;

            if (isLocalChannel)
            {
                if (candidate->GetMapId() != player->GetMapId())
                    continue;
                uint32 botZone = candidate->GetMap()
                    ? candidate->GetMap()->GetZoneId(candidate->GetPhaseMask(),
                          candidate->GetPositionX(), candidate->GetPositionY(), candidate->GetPositionZ())
                    : candidate->GetZoneId();
                uint32 playerZone = player->GetMap()
                    ? player->GetMap()->GetZoneId(player->GetPhaseMask(),
                          player->GetPositionX(), player->GetPositionY(), player->GetPositionZ())
                    : player->GetZoneId();
                if (botZone != playerZone)
                    continue;
            }

            if (candidate->GetTeamId() != player->GetTeamId() && !isGlobalChannel)
                continue;

            // General is zone chat. Bots often never JoinChannel, so membership
            // is not required — they join on send. Custom channels still must match.
            if (!isGeneral && !isLocalDef && !candidate->IsInChannel(channel))
                continue;
            
            // REAL PLAYER CHECK: Channel must have at least one real player
            bool hasRealPlayerInChannel = false;
            for (auto const& playerItr : allPlayers)
            {
                Player* potentialRealPlayer = playerItr.second;
                if (potentialRealPlayer && potentialRealPlayer->IsInChannel(channel))
                {
                    PlayerbotAI* realPlayerAI = PlayerbotsMgr::instance().GetPlayerbotAI(potentialRealPlayer);
                    if (!realPlayerAI || !realPlayerAI->IsBotAI())
                    {
                        hasRealPlayerInChannel = true;
                        break;
                    }
                }
            }
            
            if (!hasRealPlayerInChannel)
            {
                if(g_DebugEnabled)
                {
                    //LOG_INFO("server.loading", "[Bot Chat] Bot {} skipped - no real players in channel '{}'", candidate->GetName(), channel->GetName());
                }
                continue;
            }
            
            // ONLY add bots that passed ALL verifications
            eligibleBots.push_back(candidate);
            if(g_DebugEnabled)
            {
                // LOG_INFO("server.loading", "[Bot Chat] VERIFIED eligible bot {} in channel '{}' - Distance: {:.2f}, Zone match: {}", candidate->GetName(), channel->GetName(), candidate->GetDistance(player), (candidate->GetZoneId() == player->GetZoneId()));
            }
        }
        
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Found {} bots in channel instance '{}'", 
                    eligibleBots.size(), channel->GetName());
        }
    }
    else
    {
        // For other chat types (say, yell, guild, party, etc.), use all players and filter by eligibility
        auto const& allPlayers = ObjectAccessor::GetPlayers();
        for (auto const& itr : allPlayers)
        {
            Player* candidate = itr.second;
            if (candidate->IsInWorld() && candidate != player)
            {
                PlayerbotAI* candidateAI = PlayerbotsMgr::instance().GetPlayerbotAI(candidate);
                if (candidateAI && candidateAI->IsBotAI())
                {
                    // For Guild/Party, verify there's a real player in that guild/party
                    if (sourceLocal == SRC_GUILD_LOCAL || sourceLocal == SRC_OFFICER_LOCAL)
                    {
                        if (candidate->GetGuildId() != 0)
                        {
                            // Check if any real player is online in this guild
                            bool hasRealPlayerInGuild = false;
                            for (auto const& guildPlayerItr : allPlayers)
                            {
                                Player* guildMember = guildPlayerItr.second;
                                if (guildMember && guildMember->GetGuildId() == candidate->GetGuildId())
                                {
                                    PlayerbotAI* memberAI = PlayerbotsMgr::instance().GetPlayerbotAI(guildMember);
                                    if (!memberAI || !memberAI->IsBotAI())
                                    {
                                        hasRealPlayerInGuild = true;
                                        break;
                                    }
                                }
                            }
                            if (!hasRealPlayerInGuild)
                                continue; // Skip bot - no real players in guild
                        }
                    }
                    else if (sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL)
                    {
                        Group* group = candidate->GetGroup();
                        if (group)
                        {
                            // Check if any real player is in this group
                            bool hasRealPlayerInGroup = false;
                            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                            {
                                Player* member = ref->GetSource();
                                if (member)
                                {
                                    PlayerbotAI* memberAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);
                                    if (!memberAI || !memberAI->IsBotAI())
                                    {
                                        hasRealPlayerInGroup = true;
                                        break;
                                    }
                                }
                            }
                            if (!hasRealPlayerInGroup)
                                continue; // Skip bot - no real players in group
                        }
                    }
                    else if (sourceLocal == SRC_SAY_LOCAL || sourceLocal == SRC_YELL_LOCAL)
                    {
                        // For Say/Yell, require a real player within hearing distance
                        float threshold = (sourceLocal == SRC_SAY_LOCAL) ? g_SayDistance : g_YellDistance;
                        bool hasRealPlayerNearby = false;
                        
                        if (candidate->IsInWorld() && threshold > 0.0f)
                        {
                            for (auto const& nearbyPlayerItr : allPlayers)
                            {
                                Player* nearbyPlayer = nearbyPlayerItr.second;
                                if (nearbyPlayer && nearbyPlayer->IsInWorld())
                                {
                                    PlayerbotAI* nearbyAI = PlayerbotsMgr::instance().GetPlayerbotAI(nearbyPlayer);
                                    if (!nearbyAI || !nearbyAI->IsBotAI())
                                    {
                                        if (candidate->GetDistance(nearbyPlayer) <= threshold)
                                        {
                                            hasRealPlayerNearby = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        
                        if (!hasRealPlayerNearby)
                            continue; // Skip bot - no real player can hear Say/Yell
                    }
                    
                    eligibleBots.push_back(candidate);
                }
            }
        }
    }
    
    std::vector<Player*> candidateBots;
    int notEligibleCount = 0;
    for (Player* bot : eligibleBots)
    {
        if (!bot)
        {
            continue;
        }
        
        // For channel messages, bots in eligibleBots have already passed STRICT channel checks
        // Only run additional eligibility checks for non-channel sources
        // EXCEPTION: If channel is nullptr but sourceLocal is a channel type (like GENERAL), 
        // treat it as a channel message (happens with bot-initiated messages)
        bool isChannelSource = (sourceLocal == SRC_GENERAL_LOCAL);
        
        if (channel != nullptr || isChannelSource)
        {
            // Channel bots have already been verified to be in EXACT same channel instance
            // OR this is a channel-type source (General) even without channel object
            candidateBots.push_back(bot);
        }
        else
        {
            // For non-channel sources (Say/Yell/Guild/Party/Whisper), run the full eligibility check
            if (IsBotEligibleForChatChannelLocal(bot, player, sourceLocal, channel, receiver))
                candidateBots.push_back(bot);
            else
                notEligibleCount++;
        }
    }
    
    if (g_DebugEnabled && notEligibleCount > 0)
    {
        LOG_INFO("server.loading", "[Bot Chat] {} bots not eligible for {} (distance/guild/party checks failed)", 
                notEligibleCount, ChatChannelSourceLocalStr[sourceLocal]);
    }
    
    // Determine reply chance based on channel type
    uint32_t chance;
    if (sourceLocal == SRC_SAY_LOCAL || sourceLocal == SRC_YELL_LOCAL)
    {
        // Say/Yell channel type
        chance = senderIsBot ? ScaleChatChance(g_BotReplyChance_Say) : g_PlayerReplyChance_Say;
    }
    else if (sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL)
    {
        // Party/Raid channel type
        chance = senderIsBot ? ScaleChatChance(g_BotReplyChance_Party) : g_PlayerReplyChance_Party;
    }
    else if (sourceLocal == SRC_GUILD_LOCAL || sourceLocal == SRC_OFFICER_LOCAL)
    {
        // Guild/Officer channel type
        chance = senderIsBot ? ScaleChatChance(g_BotReplyChance_Guild) : g_PlayerReplyChance_Guild;
    }
    else if (sourceLocal == SRC_GENERAL_LOCAL)
    {
        // General/Trade/Custom channel type
        chance = senderIsBot ? ScaleChatChance(g_BotReplyChance_Channel) : g_PlayerReplyChance_Channel;
    }
    else
    {
        // Default fallback (whispers, etc.) - use Say chances
        chance = senderIsBot ? ScaleChatChance(g_BotReplyChance_Say) : g_PlayerReplyChance_Say;
    }
    
    if(g_DebugEnabled)
    {
        LOG_INFO("server.loading", "[Bot Chat] Sender: {} ({}), Channel: {}, Reply Chance: {}%, Candidate Bots: {}",
                player->GetName(), senderIsBot ? "BOT" : "PLAYER", ChatChannelSourceLocalStr[sourceLocal], chance, candidateBots.size());
    }
    
    std::vector<Player*> finalCandidates;

    // Combat silence is for ambient and nearby strangers. A real player talking
    // in party, guild, or whisper still gets an answer — that is the conversation.
    bool const playerGroupTalk = !senderIsBot &&
        (sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL ||
         sourceLocal == SRC_GUILD_LOCAL || sourceLocal == SRC_OFFICER_LOCAL ||
         sourceLocal == SRC_WHISPER_LOCAL);
    auto silentInCombat = [&](Player* bot) -> bool
    {
        if (!bot || !g_DisableRepliesInCombat || !bot->IsInCombat())
            return false;
        if (playerGroupTalk)
            return false;
        if (!senderIsBot && GetConversationBond(player->GetGUID().GetRawValue(), g_TopicIdleSeconds) ==
            bot->GetGUID().GetRawValue())
            return false;
        if (!senderIsBot && player && bot->GetGroup() && player->GetGroup() &&
            bot->GetGroup() == player->GetGroup())
            return false;
        return true;
    };

    // For whispers, handle directly - there should only be one receiver bot
    if (sourceLocal == SRC_WHISPER_LOCAL)
    {
        if (!candidateBots.empty())
        {
            Player* whisperBot = candidateBots[0]; // Should only be one bot for whispers
            finalCandidates.push_back(whisperBot);
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Whisper: Bot {} selected to respond", whisperBot->GetName());
        }
    }
    else
    {
        // Handle non-whisper chats with normal multi-bot logic
        std::vector<std::pair<size_t, Player*>> mentionedBots;

        // Helper to convert string to lowercase safely
        auto toLowerStr = [](const std::string& str) -> std::string {
            std::string result = str;
            for (char& c : result)
            {
                c = std::tolower(static_cast<unsigned char>(c));
            }
            return result;
        };

        // Helper to check if a bot name is mentioned as a complete word
        auto isBotNameMentioned = [&trimmedMsg, &toLowerStr](const std::string& botName) -> size_t {
            std::string lowerMsg = toLowerStr(trimmedMsg);
            std::string lowerBotName = toLowerStr(botName);
            
            size_t pos = 0;
            while ((pos = lowerMsg.find(lowerBotName, pos)) != std::string::npos)
            {
                // Check if it's a word boundary before the name
                bool validStart = (pos == 0 || !std::isalnum(static_cast<unsigned char>(lowerMsg[pos - 1])));
                // Check if it's a word boundary after the name
                size_t endPos = pos + lowerBotName.length();
                bool validEnd = (endPos >= lowerMsg.length() || !std::isalnum(static_cast<unsigned char>(lowerMsg[endPos])));
                
                if (validStart && validEnd)
                {
                    return pos; // Found a valid word-boundary match
                }
                pos++; // Continue searching
            }
            return std::string::npos;
        };

        for (Player* bot : candidateBots)
        {
            if (!bot)
            {
                continue;
            }
            if (silentInCombat(bot))
            {
                continue;
            }

            size_t pos = isBotNameMentioned(bot->GetName());
            if (pos != std::string::npos)
            {
                mentionedBots.emplace_back(pos, bot);
                if(g_DebugEnabled)
                {
                    LOG_INFO("server.loading", "[Bot Chat] Bot {} mentioned at position {} in message", bot->GetName(), pos);
                }
            }
        }

        if (!mentionedBots.empty())
        {
            // Sort by position to get the first mentioned bot
            std::sort(mentionedBots.begin(), mentionedBots.end(),
                      [](const std::pair<size_t, Player*> &a, const std::pair<size_t, Player*> &b) { return a.first < b.first; });
            Player* chosen = mentionedBots.front().second;
            if (!silentInCombat(chosen))
            {
                finalCandidates.push_back(chosen);
                if(g_DebugEnabled)
                {
                    LOG_INFO("server.loading", "[Bot Chat] Bot {} selected (mentioned first at position {})", 
                            chosen->GetName(), mentionedBots.front().first);
                }
            }
        }
        else
        {
            if (!senderIsBot && sourceLocal == SRC_GENERAL_LOCAL)
            {
                ChatTone const tone = DetectChatTone(msg);
                bool const keep =
                    GetConversationBond(player->GetGUID().GetRawValue(), g_TopicIdleSeconds) != 0 ||
                    chatIntent == ChatIntent::HelpRequest || chatIntent == ChatIntent::Question ||
                    parsedQuery.topic == ChatTopic::LookingForGroup ||
                    tone == ChatTone::Hostile ||
                    IsShortFollowUp(msg) ||
                    LooksLikeArgument(msg);
                if (!keep && urand(0, 99) < 32)
                {
                    NotePlayerIgnored(threadKey, true);
                    if (g_DebugEnabled)
                        LOG_INFO("server.loading", "[Bot Chat] General ignores unsolicited line from {}",
                                 player->GetName());
                    return;
                }
                NotePlayerIgnored(threadKey, false);
            }

            uint32_t effectiveChance = chance;
            if (!senderIsBot && (chatIntent == ChatIntent::HelpRequest || chatIntent == ChatIntent::Question))
                effectiveChance = std::min<uint32_t>(100, chance + g_QuestionReplyChanceBonus);
            // General: don't let bots keep a fake thread going among themselves.
            if (senderIsBot && sourceLocal == SRC_GENERAL_LOCAL)
                effectiveChance = 0;
            // One answer is enough. Don't let other bots pile onto a help reply.
            if (senderIsBot && !GetLastPlayerHelpQuery(threadKey).empty())
                effectiveChance = 0;

            if (!senderIsBot && finalCandidates.empty())
            {
                uint64_t bonded = GetConversationBond(player->GetGUID().GetRawValue(), g_TopicIdleSeconds);
                if (bonded)
                {
                    for (Player* bot : candidateBots)
                    {
                        if (!bot || bot->GetGUID().GetRawValue() != bonded)
                            continue;
                        if (silentInCombat(bot))
                            break;
                        finalCandidates.push_back(bot);
                        if (g_DebugEnabled)
                            LOG_INFO("server.loading", "[Bot Chat] Sticking with conversation partner {}",
                                     bot->GetName());
                        break;
                    }
                }
            }

            if (!senderIsBot && finalCandidates.empty())
            {
                uint64_t lastBot = GetLastBotSpeaker(threadKey);
                if (lastBot)
                {
                    for (Player* bot : candidateBots)
                    {
                        if (!bot || bot->GetGUID().GetRawValue() != lastBot)
                            continue;
                        if (silentInCombat(bot))
                            break;
                        finalCandidates.push_back(bot);
                        if (g_DebugEnabled)
                            LOG_INFO("server.loading", "[Bot Chat] Sticking with last speaker {}", bot->GetName());
                        break;
                    }
                }
            }

            if (!senderIsBot && finalCandidates.empty())
            {
                uint64_t sharedBot = FindRecentSharedBotSpeaker(player, candidateBots, g_TopicIdleSeconds);
                if (sharedBot)
                {
                    for (Player* bot : candidateBots)
                    {
                        if (!bot || bot->GetGUID().GetRawValue() != sharedBot)
                            continue;
                        finalCandidates.push_back(bot);
                        if (g_DebugEnabled)
                            LOG_INFO("server.loading", "[Bot Chat] Sticking with shared-thread speaker {}",
                                     bot->GetName());
                        break;
                    }
                }
            }

            if (finalCandidates.empty())
            {
                std::vector<std::pair<int, Player*>> scored;
                std::vector<std::pair<int, Player*>> failed;
                for (Player* bot : candidateBots)
                {
                    if (silentInCombat(bot))
                    {
                        if(g_DebugEnabled)
                        {
                            LOG_INFO("server.loading", "[Bot Chat] Bot {} skipped - in combat", bot->GetName());
                        }
                        continue;
                    }

                    int score = 0;
                    bool isGuild = (sourceLocal == SRC_GUILD_LOCAL || sourceLocal == SRC_OFFICER_LOCAL);
                    bool recent = g_PreferThreadRegulars && IsRecentSpeaker(threadKey, bot->GetGUID().GetRawValue());
                    if (isGuild && ThreadIsActive(threadKey, g_TopicIdleSeconds) && !recent && urand(0, 99) < 80)
                        continue;
                    if (GetConversationBond(player->GetGUID().GetRawValue(), g_TopicIdleSeconds) ==
                        bot->GetGUID().GetRawValue())
                        score += 6;
                    if (recent)
                        score += isGuild ? 5 : 3;
                    if (bot->GetZoneId() == player->GetZoneId())
                        score += 2;
                    if ((chatIntent == ChatIntent::HelpRequest || chatIntent == ChatIntent::Question) &&
                        bot->GetLevel() >= player->GetLevel())
                        score += 1;

                    uint32_t roll = urand(0, 99);
                    if (roll < effectiveChance)
                    {
                        scored.push_back({score, bot});
                        if(g_DebugEnabled)
                        {
                            LOG_INFO("server.loading", "[Bot Chat] Bot {} PASSED chance roll ({} < {}%) score={}", bot->GetName(), roll, effectiveChance, score);
                        }
                    }
                    else
                    {
                        failed.push_back({score, bot});
                        if(g_DebugEnabled)
                        {
                            LOG_INFO("server.loading", "[Bot Chat] Bot {} FAILED chance roll ({} >= {}%) score={}", bot->GetName(), roll, effectiveChance, score);
                        }
                    }
                }

                auto byScoreDesc = [](const std::pair<int, Player*>& a, const std::pair<int, Player*>& b) {
                    return a.first > b.first;
                };
                std::sort(scored.begin(), scored.end(), byScoreDesc);
                std::sort(failed.begin(), failed.end(), byScoreDesc);

                if (scored.empty() && !senderIsBot && g_AlwaysReplyToPlayerQuestions &&
                    (chatIntent == ChatIntent::HelpRequest || chatIntent == ChatIntent::Question ||
                     parsedQuery.topic == ChatTopic::LookingForGroup) &&
                    !failed.empty())
                {
                    scored.push_back(failed.front());
                    if (g_DebugEnabled)
                        LOG_INFO("server.loading", "[Bot Chat] Forcing reply to player question from {}", failed.front().second->GetName());
                }

                for (auto const& [score, bot] : scored)
                {
                    (void)score;
                    finalCandidates.push_back(bot);
                }
            }
        }
    }

    
    if (finalCandidates.empty())
    {
        bool const playerSay = !senderIsBot &&
            (sourceLocal == SRC_SAY_LOCAL || sourceLocal == SRC_WHISPER_LOCAL);
        std::vector<Player*> const& stickyPool = playerGroupTalk ? candidateBots : eligibleBots;
        if ((playerSay || playerGroupTalk) && !stickyPool.empty())
        {
            Player* closest = nullptr;
            float best = 99999.0f;
            for (Player* bot : stickyPool)
            {
                if (!bot)
                    continue;
                if (silentInCombat(bot))
                    continue;
                float d = player->GetDistance(bot);
                if (!closest || d < best)
                {
                    closest = bot;
                    best = d;
                }
            }
            if (closest)
                finalCandidates.push_back(closest);
        }
        if (finalCandidates.empty())
        {
            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[Bot Chat] *** NO BOTS RESPONDING *** to {} from {} in {} channel. "
                        "Eligible: {}, Candidates: {}, Final: 0, Chance: {}%",
                        senderIsBot ? "BOT" : "PLAYER", player->GetName(), ChatChannelSourceLocalStr[sourceLocal],
                        eligibleBots.size(), candidateBots.size(), chance);
                LOG_INFO("server.loading", "[Bot Chat] No eligible bots found to respond to message '{}'. "
                        "Source: {}, Eligible bots: {}, Candidate bots: {}, Combat disabled: {}",
                        msg, ChatChannelSourceLocalStr[sourceLocal], eligibleBots.size(),
                        candidateBots.size(), g_DisableRepliesInCombat);
            }
            return;
        }
    }
    
    if (finalCandidates.size() > g_MaxBotsToPick)
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(finalCandidates.begin(), finalCandidates.end(), g);
        uint32_t countToPick = urand(1, g_MaxBotsToPick);
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Limiting {} bots to {} (MaxBotsToPick)", finalCandidates.size(), countToPick);
        }
        finalCandidates.resize(countToPick);
    }
    
    if(g_DebugEnabled && !finalCandidates.empty())
    {
        std::string botNames;
        for (Player* bot : finalCandidates)
        {
            if (!botNames.empty()) botNames += ", ";
            botNames += bot->GetName();
        }
        LOG_INFO("server.loading", "[Bot Chat] *** {} BOTS RESPONDING *** to {} from {} in {}: [{}]",
                finalCandidates.size(), senderIsBot ? "BOT" : "PLAYER", player->GetName(),
                ChatChannelSourceLocalStr[sourceLocal], botNames);
    }

    if (!senderIsBot && !finalCandidates.empty() && finalCandidates.front())
        NoteConversationBond(player->GetGUID().GetRawValue(), finalCandidates.front()->GetGUID().GetRawValue());

    if (g_EnableSocialConventions)
    {
        SocialAct socialAct = DetectSocialAct(msg);
        bool ritual = socialAct == SocialAct::Congrats || socialAct == SocialAct::WelcomeBack ||
                      socialAct == SocialAct::Welcome || socialAct == SocialAct::Thanks ||
                      socialAct == SocialAct::GoodLuck || socialAct == SocialAct::GoodGame ||
                      socialAct == SocialAct::Condolence || socialAct == SocialAct::Apology ||
                      socialAct == SocialAct::Back || socialAct == SocialAct::Brb ||
                      socialAct == SocialAct::Greeting || socialAct == SocialAct::Reaction ||
                      socialAct == SocialAct::Farewell;
        if (socialAct != SocialAct::None && ritual)
        {
            // Don't let canned ty/np chain off another bot's canned line.
            if (senderIsBot)
                return;
            FilterSocialRecipients(finalCandidates, eligibleBots, msg, socialAct);
            if (finalCandidates.empty())
            {
                if (g_DebugEnabled)
                    LOG_INFO("server.loading", "[Bot Chat] Social act {} had no recipient", static_cast<int>(socialAct));
                return;
            }
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Social act {} for {} bot(s)", static_cast<int>(socialAct), finalCandidates.size());
            DeliverSocialReplies(finalCandidates, player, "", sourceLocal, channel, socialAct);
            return;
        }
    }

    if (!senderIsBot && g_EnableSocialConventions && IsActivityAsk(msg) && !finalCandidates.empty())
    {
        for (Player* bot : finalCandidates)
        {
            if (!bot)
                continue;
            std::string const threadKey = MakeThreadKey(player, sourceLocal, channel);
            std::string line = PickActivityReply(bot, threadKey);
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Activity ask -> {} : {}", bot->GetName(), line);
            DeliverSocialReplies({ bot }, player, line, sourceLocal, channel);
        }
        return;
    }

    if (!senderIsBot && g_EnableSocialConventions && IsGuildInviteTalk(msg) && !finalCandidates.empty())
    {
        std::string const threadKey = MakeThreadKey(player, sourceLocal, channel);
        for (Player* bot : finalCandidates)
        {
            if (!bot)
                continue;
            std::string line = PickGuildInviteReply(threadKey);
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Guild-invite talk -> {} : {}", bot->GetName(), line);
            DeliverSocialReplies({ bot }, player, line, sourceLocal, channel);
        }
        return;
    }

    std::string const lastBotLine = GetLastBotText(threadKey);
    bool const lastBotWasGroup = !lastBotLine.empty() &&
        ParseChatQuery(lastBotLine).topic == ChatTopic::LookingForGroup;

    if (!senderIsBot && lastBotWasGroup && !finalCandidates.empty() &&
        (IsGroupAsk(msg) || IsGroupJoin(msg) || IsShortFollowUp(msg)))
    {
        Player* starter = finalCandidates.front();
        std::string const lfgKey = MakeThreadKey(player, sourceLocal, channel, starter);
        std::string line = IsGroupJoin(msg) ? PickGroupReply(lfgKey, false, starter)
                                            : PickGroupPurpose(starter, lfgKey);
        if (line.empty() && IsGroupAsk(msg))
            line = PickGroupPurpose(starter, lfgKey);
        if (!line.empty())
        {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Group follow-up -> {} : {}", starter->GetName(), line);
            DeliverSocialReplies({ starter }, player, line, sourceLocal, channel);
            MaybeChimeIn(candidateBots, starter, player, sourceLocal, channel, lfgKey, true, lastBotLine);
        }
        return;
    }

    if (g_EnableSocialConventions && parsedQuery.topic == ChatTopic::LookingForGroup && !finalCandidates.empty())
    {
        // Bot-to-bot LFG is ambient continue, not a canned "gl"/"what for".
        if (senderIsBot)
            return;
        std::string const lfgKey = MakeThreadKey(player, sourceLocal, channel);
        bool const inParty = (sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL);
        std::string line = PickGroupReply(lfgKey, inParty, finalCandidates.front());
        if (!line.empty())
        {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Group talk -> {} : {}", finalCandidates.front()->GetName(), line);
            DeliverSocialReplies({ finalCandidates.front() }, player, line, sourceLocal, channel);
            MaybeChimeIn(candidateBots, finalCandidates.front(), player, sourceLocal, channel, lfgKey, true, lastBotLine);
        }
        return;
    }

    // "ok" / "yeah" on an empty or looping thread closes the beat. If they are
    // answering a bot, stay on that topic.
    if (!senderIsBot && IsAck(msg))
    {
        if (finalCandidates.empty())
            return;
        bool const answeringBot = !lastBotLine.empty() && !ThreadLooksLooped(threadKey);
        if (!answeringBot)
        {
            if (urand(0, 99) < 70)
                return;
            std::string const ackKey = MakeThreadKey(player, sourceLocal, channel);
            std::string line = PickSocialReply(SocialAct::Reaction, ackKey);
            if (line.empty())
                line = "kk";
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] Ack from {} -> {}", player->GetName(), line);
            DeliverSocialReplies({ finalCandidates.front() }, player, line, sourceLocal, channel);
            return;
        }
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] Ack on live bot topic from {}", player->GetName());
        TryBotChatLlmReply(finalCandidates, player, msg, ChatTone::Neutral, sourceLocal, channel,
                           PickSocialReply(SocialAct::Reaction, threadKey));
        MaybeChimeIn(candidateBots, finalCandidates.front(), player, sourceLocal, channel,
                     threadKey, lastBotWasGroup, lastBotLine);
        return;
    }

    // Help facts stay canned from taxi/NPC data. "what u mean" after a real
    // help ask reuses that. Conversational "what"/"huh"/"where" with no prior
    // help query is a thread continue — do not swallow it.
    if (chatIntent == ChatIntent::HelpRequest || parsedQuery.topic == ChatTopic::FollowUp)
    {
        if (finalCandidates.empty())
            return;

        std::vector<std::string> contextKeys;
        contextKeys.push_back(threadKey);
        if (!finalCandidates.empty() && finalCandidates.front())
        {
            for (std::string const& key : SharedThreadKeys(player, finalCandidates.front()))
            {
                if (std::find(contextKeys.begin(), contextKeys.end(), key) == contextKeys.end())
                    contextKeys.push_back(key);
            }
        }

        std::string helpQuery = (parsedQuery.topic == ChatTopic::FollowUp)
            ? GetLastPlayerHelpQueryAmong(contextKeys)
            : msg;
        if (parsedQuery.topic == ChatTopic::FollowUp && !helpQuery.empty())
        {
            std::string const lastPlayer = GetLastPlayerMessageExceptAmong(contextKeys, msg);
            if (ClassifyChatIntent(lastPlayer) != ChatIntent::HelpRequest)
                helpQuery.clear();
        }

        if (!helpQuery.empty())
        {
            std::string line = PickKnowledgeReply(player, helpQuery);
            if (!line.empty() && !LineTooSimilarToRecent(threadKey, line))
            {
                if (g_DebugEnabled)
                    LOG_INFO("server.loading", "[Bot Chat] Knowledge reply -> {} : {}",
                             finalCandidates.front()->GetName(), line);
                DeliverSocialReplies({ finalCandidates.front() }, player, line, sourceLocal, channel);
                return;
            }
            // Real help question (or follow-up to one) with nothing fresh.
            // Stay quiet rather than invent a location.
            return;
        }
        if (chatIntent == ChatIntent::HelpRequest)
            return;
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] Conversational follow-up from {}, using LLM",
                     player->GetName());
    }

    if (senderIsBot || finalCandidates.empty())
        return;

    ChatTone const tone = DetectChatTone(msg);
    std::string fallback;
    if (tone == ChatTone::Hostile || tone == ChatTone::Dismissive)
        fallback = PickHostileReply(threadKey, msg);
    else if (IsShortFollowUp(msg))
        fallback = PickFollowUpReply(threadKey);

    uint32 const playerLines = CountRecentPlayerLines(threadKey);
    bool const oneToOne = sourceLocal == SRC_WHISPER_LOCAL || sourceLocal == SRC_SAY_LOCAL;
    bool const looping = ThreadLooksLooped(threadKey) ||
                         (!oneToOne && (playerLines >= 18 || (playerLines >= 12 && urand(0, 99) < 40)));
    if (looping && tone == ChatTone::Neutral)
    {
        std::string closer = PickCloserReply(threadKey);
        if (closer.empty())
            return;
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] Closing looping thread -> {} : {}",
                     finalCandidates.front()->GetName(), closer);
        DeliverSocialReplies({ finalCandidates.front() }, player, closer, sourceLocal, channel);
        return;
    }

    TryBotChatLlmReply(finalCandidates, player, msg, tone, sourceLocal, channel, fallback);
    if (tone == ChatTone::Neutral)
        MaybeChimeIn(candidateBots, finalCandidates.front(), player, sourceLocal, channel,
                     threadKey, lastBotWasGroup, lastBotLine);
}

#if 0
    uint64_t senderGuid = player->GetGUID().GetRawValue();
    
    for (Player* bot : finalCandidates)
    {
        float distance = player->GetDistance(bot);
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "[Bot Chat] Bot {} (distance: {}) is set to respond.", bot->GetName(), distance);
        }
        if (bot == nullptr) {
            continue;
        }
        std::string prompt = GenerateBotPrompt(bot, msg, player, threadKey, gameKnowledgeText, sourceLocal);
        uint64_t botGuid = bot->GetGUID().GetRawValue();
        
        std::thread([botGuid, senderGuid, prompt, sourceLocal, threadKey,
                     channelId = (channel ? channel->GetChannelId() : 0),
                     channelName = (channel ? channel->GetName() : ""), msg]() {
            try {
                // Use the QueryManager to submit the query.
                auto responseFuture = SubmitQuery(prompt);
                if (!responseFuture.valid())
                {
                    return;
                }
                std::string response = responseFuture.get();

                // Reacquire pointers by GUID.
                Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                Player* senderPtr = ObjectAccessor::FindPlayer(ObjectGuid(senderGuid));
                if (!botPtr)
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "[Bot Chat] Failed to reacquire bot from GUID {}", botGuid);
                    }
                    return;
                }
                if (!senderPtr)
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "[Bot Chat] Failed to reacquire sender from GUID {}", senderGuid);
                    }
                    return;
                }
                response = SanitizeBotChatLine(response);
                if (response.empty() || LineTooSimilarToRecent(threadKey, response))
                {
                    if(g_DebugEnabled)
                    {
                        LOG_INFO("server.loading", "[BotChat] Bot {} skipped reply due to empty, slang-salad, or repeat", botPtr->GetName());
                    }
                    return;
                }
                PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(botPtr);
                if (!botAI)
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "[Bot Chat] No PlayerbotAI found for bot {}", botPtr->GetName());
                    }
                    return;
                }
                
                // Simulate typing delay if enabled
                if (g_EnableTypingSimulation)
                {
                    uint32_t delay = g_TypingSimulationBaseDelay + (response.length() * g_TypingSimulationDelayPerChar);
                    if (g_DebugEnabled)
                        LOG_INFO("server.loading", "[BotChat] Bot {} simulating typing delay: {}ms for {} characters", 
                                 botPtr->GetName(), delay, response.length());
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                    
                    // Reacquire pointers after delay
                    botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                    if (!botPtr) return;
                    botAI = PlayerbotsMgr::instance().GetPlayerbotAI(botPtr);
                    if (!botAI) return;
                    senderPtr = ObjectAccessor::FindPlayer(ObjectGuid(senderGuid));
                    if (!senderPtr) return;
                }
                
                // Route the response.
                if (channelId != 0 && !channelName.empty())
                {
                    // For channels, get the channel instance for the bot's team
                    ChannelMgr* cMgr = ChannelMgr::forTeam(botPtr->GetTeamId());
                    if (cMgr)
                    {
                        Channel* targetChannel = cMgr->GetChannel(channelName, botPtr);
                        if (targetChannel)
                        {
                            if(g_DebugEnabled)
                            {
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} found channel '{}' (ID: {}), checking membership...", 
                                        botPtr->GetName(), channelName, targetChannel->GetChannelId());
                            }
                            
                            if (botPtr->IsInChannel(targetChannel))
                            {
                                if(g_DebugEnabled)
                                {
                                    LOG_INFO("server.loading", "[Bot Chat] Bot {} is confirmed in channel '{}', sending message...", 
                                            botPtr->GetName(), channelName);
                                }
                                targetChannel->Say(botPtr->GetGUID(), response, LANG_UNIVERSAL);
                                ProcessBotChatMessage(botPtr, response, SRC_GENERAL_LOCAL, targetChannel);
                                if(g_DebugEnabled)
                                {
                                    LOG_INFO("server.loading", "[Bot Chat] Bot {} responded in channel {}: {}", 
                                            botPtr->GetName(), channelName, response);
                                }
                            }
                            else
                            {
                                if(g_DebugEnabled)
                                {
                                    LOG_ERROR("server.loading", "[Bot Chat] Bot {} NOT in channel '{}' according to IsInChannel check - skipping reply", 
                                                botPtr->GetName(), channelName);
                                }
                                // Don't fallback to Say - if bot isn't in the channel, don't reply at all
                            }
                        }
                        else
                        {
                            if(g_DebugEnabled)
                            {
                                LOG_ERROR("server.loading", "[Bot Chat] Bot {} cannot find channel '{}' (ID: {}) for team {} - skipping reply", 
                                         botPtr->GetName(), channelName, channelId, (int)botPtr->GetTeamId());
                            }
                            // Don't fallback to Say - if channel doesn't exist, don't reply at all
                        }
                    }
                }
                else
                {
                    switch (sourceLocal)
                    {
                        case SRC_GUILD_LOCAL: 
                            botAI->SayToGuild(response);
                            ProcessBotChatMessage(botPtr, response, SRC_GUILD_LOCAL, nullptr);
                            break;
                        case SRC_OFFICER_LOCAL: 
                            botAI->SayToGuild(response);
                            ProcessBotChatMessage(botPtr, response, SRC_OFFICER_LOCAL, nullptr);
                            break;
                        case SRC_PARTY_LOCAL: 
                            botAI->SayToParty(response);
                            ProcessBotChatMessage(botPtr, response, SRC_PARTY_LOCAL, nullptr);
                            break;
                        case SRC_RAID_LOCAL:  
                            botAI->SayToRaid(response);
                            ProcessBotChatMessage(botPtr, response, SRC_RAID_LOCAL, nullptr);
                            break;
                        case SRC_SAY_LOCAL:
                            // Only send Say if someone (real player or bot) is within say distance
                            {
                                bool someoneCanHear = false;
                                if (botPtr->IsInWorld())
                                {
                                    for (auto const& pair : ObjectAccessor::GetPlayers())
                                    {
                                        Player* nearbyPlayer = pair.second;
                                        if (nearbyPlayer && nearbyPlayer != botPtr && nearbyPlayer->IsInWorld())
                                        {
                                            if (botPtr->GetDistance(nearbyPlayer) <= g_SayDistance)
                                            {
                                                someoneCanHear = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                
                                if (someoneCanHear)
                                {
                                    botAI->Say(response);
                                    ProcessBotChatMessage(botPtr, response, SRC_SAY_LOCAL, nullptr);
                                }
                                else if (g_DebugEnabled)
                                {
                                    LOG_INFO("server.loading", "[Bot Chat] Bot {} skipping Say reply - no one within {} yards to hear it", 
                                            botPtr->GetName(), g_SayDistance);
                                }
                            }
                            break;
                        case SRC_YELL_LOCAL:
                            // Only send Yell if someone is within yell distance
                            {
                                bool someoneCanHear = false;
                                if (botPtr->IsInWorld())
                                {
                                    for (auto const& pair : ObjectAccessor::GetPlayers())
                                    {
                                        Player* nearbyPlayer = pair.second;
                                        if (nearbyPlayer && nearbyPlayer != botPtr && nearbyPlayer->IsInWorld())
                                        {
                                            if (botPtr->GetDistance(nearbyPlayer) <= g_YellDistance)
                                            {
                                                someoneCanHear = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                
                                if (someoneCanHear)
                                {
                                    botAI->Yell(response);
                                    ProcessBotChatMessage(botPtr, response, SRC_YELL_LOCAL, nullptr);
                                }
                                else if (g_DebugEnabled)
                                {
                                    LOG_INFO("server.loading", "[Bot Chat] Bot {} skipping Yell reply - no one within {} yards to hear it", 
                                            botPtr->GetName(), g_YellDistance);
                                }
                            }
                            break;
                        case SRC_WHISPER_LOCAL:
                            // For whispers, find the original sender and whisper back
                            {
                                Player* originalSender = ObjectAccessor::FindPlayer(ObjectGuid(senderGuid));
                                if (originalSender)
                                {
                                    if(g_DebugEnabled)
                                    {
                                        LOG_INFO("server.loading", "[Bot Chat] Bot {} whispering response '{}' to {}", 
                                                botPtr->GetName(), response, originalSender->GetName());
                                    }
                                    botAI->Whisper(response, originalSender->GetName());
                                    // Don't trigger ProcessBotChatMessage for whispers - they're private
                                }
                                else if(g_DebugEnabled)
                                {
                                    LOG_ERROR("server.loading", "[Bot Chat] Cannot whisper response - original sender not found for GUID {}", senderGuid);
                                }
                            }
                            break;
                        default:              
                            botAI->Say(response);
                            ProcessBotChatMessage(botPtr, response, SRC_SAY_LOCAL, nullptr);
                            break;
                    }
                }
                
                // Update sentiment based on the player's message
                UpdateBotPlayerSentiment(botPtr, senderPtr, msg);
                
                AppendBotConversation(botGuid, senderGuid, msg, response);
                if (botPtr->IsInWorld() && senderPtr->IsInWorld())
                {
                    float respDistance = senderPtr->GetDistance(botPtr);
                    if(g_DebugEnabled)
                    {
                        LOG_INFO("server.loading", "[Bot Chat] Bot {} (distance: {}) responded: {}", botPtr->GetName(), respDistance, response);
                    }
                }
                else
                {
                    if(g_DebugEnabled)
                    {
                        LOG_INFO("server.loading", "[Bot Chat] Bot {} responded: {} (distance not calculated - players not in world)", botPtr->GetName(), response);
                    }
                }
            }
            catch (const std::exception& ex)
            {
                if(g_DebugEnabled)
                {
                    LOG_ERROR("server.loading", "[Bot Chat] Exception in bot response thread: {}", ex.what());
                }
            }
        }).detach();

    }
#endif

static bool IsBotEligibleForChatChannelLocal(Player* bot, Player* player, ChatChannelSourceLocal source, Channel* channel, Player* receiver)
{
    if (!bot || !player || bot == player)
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] IsBotEligible: FAILED basic check - bot={}, player={}, same={}", 
                    (void*)bot, (void*)player, (bot == player));
        return false;
    }
    if (!PlayerbotsMgr::instance().GetPlayerbotAI(bot))
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] IsBotEligible: Bot {} FAILED - no PlayerbotAI", bot->GetName());
        return false;
    }
        
    // For whispers, only the specific receiver should respond
    if (source == SRC_WHISPER_LOCAL)
    {
        // Don't allow bot-to-bot whisper responses
        PlayerbotAI* senderAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        if (senderAI && senderAI->IsBotAI())
        {
            return false;
        }
        
        return (receiver && bot == receiver);
    }
    
    // Check team compatibility for non-proximity chats (except channels which can be cross-faction)
    // Say and Yell are proximity-based and don't require same faction
    bool isProximityChatSource = (source == SRC_SAY_LOCAL || source == SRC_YELL_LOCAL);
    if (!channel && !isProximityChatSource && bot->GetTeamId() != player->GetTeamId())
        return false;
    
    // For channels, check if bot is in the specific channel instance
    if (channel)
    {
        // Verify the channel is valid before proceeding
        if (!channel)
        {
            if(g_DebugEnabled)
            {
                LOG_ERROR("server.loading", "[Bot Chat] IsBotEligibleForChatChannelLocal: Channel is null");
            }
            return false;
        }
            
        // ONLY use exact channel instance check - NO Player::IsInChannel() anymore
        ChannelMgr* candidateCMgr = ChannelMgr::forTeam(bot->GetTeamId());
        if (!candidateCMgr)
            return false;
            
        Channel* candidateChannel = candidateCMgr->GetChannel(channel->GetName(), bot);
        // Verify both channels are valid and are the exact same instance
        if (!candidateChannel || candidateChannel != channel)
        {
            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[Bot Chat] IsBotEligibleForChatChannelLocal: Bot {} not in same channel instance '{}' - Bot team: {}, Channel ptr: {} vs {}", 
                        bot->GetName(), channel->GetName(), (int)bot->GetTeamId(),
                        (void*)candidateChannel, (void*)channel);
            }
            return false;
        }
        
        // Additional team check for cross-faction channels - only allow same faction unless it's a global channel
        if (bot->GetTeamId() != player->GetTeamId())
        {
            // Allow cross-faction only for specific global channels
            bool isGlobalChannel = (channel->GetName().find("World") != std::string::npos || 
                                   channel->GetName().find("LookingForGroup") != std::string::npos);
            if (!isGlobalChannel)
            {
                if(g_DebugEnabled)
                {
                    LOG_INFO("server.loading", "[Bot Chat] IsBotEligibleForChatChannelLocal: Bot {} different faction from player - Bot: {}, Player: {}, Channel: '{}'", bot->GetName(), (int)bot->GetTeamId(), (int)player->GetTeamId(), channel->GetName());
                }
                return false;
            }
        }
    }
    
    bool isInParty = (player->GetGroup() && bot->GetGroup() && (player->GetGroup() == bot->GetGroup()));
    float threshold = 0.0f;
    
    switch (source)
    {
        case SRC_SAY_LOCAL:    
            threshold = g_SayDistance;
            if (threshold > 0.0f)
            {
                if (!bot->IsInWorld() || !player->IsInWorld())
                    return false;
                    
                float distance = bot->GetDistance(player);
                return distance <= threshold;
            }
            return false;
            
        case SRC_YELL_LOCAL:   
            threshold = g_YellDistance;
            return (threshold > 0.0f && player->GetDistance(bot) <= threshold);
            
        case SRC_GUILD_LOCAL:
        case SRC_OFFICER_LOCAL:
            return (player->GetGuild() && bot->GetGuildId() == player->GetGuildId());
            
        case SRC_PARTY_LOCAL:
        case SRC_RAID_LOCAL:
            return isInParty;
            
        case SRC_WHISPER_LOCAL:
            // For whispers, the bot should only respond if it's the specific receiver
            return (receiver && bot == receiver);
            
        case SRC_GENERAL_LOCAL:
            // For channels like General, Trade, etc., no distance check - only channel membership matters
            // Channel membership was already checked above
            return true;
            
        default:
            return false;
    }
}

[[maybe_unused]] static std::string GenerateBotPrompt(Player* bot, std::string playerMessage, Player* player,
                              const std::string& threadKey, const std::string& gameKnowledge,
                              ChatChannelSourceLocal sourceLocal)
{
    (void)bot; (void)playerMessage; (void)player; (void)threadKey; (void)gameKnowledge; (void)sourceLocal;
    return "";
#if 0
    if (!bot || !player) {
        return "";
    }
    PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
    if (botAI == nullptr) {
        return "";
    }
    ChatHelper* helper = botAI->GetChatHelper();
    if (helper == nullptr) {
        return "";
    }
    if (g_ChatPromptTemplate.empty()) {
        LOG_ERROR("server.loading", "[Bot Chat] GenerateBotPrompt: template is empty");
        return "";
    }

    AreaTableEntry const* botCurrentArea = botAI->GetCurrentArea();
    AreaTableEntry const* botCurrentZone = botAI->GetCurrentZone();

    uint64_t botGuid                = bot->GetGUID().GetRawValue();
    uint64_t playerGuid             = player->GetGUID().GetRawValue();

    std::string personality         = GetBotPersonality(bot);
    std::string personalityPrompt   = GetPersonalityPromptAddition(personality);
    std::string botName             = bot->GetName();
    uint32_t botLevel               = bot->GetLevel();
    uint8_t botGenderByte           = bot->getGender();
    std::string botAreaName         = botCurrentArea ? botAI->GetLocalizedAreaName(botCurrentArea): "UnknownArea";
    std::string botZoneName         = botCurrentZone ? botAI->GetLocalizedAreaName(botCurrentZone): "UnknownZone";
    std::string botMapName          = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";
    std::string botClass            = CleanPromptText(botAI->GetChatHelper()->FormatClass(bot->getClass()));
    std::string botRace             = CleanPromptText(botAI->GetChatHelper()->FormatRace(bot->getRace()));
    std::string botRole             = CleanPromptText(ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot)));
    std::string botGender           = (botGenderByte == 0 ? "Male" : "Female");
    std::string botFaction          = (bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
    std::string botGuild            = (bot->GetGuild() ? bot->GetGuild()->GetName() : "No Guild");
    std::string botGroupStatus      = (bot->GetGroup() ? "In a group" : "Solo");
    uint32_t botGold                = bot->GetMoney() / 10000;

    std::string playerName          = player->GetName();
    uint32_t playerLevel            = player->GetLevel();
    std::string playerClass         = CleanPromptText(botAI->GetChatHelper()->FormatClass(player->getClass()));
    std::string playerRace          = CleanPromptText(botAI->GetChatHelper()->FormatRace(player->getRace()));
    std::string playerRole          = CleanPromptText(ChatHelper::FormatClass(player, AiFactory::GetPlayerSpecTab(player)));
    uint8_t playerGenderByte        = player->getGender();
    std::string playerGender        = (playerGenderByte == 0 ? "Male" : "Female");
    std::string playerFaction       = (player->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
    std::string playerGuild         = (player->GetGuild() ? player->GetGuild()->GetName() : "No Guild");
    std::string playerGroupStatus   = (player->GetGroup() ? "In a group" : "Solo");
    uint32_t playerGold             = player->GetMoney() / 10000;
    float playerDistance            = player->IsInWorld() && bot->IsInWorld() ? player->GetDistance(bot) : -1.0f;

    std::string chatHistory         = GetBotHistoryPrompt(botGuid, playerGuid, playerMessage);
    std::string sentimentInfo       = GetSentimentPromptAddition(bot, player);
    std::string channelThread;
    if (g_EnableChannelThreads && !threadKey.empty())
        channelThread = FormatChannelThread(threadKey, botName);

    // Retrieve RAG information if enabled
    std::string ragInfo;
    if (g_EnableRAG && g_RAGSystem && ClassifyChatIntent(playerMessage) == ChatIntent::HelpRequest) {
        auto ragResults = g_RAGSystem->RetrieveRelevantInfo(playerMessage, g_RAGMaxRetrievedItems, g_RAGSimilarityThreshold);
        std::string ragContent = g_RAGSystem->GetFormattedRAGInfo(ragResults);
        if (!ragContent.empty()) {
            ragInfo = SafeFormat(g_RAGPromptTemplate, fmt::arg("rag_info", ragContent));
        }
        if (g_DebugEnabled) {
            LOG_INFO("server.loading", "[Bot Chat] RAG Debug - Enabled: {}, System: {}, Message: '{}', Results: {}, Content length: {}",
                g_EnableRAG, (void*)g_RAGSystem, playerMessage, ragResults.size(), ragContent.length());
        }
    } else if (g_DebugEnabled) {
        LOG_INFO("server.loading", "[Bot Chat] RAG Debug - Not enabled or no system - Enabled: {}, System: {}",
            g_EnableRAG, (void*)g_RAGSystem);
    }

    std::string botPlace = DescribeBotPlace(bot);
    if (botPlace.empty())
        botPlace = botZoneName;
    bool const inDungeon = bot->GetMap() && bot->GetMap()->IsDungeon();

    std::string extraInfo = SafeFormat(
        g_ChatExtraInfoTemplate,
        fmt::arg("bot_race", botRace),
        fmt::arg("bot_gender", botGender),
        fmt::arg("bot_role", botRole),
        fmt::arg("bot_faction", botFaction),
        fmt::arg("bot_guild", botGuild),
        fmt::arg("bot_group_status", botGroupStatus),
        fmt::arg("bot_gold", botGold),
        fmt::arg("player_race", playerRace),
        fmt::arg("player_gender", playerGender),
        fmt::arg("player_role", playerRole),
        fmt::arg("player_faction", playerFaction),
        fmt::arg("player_guild", playerGuild),
        fmt::arg("player_group_status", playerGroupStatus),
        fmt::arg("player_gold", playerGold),
        fmt::arg("player_distance", playerDistance),
        fmt::arg("bot_area", botAreaName),
        fmt::arg("bot_zone", botPlace),
        fmt::arg("bot_map", botMapName)
    );
    auto eraseAll = [](std::string& s, char const* needle)
    {
        if (!needle || !*needle)
            return;
        size_t pos = 0;
        size_t const n = std::strlen(needle);
        while ((pos = s.find(needle, pos)) != std::string::npos)
            s.erase(pos, n);
    };
    eraseAll(extraInfo, "standing in the open-world zone ");
    eraseAll(extraInfo, "open-world zone ");
    eraseAll(extraInfo, " That is not a dungeon.");
    eraseAll(extraInfo, "That is not a dungeon.");
    extraInfo += inDungeon
        ? SafeFormat(" You are inside the dungeon {}.", botPlace)
        : SafeFormat(" You are in {}.", botPlace);
    if (sourceLocal == SRC_GUILD_LOCAL)
    {
        extraInfo += " This is /g. Answer what they asked. Do not start an LFM. "
                     "Do not invent a raid or a later-expansion zone.";
    }
    else if (sourceLocal == SRC_GENERAL_LOCAL)
        extraInfo += " This is zone general chat. Answer them. Do not invent a guild name or a meeting time. If they want an invite, say pst.";
    else if (sourceLocal == SRC_SAY_LOCAL || sourceLocal == SRC_YELL_LOCAL)
        extraInfo += " This is /s. Short chat line. If they want a group or help, say yes or what you can do.";
    else if (sourceLocal == SRC_OFFICER_LOCAL)
        extraInfo += " This is officer chat. Keep it brief and in-guild. Do not fill the channel.";
    if (!gameKnowledge.empty())
        extraInfo += " If KNOWN FACTS name a place or NPC, say that name and the coords. Do not invent a second location.";
    
    std::string prompt = SafeFormat(
        g_ChatPromptTemplate,
        fmt::arg("bot_name", botName),
        fmt::arg("bot_level", botLevel),
        fmt::arg("bot_class", botClass),
        fmt::arg("bot_personality", personalityPrompt),
        fmt::arg("bot_personality_name", personality),
        fmt::arg("player_level", playerLevel),
        fmt::arg("player_class", playerClass),
        fmt::arg("player_name", playerName),
        fmt::arg("player_message", playerMessage),
        fmt::arg("extra_info", extraInfo),
        fmt::arg("chat_history", chatHistory),
        fmt::arg("sentiment_info", sentimentInfo),
        fmt::arg("channel_thread", channelThread),
        fmt::arg("game_knowledge", gameKnowledge)
    );

    if (!channelThread.empty() && prompt.find(channelThread) == std::string::npos)
        prompt += "\n" + channelThread;
    if (!gameKnowledge.empty() && prompt.find(gameKnowledge) == std::string::npos)
        prompt += "\n" + gameKnowledge;

    // Add RAG information to the prompt if available
    if (!ragInfo.empty()) {
        prompt += ragInfo + "\n";
    }

    if(g_EnableChatBotSnapshotTemplate)
    {
        prompt += GenerateBotGameStateSnapshot(bot);
    }

    // Debug logging for full prompt including RAG information
    prompt = CleanPromptText(prompt);
    {
        size_t pos = 0;
        char const* needle = "Zones are not dungeons.";
        size_t const n = std::strlen(needle);
        while ((pos = prompt.find(needle, pos)) != std::string::npos)
            prompt.erase(pos, n);
    }

    if (g_DebugEnabled && g_DebugShowFullPrompt) {
        LOG_INFO("server.loading", "[Bot Chat] Full prompt sent to bot {} for player {}: {}", botName, playerName, prompt);
    }

    return prompt;
#endif
}
