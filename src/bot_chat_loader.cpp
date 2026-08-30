#include "bot_chat_config.h"
#include "bot_chat_handler.h"
#include "bot_chat_random.h"
#include "bot_chat_events.h"
#include "Log.h"

void Addmod_bot_chatScripts()
{
    LOG_INFO("server.loading", "[Bot Chat] Registering mod-bot-chat.");
    new BotChatConfigWorldScript();
    new BotChatHandler();
    new BotChatRandom();
    new ChatOnKill();
    new ChatOnLoot();
    new ChatOnDeath();
    new ChatOnQuest();
    new ChatOnLearn();
    new ChatOnDuel();
    new ChatOnLevelUp();
    new ChatOnAchievement();
    new ChatOnGameObjectUse();
    new ChatOnGuildMemberChange();
}
