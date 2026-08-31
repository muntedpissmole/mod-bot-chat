#ifndef BOT_CHAT_LLM_H
#define BOT_CHAT_LLM_H

#include "bot_chat_handler.h"
#include "bot_chat_social.h"
#include <string>
#include <vector>

class Player;
class Channel;

// One short Wrath line from the local Ollama model. Rules still decide whether
// we talk; this only fills the sentence. Empty fallback + failed query = quiet.
bool TryBotChatLlmReply(std::vector<Player*> const& bots, Player* sender, std::string const& msg,
                        ChatTone tone, ChatChannelSourceLocal sourceLocal, Channel* channel,
                        std::string const& fallback);

// Wording for a bot-to-bot continue. Engine already decided we should talk.
bool TryBotChatLlmContinue(Player* bot, std::string const& lastMsg,
                           ChatChannelSourceLocal sourceLocal, Channel* channel,
                           std::string const& threadKey, std::string const& fallback);

// Guild hangout starter. Engine picked the bot and a canned vibe; the model
// writes the line. Busy/fail uses fallback. Not for LFG or General topics.
bool TryBotChatLlmStart(Player* bot, std::string const& hint,
                        ChatChannelSourceLocal sourceLocal, Channel* channel,
                        std::string const& threadKey, std::string const& fallback);

#endif
