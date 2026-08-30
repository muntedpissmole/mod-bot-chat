#include "bot_chat_thread.h"
#include "bot_chat_config.h"
#include "bot_chat_knowledge.h"
#include "bot_chat_util.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "ObjectAccessor.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Guild.h"
#include "Group.h"
#include "Log.h"
#include <mutex>
#include <unordered_map>
#include <deque>
#include <algorithm>

namespace
{
    struct ChannelThread
    {
        std::deque<ChannelThreadLine> lines;
        time_t lastActivity = 0;
        std::deque<uint64_t> recentSpeakers;
    };

    std::mutex g_ThreadMutex;
    std::unordered_map<std::string, ChannelThread> g_Threads;

    constexpr size_t MAX_RECENT_SPEAKERS = 8;

    ChannelThread& GetOrCreateThreadUnlocked(const std::string& key)
    {
        return g_Threads[key];
    }
}

Channel* FindPlayerChannel(Player* player, char const* namePrefix)
{
    if (!player || !namePrefix || !*namePrefix)
        return nullptr;

    ChannelMgr* cMgr = ChannelMgr::forTeam(player->GetTeamId());
    if (!cMgr)
        return nullptr;

    std::string const prefix = namePrefix;
    for (auto const& pair : cMgr->GetChannels())
    {
        Channel* ch = pair.second;
        if (!ch)
            continue;
        std::string const& name = ch->GetName();
        if (name.rfind(prefix, 0) != 0 && name.find(prefix) == std::string::npos)
            continue;
        if (player->IsInChannel(ch))
            return ch;
    }
    return cMgr->GetChannel(prefix, player, false);
}

std::string MakeThreadKey(Player* player, ChatChannelSourceLocal source, Channel* channel)
{
    if (!player)
        return "unknown";

    uint32_t team = static_cast<uint32_t>(player->GetTeamId());

    switch (source)
    {
        case SRC_SAY_LOCAL:
        {
            uint32_t mapId = player->GetMapId();
            uint32_t zoneId = player->GetZoneId();
            int gx = static_cast<int>(player->GetPositionX() / 80.0f);
            int gy = static_cast<int>(player->GetPositionY() / 80.0f);
            return SafeFormat("say:{}:{}:{}:{}:{}", mapId, zoneId, gx, gy, team);
        }
        case SRC_YELL_LOCAL:
            return SafeFormat("yell:{}:{}:{}", player->GetMapId(), player->GetZoneId(), team);
        case SRC_GUILD_LOCAL:
        case SRC_OFFICER_LOCAL:
            return SafeFormat("guild:{}", player->GetGuildId());
        case SRC_PARTY_LOCAL:
            return SafeFormat("party:{}", player->GetGroup() ? player->GetGroup()->GetGUID().GetRawValue() : 0);
        case SRC_RAID_LOCAL:
            return SafeFormat("raid:{}", player->GetGroup() ? player->GetGroup()->GetGUID().GetRawValue() : 0);
        case SRC_GENERAL_LOCAL:
            if (channel)
                return SafeFormat("chan:{}:{}", team, channel->GetName());
            return SafeFormat("chan:{}:General", team);
        case SRC_WHISPER_LOCAL:
            return SafeFormat("whisper:{}", player->GetGUID().GetRawValue());
        default:
            return SafeFormat("misc:{}:{}", team, static_cast<int>(source));
    }
}

void AppendChannelThread(const std::string& key, const std::string& speaker, uint64_t speakerGuid, bool isBot, const std::string& text)
{
    if (!g_EnableChannelThreads || key.empty() || text.empty())
        return;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    ChannelThread& thread = GetOrCreateThreadUnlocked(key);

    if (!thread.lines.empty())
    {
        const ChannelThreadLine& last = thread.lines.back();
        if (last.speakerGuid == speakerGuid && last.text == text)
            return;
    }

    ChannelThreadLine line;
    line.speaker = speaker;
    line.speakerGuid = speakerGuid;
    line.isBot = isBot;
    line.text = text;
    line.timestamp = time(nullptr);
    thread.lines.push_back(line);
    thread.lastActivity = line.timestamp;

    thread.recentSpeakers.erase(
        std::remove(thread.recentSpeakers.begin(), thread.recentSpeakers.end(), speakerGuid),
        thread.recentSpeakers.end());
    thread.recentSpeakers.push_back(speakerGuid);
    while (thread.recentSpeakers.size() > MAX_RECENT_SPEAKERS)
        thread.recentSpeakers.pop_front();

    uint32_t maxLines = g_ChannelThreadMaxLines > 0 ? g_ChannelThreadMaxLines : 24;
    while (thread.lines.size() > maxLines)
        thread.lines.pop_front();
}

std::string FormatChannelThread(const std::string& key, const std::string& selfName, uint32 maxPromptLines)
{
    if (!g_EnableChannelThreads || key.empty())
        return "";

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return "";

    std::string header = g_ChannelThreadHeaderTemplate.empty()
        ? "RECENT CHANNEL CHAT (stay on this topic. Do not greet or start a new one):\n"
        : g_ChannelThreadHeaderTemplate;

    std::string result = header;
    auto const& lines = it->second.lines;
    uint32 const cap = maxPromptLines ? maxPromptLines : 6;
    size_t start = lines.size() > cap ? lines.size() - cap : 0;
    for (size_t i = start; i < lines.size(); ++i)
    {
        ChannelThreadLine const& line = lines[i];
        std::string name = (line.speaker == selfName) ? "You" : line.speaker;
        if (!g_ChannelThreadLineTemplate.empty())
        {
            result += SafeFormat(g_ChannelThreadLineTemplate,
                fmt::arg("speaker", name),
                fmt::arg("message", line.text));
        }
        else
        {
            result += SafeFormat("[{}]: {}\n", name, line.text);
        }
    }
    return result;
}

std::string GetLastThreadText(const std::string& key)
{
    if (key.empty())
        return "";

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return "";
    return it->second.lines.back().text;
}

bool ThreadIsActive(const std::string& key, uint32_t idleSeconds)
{
    if (!g_EnableChannelThreads || key.empty())
        return false;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return false;

    return difftime(time(nullptr), it->second.lastActivity) <= static_cast<double>(idleSeconds);
}

uint64_t GetLastBotSpeaker(const std::string& key)
{
    if (!g_EnableChannelThreads || key.empty())
        return 0;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end())
        return 0;

    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (line->isBot && line->speakerGuid)
            return line->speakerGuid;
    }
    return 0;
}

bool IsRecentSpeaker(const std::string& key, uint64_t speakerGuid)
{
    if (!g_EnableChannelThreads || key.empty() || speakerGuid == 0)
        return false;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end())
        return false;

    for (uint64_t guid : it->second.recentSpeakers)
    {
        if (guid == speakerGuid)
            return true;
    }
    return false;
}

time_t GetThreadLastActivity(const std::string& key)
{
    if (key.empty())
        return 0;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end())
        return 0;
    return it->second.lastActivity;
}

std::string GetLastPlayerMessageExcept(const std::string& key, const std::string& exceptText)
{
    if (key.empty())
        return "";

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end())
        return "";

    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (line->isBot)
            continue;
        if (!exceptText.empty() && line->text == exceptText)
            continue;
        return line->text;
    }
    return "";
}

std::string GetLastPlayerHelpQuery(const std::string& key)
{
    if (key.empty())
        return "";

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end())
        return "";

    time_t now = time(nullptr);
    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (line->isBot)
            continue;
        if (difftime(now, line->timestamp) > 180.0)
            break;
        if (ClassifyChatIntent(line->text) == ChatIntent::HelpRequest)
            return line->text;
    }
    return "";
}

std::vector<uint64_t> GetRecentSpeakers(const std::string& key)
{
    std::vector<uint64_t> result;
    if (key.empty())
        return result;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end())
        return result;

    result.assign(it->second.recentSpeakers.begin(), it->second.recentSpeakers.end());
    return result;
}

bool LineTooSimilarToRecent(const std::string& key, const std::string& text, uint32 lookback)
{
    if (key.empty() || text.empty())
        return false;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return false;

    uint32 checked = 0;
    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (ChatLinesSimilar(line->text, text))
            return true;
        if (++checked >= lookback)
            break;
    }
    return false;
}

void ClearChannelThreads()
{
    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    g_Threads.clear();
}

std::string GuessAmbientThreadKey(Player* bot, ChatChannelSourceLocal& outSource, Channel*& outChannel)
{
    outSource = SRC_SAY_LOCAL;
    outChannel = nullptr;
    if (!bot)
        return "";

    auto guildHasRealPlayer = [](Guild* guild) -> bool
    {
        if (!guild)
            return false;
        uint32 const id = guild->GetId();
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (!player || !player->IsInWorld())
                continue;
            if (PlayerbotsMgr::instance().GetPlayerbotAI(player))
                continue;
            if (player->GetGuild() && player->GetGuild()->GetId() == id)
                return true;
        }
        return false;
    };

    // Only continue a guild thread if a real player can hear it. Otherwise
    // nearby bots in other guilds keep talking to an empty /g.
    if (bot->GetGuild() && guildHasRealPlayer(bot->GetGuild()))
    {
        std::string guildKey = MakeThreadKey(bot, SRC_GUILD_LOCAL, nullptr);
        if (ThreadIsActive(guildKey, g_TopicIdleSeconds))
        {
            outSource = SRC_GUILD_LOCAL;
            return guildKey;
        }
    }

    auto groupHasRealPlayer = [](Group* group) -> bool
    {
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;
            if (!PlayerbotsMgr::instance().GetPlayerbotAI(member))
                return true;
        }
        return false;
    };

    if (bot->GetGroup() && groupHasRealPlayer(bot->GetGroup()))
    {
        ChatChannelSourceLocal partySource = bot->GetGroup()->isRaidGroup() ? SRC_RAID_LOCAL : SRC_PARTY_LOCAL;
        std::string partyKey = MakeThreadKey(bot, partySource, nullptr);
        if (ThreadIsActive(partyKey, g_TopicIdleSeconds))
        {
            outSource = partySource;
            return partyKey;
        }
    }

    Channel* general = FindPlayerChannel(bot, "General");

    std::string sayKey = MakeThreadKey(bot, SRC_SAY_LOCAL, nullptr);
    std::string genKey = MakeThreadKey(bot, SRC_GENERAL_LOCAL, general);

    time_t sayActivity = GetThreadLastActivity(sayKey);
    time_t genActivity = GetThreadLastActivity(genKey);

    bool sayActive = ThreadIsActive(sayKey, g_TopicIdleSeconds);
    bool genActive = ThreadIsActive(genKey, g_TopicIdleSeconds);

    if (genActive && (!sayActive || genActivity >= sayActivity))
    {
        outSource = SRC_GENERAL_LOCAL;
        outChannel = general;
        return genKey;
    }
    if (sayActive)
    {
        outSource = SRC_SAY_LOCAL;
        return sayKey;
    }

    // Cold start: prefer General if a real conversation might happen there.
    if (general)
    {
        outSource = SRC_GENERAL_LOCAL;
        outChannel = general;
        return genKey;
    }

    outSource = SRC_SAY_LOCAL;
    return sayKey;
}
