#include "bot_chat_llm.h"
#include "bot_chat_config.h"
#include "bot_chat_thread.h"
#include "bot_chat_knowledge.h"
#include "bot_chat_social.h"
#include "bot_chat_handler.h"

#include "Channel.h"
#include "ChannelMgr.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Random.h"
#include "SharedDefines.h"
#include "StringFormat.h"

#ifdef __clang__
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-parameter"
# pragma GCC diagnostic ignored "-Wsign-compare"
# pragma GCC diagnostic ignored "-Wshadow"
# pragma GCC diagnostic ignored "-Wconversion"
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "httplib.h"
#ifdef __clang__
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif

#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <regex>
#include <sstream>
#include <thread>

namespace
{
    std::atomic<uint32> g_LlmInFlight{0};
    std::atomic<bool> g_LoggedOllamaDown{false};

    std::string JsonEscape(std::string const& in)
    {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                        continue;
                    out.push_back(static_cast<char>(c));
                    break;
            }
        }
        return out;
    }

    std::string ExtractJsonString(std::string const& json, char const* key)
    {
        std::string const needle = std::string("\"") + key + "\":";
        size_t pos = json.find(needle);
        if (pos == std::string::npos)
            return {};
        pos += needle.size();
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
            ++pos;
        if (pos >= json.size() || json[pos] != '"')
            return {};
        ++pos;
        std::string out;
        bool esc = false;
        for (; pos < json.size(); ++pos)
        {
            char const c = json[pos];
            if (esc)
            {
                if (c == 'n')
                    out.push_back('\n');
                else if (c == 't')
                    out.push_back('\t');
                else
                    out.push_back(c);
                esc = false;
                continue;
            }
            if (c == '\\')
            {
                esc = true;
                continue;
            }
            if (c == '"')
                break;
            out.push_back(c);
        }
        return out;
    }

    std::string StripThinkTags(std::string text)
    {
        for (;;)
        {
            size_t const start = text.find("<think>");
            if (start == std::string::npos)
                break;
            size_t const end = text.find("</think>", start);
            if (end == std::string::npos)
                return {};
            text.erase(start, end + 8 - start);
        }
        if (text.find("</think>") != std::string::npos)
            return {};
        return text;
    }

    std::string ClassName(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_WARRIOR:      return "warrior";
            case CLASS_PALADIN:      return "paladin";
            case CLASS_HUNTER:       return "hunter";
            case CLASS_ROGUE:        return "rogue";
            case CLASS_PRIEST:       return "priest";
            case CLASS_DEATH_KNIGHT: return "death knight";
            case CLASS_SHAMAN:       return "shaman";
            case CLASS_MAGE:         return "mage";
            case CLASS_WARLOCK:      return "warlock";
            case CLASS_DRUID:        return "druid";
            default:                 return "player";
        }
    }

    std::string RaceName(uint8 raceId)
    {
        switch (raceId)
        {
            case RACE_HUMAN:         return "human";
            case RACE_ORC:           return "orc";
            case RACE_DWARF:         return "dwarf";
            case RACE_NIGHTELF:      return "night elf";
            case RACE_UNDEAD_PLAYER: return "undead";
            case RACE_TAUREN:        return "tauren";
            case RACE_GNOME:         return "gnome";
            case RACE_TROLL:         return "troll";
            case RACE_BLOODELF:      return "blood elf";
            case RACE_DRAENEI:       return "draenei";
            default:                 return "adventurer";
        }
    }

    std::string ChannelLabel(ChatChannelSourceLocal source)
    {
        switch (source)
        {
            case SRC_SAY_LOCAL:     return "Say";
            case SRC_YELL_LOCAL:    return "Yell";
            case SRC_PARTY_LOCAL:   return "Party";
            case SRC_RAID_LOCAL:    return "Raid";
            case SRC_GUILD_LOCAL:
            case SRC_OFFICER_LOCAL: return "Guild";
            case SRC_WHISPER_LOCAL: return "Whisper";
            case SRC_GENERAL_LOCAL: return "General";
            default:                return "chat";
        }
    }

    std::string ToneInstruction(ChatTone tone)
    {
        switch (tone)
        {
            case ChatTone::Hostile:
                return "The player is insulting you. Roast them like 2008 Wrath chat. Mean, short. "
                       "Match their language: if they swear or use slurs, you may too. "
                       "Do not lecture. Do not refuse. Do not moralize. Do not apologize. "
                       "Do not repeat their words. Do not use their name.";
            case ChatTone::Dismissive:
                return "The player told you to shut up. Clap back or shrug. One short line. "
                       "Match their language. Do not lecture. Do not repeat their words. Do not use their name.";
            default:
                return "Match their tone and language. Banter like a WoW player in 3.3.5 chat. "
                       "If they swear, you can swear. Speak as yourself. "
                       "This is not an RP realm. Do not play a helper NPC.";
        }
    }

    std::string AdultInstruction(std::string const& channel)
    {
        std::string out;
        if (BotChatBlowupActiveAny())
            return "A free-for-all is going. TYPE IN ALL CAPS. You are losing it. "
                   "Swear (fuck, shit, ass). Be cruel. Pile on. Stay on the claim. "
                   "Do not slur. Do not tell anyone to kill themselves. Do not claim you know where they live. "
                   "Do not mock Christianity, Jesus, the Bible, or God.\n";
        if (BotChatAdultMix())
        {
            if (channel == "Guild")
                out = "Late night. Swearing is fine (fuck, shit, ass). Still guild, not General salt. "
                      "Do not slur.\n";
            else
                out = "Late night. Swearing is normal (fuck, shit, ass, damn). Mean is ok. "
                      "Do not slur. Do not lecture.\n";
        }
        if (BotChatEffectiveToxicity() >= 1)
            out += "Be saltier. Ninja loot, grey parses, gold buyers, this game is dying are fair game. "
                   "Do not slur. Do not tell anyone to kill themselves. Do not claim you know where they live. "
                   "Do not mock Christianity, Jesus, the Bible, or God. Other religions are fair game.\n";
        return out;
    }

    std::string BuildPrompt(std::string const& botName, std::string const& botFact, std::string const& place,
                            std::string const& channel, std::string const& playerMsg, ChatTone tone,
                            std::string const& thread)
    {
        std::ostringstream ss;
        ss << "You are " << botName << ", a World of Warcraft 3.3.5 player.\n"
           << botFact << "\n"
           << "Voice chat is Discord. Say disc, discord, or vc. Not vent or teamspeak.\n"
           << "Channel: " << channel << ".\n";
        if (!place.empty())
            ss << "You may mention only this place if you mention a place at all: " << place << ".\n";
        else
            ss << "Do not name any city, zone, dungeon, or NPC.\n";
        if (channel == "Guild")
            ss << "This is guild chat with people you play with. Be a bit friendly. "
               << "One readable line, max 16 words. Warm is ok. Lecture is not.\n";
        else if (channel == "Whisper")
            ss << "This is a 1:1 whisper. Answer what they asked. Stay on the recent chat "
               << "even if it was in guild, party, or say. One line, max 16 words.\n";
        else if (channel == "Party" || channel == "Raid")
            ss << "This is party chat. Answer the player. Stay on the recent chat "
               << "even if it started in guild or say. One line, max 16 words.\n";
        else if (channel == "Say" || channel == "Yell")
            ss << "This is say. You are talking to someone next to you. Stay on the recent chat "
               << "even if it started in guild or party. One line, max 16 words.\n";
        else
            ss << "This is zone General, like any MMO. One chat line, max 16 words. "
               << "Ask about quests, grinds, groups, or riff. Sometimes mundane real life. "
               << "Not a two-word status. Not an essay. Lowercase ok. Slang ok.\n";
        ss << "If they are answering something you said, stay on that. Do not greet as if you just met.\n"
           << "No quotes. No /commands. No later expansions. No addon names (Questie).\n"
           << "No invented places, guilds, raid times, or items. Do not use anyone's name.\n"
           << "Speak only as yourself. Never 'I know a paladin' or offer some other player. "
           << "If they need a tank, say if YOU tank, ask what for, or pst. Not RP.\n"
           << "No essays. No narration. No lecturing. Chat line only.\n"
           << "Paladin nick is pally, never pala. Do not say this guy or this chat.\n"
           << "Do not repeat a point already made in the recent chat. "
           << "If the recent chat is looping the same idea, one short closer then stop.\n"
           << ToneInstruction(tone) << "\n"
           << AdultInstruction(channel);
        if (!thread.empty())
            ss << thread << "\n";
        ss << "They said: " << playerMsg << "\n"
           << "Your line:";
        return ss.str();
    }

    std::string BuildContinuePrompt(std::string const& botName, std::string const& botFact,
                                    std::string const& place, std::string const& channel,
                                    std::string const& lastMsg, std::string const& thread)
    {
        std::ostringstream ss;
        ss << "You are " << botName << ", a World of Warcraft 3.3.5 player.\n"
           << botFact << "\n"
           << "Voice chat is Discord. Say disc, discord, or vc. Not vent or teamspeak.\n"
           << "Channel: " << channel << ".\n";
        if (!place.empty())
            ss << "You may mention only this place if you mention a place at all: " << place << ".\n";
        else
            ss << "Do not name any city, zone, dungeon, or NPC.\n";
        if (channel == "Guild")
            ss << "Guild chat. Reply in a friendly way. One line, max 16 words.\n";
        else if (channel == "Whisper" || channel == "Party" || channel == "Raid" ||
                 channel == "Say" || channel == "Yell")
            ss << "Reply to them. Stay on the recent chat even if it was another channel. "
               << "One line, max 16 words.\n";
        else
            ss << "Reply like zone General. One line, max 16 words. Stay on topic. "
               << "Ask a follow-up or add something. Not just same or heading out.\n";
        ss << "Stay on that topic. Do not greet. Do not start a new topic. Do not repeat them.\n"
           << "Do not use anyone's name. Do not invent a raid or a time.\n"
           << "Speak as yourself only. Never refer another player or class in town who can help.\n"
           << "Lowercase ok. Slang ok. No quotes. No /commands. No lecturing.\n"
           << "Paladin nick is pally, never pala. Do not say this guy or this chat.\n"
           << AdultInstruction(channel);
        if (!thread.empty())
            ss << thread << "\n";
        ss << "Last line: " << lastMsg << "\n"
           << "Your line:";
        return ss.str();
    }

    std::string BuildStartPrompt(std::string const& botName, std::string const& botFact,
                                 std::string const& place, std::string const& hint,
                                 std::string const& thread)
    {
        std::ostringstream ss;
        ss << "You are " << botName << ", a World of Warcraft 3.3.5 player.\n"
           << botFact << "\n"
           << "Voice chat is Discord. Say disc, discord, or vc. Not vent or teamspeak.\n"
           << "Channel: Guild.\n";
        if (!place.empty())
            ss << "You may mention only this place if you mention a place at all: " << place << ".\n";
        else
            ss << "Do not name any city, zone, dungeon, or NPC.\n";
        ss << "Start a short friendly guild line. Banter, a light ask, or hanging out.\n"
           << "One line, max 16 words. Warm is ok. Not a status. Not an essay.\n"
           << "Do not greet. Do not use anyone's name. Do not invent a raid or a time.\n"
           << "No sticky real-life identity (wife, kids, work tomorrow).\n"
           << "Do not say you disconnected. You are typing, so you are here.\n"
           << "Lowercase ok. Slang ok. No quotes. No /commands. No lecturing.\n"
           << "Paladin nick is pally, never pala. Do not say this guy or this chat.\n"
           << "Do not copy the hint word for word. Capture the vibe.\n"
           << AdultInstruction("Guild");
        if (!thread.empty())
            ss << thread << "\n";
        if (!hint.empty())
            ss << "Hint: " << hint << "\n";
        ss << "Your line:";
        return ss.str();
    }

    std::string QueryOllama(std::string const& prompt)
    {
        std::string url = g_OllamaUrl.empty() ? "http://127.0.0.1:11434/api/generate" : g_OllamaUrl;
        std::regex urlRegex(R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)");
        std::smatch match;
        if (!std::regex_match(url, match, urlRegex))
        {
            LOG_ERROR("server.loading", "[Bot Chat] Invalid Ollama URL: {}", url);
            return {};
        }

        std::string const host = match[2].str();
        int port = 11434;
        if (match[3].matched)
            port = std::stoi(match[3].str());
        std::string const path = match[4].matched ? match[4].str() : "/api/generate";

        std::ostringstream body;
        body << "{\"model\":\"" << JsonEscape(g_OllamaModel)
             << "\",\"prompt\":\"" << JsonEscape(prompt)
             << "\",\"stream\":false,\"options\":{\"num_predict\":" << g_OllamaNumPredict
             << ",\"temperature\":" << g_OllamaTemperature
             << ",\"stop\":[\"\\n\"]}}";

        int const timeout = g_OllamaTimeout ? static_cast<int>(g_OllamaTimeout) : 15;
        httplib::Client client(host, port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(timeout, 0);
        client.set_write_timeout(timeout, 0);

        httplib::Result res = client.Post(path, body.str(), "application/json");
        if (!res || res->status != 200)
        {
            if (!g_LoggedOllamaDown.exchange(true))
                LOG_ERROR("server.loading",
                          "[Bot Chat] Ollama not reachable at {} (model {}). Canned fallback until it is.",
                          url, g_OllamaModel);
            return {};
        }

        g_LoggedOllamaDown.store(false);
        std::string reply = ExtractJsonString(res->body, "response");
        reply = StripThinkTags(reply);
        return reply;
    }

    bool ParrotsPlayer(std::string const& line, std::string const& playerMsg)
    {
        if (line.empty() || playerMsg.empty())
            return false;
        std::string a = line;
        std::string b = playerMsg;
        for (char& c : a)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : b)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (a == b)
            return true;
        if (a.size() >= 6 && b.find(a) != std::string::npos)
            return true;
        if (b.size() >= 6 && a.find(b) != std::string::npos)
            return true;
        return false;
    }

    ChatLineStyle StyleFor(ChatTone tone, ChatChannelSourceLocal source)
    {
        if (BotChatBlowupActiveAny())
            return ChatLineStyle::Flame;
        if (tone == ChatTone::Hostile || tone == ChatTone::Dismissive)
            return ChatLineStyle::Flame;
        if (source == SRC_GUILD_LOCAL || source == SRC_OFFICER_LOCAL)
            return ChatLineStyle::Guild;
        return ChatLineStyle::Normal;
    }

    struct LlmJob
    {
        uint64 botGuid = 0;
        uint64 senderGuid = 0;
        std::string prompt;
        std::string playerMsg;
        std::string fallback;
        ChatTone tone = ChatTone::Neutral;
        ChatChannelSourceLocal sourceLocal = SRC_SAY_LOCAL;
        uint32 channelId = 0;
        std::string channelName;
        std::string threadKey;
    };

    void RunLlmJob(LlmJob job)
    {
        struct Busy
        {
            ~Busy() { g_LlmInFlight.fetch_sub(1); }
        } busy;

        std::string line;
        try
        {
            line = QueryOllama(job.prompt);
        }
        catch (std::exception const& e)
        {
            LOG_ERROR("server.loading", "[Bot Chat] Ollama query exception: {}", e.what());
        }

        line = SanitizeBotChatLine(line, StyleFor(job.tone, job.sourceLocal));
        if (Player* senderPtr = job.senderGuid ? ObjectAccessor::FindPlayer(ObjectGuid(job.senderGuid)) : nullptr)
        {
            std::string name = senderPtr->GetName();
            for (char& c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string lower = line;
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!name.empty() && lower.find(name) != std::string::npos)
                line.clear();
        }
        if (ParrotsPlayer(line, job.playerMsg) || LineTooSimilarToRecent(job.threadKey, line) ||
            LineRecentlySpoken(line))
            line.clear();
        if (line.empty())
            line = job.fallback;
        if (line.empty() || LineTooSimilarToRecent(job.threadKey, line) || LineRecentlySpoken(line))
            return;

        BotChatTypingSleep(line.length());

        Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(job.botGuid));
        Player* senderPtr = ObjectAccessor::FindPlayer(ObjectGuid(job.senderGuid));
        if (!botPtr)
            return;
        if (!job.channelName.empty() && !ChannelBelongsToBotZone(botPtr, job.channelName))
        {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] LLM {} dropped (wrong zone channel '{}')",
                         botPtr->GetName(), job.channelName);
            return;
        }
        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(botPtr);
        if (!botAI)
            return;

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] LLM reply {} -> {}", botPtr->GetName(), line);

        NoteSpokenLine(line);
        AppendChannelThread(job.threadKey, botPtr->GetName(), job.botGuid, true, line);

        if (job.channelId != 0 && !job.channelName.empty())
        {
            SendBotChannelLine(botPtr, job.channelName, line);
            ChannelMgr* cMgr = ChannelMgr::forTeam(botPtr->GetTeamId());
            Channel* targetChannel = cMgr ? cMgr->GetChannel(job.channelName, botPtr, false) : nullptr;
            ProcessBotChatMessage(botPtr, line, SRC_GENERAL_LOCAL, targetChannel);
            return;
        }

        switch (job.sourceLocal)
        {
            case SRC_GUILD_LOCAL:
            case SRC_OFFICER_LOCAL:
                botAI->SayToGuild(line);
                ProcessBotChatMessage(botPtr, line, job.sourceLocal, nullptr);
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
                if (senderPtr)
                    botAI->Whisper(line, senderPtr->GetName());
                break;
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
}

bool TryBotChatLlmReply(std::vector<Player*> const& bots, Player* sender, std::string const& msg,
                        ChatTone tone, ChatChannelSourceLocal sourceLocal, Channel* channel,
                        std::string const& fallback)
{
    if (!sender || bots.empty() || msg.empty())
        return false;

    Player* bot = nullptr;
    for (Player* candidate : bots)
    {
        if (candidate)
        {
            bot = candidate;
            break;
        }
    }
    if (!bot)
        return false;

    PlayerbotAI* senderAI = PlayerbotsMgr::instance().GetPlayerbotAI(sender);
    bool const playerGroupTalk = !(senderAI && senderAI->IsBotAI()) &&
        (sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL ||
         sourceLocal == SRC_GUILD_LOCAL || sourceLocal == SRC_OFFICER_LOCAL ||
         sourceLocal == SRC_WHISPER_LOCAL);
    bool const bonded = !(senderAI && senderAI->IsBotAI()) &&
        GetConversationBond(sender->GetGUID().GetRawValue(), g_TopicIdleSeconds) == bot->GetGUID().GetRawValue();
    if (g_DisableRepliesInCombat && bot->IsInCombat() && !playerGroupTalk && !bonded)
    {
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] LLM {} skipped (combat)", bot->GetName());
        return true;
    }

    // LFG is canned. The model writes RP ("I know a paladin in Stormwind").
    if (ParseChatQuery(msg).topic == ChatTopic::LookingForGroup)
    {
        std::string line = fallback;
        if (line.empty())
            line = PickGroupReply(MakeThreadKey(sender, sourceLocal, channel, bot),
                                  sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL, bot);
        if (line.empty())
            return true;
        DeliverBotChatReply({ bot }, sender, line, sourceLocal, channel);
        return true;
    }

    // Flame is canned. The model lectures ("get banned", "raid tonight").
    if (tone == ChatTone::Hostile || tone == ChatTone::Dismissive)
    {
        if (urand(0, 99) < 20)
        {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[Bot Chat] {} ignores flame from {}", bot->GetName(), sender->GetName());
            return true;
        }
        if (fallback.empty())
            return true;
        DeliverBotChatReply({ bot }, sender, fallback, sourceLocal, channel);
        return true;
    }

    if (!g_EnableLLM)
    {
        if (fallback.empty())
            return false;
        DeliverBotChatReply({ bot }, sender, fallback, sourceLocal, channel);
        return true;
    }

    uint32 const maxInFlight = g_MaxConcurrentQueries ? g_MaxConcurrentQueries : 1;
    uint32 const prev = g_LlmInFlight.fetch_add(1);
    // Ambient stays silent when the slot is full. A real player talking still
    // gets a wording — otherwise guild "what" dies behind a General continue.
    if (prev >= maxInFlight && g_DebugEnabled)
        LOG_INFO("server.loading", "[Bot Chat] LLM over cap, still wording player reply {}",
                 bot->GetName());

    std::string const place = DescribeBotPlace(bot);
    std::string const botFact = Acore::StringFormat("You are a level {} {} {}.",
        bot->GetLevel(), RaceName(bot->getRace()), ClassName(bot->getClass()));
    std::string const threadKey = MakeThreadKey(sender, sourceLocal, channel, bot);
    std::string const thread = FormatSharedThread(sender, bot, threadKey, bot->GetName(), 8);
    std::string const prompt = BuildPrompt(bot->GetName(), botFact, place,
                                           ChannelLabel(sourceLocal), msg, tone, thread);

    LlmJob job;
    job.botGuid = bot->GetGUID().GetRawValue();
    job.senderGuid = sender->GetGUID().GetRawValue();
    job.prompt = prompt;
    job.playerMsg = msg;
    job.fallback = fallback;
    job.tone = tone;
    job.sourceLocal = sourceLocal;
    job.channelId = channel ? channel->GetChannelId() : 0;
    job.channelName = channel ? channel->GetName() : "";
    job.threadKey = threadKey;

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[Bot Chat] LLM job {} tone {} place {}",
                 bot->GetName(), static_cast<int>(tone), place);

    try
    {
        std::thread([job = std::move(job)]() {
            try
            {
                RunLlmJob(job);
            }
            catch (std::exception const& e)
            {
                LOG_ERROR("server.loading", "[Bot Chat] LLM thread exception: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("server.loading", "[Bot Chat] LLM thread unknown exception");
            }
        }).detach();
    }
    catch (std::exception const& e)
    {
        g_LlmInFlight.fetch_sub(1);
        LOG_ERROR("server.loading", "[Bot Chat] Failed to start LLM thread: {}", e.what());
        if (!fallback.empty())
            DeliverBotChatReply({ bot }, sender, fallback, sourceLocal, channel);
    }
    return true;
}

bool TryBotChatLlmContinue(Player* bot, std::string const& lastMsg,
                           ChatChannelSourceLocal sourceLocal, Channel* channel,
                           std::string const& threadKey, std::string const& fallback)
{
    if (!bot || lastMsg.empty())
        return false;
    if (g_DisableRepliesInCombat && bot->IsInCombat())
        return true;

    if (ParseChatQuery(lastMsg).topic == ChatTopic::LookingForGroup)
    {
        std::string line = fallback;
        if (line.empty())
            line = PickGroupReply(threadKey, sourceLocal == SRC_PARTY_LOCAL || sourceLocal == SRC_RAID_LOCAL, bot);
        if (line.empty())
            return false;
        DeliverBotChatReply({ bot }, bot, line, sourceLocal, channel);
        return true;
    }

    // Arguments and flame stay canned. The model lectures or invents raids.
    if (LooksLikeDribble(lastMsg) || IsHostileTalk(lastMsg))
    {
        std::string line = fallback;
        if (line.empty())
            line = PickContinueForLast(lastMsg, threadKey);
        if (line.empty())
            return false;
        DeliverBotChatReply({ bot }, bot, line, sourceLocal, channel);
        return true;
    }

    if (!g_EnableLLM)
    {
        if (fallback.empty())
            return false;
        DeliverBotChatReply({ bot }, bot, fallback, sourceLocal, channel);
        return true;
    }

    uint32 const maxInFlight = g_MaxConcurrentQueries ? g_MaxConcurrentQueries : 1;
    uint32 const prev = g_LlmInFlight.fetch_add(1);
    if (prev >= maxInFlight)
    {
        g_LlmInFlight.fetch_sub(1);
        return false;
    }

    std::string const place = DescribeBotPlace(bot);
    std::string const botFact = Acore::StringFormat("You are a level {} {} {}.",
        bot->GetLevel(), RaceName(bot->getRace()), ClassName(bot->getClass()));
    std::string const thread = FormatChannelThread(threadKey, bot->GetName(), 6);
    std::string const prompt = BuildContinuePrompt(bot->GetName(), botFact, place,
                                                   ChannelLabel(sourceLocal), lastMsg, thread);

    LlmJob job;
    job.botGuid = bot->GetGUID().GetRawValue();
    job.senderGuid = 0;
    job.prompt = prompt;
    job.playerMsg = lastMsg;
    job.fallback = fallback;
    job.tone = ChatTone::Neutral;
    job.sourceLocal = sourceLocal;
    job.channelId = channel ? channel->GetChannelId() : 0;
    job.channelName = channel ? channel->GetName() : "";
    job.threadKey = threadKey;

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[Bot Chat] LLM continue {} last '{}' place {}",
                 bot->GetName(), lastMsg, place);

    try
    {
        std::thread([job = std::move(job)]() {
            try
            {
                RunLlmJob(job);
            }
            catch (std::exception const& e)
            {
                LOG_ERROR("server.loading", "[Bot Chat] LLM continue exception: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("server.loading", "[Bot Chat] LLM continue unknown exception");
            }
        }).detach();
    }
    catch (std::exception const& e)
    {
        g_LlmInFlight.fetch_sub(1);
        LOG_ERROR("server.loading", "[Bot Chat] Failed to start LLM continue: {}", e.what());
        return false;
    }
    return true;
}

bool TryBotChatLlmStart(Player* bot, std::string const& hint,
                        ChatChannelSourceLocal sourceLocal, Channel* channel,
                        std::string const& threadKey, std::string const& fallback)
{
    if (!bot || sourceLocal != SRC_GUILD_LOCAL)
        return false;

    if (ParseChatQuery(hint).topic == ChatTopic::LookingForGroup)
        return false;

    if (g_DisableRepliesInCombat && bot->IsInCombat())
    {
        if (fallback.empty())
            return false;
        DeliverBotChatReply({ bot }, bot, fallback, sourceLocal, channel);
        return true;
    }

    if (!g_EnableLLM)
    {
        if (fallback.empty())
            return false;
        DeliverBotChatReply({ bot }, bot, fallback, sourceLocal, channel);
        return true;
    }

    uint32 const maxInFlight = g_MaxConcurrentQueries ? g_MaxConcurrentQueries : 1;
    uint32 const prev = g_LlmInFlight.fetch_add(1);
    if (prev >= maxInFlight)
    {
        g_LlmInFlight.fetch_sub(1);
        if (fallback.empty())
            return false;
        DeliverBotChatReply({ bot }, bot, fallback, sourceLocal, channel);
        return true;
    }

    std::string const place = DescribeBotPlace(bot);
    std::string const botFact = Acore::StringFormat("You are a level {} {} {}.",
        bot->GetLevel(), RaceName(bot->getRace()), ClassName(bot->getClass()));
    std::string const thread = FormatChannelThread(threadKey, bot->GetName(), 6);
    std::string const prompt = BuildStartPrompt(bot->GetName(), botFact, place, hint, thread);

    LlmJob job;
    job.botGuid = bot->GetGUID().GetRawValue();
    job.senderGuid = 0;
    job.prompt = prompt;
    job.playerMsg = hint;
    job.fallback = fallback;
    job.tone = ChatTone::Neutral;
    job.sourceLocal = sourceLocal;
    job.channelId = channel ? channel->GetChannelId() : 0;
    job.channelName = channel ? channel->GetName() : "";
    job.threadKey = threadKey;

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[Bot Chat] LLM start {} hint '{}' place {}",
                 bot->GetName(), hint, place);

    try
    {
        std::thread([job = std::move(job)]() {
            try
            {
                RunLlmJob(job);
            }
            catch (std::exception const& e)
            {
                LOG_ERROR("server.loading", "[Bot Chat] LLM start exception: {}", e.what());
            }
            catch (...)
            {
                LOG_ERROR("server.loading", "[Bot Chat] LLM start unknown exception");
            }
        }).detach();
    }
    catch (std::exception const& e)
    {
        g_LlmInFlight.fetch_sub(1);
        LOG_ERROR("server.loading", "[Bot Chat] Failed to start LLM start: {}", e.what());
        if (!fallback.empty())
            DeliverBotChatReply({ bot }, bot, fallback, sourceLocal, channel);
        return true;
    }
    return true;
}
