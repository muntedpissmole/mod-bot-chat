#ifndef BOT_CHAT_KNOWLEDGE_H
#define BOT_CHAT_KNOWLEDGE_H

#include <string>
#include <cstdint>

class Player;

// High-level: should we treat this as a help question, a normal question, or chatter?
enum class ChatIntent
{
    Social = 0,
    Question,
    HelpRequest
};

// What kind of question it is. Slots are filled from the text; answers come from
// live AzerothCore / taxi / spawn data, not per-destination if/else lists.
enum class ChatTopic
{
    Social = 0,
    FollowUp,
    FindService,      // where fp / trainer / vendor / inn / ah / bank / mail
    TravelTo,         // how do I get to X, where is [zone/city]
    FindEntity,       // where is [named npc/mob]
    QuestHelp,        // turn-in, where is this quest
    LookingForGroup,  // lfg / anyone for quests / need tank
    Trade             // wts / wtb
};

struct ChatQuery
{
    ChatIntent intent = ChatIntent::Social;
    ChatTopic topic = ChatTopic::Social;
    uint32_t serviceFlag = 0;
    std::string serviceLabel;
    std::string entity;
};

ChatQuery ParseChatQuery(const std::string& message);
ChatIntent ClassifyChatIntent(const std::string& message);
bool IsShortFollowUp(const std::string& message);
bool IsAck(const std::string& message);
std::string DescribeBotPlace(Player* bot);
void InitializeGameKnowledge();
std::string LookupGameKnowledge(Player* asker, const std::string& message);
// Short /s line from live taxi/NPC/quest data. Empty means stay quiet.
std::string PickKnowledgeReply(Player* asker, const std::string& message);

#endif
