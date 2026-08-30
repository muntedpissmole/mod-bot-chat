#include "bot_chat_social.h"
#include "bot_chat_config.h"
#include "bot_chat_thread.h"
#include "bot_chat_util.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "Random.h"
#include "Map.h"
#include "DBCStores.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    struct Cue
    {
        SocialAct act = SocialAct::None;
        time_t when = 0;
    };

    std::mutex g_CueMutex;
    std::unordered_map<uint64, Cue> g_Cues;

    std::string ToLowerCopy(std::string text)
    {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    bool HasWord(std::string const& hay, std::string const& needle)
    {
        if (needle.empty())
            return false;
        size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos)
        {
            bool before = (pos == 0) || !std::isalnum(static_cast<unsigned char>(hay[pos - 1]));
            bool after = (pos + needle.size() >= hay.size()) ||
                         !std::isalnum(static_cast<unsigned char>(hay[pos + needle.size()]));
            if (before && after)
                return true;
            ++pos;
        }
        return false;
    }

    bool HasAnyWord(std::string const& hay, std::initializer_list<char const*> words)
    {
        for (char const* word : words)
        {
            if (HasWord(hay, word))
                return true;
        }
        return false;
    }

    bool LooksLikeHelp(std::string const& lower)
    {
        if (lower.find("where") != std::string::npos || lower.find("how do") != std::string::npos ||
            lower.find("how to") != std::string::npos)
            return true;
        if (HasAnyWord(lower, { "lfg", "lfm", "lf1m", "lf2m", "wts", "wtb" }))
            return true;
        return false;
    }

    bool NameMentioned(std::string const& lower, std::string const& name)
    {
        if (name.empty())
            return false;
        return HasWord(lower, ToLowerCopy(name));
    }

    std::string PickFrom(std::initializer_list<char const*> replies, std::string const& threadKey = {},
                         bool allowRepeat = true)
    {
        std::vector<char const*> all(replies);
        if (all.empty())
            return allowRepeat ? "ty" : "";

        std::vector<char const*> fresh;
        for (char const* line : all)
        {
            if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
                continue;
            fresh.push_back(line);
        }

        if (fresh.empty())
        {
            if (!allowRepeat)
                return "";
            fresh = all;
        }
        return fresh[urand(0, fresh.size() - 1)];
    }

    std::string StripPlacePrefix(std::string name)
    {
        while (name.size() > 4 &&
               (name.compare(0, 4, "The ") == 0 || name.compare(0, 4, "the ") == 0))
            name.erase(0, 4);
        while (name.size() > 2 && (name.compare(0, 2, "A ") == 0 || name.compare(0, 2, "a ") == 0))
            name.erase(0, 2);
        return name;
    }

    std::string FirstPlaceWord(std::string name)
    {
        name = StripPlacePrefix(name);
        std::string out;
        for (unsigned char c : name)
        {
            if (std::isspace(c))
                break;
            if (std::isalnum(c) || c == '\'')
                out.push_back(static_cast<char>(std::tolower(c)));
        }
        return out.empty() ? "here" : out;
    }

    std::string ShortPlacePhrase(std::string name)
    {
        name = StripPlacePrefix(name);
        std::string out;
        unsigned spaces = 0;
        for (unsigned char c : name)
        {
            if (std::isspace(c))
            {
                if (!out.empty() && out.back() != ' ')
                {
                    out.push_back(' ');
                    ++spaces;
                }
                if (spaces >= 2)
                    break;
                continue;
            }
            if (std::isalnum(c) || c == '\'')
                out.push_back(static_cast<char>(std::tolower(c)));
        }
        if (!out.empty() && out.back() == ' ')
            out.pop_back();
        if (out.empty())
            return "here";
        if (out.size() > 18)
            return FirstPlaceWord(out);
        return out;
    }

    std::string PickSlotted(std::initializer_list<std::string> lines, std::string const& threadKey)
    {
        std::vector<std::string> fresh;
        for (std::string const& line : lines)
        {
            if (line.empty())
                continue;
            if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
                continue;
            fresh.push_back(line);
        }
        if (fresh.empty())
            return "";
        return fresh[urand(0, fresh.size() - 1)];
    }

    std::string BotPlaceName(Player* bot)
    {
        if (!bot)
            return "here";
        if (bot->GetMap() && bot->GetMap()->IsDungeon())
        {
            std::string const mapName = bot->GetMap()->GetMapName();
            if (!mapName.empty())
                return FirstPlaceWord(mapName);
        }
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(bot->GetAreaId()))
        {
            if (area->area_name[0] && area->area_name[0][0])
                return FirstPlaceWord(area->area_name[0]);
        }
        if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
        {
            if (zone->area_name[0] && zone->area_name[0][0])
                return FirstPlaceWord(zone->area_name[0]);
        }
        return "here";
    }

    std::string ShortQuestHint(std::string const& title)
    {
        if (title.empty())
            return "";
        std::string hint = StripPlacePrefix(title);
        size_t colon = hint.rfind(':');
        if (colon != std::string::npos && colon + 2 < hint.size())
            hint = StripPlacePrefix(hint.substr(colon + 2));
        std::string out;
        unsigned words = 0;
        bool inWord = false;
        for (unsigned char c : hint)
        {
            if (std::isspace(c))
            {
                inWord = false;
                if (words >= 3)
                    break;
                if (!out.empty() && out.back() != ' ')
                    out.push_back(' ');
                continue;
            }
            if (!inWord)
            {
                ++words;
                inWord = true;
            }
            out.push_back(static_cast<char>(std::tolower(c)));
            if (out.size() >= 24)
                break;
        }
        if (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }
}

SocialAct DetectSocialAct(std::string const& message)
{
    if (message.empty())
        return SocialAct::None;

    std::string lower = ToLowerCopy(message);
    if (LooksLikeHelp(lower))
        return SocialAct::None;

    // Long messages are real chat, not a speech-act token.
    if (lower.size() > 72)
        return SocialAct::None;

    if (HasAnyWord(lower, { "tyvm", "tysm", "ty", "thx", "thanks" }) ||
        lower.find("thank you") != std::string::npos)
        return SocialAct::Thanks;

    if (HasAnyWord(lower, { "grats", "gratz", "congrats", "congrat", "congratz", "gz", "gzz", "gj" }) ||
        lower.find("good job") != std::string::npos || lower.find("well done") != std::string::npos ||
        lower.find("nice ding") != std::string::npos || lower.find("nice one") != std::string::npos)
        return SocialAct::Congrats;

    if (HasAnyWord(lower, { "wb" }) || lower.find("welcome back") != std::string::npos)
        return SocialAct::WelcomeBack;

    if (HasAnyWord(lower, { "welc", "welcome" }))
        return SocialAct::Welcome;

    if (HasAnyWord(lower, { "glhf", "gl", "hf" }) || lower.find("good luck") != std::string::npos ||
        lower.find("have fun") != std::string::npos)
        return SocialAct::GoodLuck;

    if (HasAnyWord(lower, { "gg", "wp" }) || lower.find("good game") != std::string::npos)
        return SocialAct::GoodGame;

    if (HasAnyWord(lower, { "sorry", "mb", "oops" }) || lower.find("my bad") != std::string::npos)
        return SocialAct::Apology;

    if (HasAnyWord(lower, { "rip", "f", "oof" }))
        return SocialAct::Condolence;

    if (HasWord(lower, "re"))
        return SocialAct::Back;

    if (HasAnyWord(lower, { "bye", "cya", "gn", "nite", "night", "later", "bb" }))
        return SocialAct::Farewell;

    if (HasAnyWord(lower, { "hi", "hey", "hello", "yo", "sup", "hiya" }) ||
        lower.find("whats up") != std::string::npos || lower.find("what's up") != std::string::npos)
        return SocialAct::Greeting;

    if (HasAnyWord(lower, { "brb", "afk", "bio" }))
        return SocialAct::Brb;

    if (HasAnyWord(lower, { "yawn", "bored", "tired", "lol", "lmao", "rofl" }))
        return SocialAct::Reaction;

    return SocialAct::None;
}

bool IsActivityAsk(std::string const& message)
{
    if (message.empty())
        return false;
    std::string lower = ToLowerCopy(message);
    if (HasWord(lower, "wyd"))
        return true;
    if (lower.find("what u want") != std::string::npos || lower.find("what you want") != std::string::npos ||
        lower.find("whatcha want") != std::string::npos || lower.find("what do you want") != std::string::npos ||
        lower.find("wut u want") != std::string::npos)
        return true;
    if (lower.find("where u at") != std::string::npos || lower.find("where are you") != std::string::npos ||
        lower.find("where you at") != std::string::npos)
        return true;
    if (lower.find("hows it going") != std::string::npos || lower.find("how's it going") != std::string::npos ||
        lower.find("how are you") != std::string::npos || lower.find("how r u") != std::string::npos ||
        lower.find("how u doin") != std::string::npos || lower.find("how you doing") != std::string::npos ||
        lower.find("whatcha doing") != std::string::npos || lower.find("what you doing") != std::string::npos)
        return true;
    if (LooksLikeHelp(lower))
        return false;
    bool asking = lower.find("what") != std::string::npos || HasWord(lower, "wat") ||
                  lower.find("everyone") != std::string::npos || lower.find("guys") != std::string::npos;
    if (asking && (lower.find("doing") != std::string::npos || lower.find("up to") != std::string::npos))
        return true;
    return false;
}

std::string PickActivityReply(Player* bot, std::string const& threadKey)
{
    if (!bot)
        return PickFrom({ "nothing much", "same old", "just chilling", "idk" }, threadKey);

    auto tryLine = [&](std::string const& line) -> std::string
    {
        if (line.empty())
            return "";
        if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
            return "";
        return line;
    };

    if (bot->isDead())
        return PickFrom({ "dead", "ghost", "running back", "corpse run" }, threadKey);
    if (bot->GetMap() && bot->GetMap()->IsDungeon())
    {
        if (urand(0, 99) < 45)
        {
            std::string line = tryLine("in " + FirstPlaceWord(bot->GetMap()->GetMapName()));
            if (!line.empty())
                return line;
        }
        return PickFrom({ "in a run", "dungeon", "in an instance" }, threadKey);
    }
    if (bot->IsInCombat())
        return PickFrom({ "fighting", "busy", "in combat", "pulling" }, threadKey);
    if (bot->IsInFlight())
        return PickFrom({ "flying", "on a flight" }, threadKey);
    if (bot->HasPlayerFlag(PLAYER_FLAGS_RESTING))
    {
        if (urand(0, 99) < 40)
        {
            std::string line = tryLine("in " + BotPlaceName(bot));
            if (!line.empty())
                return line;
        }
        return PickFrom({ "in town", "inn", "ah", "afk town" }, threadKey);
    }
    if (bot->GetGroup())
        return PickFrom({ "grouped", "got a grp", "in a group" }, threadKey);

    if (urand(0, 99) < 35)
    {
        std::string line = tryLine("in " + BotPlaceName(bot));
        if (!line.empty())
            return line;
    }
    return PickFrom({ "questing", "grinding", "nmu", "nothing much", "same old" }, threadKey);
}

bool IsGuildInviteTalk(std::string const& message)
{
    if (message.empty())
        return false;
    std::string lower = ToLowerCopy(message);
    if (LooksLikeHelp(lower))
        return false;
    if (lower.find("guild") != std::string::npos)
        return true;
    if (lower.find("invite") != std::string::npos || HasWord(lower, "inv"))
        return true;
    if (lower.find("recruit") != std::string::npos)
        return true;
    return false;
}

std::string PickGuildInviteReply(std::string const& threadKey)
{
    return PickFrom({ "pst", "pst for inv", "whisper me", "sure pst", "can inv", "got room pst" }, threadKey);
}

bool LooksLikeAddonAnnounce(std::string const& message)
{
    if (message.empty())
        return false;

    std::string const lower = ToLowerCopy(message);
    if (lower.find("questie:") != std::string::npos || lower.find("pfquest") != std::string::npos)
        return true;
    if (HasWord(lower, "questie"))
        return true;
    if (lower.find("[[") != std::string::npos && lower.find("]]") != std::string::npos)
        return true;

    size_t i = 0;
    while (i < message.size() && std::isspace(static_cast<unsigned char>(message[i])))
        ++i;
    if (i < message.size() && message[i] == '{')
    {
        std::string const rest = lower.substr(i);
        if (rest.compare(0, 3, "{rt") == 0 || rest.compare(0, 5, "{star") == 0 ||
            rest.compare(0, 7, "{circle") == 0 || rest.compare(0, 6, "{skull") == 0)
            return true;
    }
    return false;
}

bool IsDismissal(std::string const& message)
{
    if (message.empty())
        return false;
    std::string const lower = ToLowerCopy(message);
    if (HasAnyWord(lower, { "stfu", "idc", "idgaf", "nvm" }))
        return true;
    if (lower.find("shut up") != std::string::npos || lower.find("shut it") != std::string::npos)
        return true;
    if (lower.find("be quiet") != std::string::npos || lower.find("stop talking") != std::string::npos)
        return true;
    if (lower == "w/e" || lower == "w.e" || lower == "whatever")
        return true;
    return false;
}

std::string PickFollowUpReply(std::string const& threadKey)
{
    return PickFrom({ "huh", "?", "wdym", "n", "what" }, threadKey, false);
}

std::string PickSocialReply(SocialAct act, std::string const& threadKey)
{
    switch (act)
    {
        case SocialAct::Congrats:
        case SocialAct::WelcomeBack:
            return PickFrom({ "ty", "thx", "tyvm", "ty guys", "ha ty" }, threadKey);
        case SocialAct::Welcome:
            return PickFrom({ "ty", "hi", "hey" }, threadKey);
        case SocialAct::Thanks:
            return PickFrom({ "np", "yw", "np np", "sure" }, threadKey);
        case SocialAct::Greeting:
            return PickFrom({ "hey", "hi", "yo", "sup" }, threadKey);
        case SocialAct::Farewell:
            return PickFrom({ "cya", "later", "gn", "bb", "night" }, threadKey);
        case SocialAct::GoodLuck:
            return PickFrom({ "ty", "gl", "u2" }, threadKey);
        case SocialAct::GoodGame:
            return PickFrom({ "gg", "wp", "ty" }, threadKey);
        case SocialAct::Condolence:
            return PickFrom({ "lol", "re", "f", "oof" }, threadKey);
        case SocialAct::Apology:
            return PickFrom({ "np", "all g", "nbd" }, threadKey);
        case SocialAct::Back:
            return PickFrom({ "wb", "ty", "hey" }, threadKey);
        case SocialAct::Brb:
            return PickFrom({ "kk", "o", "k", "cya" }, threadKey);
        case SocialAct::Reaction:
            return PickFrom({ "lol", "same", "yep", "true", "lmao", "n" }, threadKey);
        default:
            return "";
    }
}

std::string PickSocialComment(SocialAct act, std::string const& threadKey)
{
    switch (act)
    {
        case SocialAct::Congrats:
            return PickFrom({ "gz", "gj", "nice", "grats" }, threadKey);
        case SocialAct::WelcomeBack:
            return PickFrom({ "wb", "hey", "re" }, threadKey);
        case SocialAct::Welcome:
            return PickFrom({ "welc", "hey", "wb", "hi" }, threadKey);
        case SocialAct::Condolence:
            return PickFrom({ "rip", "f", "re", "oof" }, threadKey);
        default:
            return PickSocialReply(act, threadKey);
    }
}

std::string PickAmbientLifeLine(std::string const& threadKey)
{
    return PickFrom({
        "brb food", "brb 5", "one more then bed", "work in the morning",
        "kids yelling", "lagging hard", "dc'd earlier", "need a smoke",
        "gonna grab a drink", "afk dog", "brb bio", "eyes hurting",
        "been on too long", "this coffee isnt working", "wife wants the pc",
        "phone call", "dinner soon", "brb shower", "queue popping",
        "client froze", "fps dying", "one more quest", "almost done tonight",
        "gonna log soon", "one more then im off", "lag spikes", "dc incoming"
    }, threadKey, false);
}

std::string PickAmbientCityLine(std::string const& zone, std::string const& threadKey)
{
    std::string const place = ShortPlacePhrase(zone);
    std::string slotted = PickSlotted({
        "in " + place,
        "ah in " + place,
        "bored in " + place,
        "training in " + place,
        "repairing then out",
        "bank then " + place + " ah"
    }, threadKey);
    if (!slotted.empty() && urand(0, 99) < 70)
        return slotted;
    return PickFrom({
        "ah then out", "bank then out", "training", "repairing",
        "heading out", "bored in town", "vendor then go", "ah",
        "need to train", "afk town", "selling junk", "repair bill hurts"
    }, threadKey, false);
}

std::string PickAmbientZoneLine(std::string const& zone, std::string const& threadKey)
{
    std::string const place = ShortPlacePhrase(zone);
    std::string slotted = PickSlotted({
        place + " is slow",
        "grinding " + place,
        place + " again",
        "anyone in " + place,
        "xp is ok in " + place,
        "mobs in " + place + " suck",
        "need a grp in " + place,
        "almost done " + place
    }, threadKey);
    if (!slotted.empty() && urand(0, 99) < 75)
        return slotted;
    return PickFrom({
        "this zone is slow", "grinding here", "anyone here",
        "mobs suck", "almost done here", "this zone sucks",
        "need a grp here", "drops are trash", "xp is ok here"
    }, threadKey, false);
}

std::string PickAmbientQuestLine(std::string const& title, std::string const& threadKey)
{
    std::string const hint = ShortQuestHint(title);
    if (!hint.empty())
    {
        std::string slotted = PickSlotted({
            "still on " + hint,
            "anyone on " + hint,
            hint + " is annoying",
            "almost done " + hint,
            "need a hand with " + hint
        }, threadKey);
        if (!slotted.empty() && urand(0, 99) < 75)
            return slotted;
    }
    return PickFrom({
        "anyone on this quest", "this quest is annoying", "need a hand with this",
        "almost done this quest", "this one sucks", "anyone else on this"
    }, threadKey, false);
}

std::string PickAmbientGroupLine(bool inGuild, std::string const& guildie, std::string const& threadKey)
{
    if (inGuild && !guildie.empty() && urand(0, 99) < 55)
    {
        std::string slotted = PickSlotted({
            "wb " + guildie,
            "hey " + guildie,
            guildie + " wanna run",
            "gl " + guildie
        }, threadKey);
        if (!slotted.empty())
            return slotted;
    }
    if (inGuild)
        return PickFrom({ "need 1", "lf1m", "going in", "anyone for a run", "one more then bed" }, threadKey, false);
    return PickFrom({ "need 1", "lf1m", "going in", "ready" }, threadKey, false);
}

std::string PickAmbientClassLine(std::string const& className, std::string const& threadKey)
{
    std::string const cls = className.empty() ? "this class" : className;
    std::string slotted = PickSlotted({
        cls + " is slow",
        "this " + cls + " spec feels off",
        "anyone else play " + cls,
        cls + " grinding is rough"
    }, threadKey);
    if (!slotted.empty())
        return slotted;
    return PickFrom({ "this class is slow", "hate this spec", "respec soon" }, threadKey, false);
}

std::string PickAmbientDungeonLine(std::string const& dungeon, std::string const& threadKey)
{
    std::string const place = ShortPlacePhrase(dungeon);
    std::string slotted = PickSlotted({
        "in " + place,
        place + " is easy",
        place + " again",
        "this run is slow",
        "need a rez in " + place
    }, threadKey);
    if (!slotted.empty() && urand(0, 99) < 75)
        return slotted;
    return PickFrom({ "in a run", "this run is slow", "need a rez", "boss next" }, threadKey, false);
}

std::string PickGroupReply(std::string const& threadKey, bool inParty)
{
    if (inParty)
        return PickFrom({ "yeah", "me", "on it", "same", "kk" }, threadKey, false);
    return PickFrom({ "pst", "what for", "gl", "inv", "need 1?", "busy" }, threadKey, false);
}

bool LooksLikeSlangSalad(std::string const& line)
{
    std::string lower = ToLowerCopy(line);
    static char const* tokens[] = {
        "gz", "wb", "brb", "lfm", "lfg", "ty", "lol", "gj", "gl", "hf",
        "tbwz", "tbh", "ngl", "omw", "tyvm"
    };
    uint32 hits = 0;
    for (char const* token : tokens)
    {
        if (HasWord(lower, token))
            ++hits;
    }
    return hits >= 3;
}

bool LooksUnlikeWowChat(std::string const& line)
{
    if (line.size() > 70)
        return true;

    unsigned words = 0;
    bool inWord = false;
    unsigned commas = 0;
    unsigned periods = 0;
    for (unsigned char c : line)
    {
        if (std::isspace(c))
            inWord = false;
        else if (!inWord)
        {
            ++words;
            inWord = true;
        }
        if (c == ',')
            ++commas;
        if (c == '.')
            ++periods;
    }
    if (words > 12 || commas > 1 || periods > 1)
        return true;

    std::string const lower = ToLowerCopy(line);
    static char const* bans[] = {
        "i think", "i believe", "that's not", "that is not",
        "nearby", "the area", "for some",
        "valley of the four", "pandaria", "draenor", "broken isles",
        "kul tiras", "zuldazar", "shadowlands", "dragon isles",
        "khaz algar", "garrosh", "stocked with", "exactly",
        "restrooms", "office", "narrat", "hanging out", "just hanging",
        "questie", "pretty sweet", "deserves", "got my first", "just got the",
        "what's next", "whats next", "good reward", "shut up", "stfu"
    };
    for (char const* ban : bans)
    {
        if (lower.find(ban) != std::string::npos)
            return true;
    }
    if (lower.rfind("going to", 0) == 0 || lower.rfind("i am ", 0) == 0 ||
        lower.rfind("i'll ", 0) == 0 || lower.rfind("there is ", 0) == 0)
        return true;
    return false;
}

std::string SanitizeBotChatLine(std::string const& line)
{
    std::string out = line;
    size_t nl = out.find('\n');
    if (nl != std::string::npos)
        out.resize(nl);
    while (!out.empty() && (out.front() == '"' || out.front() == '\'' || out.front() == '[' ||
                            std::isspace(static_cast<unsigned char>(out.front()))))
        out.erase(out.begin());
    while (!out.empty() && (out.back() == '"' || out.back() == '\'' || out.back() == '.' ||
                            out.back() == ']' || std::isspace(static_cast<unsigned char>(out.back()))))
        out.pop_back();
    // Models often prefix a slash command (`/g going in`). Strip those.
    while (!out.empty() && out.front() == '/')
    {
        size_t space = out.find(' ');
        if (space == std::string::npos)
            return "";
        out.erase(0, space + 1);
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())))
            out.erase(out.begin());
    }
    if (out.empty() || LooksLikeSlangSalad(out) || LooksUnlikeWowChat(out))
        return "";
    return out;
}

void NoteSocialCue(Player* bot, SocialAct act)
{
    if (!bot || act == SocialAct::None)
        return;
    if (!PlayerbotsMgr::instance().GetPlayerbotAI(bot))
        return;

    std::lock_guard<std::mutex> lock(g_CueMutex);
    g_Cues[bot->GetGUID().GetRawValue()] = { act, time(nullptr) };
}

bool BotHasSocialCue(Player* bot, SocialAct act)
{
    if (!bot || act == SocialAct::None)
        return false;

    uint32 ttl = g_SocialCueSeconds ? g_SocialCueSeconds : 45;
    std::lock_guard<std::mutex> lock(g_CueMutex);
    auto it = g_Cues.find(bot->GetGUID().GetRawValue());
    if (it == g_Cues.end())
        return false;
    if (it->second.act != act)
        return false;
    if (difftime(time(nullptr), it->second.when) > ttl)
    {
        g_Cues.erase(it);
        return false;
    }
    return true;
}

void FilterSocialRecipients(std::vector<Player*>& candidates, std::vector<Player*> const& eligible,
                            std::string const& message, SocialAct act)
{
    if (act == SocialAct::None)
        return;

    std::string lower = ToLowerCopy(message);
    std::vector<Player*> mentioned;
    std::vector<Player*> cued;

    auto consider = [&](Player* bot)
    {
        if (!bot)
            return;
        if (NameMentioned(lower, bot->GetName()))
            mentioned.push_back(bot);
        if (BotHasSocialCue(bot, act))
            cued.push_back(bot);
    };

    for (Player* bot : eligible)
        consider(bot);

    if (!mentioned.empty())
    {
        candidates = mentioned;
        return;
    }

    // grats / wb / rip / hi should hit the bot the line is about, not a random neighbour
    if ((act == SocialAct::Congrats || act == SocialAct::WelcomeBack ||
         act == SocialAct::Welcome || act == SocialAct::Condolence ||
         act == SocialAct::Greeting) && !cued.empty())
    {
        candidates = cued;
        return;
    }

    if (act == SocialAct::Brb && urand(0, 99) >= 35)
        candidates.clear();
}

