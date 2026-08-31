#ifndef BOT_CHAT_THREAD_H
#define BOT_CHAT_THREAD_H

#include "bot_chat_handler.h"
#include <cstdint>
#include <string>
#include <vector>
#include <ctime>

class Player;
class Channel;

struct ChannelThreadLine
{
    std::string speaker;
    uint64_t speakerGuid = 0;
    bool isBot = false;
    std::string text;
    time_t timestamp = 0;
};

std::string MakeThreadKey(Player* player, ChatChannelSourceLocal source, Channel* channel, Player* peer = nullptr);
Channel* FindPlayerChannel(Player* player, char const* namePrefix);
Channel* FindZoneGeneral(Player* player);
uint32 BotLiveZoneId(Player* player);
bool ChannelBelongsToBotZone(Player* player, std::string const& channelName);
void AppendChannelThread(const std::string& key, const std::string& speaker, uint64_t speakerGuid, bool isBot, const std::string& text);
std::string FormatChannelThread(const std::string& key, const std::string& selfName, uint32 maxPromptLines = 6);
// Guild/party/say/whisper this player and bot both saw. Empty if nothing recent.
std::string FormatSharedThread(Player* player, Player* bot, std::string const& currentKey,
                               std::string const& selfName, uint32 maxPromptLines = 8);
std::vector<std::string> SharedThreadKeys(Player* player, Player* other);
std::string GetLastThreadText(const std::string& key);
bool ThreadIsActive(const std::string& key, uint32_t idleSeconds);
bool IsRecentSpeaker(const std::string& key, uint64_t speakerGuid);
uint64_t GetLastBotSpeaker(const std::string& key);
// Last bot this player could be answering, if that bot is in `candidates`.
uint64_t FindRecentSharedBotSpeaker(Player* player, std::vector<Player*> const& candidates, uint32 idleSeconds);
void NoteConversationBond(uint64_t playerGuid, uint64_t botGuid);
uint64_t GetConversationBond(uint64_t playerGuid, uint32 idleSeconds);
time_t GetThreadLastActivity(const std::string& key);
std::vector<uint64_t> GetRecentSpeakers(const std::string& key);
std::string GetLastPlayerMessageExcept(const std::string& key, const std::string& exceptText);
std::string GetLastPlayerMessageExceptAmong(std::vector<std::string> const& keys, std::string const& exceptText);
std::string GetLastPlayerHelpQuery(const std::string& key);
std::string GetLastPlayerHelpQueryAmong(std::vector<std::string> const& keys);
bool LineTooSimilarToRecent(const std::string& key, const std::string& text, uint32 lookback = 12);
bool LineRecentlySpoken(const std::string& text);
void NoteSpokenLine(const std::string& text);
uint32 CountTrailingBotLines(const std::string& key);
bool LastThreadSpeakerIsPlayer(const std::string& key);
bool ThreadLooksLooped(const std::string& key);
uint32 CountRecentPlayerLines(const std::string& key, uint32_t windowSeconds = 180);
bool PlayerSpokeRecently(const std::string& key, uint32_t withinSeconds = 25);
ChannelThreadLine GetLastThreadLine(const std::string& key);
void ClearChannelThreads();

// Pick the most relevant ambient thread for a bot that is about to start talking on its own.
std::string GuessAmbientThreadKey(Player* bot, ChatChannelSourceLocal& outSource, Channel*& outChannel);

#endif
