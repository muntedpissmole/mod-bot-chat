#ifndef BOT_CHAT_RANDOM_H
#define BOT_CHAT_RANDOM_H

#include "ScriptMgr.h"

class BotChatRandom : public WorldScript
{
public:
    BotChatRandom();
    void OnUpdate(uint32 diff) override;

private:
    void HandleRandomChatter();
};

#endif // BOT_CHAT_RANDOM_H
