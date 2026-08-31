#ifndef BOT_CHAT_SOCIAL_H
#define BOT_CHAT_SOCIAL_H

#include "Define.h"
#include <string>
#include <vector>

class Player;

// Conventional Wrath-era speech acts. These get a canned /g-style reply
// instead of a full LLM line (grats -> ty, wb -> ty, ty -> np, ...).
enum class SocialAct
{
    None = 0,
    Congrats,
    WelcomeBack,
    Welcome,
    Thanks,
    Greeting,
    Farewell,
    GoodLuck,
    GoodGame,
    Condolence,
    Apology,
    Back,
    Brb,
    Reaction
};

enum class ChatTone
{
    Neutral = 0,
    Hostile,
    Dismissive
};

enum class ChatLineStyle
{
    Normal = 0,
    Flame,
    Guild
};

// Stable per-bot General mouth. Same guid always sounds like the same kind of chatter.
enum class BotChatMouth
{
    Mix = 0,
    Quiet,
    Chatty,
    Lfg,
    Salt
};

SocialAct DetectSocialAct(std::string const& message);
ChatTone DetectChatTone(std::string const& message);
bool IsHostileTalk(std::string const& message);
bool IsActivityAsk(std::string const& message);
std::string PickActivityReply(Player* bot, std::string const& threadKey = {});
bool IsGuildInviteTalk(std::string const& message);
std::string PickGuildInviteReply(std::string const& threadKey = {});
bool LooksLikeAddonAnnounce(std::string const& message);
bool IsDismissal(std::string const& message);
std::string PickFollowUpReply(std::string const& threadKey = {});
std::string PickHostileReply(std::string const& threadKey = {}, std::string const& playerMsg = {});
std::string PickCloserReply(std::string const& threadKey = {});
std::string PickContinueLine(std::string const& threadKey = {});
std::string PickContinueForLast(std::string const& last, std::string const& threadKey = {},
                                std::string const& speaker = {}, bool speakerIsBot = false,
                                uint32 topicId = 0);
std::string PickDribbleContinue(std::string const& last, std::string const& threadKey = {},
                                std::string const& speaker = {}, bool speakerIsBot = false,
                                uint32 topicId = 0);
bool LooksLikeArgument(std::string const& message);
BotChatMouth MouthForBot(Player* bot);
bool UsesProfanity(std::string const& message);
bool UsesSlur(std::string const& message);
uint32 BotChatPopulationPct();
uint32 ScaleChatChance(uint32 chance);
uint32 ScaleChatInterval(uint32 seconds);
std::string PickSocialReply(SocialAct act, std::string const& threadKey = {});
std::string PickSocialComment(SocialAct act, std::string const& threadKey = {});
std::string PickAmbientLifeLine(std::string const& threadKey = {});
std::string PickAmbientGuildLine(std::string const& threadKey = {});
std::string PickAmbientWorldLine(std::string const& threadKey = {});
std::string PickAmbientDribbleLine(std::string const& threadKey = {}, std::string const& className = {},
                                  std::string const& questTitle = {}, std::string const& zone = {},
                                  Player* bot = nullptr, std::string const& nearbyMob = {});
bool LooksLikeDribble(std::string const& message);
std::string PickAmbientCityLine(std::string const& zone, std::string const& threadKey = {});
std::string PickAmbientZoneLine(std::string const& zone, std::string const& threadKey = {});
std::string PickAmbientQuestLine(std::string const& title, std::string const& threadKey = {});
std::string PickAmbientGroupLine(bool inGuild, std::string const& guildie, std::string const& threadKey = {},
                                 std::string const& zone = {});
std::string PickAmbientClassLine(std::string const& className, std::string const& threadKey = {});
std::string PickAmbientDungeonLine(std::string const& dungeon, std::string const& threadKey = {});
std::string PickGroupReply(std::string const& threadKey = {}, bool inParty = false, Player* bot = nullptr);
bool IsGroupAsk(std::string const& message);
bool IsGroupJoin(std::string const& message);
std::string PickGroupPurpose(Player* bot, std::string const& threadKey = {});
std::string PickGroupChime(Player* bot, std::string const& threadKey = {});
bool LooksLikeSlangSalad(std::string const& line);
// Strip quotes/newlines and drop lines that are not typical WoW chat
// (essays, narration, later-expansion places). Empty means "do not send".
// Flame style allows roast words the normal filter rejects (stfu parroting).
std::string SanitizeBotChatLine(std::string const& line, ChatLineStyle style = ChatLineStyle::Normal);
void NoteSocialCue(Player* bot, SocialAct act);
bool BotHasSocialCue(Player* bot, SocialAct act);
void FilterSocialRecipients(std::vector<Player*>& candidates, std::vector<Player*> const& eligible,
                            std::string const& message, SocialAct act);
// Sleep as if the bot is typing. Call from a worker thread, then re-resolve pointers.
void BotChatTypingSleep(size_t charCount);

#endif
