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

SocialAct DetectSocialAct(std::string const& message);
bool IsActivityAsk(std::string const& message);
std::string PickActivityReply(Player* bot, std::string const& threadKey = {});
bool IsGuildInviteTalk(std::string const& message);
std::string PickGuildInviteReply(std::string const& threadKey = {});
bool LooksLikeAddonAnnounce(std::string const& message);
bool IsDismissal(std::string const& message);
std::string PickFollowUpReply(std::string const& threadKey = {});
std::string PickSocialReply(SocialAct act, std::string const& threadKey = {});
std::string PickSocialComment(SocialAct act, std::string const& threadKey = {});
std::string PickAmbientLifeLine(std::string const& threadKey = {});
std::string PickAmbientCityLine(std::string const& zone, std::string const& threadKey = {});
std::string PickAmbientZoneLine(std::string const& zone, std::string const& threadKey = {});
std::string PickAmbientQuestLine(std::string const& title, std::string const& threadKey = {});
std::string PickAmbientGroupLine(bool inGuild, std::string const& guildie, std::string const& threadKey = {});
std::string PickAmbientClassLine(std::string const& className, std::string const& threadKey = {});
std::string PickAmbientDungeonLine(std::string const& dungeon, std::string const& threadKey = {});
std::string PickGroupReply(std::string const& threadKey = {}, bool inParty = false);
bool LooksLikeSlangSalad(std::string const& line);
// Strip quotes/newlines and drop lines that are not typical WoW chat
// (essays, narration, later-expansion places). Empty means "do not send".
std::string SanitizeBotChatLine(std::string const& line);
void NoteSocialCue(Player* bot, SocialAct act);
bool BotHasSocialCue(Player* bot, SocialAct act);
void FilterSocialRecipients(std::vector<Player*>& candidates, std::vector<Player*> const& eligible,
                            std::string const& message, SocialAct act);

#endif
