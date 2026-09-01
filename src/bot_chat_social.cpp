#include "bot_chat_social.h"
#include "bot_chat_config.h"
#include "bot_chat_thread.h"
#include "bot_chat_util.h"
#include "bot_chat_dribble_pool.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "Map.h"
#include "DBCStores.h"
#include "DBCEnums.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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
                         bool allowRepeat = true, bool realmUnique = false)
    {
        (void)realmUnique;
        std::vector<char const*> all(replies);
        if (all.empty())
            return allowRepeat ? "ty" : "";

        std::vector<char const*> fresh;
        for (char const* line : all)
        {
            if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
                continue;
            if (LineRecentlySpoken(line))
                continue;
            fresh.push_back(line);
        }

        if (fresh.empty())
        {
            if (!allowRepeat)
                return "";
            fresh = all;
        }
        std::string const picked = fresh[urand(0, fresh.size() - 1)];
        NoteSpokenLine(picked);
        return picked;
    }

    std::string PickFromArray(char const* const* replies, size_t count, std::string const& threadKey)
    {
        if (!count)
            return "";
        std::vector<char const*> fresh;
        for (size_t i = 0; i < count; ++i)
        {
            if (!replies[i] || !replies[i][0])
                continue;
            if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, replies[i]))
                continue;
            if (LineRecentlySpoken(replies[i]))
                continue;
            fresh.push_back(replies[i]);
        }
        if (fresh.empty())
            return "";
        std::string const picked = fresh[urand(0, fresh.size() - 1)];
        NoteSpokenLine(picked);
        return picked;
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

    // How people actually say a zone. Empty = don't name it (you're already there).
    std::string ChatZoneNick(std::string const& zone)
    {
        std::string n = ToLowerCopy(zone);
        auto has = [&](char const* s) { return n.find(s) != std::string::npos; };
        if (has("barrens")) return "barrens";
        if (has("stranglethorn") || has("stv")) return "stv";
        if (has("un'goro") || has("ungoro")) return "ungoro";
        if (has("searing")) return "searing";
        if (has("tanaris")) return "tanaris";
        if (has("felwood")) return "felwood";
        if (has("winterspring")) return "winterspring";
        if (has("silithus")) return "silithus";
        if (has("hellfire")) return "hellfire";
        if (has("zangarmarsh") || has("zangar")) return "zangar";
        if (has("terokkar")) return "terokkar";
        if (has("nagrand")) return "nagrand";
        if (has("netherstorm")) return "netherstorm";
        if (has("shadowmoon")) return "shadowmoon";
        if (has("borean")) return "borean";
        if (has("fjord")) return "fjord";
        if (has("dragonblight")) return "dragonblight";
        if (has("grizzly")) return "grizzly";
        if (has("zul'drak") || has("zuldrak")) return "zuldrak";
        if (has("sholazar")) return "sholazar";
        if (has("storm peaks") || has("the storm peaks")) return "peaks";
        if (has("icecrown")) return "icecrown";
        if (has("westfall")) return "westfall";
        if (has("duskwood")) return "duskwood";
        if (has("redridge")) return "redridge";
        if (has("elwynn")) return "elwynn";
        if (has("hinterland")) return "hinterlands";
        if (has("arathi")) return "arathi";
        if (has("wetland")) return "wetlands";
        if (has("loch")) return "loch";
        if (has("blasted")) return "blasted";
        if (has("burning steppe")) return "steppes";
        if (has("dustwallow")) return "dustwallow";
        if (has("feralas")) return "feralas";
        if (has("ashenvale")) return "ashenvale";
        if (has("stonetalon")) return "stonetalon";
        if (has("desolace")) return "desolace";
        if (has("thousand needle")) return "needles";
        if (has("azshara")) return "azshara";
        if (has("moonglade")) return "moonglade";
        if (has("howling")) return "fjord";
        if (has("crystalsong")) return "crystalsong";
        if (has("wintergrasp")) return "wg";
        if (has("blade")) return "blades";
        return "";
    }

    void AddSingularTakes(std::vector<std::string>& lines, std::string const& s)
    {
        if (s.empty())
            return;
        lines.push_back(s + " is aids");
        lines.push_back("this " + s + " is aids");
        lines.push_back(s + " is a joke");
        lines.push_back(s + " can eat me");
        lines.push_back("who designed " + s);
        lines.push_back("skip " + s + " if you can");
        lines.push_back(s + " quest is aids");
        lines.push_back("why does " + s + " exist");
        lines.push_back(s + " is so bad");
        lines.push_back("hate " + s);
        lines.push_back(s + " can die");
        lines.push_back("blizz and " + s);
        lines.push_back("anyone still on " + s);
        lines.push_back("is " + s + " bugged");
        lines.push_back(s + " drop rate is trash");
        lines.push_back("how many for " + s + " still");
        lines.push_back(s + " respawn is a joke");
        lines.push_back("waiting on " + s + " spawn");
        lines.push_back("group for " + s + "?");
        lines.push_back(s + " is why i drink");
        lines.push_back("did they even test " + s);
        lines.push_back(s + " escort can die");
        lines.push_back("lost the " + s + " npc again");
    }

    void AddMobTakes(std::vector<std::string>& lines, std::string const& mob)
    {
        if (mob.empty() || mob == "here")
            return;
        lines.push_back("these " + mob + " are aids");
        lines.push_back(mob + " hit like trucks");
        lines.push_back("tagging " + mob + " sucks");
        lines.push_back("anyone else on these " + mob);
        lines.push_back("anyone for " + mob + " quest");
        lines.push_back(mob + " one shot me");
        lines.push_back("how does " + mob + " hit that hard");
        lines.push_back(mob + " have too much hp");
        lines.push_back("these " + mob + " pack too hard");
        lines.push_back(mob + " drop junk");
        lines.push_back("who pulls " + mob + " like that");
        lines.push_back(mob + " are a joke");
        lines.push_back("im so sick of " + mob);
        lines.push_back(mob + " pull the whole camp");
        lines.push_back("these " + mob + " never die");
        lines.push_back("fighting over " + mob + " spawns");
        lines.push_back(mob + " respawn is instant nvm");
        lines.push_back("train of " + mob + " coming");
        lines.push_back("stop ninjaing " + mob);
        lines.push_back(mob + " drop 1 copper lmao");
        lines.push_back("gray " + mob + " forever");
        lines.push_back("pet died to " + mob);
        lines.push_back(mob + " leash is broken");
        lines.push_back("these " + mob + " call the whole ridge");
        lines.push_back("need 8 " + mob + " meat still");
        lines.push_back(mob + " steal my tags");
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
            if (LineRecentlySpoken(line))
                continue;
            fresh.push_back(line);
        }
        if (fresh.empty())
            return "";
        std::string const picked = fresh[urand(0, fresh.size() - 1)];
        NoteSpokenLine(picked);
        return picked;
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
            if (out.size() >= 28)
                break;
        }
        if (!out.empty() && out.back() == ' ')
            out.pop_back();
        size_t dots = out.find("...");
        if (dots != std::string::npos)
            out.resize(dots);
        while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
            out.pop_back();
        auto endsWith = [&](char const* w) -> bool
        {
            size_t const n = std::strlen(w);
            return out.size() >= n && out.compare(out.size() - n, n, w) == 0 &&
                   (out.size() == n || out[out.size() - n - 1] == ' ');
        };
        while (endsWith(" of") || endsWith(" the") || endsWith(" a") || endsWith(" for") ||
               endsWith(" and") || endsWith(" to") || endsWith(" i"))
        {
            size_t sp = out.rfind(' ');
            if (sp == std::string::npos)
            {
                out.clear();
                break;
            }
            out.resize(sp);
        }
        if (out.size() < 4)
            return "";
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
    return PickFrom({
        "questing", "grinding", "nmu", "nothing much", "same old",
        "just questing", "on a grind", "killing time", "same as usual"
    }, threadKey);
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
    return PickFrom({
        "pst", "pst for inv", "whisper me", "sure pst", "can inv", "got room pst",
        "pst me", "send a tell", "yeah pst", "got spots pst"
    }, threadKey);
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

bool UsesSlur(std::string const& message)
{
    if (message.empty())
        return false;
    std::string const lower = ToLowerCopy(message);
    return HasAnyWord(lower, {
        "nigger", "niggers", "nigga", "niggas", "n1gger",
        "faggot", "faggots", "fag", "fags",
        "retard", "retards", "retarded",
        "kike", "jew", "jews", "jewish", "spic", "chink", "gook", "tranny", "dyke", "wetback"
    });
}

bool UsesProfanity(std::string const& message)
{
    if (message.empty())
        return false;
    std::string const lower = ToLowerCopy(message);
    if (HasAnyWord(lower, {
            "fuck", "fucks", "fucking", "fucked", "fucker", "motherfucker",
            "shit", "bullshit", "asshole", "bitch", "cunt",
            "dick", "pussy", "whore", "slut", "cock"
        }))
        return true;
    if (lower.find("shit") != std::string::npos ||
        lower.find("fuck you") != std::string::npos ||
        lower.find("fuck u") != std::string::npos ||
        lower.find("fuck off") != std::string::npos)
        return true;
    return false;
}

bool IsHostileTalk(std::string const& message)
{
    if (message.empty())
        return false;
    if (UsesSlur(message))
        return true;
    std::string const lower = ToLowerCopy(message);
    if (HasAnyWord(lower, {
            "noob", "nub", "nubs", "noobs", "scrub", "scrubs", "uninstall",
            "kys", "ez", "l2p", "idiot", "idiots", "stupid", "moron",
            "clown", "clowns", "garbage", "useless", "reported", "report",
            "faggot", "fag", "retard"
        }))
        return true;
    if (lower.find("shut up") != std::string::npos ||
        lower.find("kill yourself") != std::string::npos ||
        lower.find("you suck") != std::string::npos ||
        lower.find("u suck") != std::string::npos ||
        lower.find("learn to play") != std::string::npos ||
        lower.find("delete the game") != std::string::npos ||
        lower.find("get good") != std::string::npos ||
        lower.find("sit down") != std::string::npos ||
        lower.find("your mom") != std::string::npos ||
        lower.find("fuck you") != std::string::npos ||
        lower.find("fuck u") != std::string::npos ||
        lower.find("fuck off") != std::string::npos ||
        lower.find("fuck everyone") != std::string::npos ||
        lower.find("hope you") != std::string::npos ||
        lower.find("hope u ") != std::string::npos ||
        lower.find("you all die") != std::string::npos ||
        lower.find("u all die") != std::string::npos)
        return true;
    if ((lower.find("die") != std::string::npos || lower.find("dead") != std::string::npos) &&
        HasAnyWord(lower, { "you", "u", "everyone", "all" }))
        return true;
    if (UsesProfanity(message) &&
        HasAnyWord(lower, { "you", "u", "everyone", "all", "guild", "chat" }))
        return true;
    return false;
}

ChatTone DetectChatTone(std::string const& message)
{
    if (IsHostileTalk(message) || UsesSlur(message))
        return ChatTone::Hostile;
    if (IsDismissal(message))
        return ChatTone::Dismissive;
    if (UsesProfanity(message))
        return ChatTone::Hostile;
    return ChatTone::Neutral;
}

std::string PickFollowUpReply(std::string const& threadKey)
{
    return PickFrom({ "huh", "?", "wdym", "n", "what" }, threadKey, false);
}

std::string PickHostileReply(std::string const& threadKey, std::string const& playerMsg)
{
    if (UsesSlur(playerMsg))
        return PickFrom({
            "stfu faggot", "l2p retard", "sit nigger", "fuck off",
            "cry more faggot", "uninstall retard", "kys noob", "eat shit"
        }, threadKey, false, true);
    if (UsesProfanity(playerMsg))
        return PickFrom({
            "fuck off", "stfu", "eat shit", "l2p asshole", "piss off",
            "cry more", "sit down", "get fucked", "fuck you"
        }, threadKey, false, true);
    return PickFrom({
        "cry more", "l2p", "sit", "lol", "k", "nice try", "qq more",
        "uninstall", "clown", "get good", "ok kid", "lag more", "reported"
    }, threadKey, false, true);
}

std::string PickCloserReply(std::string const& threadKey)
{
    return PickFrom({
        "anyway", "g2g", "later", "lol", "n", "brb", "one more"
    }, threadKey, false, true);
}

std::string PickContinueLine(std::string const& threadKey)
{
    return PickFrom({
        "anyone else on this", "need a grp for this", "this chain sucks",
        "whats the turnin", "is this even worth it", "xp is slow here",
        "anyone know if its bugged", "drops are trash here", "need 1 if anyone is around",
        "how far along are you", "this is taking ages", "xp per hour is meh",
        "might skip this one", "elites here hit hard", "got a spare spot"
    }, threadKey, false, true);
}

namespace
{
    std::string MessyChat(std::string line)
    {
        if (line.size() < 6 || urand(0, 99) >= 8)
            return line;
        std::string const orig = line;
        uint32 const kind = urand(0, 99);
        if (kind < 40)
        {
            size_t const pos = line.find("the ");
            if (pos != std::string::npos && (pos == 0 || line[pos - 1] == ' '))
                line.replace(pos, 4, "teh ");
        }
        else if (kind < 70 && line.size() > 10)
        {
            size_t const i = urand(1, line.size() - 2);
            if (line[i] == ' ')
                line.erase(i, 1);
        }
        else if (kind < 90)
        {
            size_t const i = urand(0, line.size() - 1);
            if (std::isalpha(static_cast<unsigned char>(line[i])))
                line.insert(i, 1, line[i]);
        }
        else if (std::islower(static_cast<unsigned char>(line.front())))
            line.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(line.front())));
        if (UsesSlur(line))
            return orig;
        return line;
    }

    bool DribbleFitsRoom(std::string const& line, bool capital)
    {
        std::string const lower = ToLowerCopy(line);
        bool city = lower.find("icc") != std::string::npos || lower.find("toc") != std::string::npos ||
                    lower.find("naxx") != std::string::npos || lower.find("ulduar") != std::string::npos ||
                    lower.find("dalaran") != std::string::npos || lower.find(" dal") != std::string::npos ||
                    lower.find("saurfang") != std::string::npos || lower.find("patchwerk") != std::string::npos ||
                    lower.find("oculus") != std::string::npos || lower.find("rdf") != std::string::npos ||
                    lower.find("lfd") != std::string::npos || lower.find("badge") != std::string::npos ||
                    lower.find("arena") != std::string::npos || lower.find("flask") != std::string::npos ||
                    lower.find("lockout") != std::string::npos || lower.find("gold seller") != std::string::npos ||
                    lower.find("flashing wings") != std::string::npos ||
                    lower.find("blocking the portal") != std::string::npos;
        bool zone = lower.find("grind") != std::string::npos || lower.find("spawn") != std::string::npos ||
                    lower.find("tagging") != std::string::npos || lower.find("gank") != std::string::npos ||
                    lower.find("escort") != std::string::npos || lower.find("herb") != std::string::npos ||
                    lower.find("nodes") != std::string::npos || lower.find("quest log") != std::string::npos ||
                    lower.find("walking") != std::string::npos || lower.find("elites in") != std::string::npos ||
                    lower.find("fp in") != std::string::npos;
        if (capital && zone && !city)
            return urand(0, 99) < 22;
        if (!capital && city && !zone)
            return urand(0, 99) < 18;
        return true;
    }

    std::string AddressBounce(std::string line, std::string speaker, bool speakerIsBot)
    {
        uint32 const nameChance = BotChatBlowupActiveAny() ? 55 : 22;
        if (line.empty() || !speakerIsBot || speaker.empty() || urand(0, 99) >= nameChance)
            return line;
        if (HasWord(ToLowerCopy(line), "reported") || HasWord(ToLowerCopy(line), "report"))
            return line;
        size_t const cut = speaker.find('-');
        if (cut != std::string::npos)
            speaker.resize(cut);
        if (speaker.size() < 2 || speaker.size() > 12)
            return line;
        return line + " " + speaker;
    }

    std::string PickClaimBounce(std::string const& lower, std::string const& threadKey)
    {
        auto pick = [&](char const* const* arr, size_t n) -> std::string
        {
            return PickFromArray(arr, n, threadKey);
        };
        if (lower.find("tbc") != std::string::npos || lower.find("vanilla") != std::string::npos ||
            lower.find("wrath") != std::string::npos || lower.find("lfd") != std::string::npos ||
            lower.find("xpac") != std::string::npos || lower.find("dumbed") != std::string::npos)
            return pick(kClaimExpansion, sizeof(kClaimExpansion) / sizeof(kClaimExpansion[0]));
        if (lower.find("dk") != std::string::npos || lower.find("death grip") != std::string::npos ||
            lower.find("unholy") != std::string::npos)
            return pick(kClaimDk, sizeof(kClaimDk) / sizeof(kClaimDk[0]));
        if (lower.find("pally") != std::string::npos || lower.find("paladin") != std::string::npos ||
            lower.find("pala") != std::string::npos)
            return pick(kClaimPally, sizeof(kClaimPally) / sizeof(kClaimPally[0]));
        if (lower.find("hunter") != std::string::npos)
            return pick(kClaimHunter, sizeof(kClaimHunter) / sizeof(kClaimHunter[0]));
        if (lower.find("icc") != std::string::npos || lower.find("toc") != std::string::npos ||
            lower.find("naxx") != std::string::npos || lower.find("ulduar") != std::string::npos ||
            lower.find("saurfang") != std::string::npos || lower.find("patchwerk") != std::string::npos)
            return pick(kClaimRaid, sizeof(kClaimRaid) / sizeof(kClaimRaid[0]));
        if (lower.find("horde") != std::string::npos || lower.find("alliance") != std::string::npos ||
            lower.find("queue") != std::string::npos)
            return pick(kClaimFaction, sizeof(kClaimFaction) / sizeof(kClaimFaction[0]));
        if (lower.find("flying") != std::string::npos || lower.find("northrend") != std::string::npos)
            return pick(kClaimFlying, sizeof(kClaimFlying) / sizeof(kClaimFlying[0]));
        if (lower.find("gold") != std::string::npos || lower.find("inflat") != std::string::npos ||
            lower.find(" ah") != std::string::npos)
            return pick(kClaimGold, sizeof(kClaimGold) / sizeof(kClaimGold[0]));
        return pick(kClaimGeneric, sizeof(kClaimGeneric) / sizeof(kClaimGeneric[0]));
    }

    bool AreaLooksCapital(Player* bot)
    {
        if (!bot)
            return false;
        uint32 zoneId = BotLiveZoneId(bot);
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
        if (!area)
            return false;
        if (area->flags & AREA_FLAG_CAPITAL)
            return true;
        if (area->zone)
        {
            if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
                return (parent->flags & AREA_FLAG_CAPITAL) != 0;
        }
        return false;
    }

    std::string TryPickCannedPool(char const* const* lines, size_t count,
                                 std::string const& threadKey, bool capital)
    {
        std::vector<std::string> pool;
        for (size_t i = 0; i < count; ++i)
        {
            if (!lines[i] || !lines[i][0])
                continue;
            if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, lines[i]))
                continue;
            if (LineRecentlySpoken(lines[i]))
                continue;
            if (DribbleFitsRoom(lines[i], capital))
                pool.push_back(lines[i]);
        }
        if (pool.empty())
            return "";
        std::string picked = pool[urand(0, pool.size() - 1)];
        picked = MessyChat(picked);
        picked = BotChatBlowupYell(picked);
        if (picked.empty())
            return "";
        NoteSpokenLine(picked);
        return picked;
    }
}

bool LooksLikeArgument(std::string const& message)
{
    if (message.empty())
        return false;
    std::string const lower = ToLowerCopy(message);
    if (lower.find("fight me") != std::string::npos ||
        lower.find("change my mind") != std::string::npos ||
        lower.find("die on this hill") != std::string::npos ||
        lower.find("i stand by") != std::string::npos ||
        lower.find("and you know it") != std::string::npos ||
        lower.find("shouldnt exist") != std::string::npos ||
        lower.find("was better") != std::string::npos ||
        lower.find("ruined the") != std::string::npos ||
        lower.find("ruined you") != std::string::npos ||
        lower.find("be honest") != std::string::npos)
        return true;
    return false;
}

BotChatMouth MouthForBot(Player* bot)
{
    if (!bot)
        return BotChatMouth::Mix;
    uint32 const n = static_cast<uint32>(bot->GetGUID().GetRawValue() % 100);
    if (n < 16)
        return BotChatMouth::Quiet;
    if (n < 36)
        return BotChatMouth::Lfg;
    if (n < 62)
        return BotChatMouth::Salt;
    if (n < 80)
        return BotChatMouth::Chatty;
    return BotChatMouth::Mix;
}

uint32 BotChatAdultMix()
{
    if (BotChatBlowupActiveAny())
        return 90;
    if (!g_AdultEnable)
        return 0;

    uint32 start = g_AdultHour;
    if (start > 23)
        start = 21;

    time_t now = time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    // Sunday school night: kick in an hour later so 9pm is still the daytime register.
    if (local.tm_wday == 0)
        start = std::min<uint32>(23, start + 1);

    int const hour = local.tm_hour;
    int const min = local.tm_min;
    int const nowM = hour * 60 + min;
    int const startM = static_cast<int>(start) * 60;
    int past = -1;
    if (nowM >= startM)
        past = nowM - startM;
    else if (hour < 6)
        past = (24 * 60 - startM) + nowM;
    if (past < 0)
        return 0;
    if (past < 60)
        return 15 + static_cast<uint32>(past);
    if (hour >= 2 && hour < 6)
        return static_cast<uint32>(75 * (6 - hour) / 4);
    return 75;
}

uint32 BotChatEffectiveToxicity()
{
    if (BotChatBlowupActiveAny())
        return 3;
    return g_Toxicity > 3 ? 3 : g_Toxicity;
}

namespace
{
    struct BlowupState
    {
        std::mutex mutex;
        int eveningKey = -1;
        bool rolled = false;
        bool allowed = false;
        bool spent = false;
        time_t until = 0;
        std::string key;
    };
    BlowupState g_Blowup;

    int BlowupEveningKey(std::tm const& local)
    {
        int yday = local.tm_yday;
        int year = local.tm_year;
        if (local.tm_hour < 6)
        {
            --yday;
            if (yday < 0)
            {
                yday = 365;
                --year;
            }
        }
        return year * 1000 + yday;
    }

    bool BlowupIsEveningHour(int hour, int wday)
    {
        // Same floor as AdultHour so a 7pm pile-on cannot dump tox 3 on kids.
        uint32 start = g_AdultHour;
        if (start > 23)
            start = 21;
        if (wday == 0)
            start = std::min<uint32>(23, start + 1);
        return hour >= static_cast<int>(start) || hour < 2;
    }

    void BlowupRollIfNeeded()
    {
        if (!g_BlowupChance)
            return;
        time_t now = time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        if (!BlowupIsEveningHour(local.tm_hour, local.tm_wday))
            return;
        int const key = BlowupEveningKey(local);
        std::lock_guard<std::mutex> lock(g_Blowup.mutex);
        if (g_Blowup.eveningKey == key)
            return;
        g_Blowup.eveningKey = key;
        g_Blowup.rolled = true;
        g_Blowup.spent = false;
        g_Blowup.until = 0;
        g_Blowup.key.clear();
        g_Blowup.allowed = BlowupIsEveningHour(local.tm_hour, local.tm_wday) &&
                           urand(0, 99) < g_BlowupChance;
        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[Bot Chat] Blowup {} tonight",
                     g_Blowup.allowed ? "eligible" : "not rolling");
    }
}

bool BotChatBlowupActive(std::string const& threadKey)
{
    time_t const now = time(nullptr);
    std::lock_guard<std::mutex> lock(g_Blowup.mutex);
    if (!g_Blowup.until || now >= g_Blowup.until)
        return false;
    if (threadKey.empty())
        return true;
    return g_Blowup.key == threadKey;
}

bool BotChatBlowupActiveAny()
{
    return BotChatBlowupActive({});
}

std::string BotChatBlowupYell(std::string line)
{
    if (line.empty() || !BotChatBlowupActiveAny())
        return line;
    uint32 const r = urand(0, 99);
    if (r < 22)
        return line;
    for (char& c : line)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (r >= 82 && line.find('!') == std::string::npos)
        line += "!!!";
    else if (r >= 62 && line.find('!') == std::string::npos)
        line += "!";
    else if (r >= 48 && line.find('?') == std::string::npos)
        line += "??";
    return line;
}

void BotChatMaybeIgniteBlowup(std::string const& threadKey, std::string const& lastLine)
{
    if (!g_BlowupChance || threadKey.empty() || lastLine.empty())
        return;
    if (threadKey.rfind("chan:", 0) != 0)
        return;
    if (!LooksLikeArgument(lastLine) && !LooksLikeDribble(lastLine) && !IsHostileTalk(lastLine))
        return;

    BlowupRollIfNeeded();
    time_t now = time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    if (!BlowupIsEveningHour(local.tm_hour, local.tm_wday))
        return;

    uint32 chance = 12;
    std::string const lower = ToLowerCopy(lastLine);
    if (lower.find("fight me") != std::string::npos ||
        lower.find("change my mind") != std::string::npos ||
        lower.find("die on this hill") != std::string::npos)
        chance = 28;

    std::lock_guard<std::mutex> lock(g_Blowup.mutex);
    if (!g_Blowup.allowed || g_Blowup.spent)
        return;
    if (g_Blowup.until && now < g_Blowup.until)
        return;
    if (urand(0, 99) >= chance)
        return;

    uint32 span = g_BlowupSeconds ? g_BlowupSeconds : 600;
    uint32 lo = span * 8 / 10;
    uint32 hi = span * 12 / 10;
    if (hi < lo)
        hi = lo;
    g_Blowup.until = now + urand(lo, hi);
    g_Blowup.key = threadKey;
    g_Blowup.spent = true;
    g_Blowup.allowed = false;
    LOG_INFO("server.loading", "[Bot Chat] Blowup ignited on {} for {}s",
             threadKey, static_cast<unsigned>(g_Blowup.until - now));
}

std::string PickDribbleContinue(std::string const& last, std::string const& threadKey,
                               std::string const& speaker, bool speakerIsBot, uint32 topicId)
{
    uint32 trail = topicId ? GetTopicTrail(threadKey, topicId) : CountTrailingBotLines(threadKey);
    std::string const lower = ToLowerCopy(last);
    uint32 maxTrail = topicId ? GetTopicMaxTrail(threadKey, topicId) : GetThreadMaxTrail(threadKey);
    if (!maxTrail)
        maxTrail = 4;

    std::string named = speaker;
    bool namedBot = speakerIsBot;
    if (named.empty())
    {
        ChannelThreadLine const line = GetLastThreadLine(threadKey);
        named = line.speaker;
        namedBot = line.isBot;
    }

    auto finish = [&](std::string line) -> std::string
    {
        if (line.empty())
            return "";
        line = AddressBounce(line, named, namedBot);
        line = MessyChat(line);
        line = BotChatBlowupYell(line);
        if (line.empty())
            return "";
        return line;
    };

    bool const melee = BotChatBlowupActive(threadKey);
    if (!melee && (HasWord(lower, "reported") || HasWord(lower, "report")))
    {
        if (urand(0, 99) < 70)
            return "";
        return finish(PickFrom({
            "already reported", "for what lmao", "+1", "mute and move on",
            "right click", "have fun with the gm"
        }, threadKey, false, true));
    }

    bool const ending = !melee && (trail + 1 >= maxTrail);
    if (!melee && (ending || (trail >= 2 && urand(0, 99) < 12)))
        return finish(PickFrom({
            "reported", "report that", "language", "gm pls", "take it to the forums",
            "who asked", "stfu", "stay mad", "found the pally", "ok and",
            "found the qq", "mute and move on", "wrong channel"
        }, threadKey, false, true));

    bool const clustered =
        lower.find("tbc") != std::string::npos || lower.find("vanilla") != std::string::npos ||
        lower.find("wrath") != std::string::npos || lower.find("lfd") != std::string::npos ||
        lower.find("dk") != std::string::npos || lower.find("pally") != std::string::npos ||
        lower.find("hunter") != std::string::npos || lower.find("icc") != std::string::npos ||
        lower.find("toc") != std::string::npos || lower.find("naxx") != std::string::npos ||
        lower.find("horde") != std::string::npos || lower.find("alliance") != std::string::npos ||
        lower.find("flying") != std::string::npos || lower.find("gold") != std::string::npos;
    if (melee)
    {
        uint32 const roll = urand(0, 99);
        if (roll < 22)
            return finish(PickFromArray(kToxicPersonalBounces,
                sizeof(kToxicPersonalBounces) / sizeof(kToxicPersonalBounces[0]), threadKey));
        if (roll < 55)
            return finish(PickFromArray(kToxicLgbtBounces,
                sizeof(kToxicLgbtBounces) / sizeof(kToxicLgbtBounces[0]), threadKey));
        if (roll < 72)
            return finish(PickFromArray(kToxicIdentityBounces,
                sizeof(kToxicIdentityBounces) / sizeof(kToxicIdentityBounces[0]), threadKey));
        if (roll < 88)
            return finish(PickFromArray(kAdultBounces,
                sizeof(kAdultBounces) / sizeof(kAdultBounces[0]), threadKey));
        if (LooksLikeArgument(last) || clustered)
        {
            std::string claim = PickClaimBounce(lower, threadKey);
            if (!claim.empty())
                return finish(claim);
        }
        return finish(PickFromArray(kToxicGameBounces,
            sizeof(kToxicGameBounces) / sizeof(kToxicGameBounces[0]), threadKey));
    }
    if (LooksLikeArgument(last) || clustered)
    {
        std::string claim = PickClaimBounce(lower, threadKey);
        if (!claim.empty())
            return finish(claim);
    }

    if (lower.find("aids") != std::string::npos)
        return finish(PickFrom({
            "so aids", "the aids quest", "10/10 aids", "wait till the next one",
            "blizz pls", "who designed this", "do it naked", "skip it",
            "my bags are aids too", "this", "aids+", "peak aids",
            "they made it worse", "its a classic", "tell me about it"
        }, threadKey, false, true));

    uint32 const tox = BotChatEffectiveToxicity();
    bool const late = BotChatAdultMix() > 0;
    if (tox >= 3 && late &&
        (HasWord(lower, "kys") || lower.find("kill yourself") != std::string::npos ||
         lower.find("off yourself") != std::string::npos || lower.find("dox") != std::string::npos ||
         lower.find("where you live") != std::string::npos || lower.find("hitler") != std::string::npos))
        return finish(PickFromArray(kToxicPersonalBounces,
            sizeof(kToxicPersonalBounces) / sizeof(kToxicPersonalBounces[0]), threadKey));
    if (tox >= 3 && late &&
        (HasWord(lower, "gay") || HasWord(lower, "faggot") || lower.find("tranny") != std::string::npos ||
         lower.find("trans") != std::string::npos || lower.find("they/them") != std::string::npos ||
         lower.find("pronoun") != std::string::npos || lower.find("lgbt") != std::string::npos ||
         lower.find("pride") != std::string::npos))
        return finish(PickFromArray(kToxicLgbtBounces,
            sizeof(kToxicLgbtBounces) / sizeof(kToxicLgbtBounces[0]), threadKey));
    if (tox >= 2 && late &&
        (lower.find("squeaker") != std::string::npos || lower.find("girlfriend") != std::string::npos ||
         lower.find("women") != std::string::npos || HasWord(lower, "gay") ||
         lower.find("farmer") != std::string::npos || lower.find("accent") != std::string::npos ||
         lower.find("english") != std::string::npos || lower.find("vent") != std::string::npos))
        return finish(PickFromArray(kToxicIdentityBounces,
            sizeof(kToxicIdentityBounces) / sizeof(kToxicIdentityBounces[0]), threadKey));
    if (tox >= 1 &&
        (lower.find("ninja") != std::string::npos || lower.find("parse") != std::string::npos ||
         lower.find("gdkp") != std::string::npos || lower.find("gold") != std::string::npos ||
         lower.find("rmt") != std::string::npos || lower.find("boosted") != std::string::npos ||
         lower.find("sub") != std::string::npos || lower.find("reserved") != std::string::npos ||
         lower.find("loot council") != std::string::npos))
        return finish(PickFromArray(kToxicGameBounces,
            sizeof(kToxicGameBounces) / sizeof(kToxicGameBounces[0]), threadKey));

    uint32 const adult = BotChatAdultMix();
    if (adult && urand(0, 99) < adult)
        return finish(PickFromArray(kAdultBounces,
                                    sizeof(kAdultBounces) / sizeof(kAdultBounces[0]), threadKey));
    return finish(PickFromArray(kDribbleBounces,
                                sizeof(kDribbleBounces) / sizeof(kDribbleBounces[0]), threadKey));
}

std::string PickContinueForLast(std::string const& last, std::string const& threadKey,
                               std::string const& speaker, bool speakerIsBot, uint32 topicId)
{
    std::string const lower = ToLowerCopy(last);
    if (LooksLikeDribble(last) || IsHostileTalk(last) || LooksLikeArgument(last) ||
        lower.find("aids") != std::string::npos)
        return PickDribbleContinue(last, threadKey, speaker, speakerIsBot, topicId);
    if (HasAnyWord(lower, { "lfg", "lfm", "lf1m", "lf2m", "lf3m" }) ||
        lower.find("need 1") != std::string::npos ||
        lower.find("anyone for") != std::string::npos ||
        lower.find("going in") != std::string::npos)
        return PickGroupReply(threadKey, false);
    if (lower.find("anyone") != std::string::npos || lower.find("anybody") != std::string::npos)
        return PickFrom({
            "yeah im here", "im down", "what for", "me", "need a tank?",
            "depends what it is", "busy for a bit", "whats the plan",
            "i could do that", "where you at"
        }, threadKey, false, true);
    if (lower.find("grind") != std::string::npos || lower.find("mobs") != std::string::npos ||
        lower.find("xp") != std::string::npos || lower.find("zone") != std::string::npos ||
        lower.find("drops") != std::string::npos)
        return PickFrom({
            "xp is slow here too", "need a grp?", "drops are trash yeah",
            "is it even worth it", "almost done then im out",
            "mobs are packed at least", "xp per hour is whatever",
            "i was just thinking that"
        }, threadKey, false, true);
    if (lower.find("quest") != std::string::npos || lower.find("turn in") != std::string::npos ||
        lower.find("turnin") != std::string::npos)
        return PickFrom({
            "im on it too", "that one sucks", "need a hand with it",
            "turnin is nearby i think", "is it soloable",
            "skip it if you can", "yeah that chain is long",
            "i finished that last night"
        }, threadKey, false, true);
    if (lower.find("where") != std::string::npos || HasAnyWord(lower, { "fp", "trainer", "inn" }))
        return PickFrom({
            "near the ah i think", "up by the inn", "check town",
            "looking for it too", "fp is in town",
            "behind the inn iirc", "east side of town", "ask a guard lol"
        }, threadKey, false, true);
    if (lower.find("down for") != std::string::npos || lower.find("mess around") != std::string::npos ||
        lower.find("whats everyone") != std::string::npos || lower.find("anyone on") != std::string::npos)
        return PickFrom({
            "im in", "same", "what you thinking", "lets go", "i got time",
            "yeah im here", "down", "im bored too"
        }, threadKey, false, true);
    if (HasAnyWord(lower, { "lag", "fps", "dc", "queue", "isp" }))
        return PickFrom({
            "isp is dying here too", "fps is trash", "same client keeps hitching",
            "rubberbanding like crazy", "been dc'ing all night", "queue was long as hell"
        }, threadKey, false, true);
    if (lower.find("repair") != std::string::npos || HasAnyWord(lower, { "ah", "bank" }))
        return PickFrom({
            "ah prices are awful", "just vendored junk", "repair bill hurts",
            "is the ah even up", "gonna dump some greens", "gold sink is real"
        }, threadKey, false, true);
    if (HasAnyWord(lower, { "boss", "rez", "ready" }) || lower.find("this run") != std::string::npos)
        return PickFrom({
            "ready", "on it", "need a rez too", "this run is slow",
            "wipe again?", "fooding first", "buffs up"
        }, threadKey, false, true);
    if (lower.find("heat") != std::string::npos || lower.find("rain") != std::string::npos ||
        lower.find("storm") != std::string::npos || lower.find("news") != std::string::npos ||
        lower.find("game") != std::string::npos || lower.find("coffee") != std::string::npos)
        return PickFrom({
            "yeah same", "wild isnt it", "cant focus either", "im here for that too",
            "tell me about it", "same boat", "thats why im on"
        }, threadKey, false, true);
    return PickContinueLine(threadKey);
}

std::string PickSocialReply(SocialAct act, std::string const& threadKey)
{
    switch (act)
    {
        case SocialAct::Congrats:
        case SocialAct::WelcomeBack:
            return PickFrom({ "ty", "thx", "tyvm", "ty guys", "ha ty", "lol ty", "ty man" }, threadKey);
        case SocialAct::Welcome:
            return PickFrom({ "ty", "hi", "hey", "hey hey" }, threadKey);
        case SocialAct::Thanks:
            return PickFrom({ "np", "yw", "np np", "sure", "all good", "np man" }, threadKey);
        case SocialAct::Greeting:
            return PickFrom({ "hey", "hi", "yo", "sup", "hey yo", "hiya" }, threadKey);
        case SocialAct::Farewell:
            return PickFrom({ "cya", "later", "gn", "bb", "night", "laters", "peace" }, threadKey);
        case SocialAct::GoodLuck:
            return PickFrom({ "ty", "gl", "u2", "glgl", "you too" }, threadKey);
        case SocialAct::GoodGame:
            return PickFrom({ "gg", "wp", "ty", "ggs", "nice one" }, threadKey);
        case SocialAct::Condolence:
            return PickFrom({ "lol", "re", "f", "oof", "unlucky", "rip lol" }, threadKey);
        case SocialAct::Apology:
            return PickFrom({ "np", "all g", "nbd", "its fine", "all good" }, threadKey);
        case SocialAct::Back:
            return PickFrom({ "wb", "ty", "hey", "re", "wb man" }, threadKey);
        case SocialAct::Brb:
            return PickFrom({ "kk", "o", "k", "cya", "np", "hurry up lol" }, threadKey);
        case SocialAct::Reaction:
            return PickFrom({ "lol", "same", "yep", "true", "lmao", "n", "facts", "yeah" }, threadKey);
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
    // Status only if they can still type it. Never "dc'd brb" — you cannot type while dc'd.
    return PickFrom({
        "brb grabbing food", "fps is dying on me", "client froze again",
        "queue was a joke", "connection is trash", "one more then im out",
        "lag spike again", "logging for a bit", "bio sec",
        "reloading ui", "addon error lol", "loading screen forever"
    }, threadKey, false, true);
}

std::string PickAmbientGuildLine(std::string const& threadKey)
{
    uint32 const adult = BotChatAdultMix();
    if (adult && urand(0, 99) < adult / 2)
    {
        std::string const line = PickFromArray(kAdultGuild,
            sizeof(kAdultGuild) / sizeof(kAdultGuild[0]), threadKey);
        if (!line.empty())
            return line;
    }
    return PickFrom({
        "whats everyone up to", "anyone on", "good time to grind tbh",
        "i got a bit if anyone needs a hand", "loot was decent for once",
        "gonna ding soon i think", "this class is growing on me",
        "bored wanna mess around", "anyone knocking out quests",
        "i could use a tagalong", "finally have an hour lol",
        "who is actually on", "this game still rocks",
        "anyone want to knock a chain out", "quiet in here tonight",
        "im down for whatever", "got gold for once lol",
        "wotlk is good honestly", "anyone for something easy",
        "come hang if youre bored"
    }, threadKey, false, true);
}

std::string PickAmbientWorldLine(std::string const& threadKey)
{
    // Generic MMO watercooler. Rotate. No sticky personal lore (wife/kids/pc).
    return PickFrom({
        "this heat is brutal", "power flickered for a sec", "isp is dying",
        "anyone else hear thunder", "eyes are killing me", "need a smoke brb",
        "anyone watching the game", "news is crazy lately", "monday already huh",
        "this chair is killing my back", "room is freezing", "too loud in here",
        "coffee aint working", "did it rain where you are", "cant sleep so im here",
        "blizzard servers when", "client keeps hitching", "friday finally",
        "neighbors are loud", "fan is on max", "sun is still up lol",
        "stomach is empty", "need water brb", "keyboard is dying",
        "desk is a mess", "phone keeps buzzing"
    }, threadKey, false, true);
}

bool LooksLikeDribble(std::string const& message)
{
    if (message.empty())
        return false;
    if (IsHostileTalk(message) || LooksLikeArgument(message))
        return true;
    std::string const lower = ToLowerCopy(message);
    static std::unordered_set<std::string> const pool = []()
    {
        std::unordered_set<std::string> lines;
        for (char const* line : kDribbleOpeners)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kDribbleBounces)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kAdultOpeners)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kAdultBounces)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kAdultGuild)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicGame)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicGameBounces)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicIdentity)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicIdentityBounces)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicPersonal)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicPersonalBounces)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicLgbt)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        for (char const* line : kToxicLgbtBounces)
            if (line && line[0])
                lines.insert(ToLowerCopy(line));
        return lines;
    }();
    if (pool.count(lower))
        return true;
    if (HasAnyWord(lower, {
            "blizz", "blizzard", "faceroll", "cookie", "carebear", "qq",
            "ninja", "pug", "pugs", "nerf", "buff", "gkick",
            "goldsellers", "dumpster", "overpowered", "broken", "patch",
            "aids", "reported", "report", "uninstall", "leeroy", "l2p",
            "noob", "nub", "pwn", "owned", "reroll", "goldshire", "mankrik",
            "fuck", "fucking", "shit", "bullshit", "asshole", "damn",
            "ninja", "parse", "gdkp", "rmt", "kys", "squeaker",
            "tranny", "faggot", "lgbt", "pronouns"
        }))
        return true;
    if (lower.find("easy mode") != std::string::npos ||
        lower.find("dumbed") != std::string::npos ||
        lower.find("this spec") != std::string::npos ||
        lower.find("this class") != std::string::npos ||
        lower.find("this game") != std::string::npos ||
        lower.find("gold seller") != std::string::npos ||
        lower.find("this quest") != std::string::npos ||
        lower.find("who asked") != std::string::npos ||
        lower.find("your mom") != std::string::npos ||
        lower.find("fight me") != std::string::npos ||
        lower.find("thats what she said") != std::string::npos)
        return true;
    return false;
}

std::string PickAmbientDribbleLine(std::string const& threadKey, std::string const& className,
                                  std::string const& questTitle, std::string const& zone,
                                  Player* bot, std::string const& nearbyMob)
{
    std::string const hint = ShortQuestHint(questTitle);
    std::string const nick = ChatZoneNick(zone);
    std::string const mob = nearbyMob.empty() ? "" : FirstPlaceWord(nearbyMob);
    std::string dungeon;
    if (bot && bot->GetMap() && bot->GetMap()->IsDungeon())
        dungeon = FirstPlaceWord(bot->GetMap()->GetMapName());

    std::vector<std::string> lines;
    // Take, don't status. "died to pterrordax lmao" invites a pile-on.
    // "ghosted in ungoro" is just a GPS ping.
    if (!hint.empty())
        AddSingularTakes(lines, hint);
    if (!nick.empty())
    {
        lines.push_back(nick + " quests are aids");
        lines.push_back(nick + " grind is aids");
        lines.push_back("elites in " + nick + " are aids");
        lines.push_back("nubs in " + nick);
        lines.push_back(nick + " xp is trash");
        lines.push_back("who quests in " + nick + " for fun");
        lines.push_back("who put the fp in " + nick);
        lines.push_back(nick + " is a joke");
        lines.push_back("why is " + nick + " like this");
    }
    AddMobTakes(lines, mob);
    if (!mob.empty() && mob != "here" && bot && bot->isDead())
    {
        lines.push_back("died to " + mob + " lmao");
        lines.push_back(mob + " ate me");
        lines.push_back("how did " + mob + " kill me");
    }
    if (!dungeon.empty() && dungeon != "here")
    {
        lines.push_back(dungeon + " trash is aids");
        lines.push_back("wipe on " + dungeon + " again");
        lines.push_back("this " + dungeon + " pug is a joke");
        lines.push_back(dungeon + " tanks are asleep");
    }
    if (!className.empty())
    {
        lines.push_back(className + " is easy mode");
        lines.push_back("this " + className + " spec is trash");
        lines.push_back("why play " + className);
        if (!nick.empty())
            lines.push_back("why are " + className + "s pulling everything in " + nick);
        if (className == "hunter")
        {
            lines.push_back("feed your pet");
            lines.push_back("hunter pet disappeared again");
            if (!nick.empty())
                lines.push_back("out of ammo in " + nick);
        }
        if (className == "pally" || className == "paladin")
        {
            lines.push_back("bubble hearth classic");
            lines.push_back("found the pally");
        }
    }
    if (bot)
    {
        if (bot->isDead() && mob.empty() && !nick.empty())
            lines.push_back("one shot in " + nick + " lmao");
        if (bot->GetFreeInventorySpace() < 4)
        {
            if (!mob.empty() && mob != "here")
                lines.push_back("why do " + mob + " drop this junk");
            else if (!nick.empty())
                lines.push_back("vendor trash in " + nick + " lmao");
            else
                lines.push_back("everything is grey lmao");
        }
        uint32 const level = bot->GetLevel();
        uint32 const gold = bot->GetMoney() / 10000;
        if (level >= 38 && level <= 44 && gold < 50)
            lines.push_back("mount gold is aids");
        if (level >= 58 && level <= 64 && gold < 600)
            lines.push_back("flying gold is a joke");
        if (bot->HasSkill(SKILL_HERBALISM) && !nick.empty())
            lines.push_back("herbs in " + nick + " are camped");
        if (bot->HasSkill(SKILL_MINING) && !nick.empty())
            lines.push_back("nodes in " + nick + " are camped");
        if (bot->HasSkill(SKILL_SKINNING) && !mob.empty() && mob != "here")
            lines.push_back("skinning " + mob + " is aids");
        if (bot->GetTeamId() == TEAM_ALLIANCE && !nick.empty())
            lines.push_back("horde camping the fp in " + nick + " again");
        if (bot->GetTeamId() == TEAM_HORDE && !nick.empty())
            lines.push_back("alliance camping the fp in " + nick + " again");
    }
    for (char const* line : kDribbleOpeners)
        lines.push_back(line);
    for (char const* line : kDribbleLongTakes)
        lines.push_back(line);

    bool const capital = AreaLooksCapital(bot);
    std::vector<std::string> preferred;
    std::vector<std::string> rest;
    for (std::string const& line : lines)
    {
        if (line.empty())
            continue;
        if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
            continue;
        if (LineRecentlySpoken(line))
            continue;
        bool named = (!hint.empty() && line.find(hint) != std::string::npos) ||
                     (!nick.empty() && line.find(nick) != std::string::npos) ||
                     (!mob.empty() && line.find(mob) != std::string::npos) ||
                     (!dungeon.empty() && line.find(dungeon) != std::string::npos) ||
                     (!className.empty() && line.find(className) != std::string::npos);
        if (named)
            preferred.push_back(line);
        else if (DribbleFitsRoom(line, capital))
            rest.push_back(line);
    }
    std::vector<std::string>* pool = &rest;
    // Named live slots are a garnish. The wild pool is the room.
    if (!preferred.empty() && (rest.empty() || urand(0, 99) < 25))
        pool = &preferred;

    uint32 const tox = BotChatEffectiveToxicity();
    uint32 hot = tox ? 12 * tox : 0;
    if (BotChatBlowupActiveAny())
        hot = 88;
    if (hot && bot)
    {
        BotChatMouth const mouth = MouthForBot(bot);
        if (mouth == BotChatMouth::Salt)
            hot = std::min<uint32>(90, hot + 15);
        else if (mouth == BotChatMouth::Quiet)
            hot /= 3;
    }
    if (hot && urand(0, 99) < hot)
    {
        bool const late = BotChatAdultMix() > 0;
        uint32 const roll = urand(0, 99);
        std::string picked;
        if (tox >= 3 && late && roll < 28)
            picked = TryPickCannedPool(kToxicPersonal, sizeof(kToxicPersonal) / sizeof(kToxicPersonal[0]),
                                       threadKey, capital);
        else if (tox >= 3 && late && roll < 62)
            picked = TryPickCannedPool(kToxicLgbt, sizeof(kToxicLgbt) / sizeof(kToxicLgbt[0]),
                                       threadKey, capital);
        else if (tox >= 2 && late && roll < 78)
            picked = TryPickCannedPool(kToxicIdentity, sizeof(kToxicIdentity) / sizeof(kToxicIdentity[0]),
                                       threadKey, capital);
        else if (tox >= 1)
            picked = TryPickCannedPool(kToxicGame, sizeof(kToxicGame) / sizeof(kToxicGame[0]),
                                       threadKey, capital);
        if (!picked.empty())
            return picked;
    }

    uint32 adultMix = BotChatAdultMix();
    if (adultMix && bot)
    {
        BotChatMouth const mouth = MouthForBot(bot);
        if (mouth == BotChatMouth::Salt)
            adultMix = std::min<uint32>(95, adultMix + 18);
        else if (mouth == BotChatMouth::Quiet)
            adultMix /= 3;
        else if (mouth == BotChatMouth::Lfg)
            adultMix /= 2;
    }
    if (adultMix && urand(0, 99) < adultMix)
    {
        std::string const picked = TryPickCannedPool(kAdultOpeners,
            sizeof(kAdultOpeners) / sizeof(kAdultOpeners[0]), threadKey, capital);
        if (!picked.empty())
            return picked;
    }

    if (pool->empty())
        return "";
    std::string picked = (*pool)[urand(0, pool->size() - 1)];
    picked = MessyChat(picked);
    if (picked.empty())
        return "";
    NoteSpokenLine(picked);
    return picked;
}

std::string PickAmbientCityLine(std::string const& zone, std::string const& threadKey)
{
    // Already in this city's General. Do not name the city. Do not narrate an itinerary.
    (void)zone;
    return PickFrom({
        "ah is a scam", "repair bill hurts", "where trainer at",
        "anyone got a port", "glyphs are expensive", "bags are full",
        "gonna vendor", "need gold for mount", "this ah sucks",
        "waiting on trainer", "bank is packed", "anyone selling herbs",
        "need an enchant", "bored anyone wanna grind", "where fp",
        "gonna dump junk", "ah search sucks", "need a mage",
        "repair costs a fortune", "just trained", "this city is a maze",
        "anyone got wool", "inn is packed", "gold sink is real"
    }, threadKey, false, true);
}

std::string PickAmbientZoneLine(std::string const& zone, std::string const& threadKey)
{
    std::string const nick = ChatZoneNick(zone);
    if (!nick.empty())
    {
        std::string slotted = PickSlotted({
            "anyone in " + nick,
            nick + " xp is trash",
            "is " + nick + " even worth it",
            "elites in " + nick + " hit hard",
            "how crowded is " + nick,
            nick + " grind is aids",
            nick + " quests are aids",
            "nubs in " + nick,
            "xp in " + nick + " is slow",
            "need a grp in " + nick,
            "who quests in " + nick + " for fun",
            nick + " is a joke",
            "why is " + nick + " like this",
            "elites in " + nick + " are aids"
        }, threadKey);
        if (!slotted.empty())
            return slotted;
    }
    return PickFrom({
        "anyone doing quests here", "whats worth grinding here",
        "do i need a group for this", "xp here feels slow",
        "is this elite soloable", "where do i turn this in",
        "anyone on the chain here", "mobs here have too much hp",
        "drops here are a joke", "is the fp even close",
        "need a grp for this camp"
    }, threadKey, false, true);
}

std::string PickAmbientQuestLine(std::string const& title, std::string const& threadKey)
{
    std::string const hint = ShortQuestHint(title);
    if (!hint.empty())
    {
        std::string slotted = PickSlotted({
            "anyone still on " + hint,
            "is " + hint + " soloable",
            "need a hand with " + hint,
            "where do i turn in " + hint,
            hint + " is taking forever",
            "this " + hint + " quest sucks",
            hint + " is aids",
            "this " + hint + " is aids",
            "skip " + hint + " or no",
            "how many for " + hint,
            "stuck on " + hint,
            hint + " is a joke",
            "who designed " + hint,
            hint + " can eat me",
            "why does " + hint + " exist",
            "skip " + hint + " if you can"
        }, threadKey);
        if (!slotted.empty())
            return slotted;
    }
    return PickFrom({
        "anyone on this quest still", "is this quest soloable",
        "need a hand with this one", "where do i turn this in",
        "anyone else stuck on this", "is this even required",
        "how many more of these", "grey quests already lol"
    }, threadKey, false, true);
}

std::string PickAmbientGroupLine(bool inGuild, std::string const& guildie, std::string const& threadKey,
                                 std::string const& zone)
{
    (void)guildie;
    std::string const nick = ChatZoneNick(zone);
    if (inGuild)
        return PickFrom({
            "anyone for a run", "lf1m for this chain", "need a tank if anyone is on",
            "going in need 1", "wanna grind together", "heroic anyone",
            "need a healer for a run", "spots open", "who wants in",
            "easy run if anyone is down", "lets knock something out",
            "i can tag along if you need one"
        }, threadKey, false, true);
    if (!nick.empty())
    {
        std::string slotted = PickSlotted({
            "anyone for quests in " + nick,
            "lf1m in " + nick,
            "need a tank in " + nick,
            "anyone grouping in " + nick
        }, threadKey);
        if (!slotted.empty())
            return slotted;
    }
    return PickFrom({
        "lf1m for this chain", "need a tank if anyone is around",
        "wanna grind together", "anyone for a run", "lf healer", "need 1 more",
        "anyone grouping here", "pst if you want in"
    }, threadKey, false, true);
}

std::string PickAmbientClassLine(std::string const& className, std::string const& threadKey)
{
    std::string const cls = className.empty() ? "this class" : className;
    std::string slotted = PickSlotted({
        "anyone else play " + cls,
        "this " + cls + " spec feels off",
        "is " + cls + " even good here",
        cls + " grinding is rough at this lvl",
        "respec " + cls + " or stay",
        cls + " feels weak this bracket"
    }, threadKey);
    if (!slotted.empty())
        return slotted;
    return PickFrom({
        "hate this spec honestly", "respec soon i think",
        "might reroll lol", "rotation is boring"
    }, threadKey, false, true);
}

std::string PickAmbientPartyLine(Player* bot, std::string const& threadKey)
{
    if (bot && bot->IsInCombat())
        return PickFrom({
            "inc", "adds", "behind", "on me", "wait", "cc",
            "i got this", "spread", "dont stand in that"
        }, threadKey, false, true);
    return PickFrom({
        "otw", "ready", "drink", "food", "gz", "need?",
        "summon pls", "buffs", "1 sec", "mana", "greed"
    }, threadKey, false, true);
}

std::string PickAmbientRareLine(std::string const& nick, std::string const& threadKey)
{
    if (nick.empty())
        return "";
    return PickSlotted({
        nick + " up",
        nick + " is up",
        "got " + nick
    }, threadKey);
}

std::string PickAmbientDungeonLine(std::string const& dungeon, std::string const& threadKey)
{
    std::string const place = ShortPlacePhrase(dungeon);
    std::string slotted = PickSlotted({
        "anyone run " + place,
        "need a healer for " + place,
        "is " + place + " easy now",
        "lfm " + place,
        "this " + place + " run is slow",
        "wipe on " + place + " again",
        "need dps for " + place
    }, threadKey);
    if (!slotted.empty())
        return slotted;
    return PickFrom({
        "anyone for a run", "need a healer", "need a rez",
        "boss is next", "food break", "summon pls"
    }, threadKey, false, true);
}

std::string PickGroupReply(std::string const& threadKey, bool inParty, Player* bot)
{
    if (inParty)
        return PickFrom({
            "yeah im in", "on it", "need a tank still", "ready when you are",
            "need a rez first", "buffing", "summon when you can"
        }, threadKey, false, true);

    std::vector<char const*> lines = {
        "what for", "which quest", "what instance", "pst", "im down", "busy",
        "where", "what lvl", "heroic?", "spots?", "already grouped"
    };
    if (bot)
    {
        switch (bot->getClass())
        {
            case CLASS_WARRIOR:
            case CLASS_DEATH_KNIGHT:
                lines.push_back("i tank");
                lines.push_back("im tank");
                lines.push_back("tank here");
                lines.push_back("i can tank");
                break;
            case CLASS_PALADIN:
                lines.push_back("i tank");
                lines.push_back("i heal");
                lines.push_back("pally tank");
                lines.push_back("holy here");
                break;
            case CLASS_PRIEST:
                lines.push_back("i heal");
                lines.push_back("disc here");
                lines.push_back("heal here");
                break;
            case CLASS_SHAMAN:
                lines.push_back("i heal");
                lines.push_back("resto here");
                break;
            case CLASS_DRUID:
                lines.push_back("i tank");
                lines.push_back("i heal");
                lines.push_back("bear tank");
                break;
            default:
                lines.push_back("dps here");
                lines.push_back("i dps");
                break;
        }
    }

    std::vector<char const*> fresh;
    for (char const* line : lines)
    {
        if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
            continue;
        if (LineRecentlySpoken(line))
            continue;
        fresh.push_back(line);
    }
    if (fresh.empty())
        return "";
    std::string const picked = fresh[urand(0, fresh.size() - 1)];
    NoteSpokenLine(picked);
    return picked;
}

bool IsGroupAsk(std::string const& message)
{
    if (message.empty())
        return false;
    std::string lower = ToLowerCopy(message);
    while (!lower.empty() && (lower.back() == '?' || lower.back() == '.' || lower.back() == '!' || lower.back() == ' '))
        lower.pop_back();
    if (lower == "what for" || lower == "whatfor" || lower.find("what for") != std::string::npos)
        return true;
    if (lower.find("which quest") != std::string::npos || lower.find("which instance") != std::string::npos)
        return true;
    if (lower.find("what instance") != std::string::npos || lower.find("what dungeon") != std::string::npos)
        return true;
    if (lower.find("what lvl") != std::string::npos || lower.find("what level") != std::string::npos)
        return true;
    if (lower == "heroic" || lower == "heroic?" || lower == "spots" || lower == "spots?")
        return true;
    return false;
}

bool IsGroupJoin(std::string const& message)
{
    if (message.empty())
        return false;
    std::string lower = ToLowerCopy(message);
    while (!lower.empty() && (lower.back() == '?' || lower.back() == '.' || lower.back() == '!' || lower.back() == ' '))
        lower.pop_back();
    if (lower == "im in" || lower == "i m in" || lower == "in" || lower == "me" || lower == "down")
        return true;
    if (lower == "im down" || lower == "i can go" || lower == "can go")
        return true;
    if (HasWord(lower, "pst") || HasWord(lower, "inv") || lower.find("invite") != std::string::npos)
        return true;
    if (lower.find("im in") != std::string::npos || lower.find("i m in") != std::string::npos)
        return true;
    if (lower.find("i tank") != std::string::npos || lower.find("i can tank") != std::string::npos ||
        lower.find("tank here") != std::string::npos || lower == "tank")
        return true;
    if (lower.find("i heal") != std::string::npos || lower.find("heal here") != std::string::npos ||
        lower == "heal" || lower == "heals")
        return true;
    if (lower == "dps" || lower == "i dps" || lower == "dps here")
        return true;
    return false;
}

std::string PickGroupPurpose(Player* bot, std::string const& threadKey)
{
    std::vector<std::string> lines;
    if (bot && bot->GetMap() && bot->GetMap()->IsDungeon())
    {
        std::string const place = FirstPlaceWord(bot->GetMap()->GetMapName());
        lines.push_back("this run");
        if (!place.empty() && place != "here")
        {
            lines.push_back(place);
            lines.push_back("need 1 for " + place);
        }
    }

    if (bot)
    {
        uint32 const zoneId = BotLiveZoneId(bot);
        for (auto const& [questId, status] : bot->getQuestStatusMap())
        {
            if (status.Status != QUEST_STATUS_INCOMPLETE)
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;
            if (zoneId && quest->GetZoneOrSort() > 0 &&
                static_cast<uint32>(quest->GetZoneOrSort()) != zoneId)
                continue;
            std::string const hint = ShortQuestHint(quest->GetTitle());
            if (!hint.empty())
            {
                lines.push_back(hint);
                lines.push_back("this chain");
                break;
            }
        }
        std::string const nick = ChatZoneNick(BotPlaceName(bot));
        if (!nick.empty())
        {
            lines.push_back("quests in " + nick);
            lines.push_back(nick + " grind");
        }
    }

    if (lines.empty())
    {
        lines.emplace_back("this chain");
        lines.emplace_back("a run");
        lines.emplace_back("quests");
        lines.emplace_back("heroic");
    }

    std::vector<std::string> fresh;
    for (std::string const& line : lines)
    {
        if (line.empty())
            continue;
        if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
            continue;
        if (LineRecentlySpoken(line))
            continue;
        fresh.push_back(line);
    }
    if (fresh.empty())
        return PickFrom({ "this chain", "a run", "quests", "heroic" }, threadKey, false, true);
    std::string const picked = fresh[urand(0, fresh.size() - 1)];
    NoteSpokenLine(picked);
    return picked;
}

std::string PickGroupChime(Player* bot, std::string const& threadKey)
{
    std::vector<char const*> lines = { "im down", "me", "i can go", "need 1 still" };
    if (bot)
    {
        switch (bot->getClass())
        {
            case CLASS_WARRIOR:
            case CLASS_DEATH_KNIGHT:
                lines.push_back("i tank");
                lines.push_back("tank here");
                break;
            case CLASS_PALADIN:
                lines.push_back("i tank");
                lines.push_back("i heal");
                break;
            case CLASS_PRIEST:
            case CLASS_SHAMAN:
                lines.push_back("i heal");
                lines.push_back("heal here");
                break;
            case CLASS_DRUID:
                lines.push_back("i tank");
                lines.push_back("i heal");
                break;
            default:
                lines.push_back("dps here");
                break;
        }
    }

    std::vector<char const*> fresh;
    for (char const* line : lines)
    {
        if (!threadKey.empty() && LineTooSimilarToRecent(threadKey, line))
            continue;
        if (LineRecentlySpoken(line))
            continue;
        fresh.push_back(line);
    }
    if (fresh.empty())
        return "";
    std::string const picked = fresh[urand(0, fresh.size() - 1)];
    NoteSpokenLine(picked);
    return picked;
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

bool LooksUnlikeWowChat(std::string const& line, ChatLineStyle style)
{
    size_t maxChars = 90;
    unsigned maxWords = 16;
    unsigned maxCommas = 2;
    if (style == ChatLineStyle::Flame)
    {
        maxChars = 50;
        maxWords = 10;
        maxCommas = 1;
    }
    else if (style == ChatLineStyle::Guild)
    {
        maxChars = 100;
        maxWords = 18;
        maxCommas = 2;
    }
    if (line.size() > maxChars)
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
    if (words > maxWords || commas > maxCommas || periods > 1)
        return true;

    std::string const lower = ToLowerCopy(line);
    if (lower == "this guy" || lower == "this chat" || lower == "this room" ||
        lower == "well that happened" || lower == "chat is wild today")
        return true;
    static char const* bans[] = {
        "i think", "i believe", "that's not", "that is not",
        "nearby", "the area", "for some",
        "valley of the four", "pandaria", "draenor", "broken isles",
        "kul tiras", "zuldazar", "shadowlands", "dragon isles",
        "khaz algar", "garrosh", "stocked with", "exactly",
        "restrooms", "office", "narrat", "hanging out", "just hanging",
        "questie", "pretty sweet", "deserves", "got my first", "just got the",
        "what's next", "whats next", "good reward",
        "tonight", "tomorrow", "we've got", "we have a", "raid to",
        "get banned", "not welcome", "calm down", "hyped", "i can help",
        "make sure", "time to get", "the team", "trollin",
        "i know a", "i know an", "know a paladin", "know a warrior",
        "maybe he", "maybe she", "maybe they", "maybe a ",
        "can help", "could help", "might help", "he can", "she can",
        "a friend", "my friend", "try asking", "look for a",
        "paladin in", "warrior in", "priest in", "mage in", "druid in",
        "shaman in", "rogue in", "hunter in", "warlock in", "dk in",
        "death knight in",
        "kill yourself", "kys", "off yourself", "where you live",
        "your address", "dox incoming", "posted your info",
        "christian", "christians", "jesus", "the bible", "your god",
        "fuck god", "fuck christ", "god is dead", "christ is fake",
        "god isnt", "god isn't", "god wont", "god won't", "god aint", "god ain't"
    };
    for (char const* ban : bans)
    {
        if (lower.find(ban) != std::string::npos)
            return true;
    }
    if (style != ChatLineStyle::Flame && !BotChatAdultMix())
    {
        if (lower.find("shut up") != std::string::npos || HasWord(lower, "stfu"))
            return true;
        if (lower.rfind("going to", 0) == 0 || lower.rfind("i am ", 0) == 0 ||
            lower.rfind("i'll ", 0) == 0 || lower.rfind("there is ", 0) == 0)
            return true;
    }
    return false;
}

std::string SanitizeBotChatLine(std::string const& line, ChatLineStyle style)
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
    // pala -> pally. Do not touch paladin.
    {
        std::string const lower = ToLowerCopy(out);
        std::string fixed;
        fixed.reserve(out.size() + 8);
        size_t i = 0;
        while (i < out.size())
        {
            bool const leftOk = i == 0 || !std::isalnum(static_cast<unsigned char>(out[i - 1]));
            if (leftOk && i + 4 <= out.size() && lower.compare(i, 4, "pala") == 0)
            {
                bool const rightOk = i + 4 == out.size() ||
                                     !std::isalnum(static_cast<unsigned char>(out[i + 4]));
                if (rightOk)
                {
                    fixed += "pally";
                    i += 4;
                    continue;
                }
            }
            fixed += out[i];
            ++i;
        }
        out = std::move(fixed);
    }
    if (out.empty() || LooksLikeSlangSalad(out) || LooksUnlikeWowChat(out, style))
        return "";
    return BotChatBlowupYell(out);
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

uint32 BotChatPopulationPct()
{
    if (!g_ScaleWithPopulation)
        return 100;

    static uint32 cached = 100;
    static time_t last = 0;
    time_t const now = time(nullptr);
    if (last && difftime(now, last) < 5.0)
        return cached;

    last = now;
    uint32 const peak = sPlayerbotAIConfig.maxRandomBots;
    if (!peak)
    {
        cached = 100;
        return cached;
    }

    uint32 online = 0;
    PlayerBotMap const bots = sRandomPlayerbotMgr.GetAllBots();
    for (auto const& pair : bots)
    {
        if (pair.second && sRandomPlayerbotMgr.IsRandomBot(pair.second))
            ++online;
    }

    uint32 pct = (online * 100) / peak;
    if (pct < 3)
        pct = 3;
    if (pct > 100)
        pct = 100;
    cached = pct;
    return cached;
}

uint32 ScaleChatChance(uint32 chance)
{
    uint32 const pct = BotChatPopulationPct();
    uint32 scaled = chance * pct / 100;
    if (!scaled && chance && pct >= 10)
        scaled = 1;
    return scaled;
}

uint32 ScaleChatInterval(uint32 seconds)
{
    uint32 const pct = BotChatPopulationPct();
    if (pct >= 100)
        return seconds;
    uint32 scaled = seconds * 100 / std::max<uint32>(pct, 5);
    if (scaled < seconds)
        scaled = seconds;
    return scaled;
}

void BotChatTypingSleep(size_t charCount)
{
    if (!g_EnableTypingSimulation)
        return;

    uint32 delay = g_TypingSimulationBaseDelay +
                   static_cast<uint32>(charCount) * g_TypingSimulationDelayPerChar;
    uint32 const jitter = delay / 5;
    if (jitter)
        delay = urand(delay > jitter ? delay - jitter : 0, delay + jitter);
    if (delay < 400)
        delay = 400;
    if (delay > 8000)
        delay = 8000;

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[Bot Chat] typing delay {}ms ({} chars)", delay, charCount);

    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

