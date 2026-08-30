#include "bot_chat_random.h"
#include "bot_chat_config.h"
#include "bot_chat_handler.h"
// sentiment removed
#include "bot_chat_thread.h"
#include "bot_chat_knowledge.h"
#include "bot_chat_social.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "ChannelMgr.h"
#include "Channel.h"
#include "fmt/core.h"
// ollama api removed
// personality removed
#include "bot_chat_util.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Map.h"
#include "GridNotifiers.h"
#include "Guild.h"
#include <vector>
#include <random>
#include <thread>
#include <ctime>
#include "Item.h"
#include "Bag.h"
#include "SpellMgr.h"
#include "AiFactory.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "DBCStores.h"
#include "DBCEnums.h"
#include "SharedDefines.h"
#include "Group.h"
#include <mutex>

BotChatRandom::BotChatRandom() : WorldScript("BotChatRandom") {}

std::unordered_map<uint64_t, time_t> nextRandomChatTime;

namespace
{
    enum class AmbientSeed
    {
        Continue,
        Quest,
        City,
        Zone,
        Group,
        Life,
        Class,
        Dungeon,
        Silence
    };

    struct AmbientPick
    {
        AmbientSeed seed = AmbientSeed::Silence;
        std::string fact;
        std::string instruction;
        std::string canned;
    };

    std::mutex g_AmbientTopicMutex;
    std::unordered_map<std::string, time_t> g_LastNewTopicAt;
    std::unordered_map<uint32, time_t> g_LastGuildAmbientAt;

    bool AreaIsCapital(uint32 zoneId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
        if (!area)
            return false;
        if (area->flags & AREA_FLAG_CAPITAL)
            return true;
        if (area->zone)
        {
            if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
                return (parent->flags & AREA_FLAG_CAPITAL) != 0;
        }
        return false;
    }

    std::string BotZoneName(Player* bot)
    {
        PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!ai)
            return "this zone";
        AreaTableEntry const* zone = ai->GetCurrentZone();
        if (zone)
        {
            std::string name = ai->GetLocalizedAreaName(zone);
            if (!name.empty())
                return name;
        }
        AreaTableEntry const* area = ai->GetCurrentArea();
        if (area)
        {
            std::string name = ai->GetLocalizedAreaName(area);
            if (!name.empty())
                return name;
        }
        return "this zone";
    }

    std::string BotPlaceForAmbient(Player* bot)
    {
        PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!ai)
            return BotZoneName(bot);
        if (bot->GetMap() && bot->GetMap()->IsDungeon())
        {
            std::string const mapName = bot->GetMap()->GetMapName();
            if (!mapName.empty())
                return mapName;
        }
        AreaTableEntry const* area = ai->GetCurrentArea();
        AreaTableEntry const* zone = ai->GetCurrentZone();
        if (area)
        {
            std::string name = ai->GetLocalizedAreaName(area);
            if (!name.empty())
                return name;
        }
        if (zone)
        {
            std::string name = ai->GetLocalizedAreaName(zone);
            if (!name.empty())
                return name;
        }
        return BotZoneName(bot);
    }

    std::string BotClassShort(Player* bot)
    {
        if (!bot)
            return "";
        switch (bot->getClass())
        {
            case CLASS_WARRIOR: return "warrior";
            case CLASS_PALADIN: return "paladin";
            case CLASS_HUNTER: return "hunter";
            case CLASS_ROGUE: return "rogue";
            case CLASS_PRIEST: return "priest";
            case CLASS_DEATH_KNIGHT: return "dk";
            case CLASS_SHAMAN: return "shaman";
            case CLASS_MAGE: return "mage";
            case CLASS_WARLOCK: return "warlock";
            case CLASS_DRUID: return "druid";
            default: return "";
        }
    }

    bool PlayerIsReal(Player* player)
    {
        return player && player->IsInWorld() && !PlayerbotsMgr::instance().GetPlayerbotAI(player);
    }

    bool GuildHasRealPlayer(Guild* guild)
    {
        if (!guild)
            return false;
        uint32 const id = guild->GetId();
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (!PlayerIsReal(player))
                continue;
            if (player->GetGuild() && player->GetGuild()->GetId() == id)
                return true;
        }
        return false;
    }

    bool GroupHasRealPlayer(Group* group)
    {
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            if (PlayerIsReal(ref->GetSource()))
                return true;
        }
        return false;
    }

    std::string RandomOnlineGuildieName(Player* bot)
    {
        Guild* guild = bot->GetGuild();
        if (!guild)
            return "";

        std::vector<std::string> realNames;
        std::vector<std::string> botNames;
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* member = pair.second;
            if (!member || !member->IsInWorld() || member == bot)
                continue;
            if (!member->GetGuild() || member->GetGuild()->GetId() != guild->GetId())
                continue;
            if (PlayerbotsMgr::instance().GetPlayerbotAI(member))
                botNames.push_back(member->GetName());
            else
                realNames.push_back(member->GetName());
        }
        if (!realNames.empty())
            return realNames[urand(0, realNames.size() - 1)];
        if (!botNames.empty())
            return botNames[urand(0, botNames.size() - 1)];
        return "";
    }

    std::string RandomIncompleteQuestTitle(Player* bot)
    {
        std::vector<std::string> titles;
        for (auto const& [questId, status] : bot->getQuestStatusMap())
        {
            if (status.Status != QUEST_STATUS_INCOMPLETE)
                continue;
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            {
                if (!quest->GetTitle().empty())
                    titles.push_back(quest->GetTitle());
            }
        }
        if (titles.empty())
            return "";
        return titles[urand(0, titles.size() - 1)];
    }

    bool TopicRecentlyOpened(const std::string& key, int cooldownSeconds)
    {
        if (key.empty())
            return false;
        std::lock_guard<std::mutex> lock(g_AmbientTopicMutex);
        auto it = g_LastNewTopicAt.find(key);
        if (it == g_LastNewTopicAt.end())
            return false;
        return difftime(time(nullptr), it->second) < cooldownSeconds;
    }

    void MarkTopicOpened(const std::string& key)
    {
        if (key.empty())
            return;
        std::lock_guard<std::mutex> lock(g_AmbientTopicMutex);
        g_LastNewTopicAt[key] = time(nullptr);
    }

    AmbientPick PickAmbientSeed(Player* bot, const std::string& threadKey, bool threadActive, bool inGuild)
    {
        AmbientPick pick;
        uint32 continueChance = g_ContinueTopicChance;
        if (continueChance > 100)
            continueChance = 100;
        if (threadActive && urand(0, 99) < continueChance)
        {
            pick.seed = AmbientSeed::Continue;
            std::string const last = GetLastThreadText(threadKey);
            if (IsAck(last) || IsDismissal(last) || LooksLikeAddonAnnounce(last) ||
                (IsShortFollowUp(last) && GetLastPlayerHelpQuery(threadKey).empty()))
            {
                pick.seed = AmbientSeed::Silence;
                return pick;
            }
            ChatQuery const lastQ = ParseChatQuery(last);
            SocialAct const lastAct = DetectSocialAct(last);
            if (lastAct != SocialAct::None && lastAct != SocialAct::Greeting)
                pick.canned = PickSocialReply(lastAct, threadKey);
            else if (IsActivityAsk(last))
                pick.canned = PickActivityReply(bot, threadKey);
            else if (lastQ.topic == ChatTopic::LookingForGroup)
                pick.canned = PickGroupReply(threadKey, !inGuild && bot->GetGroup());
            else if (lastQ.intent != ChatIntent::HelpRequest && lastQ.intent != ChatIntent::Question)
                pick.canned = PickSocialReply(SocialAct::Reaction, threadKey);
            if (!g_RandomChatterContinueTemplate.empty())
                pick.instruction = g_RandomChatterContinueTemplate;
            else if (inGuild)
                pick.instruction = "This is /g. One short readable line that fits the last message. Stay on that topic.";
            else
                pick.instruction = "Continue the recent channel chat with one short line. Stay on topic. Do not greet.";
            return pick;
        }

        if (TopicRecentlyOpened(threadKey, 50))
        {
            pick.seed = AmbientSeed::Silence;
            return pick;
        }

        struct Option
        {
            AmbientSeed seed;
            uint32 weight;
            std::string fact;
            std::string instruction;
        };
        std::vector<Option> options;

        std::string questTitle = RandomIncompleteQuestTitle(bot);
        std::string placeName = BotPlaceForAmbient(bot);
        std::string className = BotClassShort(bot);
        bool capital = AreaIsCapital(bot->GetZoneId());
        bool inDungeon = bot->GetMap() && bot->GetMap()->IsDungeon();

        if (inGuild)
        {
            options.push_back({AmbientSeed::Life, 30, "", ""});

            std::string guildie = RandomOnlineGuildieName(bot);
            if (!guildie.empty())
                options.push_back({AmbientSeed::Group, 22, guildie, ""});

            if (!questTitle.empty())
                options.push_back({AmbientSeed::Quest, 14, questTitle, ""});

            if (bot->GetGroup())
                options.push_back({AmbientSeed::Group, 12, "", ""});

            options.push_back({AmbientSeed::Silence, 18, "", ""});
        }
        else
        {
            if (inDungeon)
                options.push_back({AmbientSeed::Dungeon, 28, bot->GetMap()->GetMapName(), ""});
            else if (!questTitle.empty())
                options.push_back({AmbientSeed::Quest, 26, questTitle, ""});

            if (!inDungeon)
            {
                if (capital)
                    options.push_back({AmbientSeed::City, 24, placeName, ""});
                else
                    options.push_back({AmbientSeed::Zone, 26, placeName, ""});
            }

            if (bot->GetGroup())
                options.push_back({AmbientSeed::Group, 12, "", ""});

            if (!className.empty())
                options.push_back({AmbientSeed::Class, 10, className, ""});

            options.push_back({AmbientSeed::Life, capital ? 8 : 5, "", ""});
            options.push_back({AmbientSeed::Silence, 16, "", ""});
        }

        uint32 total = 0;
        for (const auto& opt : options)
            total += opt.weight;
        uint32 roll = urand(0, total - 1);
        uint32 acc = 0;
        for (const auto& opt : options)
        {
            acc += opt.weight;
            if (roll < acc)
            {
                pick.seed = opt.seed;
                pick.fact = opt.fact;
                pick.instruction = opt.instruction;
                break;
            }
        }

        if (pick.seed == AmbientSeed::Life)
            pick.canned = PickAmbientLifeLine(threadKey);
        else if (pick.seed == AmbientSeed::City)
            pick.canned = PickAmbientCityLine(pick.fact, threadKey);
        else if (pick.seed == AmbientSeed::Zone)
            pick.canned = PickAmbientZoneLine(pick.fact, threadKey);
        else if (pick.seed == AmbientSeed::Quest)
            pick.canned = PickAmbientQuestLine(pick.fact, threadKey);
        else if (pick.seed == AmbientSeed::Group)
            pick.canned = PickAmbientGroupLine(inGuild, pick.fact, threadKey);
        else if (pick.seed == AmbientSeed::Class)
            pick.canned = PickAmbientClassLine(pick.fact, threadKey);
        else if (pick.seed == AmbientSeed::Dungeon)
            pick.canned = PickAmbientDungeonLine(pick.fact, threadKey);

        if (pick.seed != AmbientSeed::Silence && pick.seed != AmbientSeed::Continue && pick.canned.empty())
            pick.seed = AmbientSeed::Silence;

        return pick;
    }
}

void BotChatRandom::OnUpdate(uint32 diff)
{
    if (!g_Enable)
        return;

    if (!g_EnableRandomChatter)
        return;

    static uint32_t timer = 0;
    if (timer <= diff)
    {
        timer = 30000;
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] RandomChatter tick fired (HandleRandomChatter called)");
        HandleRandomChatter();
    }
    else
    {
        timer -= diff;
    }
}

void BotChatRandom::HandleRandomChatter()
{
    uint32 guildAmbientThisTick = 0;
    auto const& allPlayers = ObjectAccessor::GetPlayers();

    std::vector<Player*> realPlayers;
    for (auto const& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player->IsInWorld()) continue;
        if (!PlayerbotsMgr::instance().GetPlayerbotAI(player))
            realPlayers.push_back(player);
    }

    std::unordered_set<uint64_t> processedBotsThisTick;

    for (auto const& itr : allPlayers)
    {
        Player* bot = itr.second;
        PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!ai) continue;
        if (!bot->IsInWorld() || bot->IsBeingTeleported()) continue;
        if (processedBotsThisTick.count(bot->GetGUID().GetRawValue())) continue;

        // If bot is in a guild, check if any real player from their guild is online
        Guild* guild = bot->GetGuild();
        bool hasRealPlayerInGuild = GuildHasRealPlayer(guild);

        // For non-guild random chatter, require proximity to a real player
        bool nearRealPlayer = false;
        for (Player* realPlayer : realPlayers)
        {
            if (bot->GetDistance(realPlayer) <= g_RandomChatterRealPlayerDistance)
            {
                nearRealPlayer = true;
                break;
            }
        }

        // Guild bots with real guild members online can bypass proximity requirement
        bool allowWithoutProximity = guild && hasRealPlayerInGuild;
        if (!allowWithoutProximity && !nearRealPlayer)
            continue;

        uint64_t guid = bot->GetGUID().GetRawValue();
        processedBotsThisTick.insert(guid);

            time_t now = time(nullptr);

            if (nextRandomChatTime.find(guid) == nextRandomChatTime.end())
            {
                nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
                continue;
            }

            if (now < nextRandomChatTime[guid])
                continue;

            // Guild-routed lines use GuildRandomChatterChance instead of the
            // general comment chance. Never stack both, and do not reset the
            // per-bot timer on a failed roll (retry next tick).
            bool const inGuild = hasRealPlayerInGuild && g_EnableGuildRandomAmbientChatter;
            uint32_t const talkChance = inGuild ? g_GuildRandomChatterChance : g_RandomChatterBotCommentChance;
            if (urand(0, 99) >= talkChance)
                continue;

            ChatChannelSourceLocal ambientSource = SRC_SAY_LOCAL;
            Channel* ambientChannel = nullptr;
            std::string ambientThreadKey = GuessAmbientThreadKey(bot, ambientSource, ambientChannel);
            bool threadActive = g_EnableChannelThreads && ThreadIsActive(ambientThreadKey, g_TopicIdleSeconds);

            if (inGuild)
            {
                ambientSource = SRC_GUILD_LOCAL;
                ambientChannel = nullptr;
                ambientThreadKey = MakeThreadKey(bot, SRC_GUILD_LOCAL, nullptr);
                threadActive = g_EnableChannelThreads && ThreadIsActive(ambientThreadKey, g_TopicIdleSeconds);
            }

            if (g_PreferThreadRegulars && threadActive &&
                !IsRecentSpeaker(ambientThreadKey, guid) &&
                urand(0, 99) < 65)
            {
                continue;
            }

            AmbientPick ambient = PickAmbientSeed(bot, ambientThreadKey, threadActive, inGuild);
            if (ambient.seed == AmbientSeed::Silence || ambient.canned.empty())
            {
                nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
                continue;
            }

            bool continueTopic = (ambient.seed == AmbientSeed::Continue);
            bool isGuildComment = inGuild &&
                (ambient.seed == AmbientSeed::Life || ambient.seed == AmbientSeed::Group ||
                 (continueTopic && ambientSource == SRC_GUILD_LOCAL));

            if (isGuildComment)
            {
                if (guildAmbientThisTick >= 1)
                {
                    nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
                    continue;
                }
                if (Guild* botGuild = bot->GetGuild())
                {
                    uint32 const guildId = botGuild->GetId();
                    // Keep the guild-wide gap shorter than TopicIdleSeconds so
                    // continue-topic can still fire after "going in" / "lf1m".
                    uint32 gap = g_MinRandomInterval ? g_MinRandomInterval : 45;
                    if (g_TopicIdleSeconds && gap >= g_TopicIdleSeconds)
                        gap = g_TopicIdleSeconds / 2;
                    auto const it = g_LastGuildAmbientAt.find(guildId);
                    if (it != g_LastGuildAmbientAt.end() && difftime(now, it->second) < gap)
                    {
                        nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
                        continue;
                    }
                    g_LastGuildAmbientAt[guildId] = now;
                }
                ++guildAmbientThisTick;
            }

            if (!continueTopic)
                MarkTopicOpened(ambientThreadKey);

            std::vector<std::string> candidateComments;
            std::vector<std::string> guildComments;
            if (false) { // legacy environment lottery disabled — seeds use live bot state instead

            // Creature
            {
                Unit* unitInRange = nullptr;
                Acore::AnyUnitInObjectRangeCheck creatureCheck(bot, g_SayDistance);
                Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> creatureSearcher(bot, unitInRange, creatureCheck);
                Cell::VisitObjects(bot, creatureSearcher, g_SayDistance);
                if (unitInRange && unitInRange->GetTypeId() == TYPEID_UNIT)
                    if (!g_EnvCommentCreature.empty()) {
                        uint32_t idx = g_EnvCommentCreature.size() == 1 ? 0 : urand(0, g_EnvCommentCreature.size() - 1);
                        std::string templ = g_EnvCommentCreature[idx];
                        candidateComments.push_back(SafeFormat(templ, fmt::arg("creature_name", unitInRange->ToCreature()->GetName())));
                    }
            }

            // Game Object
            {
                Acore::GameObjectInRangeCheck goCheck(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), g_SayDistance);
                GameObject* goInRange = nullptr;
                Acore::GameObjectSearcher<Acore::GameObjectInRangeCheck> goSearcher(bot, goInRange, goCheck);
                Cell::VisitObjects(bot, goSearcher, g_SayDistance);
                if (goInRange)
                {
                    if (!g_EnvCommentGameObject.empty()) {
                        uint32_t idx = g_EnvCommentGameObject.size() == 1 ? 0 : urand(0, g_EnvCommentGameObject.size() - 1);
                        std::string templ = g_EnvCommentGameObject[idx];
                        std::string gameObjectName = goInRange->GetName();
                        candidateComments.push_back(SafeFormat(templ, fmt::arg("object_name", gameObjectName)));
                    }
                }
            }

            // Equipped Item
            {
                std::vector<Item*> equippedItems;
                for (uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                    if (Item* item = bot->GetItemByPos(slot))
                        equippedItems.push_back(item);

                if (!equippedItems.empty())
                {
                    uint32_t eqIdx = equippedItems.size() == 1 ? 0 : urand(0, equippedItems.size() - 1);
                    Item* randomEquipped = equippedItems[eqIdx];
                    if (!g_EnvCommentEquippedItem.empty()) {
                        uint32_t tempIdx = g_EnvCommentEquippedItem.size() == 1 ? 0 : urand(0, g_EnvCommentEquippedItem.size() - 1);
                        std::string templ = g_EnvCommentEquippedItem[tempIdx];
                        candidateComments.push_back(SafeFormat(templ, fmt::arg("item_name", randomEquipped->GetTemplate()->Name1)));
                    }
                }
            }

            // Spell
            {
                struct NamedSpell
                {
                    uint32 id;
                    std::string name;
                    std::string effect;
                    std::string cost;
                };
                std::vector<NamedSpell> validSpells;
                for (const auto& spellPair : bot->GetSpellMap())
                {
                    uint32 spellId = spellPair.first;
                    const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                    if (!spellInfo) continue;
                    if (spellInfo->Attributes & SPELL_ATTR0_PASSIVE)
                        continue;
                    if (spellInfo->SpellFamilyName == SPELLFAMILY_GENERIC)
                        continue;
                    if (bot->HasSpellCooldown(spellId))
                        continue;

                    std::string effectText;
                    for (int i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    {
                        if (!spellInfo->Effects[i].IsEffect())
                            continue;
                        switch (spellInfo->Effects[i].Effect)
                        {
                            case SPELL_EFFECT_SCHOOL_DAMAGE: effectText = "Deals damage"; break;
                            case SPELL_EFFECT_HEAL: effectText = "Heals the target"; break;
                            case SPELL_EFFECT_APPLY_AURA: effectText = "Applies an effect"; break;
                            case SPELL_EFFECT_DISPEL: effectText = "Dispels magic"; break;
                            case SPELL_EFFECT_THREAT: effectText = "Generates threat"; break;
                            default: continue;
                        }
                        if (!effectText.empty())
                            break;
                    }
                    if (effectText.empty())
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

                    validSpells.push_back({spellId, name, effectText, costText});
                }

                if (!validSpells.empty())
                {
                    uint32_t spellIdx = validSpells.size() == 1 ? 0 : urand(0, validSpells.size() - 1);
                    const NamedSpell& randomSpell = validSpells[spellIdx];
                    if (!g_EnvCommentSpell.empty())
                    {
                        uint32_t tempIdx = g_EnvCommentSpell.size() == 1 ? 0 : urand(0, g_EnvCommentSpell.size() - 1);
                        std::string templ = g_EnvCommentSpell[tempIdx];
                        candidateComments.push_back(SafeFormat(
                            templ,
                            fmt::arg("spell_name", randomSpell.name),
                            fmt::arg("spell_effect", randomSpell.effect),
                            fmt::arg("spell_cost", randomSpell.cost)
                        ));
                    }
                }
            }

            // Quest Area
            {
                std::vector<std::string> questAreas;
                for (auto const& qkv : sObjectMgr->GetQuestTemplates())
                {
                    Quest const* qt = qkv.second;
                    if (!qt) continue;
                    int32 qlevel = qt->GetQuestLevel();
                    int32 plevel = bot->GetLevel();
                    if (qlevel < plevel - 2 || qlevel > plevel + 2)
                        continue;
                    uint32 zone = qt->GetZoneOrSort();
                    if (zone == 0) continue;
                    if (auto const* area = sAreaTableStore.LookupEntry(zone))
                    {
                        if (!g_EnvCommentQuestArea.empty()) {
                            uint32_t idx = g_EnvCommentQuestArea.size() == 1 ? 0 : urand(0, g_EnvCommentQuestArea.size() - 1);
                            std::string templ = g_EnvCommentQuestArea[idx];
                            questAreas.push_back(SafeFormat(templ, fmt::arg("quest_area", area->area_name[LocaleConstant::LOCALE_enUS])));
                        }
                    }
                }
                if (!questAreas.empty())
                {
                    uint32_t qIdx = questAreas.size() == 1 ? 0 : urand(0, questAreas.size() - 1);
                    candidateComments.push_back(questAreas[qIdx]);
                }
            }

            // Vendor
            {
                Unit* unit = nullptr;
                Acore::AnyUnitInObjectRangeCheck check(bot, g_SayDistance);
                Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, unit, check);
                Cell::VisitObjects(bot, searcher, g_SayDistance);

                if (unit && unit->GetTypeId() == TYPEID_UNIT)
                {
                    Creature* vendor = unit->ToCreature();
                    if (vendor->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
                    {
                        if (!g_EnvCommentVendor.empty()) {
                            uint32_t idx = g_EnvCommentVendor.size() == 1 ? 0 : urand(0, g_EnvCommentVendor.size() - 1);
                            std::string templ = g_EnvCommentVendor[idx];
                            candidateComments.push_back(SafeFormat(templ, fmt::arg("vendor_name", vendor->GetName())));
                        }
                    }
                }
            }

            // Questgiver
            {
                Unit* unit = nullptr;
                Acore::AnyUnitInObjectRangeCheck check(bot, g_SayDistance);
                Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, unit, check);
                Cell::VisitObjects(bot, searcher, g_SayDistance);

                if (unit && unit->GetTypeId() == TYPEID_UNIT)
                {
                    Creature* giver = unit->ToCreature();
                    if (giver->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                    {
                        auto bounds = sObjectMgr->GetCreatureQuestRelationBounds(giver->GetEntry());
                        int n       = std::distance(bounds.first, bounds.second);
                        if (!g_EnvCommentQuestgiver.empty()) {
                            uint32_t idx = g_EnvCommentQuestgiver.size() == 1 ? 0 : urand(0, g_EnvCommentQuestgiver.size() - 1);
                            std::string templ = g_EnvCommentQuestgiver[idx];
                            candidateComments.push_back(SafeFormat(templ,
                                fmt::arg("questgiver_name", giver->GetName()),
                                fmt::arg("quest_count", n)
                            ));
                        }
                    }
                }
            }

            // Free bag slots
            {
                int freeSlots = 0;
                for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                    if (!bot->GetItemByPos(i))
                        ++freeSlots;
                for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
                    if (Bag* bag = bot->GetBagByPos(b))
                        freeSlots += bag->GetFreeSlots();

                if (!g_EnvCommentBagSlots.empty()) {
                    uint32_t idx = g_EnvCommentBagSlots.size() == 1 ? 0 : urand(0, g_EnvCommentBagSlots.size() - 1);
                    std::string templ = g_EnvCommentBagSlots[idx];
                    candidateComments.push_back(SafeFormat(templ, fmt::arg("bag_slots", freeSlots)));
                }
            }

            // Dungeon
            {
                if (bot->GetMap() && bot->GetMap()->IsDungeon())
                {
                    std::string name = bot->GetMap()->GetMapName();
                    if (!g_EnvCommentDungeon.empty()) {
                        uint32_t idx = g_EnvCommentDungeon.size() == 1 ? 0 : urand(0, g_EnvCommentDungeon.size() - 1);
                        std::string templ = g_EnvCommentDungeon[idx];
                        candidateComments.push_back(SafeFormat(templ, fmt::arg("dungeon_name", name)));
                    }
                }
            }

            // Unfinished Quest
            {
                std::vector<std::string> unfinished;
                for (auto const& qs : bot->getQuestStatusMap())
                {
                    if (qs.second.Status == QUEST_STATUS_INCOMPLETE)
                    {
                        if (auto* qt = sObjectMgr->GetQuestTemplate(qs.first))
                            if (!g_EnvCommentUnfinishedQuest.empty()) {
                                uint32_t idx = g_EnvCommentUnfinishedQuest.size() == 1 ? 0 : urand(0, g_EnvCommentUnfinishedQuest.size() - 1);
                                std::string templ = g_EnvCommentUnfinishedQuest[idx];
                                unfinished.push_back(SafeFormat(templ, fmt::arg("quest_name", qt->GetTitle())));
                            }
                    }
                }
                if (!unfinished.empty())
                {
                    uint32_t uIdx = unfinished.size() == 1 ? 0 : urand(0, unfinished.size() - 1);
                    candidateComments.push_back(unfinished[uIdx]);
                }
            }

            // Guild-specific environment comments (if bot is in a guild with real players)
            if (g_EnableGuildRandomAmbientChatter && bot->GetGuild())
            {
                // Check if there are real players in the guild
                bool hasRealPlayerInGuild = false;
                Guild* guild = bot->GetGuild();
                for (auto const& pair : ObjectAccessor::GetPlayers())
                {
                    Player* player = pair.second;
                    if (!player || !player->IsInWorld())
                        continue;
                    if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                        continue;
                    if (player->GetGuild() && player->GetGuild()->GetId() == guild->GetId())
                    {
                        hasRealPlayerInGuild = true;
                        break;
                    }
                }
                if (hasRealPlayerInGuild && urand(0, 99) < g_GuildRandomChatterChance)
                {
                    // Guild member comments
                    if (!g_GuildEnvCommentGuildMember.empty())
                    {
                        std::string memberName = bot->GetName();
                        if (!memberName.empty())
                        {
                            uint32_t idx = urand(0, g_GuildEnvCommentGuildMember.size() - 1);
                            std::string templ = g_GuildEnvCommentGuildMember[idx];
                            guildComments.push_back(SafeFormat(templ, fmt::arg("member_name", memberName)));
                        }
                    }
                    // Guild MOTD comments
                    if (!g_GuildEnvCommentGuildMOTD.empty() && !guild->GetMOTD().empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildMOTD.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildMOTD[idx];
                        guildComments.push_back(SafeFormat(templ, fmt::arg("guild_motd", guild->GetMOTD())));
                    }
                    // Guild bank comments
                    if (!g_GuildEnvCommentGuildBank.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildBank.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildBank[idx];
                        guildComments.push_back(SafeFormat(templ, 
                            fmt::arg("bank_gold", guild->GetTotalBankMoney() / 10000)));
                    }
                    // Guild raid comments
                    if (!g_GuildEnvCommentGuildRaid.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildRaid.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildRaid[idx];
                        guildComments.push_back(templ);
                    }
                    // Guild endgame comments
                    if (!g_GuildEnvCommentGuildEndgame.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildEndgame.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildEndgame[idx];
                        guildComments.push_back(templ);
                    }
                    // Guild strategy comments
                    if (!g_GuildEnvCommentGuildStrategy.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildStrategy.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildStrategy[idx];
                        guildComments.push_back(templ);
                    }
                    // Guild group/quest/grind comments
                    if (!g_GuildEnvCommentGuildGroup.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildGroup.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildGroup[idx];
                        guildComments.push_back(templ);
                    }
                    // Guild PvP comments
                    if (!g_GuildEnvCommentGuildPvP.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildPvP.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildPvP[idx];
                        guildComments.push_back(templ);
                    }
                    // Guild community/social comments
                    if (!g_GuildEnvCommentGuildCommunity.empty())
                    {
                        uint32_t idx = urand(0, g_GuildEnvCommentGuildCommunity.size() - 1);
                        std::string templ = g_GuildEnvCommentGuildCommunity[idx];
                        guildComments.push_back(templ);
                    }
                    candidateComments.insert(candidateComments.end(), guildComments.begin(), guildComments.end());
                }
            }
            } // end disabled legacy environment lottery

            std::string prompt;
            if (ambient.canned.empty())
            {
            std::string environmentInfo = ambient.fact;
            std::string seedInstruction = ambient.instruction;
            std::string channelThreadText = FormatChannelThread(ambientThreadKey, bot->GetName());

            prompt = [bot, &environmentInfo, &channelThreadText, continueTopic, &seedInstruction]()
            {
                PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
                if (!botAI)
                    return std::string("Error, no bot AI");

                std::string personality         = GetBotPersonality(bot);
                std::string personalityPrompt   = GetPersonalityPromptAddition(personality);
                std::string botName             = bot->GetName();
                uint32_t botLevel               = bot->GetLevel();
                std::string botClass            = CleanPromptText(botAI->GetChatHelper()->FormatClass(bot->getClass()));
                std::string botRace             = CleanPromptText(botAI->GetChatHelper()->FormatRace(bot->getRace()));
                std::string botRole             = CleanPromptText(ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot)));
                std::string botGender           = (bot->getGender() == 0 ? "Male" : "Female");
                std::string botFaction          = (bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");

                AreaTableEntry const* botCurrentArea = botAI->GetCurrentArea();
                AreaTableEntry const* botCurrentZone = botAI->GetCurrentZone();
                std::string botAreaName = botCurrentArea ? botAI->GetLocalizedAreaName(botCurrentArea) : "UnknownArea";
                std::string botZoneName = botCurrentZone ? botAI->GetLocalizedAreaName(botCurrentZone) : "UnknownZone";
                std::string botMapName  = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";

                std::string prompt = SafeFormat(
                    g_RandomChatterPromptTemplate,
                    fmt::arg("bot_name", botName),
                    fmt::arg("bot_level", botLevel),
                    fmt::arg("bot_class", botClass),
                    fmt::arg("bot_race", botRace),
                    fmt::arg("bot_gender", botGender),
                    fmt::arg("bot_role", botRole),
                    fmt::arg("bot_faction", botFaction),
                    fmt::arg("bot_area", botAreaName),
                    fmt::arg("bot_zone", botZoneName),
                    fmt::arg("bot_map", botMapName),
                    fmt::arg("bot_personality", personalityPrompt),
                    fmt::arg("bot_personality_name", personality),
                    fmt::arg("environment_info", continueTopic ? std::string() : environmentInfo),
                    fmt::arg("channel_thread", channelThreadText)
                );

                if (!seedInstruction.empty())
                    prompt += " " + seedInstruction;
                else if (continueTopic)
                    prompt += " Continue the recent channel chat with one short line.";

                return prompt;

            }();
            }

            // Pre-validate that we have a valid destination channel before making API call
            bool hasValidDestination = false;
            
            if (isGuildComment && bot->GetGuild())
            {
                // Check if guild message can be sent
                if (!g_DisableForGuild)
                {
                    // Check if there are real players in the guild
                    Guild* guild = bot->GetGuild();
                    for (auto const& pair : ObjectAccessor::GetPlayers())
                    {
                        Player* player = pair.second;
                        if (!player || !player->IsInWorld())
                            continue;
                        if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                            continue;
                        if (player->GetGuild() && player->GetGuild()->GetId() == guild->GetId())
                        {
                            hasValidDestination = true;
                            break;
                        }
                    }
                }
            }
            else if (bot->GetGroup() && GroupHasRealPlayer(bot->GetGroup()))
            {
                // Check if party message can be sent
                if (!g_DisableForParty)
                {
                    hasValidDestination = true;
                }
            }
            else
            {
                // For non-party, non-guild-comment messages, check if real player can hear
                bool realPlayerInSayDistance = false;
                bool realPlayerInGeneral = false;
                
                // Check Say distance
                if (!g_DisableForSayYell && bot->IsInWorld())
                {
                    for (auto const& pair : ObjectAccessor::GetPlayers())
                    {
                        Player* player = pair.second;
                        if (!player || !player->IsInWorld())
                            continue;
                        if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                            continue;
                        if (bot->GetDistance(player) <= g_SayDistance)
                        {
                            realPlayerInSayDistance = true;
                            break;
                        }
                    }
                }
                
                // Check General channel - verify real player is in same zone/faction (General is zone-based)
                if (!g_DisableForCustomChannels)
                {
                    for (auto const& pair : ObjectAccessor::GetPlayers())
                    {
                        Player* player = pair.second;
                        if (!player || !player->IsInWorld())
                            continue;
                        if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                            continue;
                        // General channel is faction and zone specific
                        if (player->GetTeamId() == bot->GetTeamId() && 
                            player->GetZoneId() == bot->GetZoneId())
                        {
                            realPlayerInGeneral = true;
                            break;
                        }
                    }
                }
                
                hasValidDestination = realPlayerInSayDistance || realPlayerInGeneral;
            }
            
            if (!hasValidDestination)
            {
                if (g_DebugEnabled)
                    LOG_INFO("server.loading", "[Bot Chat] Bot {} skipping random chatter (no real player can hear the message)", bot->GetName());
                nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
                continue;
            }

            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "[Bot Chat] Random Message Prompt: {} ", prompt);
            }

            uint64_t botGuid = bot->GetGUID().GetRawValue();
            ChatChannelSourceLocal continueSource = continueTopic ? ambientSource : SRC_UNDEFINED_LOCAL;
            std::string continueChannelName = (ambientChannel ? ambientChannel->GetName() : std::string());
            std::string cannedLine = ambient.canned;
            std::string threadKey = ambientThreadKey;

            std::thread([botGuid, prompt, cannedLine, threadKey, isGuildComment,
                         continueSource, continueChannelName]() {
                try {
                    Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                    if (!botPtr) return;

                    std::string response = cannedLine;
                    if (response.empty() || LineTooSimilarToRecent(threadKey, response))
                    {
                        if (g_DebugEnabled)
                            LOG_INFO("server.loading",
                                "[BotChat] Bot skipped random chatter (empty, slang salad, or repeat)");
                        return;
                    }
                    
                    botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                    if (!botPtr) return;
                    PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(botPtr);
                    if (!botAI) return;
                    
                    // Simulate typing delay if enabled
                    if (g_EnableTypingSimulation)
                    {
                        uint32_t delay = g_TypingSimulationBaseDelay + (response.length() * g_TypingSimulationDelayPerChar);
                        if (g_DebugEnabled)
                            LOG_INFO("server.loading", "[BotChat] Bot simulating typing delay: {}ms for {} characters", 
                                     delay, response.length());
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                        
                        // Reacquire pointers after delay
                        botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                        if (!botPtr) return;
                        botAI = PlayerbotsMgr::instance().GetPlayerbotAI(botPtr);
                        if (!botAI) return;
                    }
                    
                    auto sendAndTrigger = [&](ChatChannelSourceLocal source, Channel* channel) {
                        ProcessBotChatMessage(botPtr, response, source, channel);
                    };

                    if (continueSource != SRC_UNDEFINED_LOCAL)
                    {
                        if (continueSource == SRC_GUILD_LOCAL && botPtr->GetGuild() &&
                            !g_DisableForGuild && GuildHasRealPlayer(botPtr->GetGuild()))
                        {
                            botAI->SayToGuild(response);
                            sendAndTrigger(SRC_GUILD_LOCAL, nullptr);
                            return;
                        }
                        if ((continueSource == SRC_PARTY_LOCAL || continueSource == SRC_RAID_LOCAL) &&
                            botPtr->GetGroup() && !g_DisableForParty && GroupHasRealPlayer(botPtr->GetGroup()))
                        {
                            if (continueSource == SRC_RAID_LOCAL)
                                botAI->SayToRaid(response);
                            else
                                botAI->SayToParty(response);
                            sendAndTrigger(continueSource, nullptr);
                            return;
                        }
                        if (continueSource == SRC_GENERAL_LOCAL && !g_DisableForCustomChannels)
                        {
                            Channel* generalChannel = nullptr;
                            if (ChannelMgr* cMgr = ChannelMgr::forTeam(botPtr->GetTeamId()))
                                generalChannel = cMgr->GetChannel(continueChannelName.empty() ? "General" : continueChannelName, botPtr);
                            if (botAI->SayToChannel(response, ChatChannelId::GENERAL))
                            {
                                sendAndTrigger(SRC_GENERAL_LOCAL, generalChannel);
                                return;
                            }
                        }
                        if (continueSource == SRC_SAY_LOCAL && !g_DisableForSayYell)
                        {
                            botAI->Say(response);
                            sendAndTrigger(SRC_SAY_LOCAL, nullptr);
                            return;
                        }
                    }

                    // Guild-based random chatter goes to guild chat
                    if (isGuildComment && botPtr->GetGuild())
                    {
                        // Check if guild chat is disabled
                        if (g_DisableForGuild)
                        {
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Guild random chatter skipped (guild channels disabled)");
                            return;
                        }
                        
                        // Verify there are still real players in the guild
                        bool hasRealPlayerInGuild = false;
                        Guild* guild = botPtr->GetGuild();
                        for (auto const& pair : ObjectAccessor::GetPlayers())
                        {
                            Player* player = pair.second;
                            if (!player || !player->IsInWorld())
                                continue;
                            if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                                continue;
                            if (player->GetGuild() && player->GetGuild()->GetId() == guild->GetId())
                            {
                                hasRealPlayerInGuild = true;
                                break;
                            }
                        }
                        
                        if (hasRealPlayerInGuild)
                        {
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Bot Guild-Based Random Chatter: {}", response);
                            botAI->SayToGuild(response);
                            ProcessBotChatMessage(botPtr, response, SRC_GUILD_LOCAL, nullptr);
                        }
                        else if (g_DebugEnabled)
                        {
                            LOG_INFO("server.loading", "[Bot Chat] Bot {} skipping guild random chatter (no real players in guild anymore)", botPtr->GetName());
                        }
                    }
                    else if (botPtr->GetGroup() && GroupHasRealPlayer(botPtr->GetGroup()))
                    {
                        // Check if party chat is disabled
                        if (g_DisableForParty)
                        {
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Party random chatter skipped (party channels disabled)");
                            return;
                        }
                        
                        if (g_DebugEnabled)
                            LOG_INFO("server.loading", "[Bot Chat] Bot Random Chatter Party: {}", response);
                        botAI->SayToParty(response);
                        ProcessBotChatMessage(botPtr, response, SRC_PARTY_LOCAL, nullptr);
                    }
                    else
                    {
                        // For bots not in a party, check if any real player is within Say distance
                        bool realPlayerInSayDistance = false;
                        if (botPtr->IsInWorld())
                        {
                            for (auto const& pair : ObjectAccessor::GetPlayers())
                            {
                                Player* player = pair.second;
                                if (!player || !player->IsInWorld())
                                    continue;
                                    
                                if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                                    continue;
                                    
                                if (botPtr->GetDistance(player) <= g_SayDistance)
                                {
                                    realPlayerInSayDistance = true;
                                    break;
                                }
                            }
                        }
                        
                        // Build channel list - only include channels with real players
                        std::vector<std::string> channels;
                        
                        // Check if any real player is in the General channel (same zone and faction)
                        bool realPlayerInGeneral = false;
                        if (!g_DisableForCustomChannels)
                        {
                            for (auto const& pair : ObjectAccessor::GetPlayers())
                            {
                                Player* player = pair.second;
                                if (!player || !player->IsInWorld())
                                    continue;
                                if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                                    continue;
                                // General channel is faction and zone specific
                                if (player->GetTeamId() == botPtr->GetTeamId() && 
                                    player->GetZoneId() == botPtr->GetZoneId())
                                {
                                    realPlayerInGeneral = true;
                                    break;
                                }
                            }
                            
                            if (realPlayerInGeneral)
                            {
                                channels.push_back("General");
                                if (g_DebugEnabled)
                                    LOG_INFO("server.loading", "[Bot Chat] Bot {} adding General to random chatter options (real player in channel)", botPtr->GetName());
                            }
                            else if (g_DebugEnabled)
                            {
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} NOT adding General to random chatter (no real player in channel)", botPtr->GetName());
                            }
                        }
                        
                        // Only add Say if not disabled and real player is close enough
                        if (!g_DisableForSayYell && realPlayerInSayDistance)
                        {
                            channels.push_back("Say");
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} adding Say to random chatter options (real player within {} yards)", botPtr->GetName(), g_SayDistance);
                        }
                        else if (g_DebugEnabled)
                        {
                            if (g_DisableForSayYell)
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} NOT adding Say to random chatter (Say/Yell disabled)", botPtr->GetName());
                            else
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} NOT adding Say to random chatter (no real player within {} yards)", botPtr->GetName(), g_SayDistance);
                        }
                        
                        // If no channels are available, skip random chatter
                        if (channels.empty())
                        {
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} skipping random chatter (all available channels disabled)", botPtr->GetName());
                            return;
                        }
                        
                        // Pick random channel
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        std::uniform_int_distribution<size_t> dist(0, channels.size() - 1);
                        std::string selectedChannel = channels[dist(gen)];
                        
                        if (selectedChannel == "Say") {
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} Random Chatter Say (real player within {} yards): {}", botPtr->GetName(), g_SayDistance, response);
                            botAI->Say(response);
                            ProcessBotChatMessage(botPtr, response, SRC_SAY_LOCAL, nullptr);
                        } else if (selectedChannel == "General") {
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} Random Chatter General: {}", botPtr->GetName(), response);
                            
                            Channel* generalChannel = FindPlayerChannel(botPtr, "General");
                            
                            // Use playerbots' SayToChannel method - it handles channel lookup internally
                            bool sent = botAI->SayToChannel(response, ChatChannelId::GENERAL);
                            if (g_DebugEnabled)
                                LOG_INFO("server.loading", "[Bot Chat] Bot {} SayToChannel result: {}", botPtr->GetName(), sent ? "success" : "failed, using Say fallback");
                            
                            if (sent && generalChannel)
                            {
                                ProcessBotChatMessage(botPtr, response, SRC_GENERAL_LOCAL, generalChannel);
                            }
                            else if (sent)
                            {
                                AppendChannelThread(MakeThreadKey(botPtr, SRC_GENERAL_LOCAL, generalChannel),
                                    botPtr->GetName(), botPtr->GetGUID().GetRawValue(), true, response);
                            }
                            
                            if (!sent)
                            {
                                // Fallback to Say if channel message failed (and real player is close enough)
                                if (realPlayerInSayDistance)
                                {
                                    botAI->Say(response);
                                    ProcessBotChatMessage(botPtr, response, SRC_SAY_LOCAL, nullptr);
                                }
                                else if (g_DebugEnabled)
                                {
                                    LOG_INFO("server.loading", "[Bot Chat] Bot {} cannot send to General and no real player in Say range, message lost", botPtr->GetName());
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("server.loading", "[Bot Chat] Exception in random chatter thread: {}", e.what());
                } catch (...) {
                    LOG_ERROR("server.loading", "[Bot Chat] Unknown exception in random chatter thread");
                }
            }).detach();


            nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
    }
}
