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

std::string MakeThreadKey(Player* player, ChatChannelSourceLocal source, Channel* channel);
Channel* FindPlayerChannel(Player* player, char const* namePrefix);
void AppendChannelThread(const std::string& key, const std::string& speaker, uint64_t speakerGuid, bool isBot, const std::string& text);
std::string FormatChannelThread(const std::string& key, const std::string& selfName, uint32 maxPromptLines = 6);
std::string GetLastThreadText(const std::string& key);
bool ThreadIsActive(const std::string& key, uint32_t idleSeconds);
bool IsRecentSpeaker(const std::string& key, uint64_t speakerGuid);
uint64_t GetLastBotSpeaker(const std::string& key);
time_t GetThreadLastActivity(const std::string& key);
std::vector<uint64_t> GetRecentSpeakers(const std::string& key);
std::string GetLastPlayerMessageExcept(const std::string& key, const std::string& exceptText);
std::string GetLastPlayerHelpQuery(const std::string& key);
bool LineTooSimilarToRecent(const std::string& key, const std::string& text, uint32 lookback = 12);
void ClearChannelThreads();

// Pick the most relevant ambient thread for a bot that is about to start talking on its own.
std::string GuessAmbientThreadKey(Player* bot, ChatChannelSourceLocal& outSource, Channel*& outChannel);

#endif
