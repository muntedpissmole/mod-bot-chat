#include "bot_chat_thread.h"
#include "bot_chat_config.h"
#include "bot_chat_knowledge.h"
#include "bot_chat_util.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "ObjectAccessor.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "DBCStores.h"
#include "Map.h"
#include "Guild.h"
#include "Group.h"
#include "Log.h"
#include <mutex>
#include <unordered_map>
#include <deque>
#include <algorithm>
#include <ctime>
#include <utility>

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

    std::mutex g_SpokenMutex;
    std::deque<std::pair<std::string, time_t>> g_RecentSpoken;

    std::mutex g_BondMutex;
    std::unordered_map<uint64_t, std::pair<uint64_t, time_t>> g_PlayerBonds;

    char const* ThreadKeyLabel(std::string const& key)
    {
        if (key.rfind("guild:", 0) == 0)
            return "Guild";
        if (key.rfind("party:", 0) == 0)
            return "Party";
        if (key.rfind("raid:", 0) == 0)
            return "Raid";
        if (key.rfind("say:", 0) == 0)
            return "Say";
        if (key.rfind("yell:", 0) == 0)
            return "Yell";
        if (key.rfind("whisper:", 0) == 0)
            return "Whisper";
        if (key.rfind("chan:", 0) == 0)
            return "General";
        return "Chat";
    }

    std::vector<ChannelThreadLine> CopyRecentThreadLinesUnlocked(ChannelThread const& thread,
                                                                 uint32 maxLines, uint32 maxAgeSeconds, time_t now)
    {
        std::vector<ChannelThreadLine> out;
        for (auto line = thread.lines.rbegin(); line != thread.lines.rend(); ++line)
        {
            if (maxAgeSeconds && difftime(now, line->timestamp) > static_cast<double>(maxAgeSeconds))
                break;
            out.push_back(*line);
            if (maxLines && out.size() >= maxLines)
                break;
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    std::vector<ChannelThreadLine> CopyRecentThreadLines(std::string const& key, uint32 maxLines, uint32 maxAgeSeconds)
    {
        std::vector<ChannelThreadLine> out;
        if (key.empty())
            return out;
        time_t const now = time(nullptr);
        std::lock_guard<std::mutex> lock(g_ThreadMutex);
        auto it = g_Threads.find(key);
        if (it == g_Threads.end() || it->second.lines.empty())
            return out;
        return CopyRecentThreadLinesUnlocked(it->second, maxLines, maxAgeSeconds, now);
    }

    constexpr size_t MAX_RECENT_SPEAKERS = 8;
    constexpr size_t MAX_SPOKEN_LINES = 512;
    constexpr int SPOKEN_TTL_SECONDS = 3 * 60 * 60;

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

uint32 BotLiveZoneId(Player* player)
{
    if (!player)
        return 0;
    if (player->GetMap())
        return player->GetMap()->GetZoneId(player->GetPhaseMask(),
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    return player->GetZoneId();
}

static char const* BotLiveZoneName(Player* player)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(BotLiveZoneId(player));
    if (zone && zone->area_name[0] && zone->area_name[0][0])
        return zone->area_name[0];
    return nullptr;
}

static bool ChannelIsZoneLocal(std::string const& name)
{
    return name.find("General") != std::string::npos ||
           name.find("LocalDefense") != std::string::npos;
}

bool ChannelBelongsToBotZone(Player* player, std::string const& channelName)
{
    if (!player || channelName.empty())
        return false;
    if (!ChannelIsZoneLocal(channelName))
        return true;
    char const* zoneName = BotLiveZoneName(player);
    if (!zoneName)
        return false;
    return channelName.find(zoneName) != std::string::npos;
}

Channel* FindZoneGeneral(Player* player)
{
    char const* zoneName = BotLiveZoneName(player);
    if (!player || !zoneName)
        return nullptr;

    ChannelMgr* cMgr = ChannelMgr::forTeam(player->GetTeamId());
    if (!cMgr)
        return nullptr;

    std::string const want = std::string("General - ") + zoneName;
    for (auto const& pair : cMgr->GetChannels())
    {
        Channel* ch = pair.second;
        if (ch && ch->GetName() == want)
            return ch;
    }
    return cMgr->GetChannel(want, player, false);
}

std::string MakeThreadKey(Player* player, ChatChannelSourceLocal source, Channel* channel, Player* peer)
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
        {
            uint64_t a = player->GetGUID().GetRawValue();
            uint64_t b = peer ? peer->GetGUID().GetRawValue() : 0;
            if (b && a > b)
                std::swap(a, b);
            if (b)
                return SafeFormat("whisper:{}:{}", a, b);
            return SafeFormat("whisper:{}", a);
        }
        default:
            return SafeFormat("misc:{}:{}", team, static_cast<int>(source));
    }
}

void AppendChannelThread(const std::string& key, const std::string& speaker, uint64_t speakerGuid, bool isBot, const std::string& text)
{
    if (!g_EnableChannelThreads || key.empty() || text.empty())
        return;

    {
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

    NoteSpokenLine(text);
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

std::vector<std::string> SharedThreadKeys(Player* player, Player* other)
{
    std::vector<std::string> keys;
    if (!player || !other)
        return keys;

    auto add = [&](std::string const& key)
    {
        if (key.empty())
            return;
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(key);
    };

    add(MakeThreadKey(player, SRC_WHISPER_LOCAL, nullptr, other));
    if (player->GetGuildId() && player->GetGuildId() == other->GetGuildId())
        add(MakeThreadKey(player, SRC_GUILD_LOCAL, nullptr));
    if (player->GetGroup() && other->GetGroup() && player->GetGroup() == other->GetGroup())
    {
        add(MakeThreadKey(player, SRC_PARTY_LOCAL, nullptr));
        add(MakeThreadKey(player, SRC_RAID_LOCAL, nullptr));
    }
    add(MakeThreadKey(player, SRC_SAY_LOCAL, nullptr));
    add(MakeThreadKey(other, SRC_SAY_LOCAL, nullptr));
    add(MakeThreadKey(player, SRC_YELL_LOCAL, nullptr));
    Channel* general = FindZoneGeneral(player);
    if (general && ChannelBelongsToBotZone(other, general->GetName()))
        add(MakeThreadKey(player, SRC_GENERAL_LOCAL, general));
    return keys;
}

std::string FormatSharedThread(Player* player, Player* bot, std::string const& currentKey,
                               std::string const& selfName, uint32 maxPromptLines)
{
    if (!player || !bot)
        return FormatChannelThread(currentKey, selfName, maxPromptLines);

    std::vector<std::string> keys = SharedThreadKeys(player, bot);
    if (!currentKey.empty() && std::find(keys.begin(), keys.end(), currentKey) == keys.end())
        keys.insert(keys.begin(), currentKey);
    if (keys.empty())
        return FormatChannelThread(currentKey, selfName, maxPromptLines);

    uint32 const age = g_TopicIdleSeconds ? g_TopicIdleSeconds : 180;
    struct Tagged
    {
        ChannelThreadLine line;
        char const* chan;
    };
    std::vector<Tagged> merged;
    for (std::string const& key : keys)
    {
        char const* chan = ThreadKeyLabel(key);
        std::vector<ChannelThreadLine> lines = CopyRecentThreadLines(key, 12, age);
        for (ChannelThreadLine const& line : lines)
            merged.push_back({ line, chan });
    }
    if (merged.empty())
        return "";

    std::sort(merged.begin(), merged.end(), [](Tagged const& a, Tagged const& b)
    {
        if (a.line.timestamp != b.line.timestamp)
            return a.line.timestamp < b.line.timestamp;
        if (a.line.speakerGuid != b.line.speakerGuid)
            return a.line.speakerGuid < b.line.speakerGuid;
        return a.line.text < b.line.text;
    });
    std::vector<Tagged> unique;
    for (Tagged const& row : merged)
    {
        if (!unique.empty())
        {
            Tagged const& last = unique.back();
            if (last.line.speakerGuid == row.line.speakerGuid && last.line.text == row.line.text &&
                last.line.timestamp == row.line.timestamp)
                continue;
        }
        unique.push_back(row);
    }

    uint32 const cap = maxPromptLines ? maxPromptLines : 8;
    size_t start = unique.size() > cap ? unique.size() - cap : 0;
    std::string result = "RECENT CHAT (stay on this topic even if it was another channel. "
                         "Do not greet or start a new one):\n";
    for (size_t i = start; i < unique.size(); ++i)
    {
        Tagged const& row = unique[i];
        std::string name = (row.line.speaker == selfName) ? "You" : row.line.speaker;
        result += SafeFormat("[{}] {}: {}\n", row.chan, name, row.line.text);
    }
    return result;
}

uint64_t FindRecentSharedBotSpeaker(Player* player, std::vector<Player*> const& candidates, uint32 idleSeconds)
{
    if (!player || candidates.empty())
        return 0;

    uint64_t bestGuid = 0;
    time_t bestTime = 0;
    time_t const now = time(nullptr);
    uint32 const idle = idleSeconds ? idleSeconds : 180;
    for (Player* bot : candidates)
    {
        if (!bot)
            continue;
        uint64_t const guid = bot->GetGUID().GetRawValue();
        for (std::string const& key : SharedThreadKeys(player, bot))
        {
            if (GetLastBotSpeaker(key) != guid)
                continue;
            time_t const activity = GetThreadLastActivity(key);
            if (!activity || difftime(now, activity) > static_cast<double>(idle))
                continue;
            if (activity >= bestTime)
            {
                bestTime = activity;
                bestGuid = guid;
            }
        }
    }
    return bestGuid;
}

void NoteConversationBond(uint64_t playerGuid, uint64_t botGuid)
{
    if (!playerGuid || !botGuid || playerGuid == botGuid)
        return;
    std::lock_guard<std::mutex> lock(g_BondMutex);
    g_PlayerBonds[playerGuid] = { botGuid, time(nullptr) };
}

uint64_t GetConversationBond(uint64_t playerGuid, uint32 idleSeconds)
{
    if (!playerGuid)
        return 0;
    std::lock_guard<std::mutex> lock(g_BondMutex);
    auto it = g_PlayerBonds.find(playerGuid);
    if (it == g_PlayerBonds.end())
        return 0;
    if (idleSeconds && difftime(time(nullptr), it->second.second) > static_cast<double>(idleSeconds))
        return 0;
    return it->second.first;
}

std::string GetLastPlayerMessageExceptAmong(std::vector<std::string> const& keys, std::string const& exceptText)
{
    std::string best;
    time_t bestTime = 0;
    uint32 const age = g_TopicIdleSeconds ? g_TopicIdleSeconds : 180;
    for (std::string const& key : keys)
    {
        std::vector<ChannelThreadLine> lines = CopyRecentThreadLines(key, 24, age);
        for (auto line = lines.rbegin(); line != lines.rend(); ++line)
        {
            if (line->isBot)
                continue;
            if (!exceptText.empty() && line->text == exceptText)
                continue;
            if (line->timestamp >= bestTime)
            {
                bestTime = line->timestamp;
                best = line->text;
            }
            break;
        }
    }
    return best;
}

std::string GetLastPlayerHelpQueryAmong(std::vector<std::string> const& keys)
{
    std::string best;
    time_t bestTime = 0;
    for (std::string const& key : keys)
    {
        std::vector<ChannelThreadLine> lines = CopyRecentThreadLines(key, 24, 180);
        for (auto line = lines.rbegin(); line != lines.rend(); ++line)
        {
            if (line->isBot)
                continue;
            if (ClassifyChatIntent(line->text) != ChatIntent::HelpRequest)
                continue;
            if (line->timestamp >= bestTime)
            {
                bestTime = line->timestamp;
                best = line->text;
            }
            break;
        }
    }
    return best;
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

void NoteSpokenLine(const std::string& text)
{
    std::string const norm = NormalizeChatLine(text);
    if (norm.empty())
        return;

    time_t const now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_SpokenMutex);
    g_RecentSpoken.push_back({norm, now});
    while (g_RecentSpoken.size() > MAX_SPOKEN_LINES)
        g_RecentSpoken.pop_front();
    while (!g_RecentSpoken.empty() &&
           difftime(now, g_RecentSpoken.front().second) > SPOKEN_TTL_SECONDS)
        g_RecentSpoken.pop_front();
}

bool LineRecentlySpoken(const std::string& text)
{
    std::string const norm = NormalizeChatLine(text);
    if (norm.empty())
        return false;

    time_t const now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_SpokenMutex);
    while (!g_RecentSpoken.empty() &&
           difftime(now, g_RecentSpoken.front().second) > SPOKEN_TTL_SECONDS)
        g_RecentSpoken.pop_front();

    for (auto const& spoken : g_RecentSpoken)
    {
        if (spoken.first == norm)
            return true;
        if (ChatLinesSimilar(spoken.first, norm))
            return true;
        if (ChatLineShapesMatch(spoken.first, norm))
            return true;
    }
    return false;
}

ChannelThreadLine GetLastThreadLine(const std::string& key)
{
    ChannelThreadLine empty;
    if (key.empty())
        return empty;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return empty;
    return it->second.lines.back();
}

uint32 CountTrailingBotLines(const std::string& key)
{
    if (key.empty())
        return 0;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return 0;

    uint32 count = 0;
    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (!line->isBot)
            break;
        ++count;
    }
    return count;
}

bool LastThreadSpeakerIsPlayer(const std::string& key)
{
    ChannelThreadLine const last = GetLastThreadLine(key);
    return !last.text.empty() && !last.isBot;
}

bool ThreadLooksLooped(const std::string& key)
{
    if (key.empty())
        return false;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.size() < 2)
        return false;

    auto const& lines = it->second.lines;
    size_t const n = lines.size();
    uint32 trailingBots = 0;
    for (size_t i = n; i > 0; --i)
    {
        if (!lines[i - 1].isBot)
            break;
        ++trailingBots;
    }

    uint32 similarPairs = 0;
    uint32 compared = 0;
    for (size_t i = n; i > 1 && compared < 3; --i)
    {
        if (ChatLinesSimilar(lines[i - 1].text, lines[i - 2].text))
            ++similarPairs;
        ++compared;
    }
    return similarPairs >= 2 || (trailingBots >= 2 && similarPairs >= 1);
}

uint32 CountRecentPlayerLines(const std::string& key, uint32_t windowSeconds)
{
    if (key.empty())
        return 0;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return 0;

    time_t const now = time(nullptr);
    uint32 count = 0;
    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (difftime(now, line->timestamp) > windowSeconds)
            break;
        if (!line->isBot)
            ++count;
    }
    return count;
}

bool PlayerSpokeRecently(const std::string& key, uint32_t withinSeconds)
{
    if (key.empty())
        return false;

    std::lock_guard<std::mutex> lock(g_ThreadMutex);
    auto it = g_Threads.find(key);
    if (it == g_Threads.end() || it->second.lines.empty())
        return false;

    time_t const now = time(nullptr);
    for (auto line = it->second.lines.rbegin(); line != it->second.lines.rend(); ++line)
    {
        if (difftime(now, line->timestamp) > withinSeconds)
            break;
        if (!line->isBot)
            return true;
    }
    return false;
}

void ClearChannelThreads()
{
    {
        std::lock_guard<std::mutex> lock(g_ThreadMutex);
        g_Threads.clear();
    }
    {
        std::lock_guard<std::mutex> spokenLock(g_SpokenMutex);
        g_RecentSpoken.clear();
    }
    std::lock_guard<std::mutex> bondLock(g_BondMutex);
    g_PlayerBonds.clear();
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

    Channel* general = FindZoneGeneral(bot);

    std::string sayKey = MakeThreadKey(bot, SRC_SAY_LOCAL, nullptr);
    time_t sayActivity = GetThreadLastActivity(sayKey);
    bool sayActive = ThreadIsActive(sayKey, g_TopicIdleSeconds);

    if (general)
    {
        std::string genKey = MakeThreadKey(bot, SRC_GENERAL_LOCAL, general);
        time_t genActivity = GetThreadLastActivity(genKey);
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
        outSource = SRC_GENERAL_LOCAL;
        outChannel = general;
        return genKey;
    }
    if (sayActive)
    {
        outSource = SRC_SAY_LOCAL;
        return sayKey;
    }

    outSource = SRC_SAY_LOCAL;
    return sayKey;
}
