#ifndef BOT_CHAT_UTILS_H
#define BOT_CHAT_UTILS_H

#include <string>
#include <fmt/format.h>
#include "Log.h"
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>

class Player;

inline std::string GetBotPersonality(Player*)
{
    return {};
}

inline std::string GetPersonalityPromptAddition(std::string const&)
{
    return {};
}

inline std::string GetSentimentPromptAddition(Player*, Player*)
{
    return {};
}

// Safe formatting utility for the Ollama Chat module.
// This will catch all fmt::format errors and log them.
template<typename... Args>
inline std::string SafeFormat(const std::string& templ, Args&&... args) {
    try {
        return fmt::vformat(templ, fmt::make_format_args(args...));
    } catch (const fmt::format_error& e) {
        LOG_ERROR("server.loading", "[Bot Chat] Format error: {} | Template: {}", e.what(), templ);
        return "[Format Error]";
    }
}

inline std::vector<std::string> SplitString(const std::string& str, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim))
    {
        // Trim whitespace from token
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
    }
    return tokens;
}

// Strip WoW client markup so models never see |cff / |Hitem / |h|r junk.
inline std::string StripWowMarkup(std::string text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); )
    {
        if (text[i] == '|')
        {
            if (i + 1 >= text.size())
                break;
            char code = text[i + 1];
            if (code == '|')
            {
                out.push_back('|');
                i += 2;
                continue;
            }
            if ((code == 'c' || code == 'C') && i + 10 <= text.size())
            {
                i += 10; // |cAARRGGBB
                continue;
            }
            if (code == 'r' || code == 'R' || code == 'h' || code == 'H')
            {
                if (code == 'H' || code == 'h')
                {
                    size_t end = text.find('|', i + 2);
                    if (end != std::string::npos && end + 1 < text.size() &&
                        (text[end + 1] == 'h' || text[end + 1] == 'H'))
                    {
                        i = end + 2;
                        continue;
                    }
                }
                i += 2;
                continue;
            }
            if (code == 'n' || code == 'N')
            {
                out.push_back(' ');
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

inline std::string CollapseWhitespace(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool inSpace = false;
    for (char c : text)
    {
        if (c == '\t' || c == '\r')
            c = ' ';
        if (c == ' ')
        {
            if (!inSpace)
                out.push_back(' ');
            inSpace = true;
            continue;
        }
        inSpace = false;
        out.push_back(c);
    }
    return out;
}

inline std::string CleanPromptText(const std::string& text)
{
    return CollapseWhitespace(StripWowMarkup(text));
}

// Lowercase, keep letters/digits/apostrophe, collapse everything else to a single space.
inline std::string NormalizeChatLine(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (unsigned char c : text)
    {
        if (std::isalnum(c) || c == '\'')
        {
            out.push_back(static_cast<char>(std::tolower(c)));
            space = false;
        }
        else if (!out.empty() && !space)
        {
            out.push_back(' ');
            space = true;
        }
    }
    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// Exact match, or high token overlap. Do not use substring containment:
// "going in" is a substring of "going into frostwolf" and was dropping real replies.
inline bool ChatLinesSimilar(std::string const& a, std::string const& b)
{
    std::string const na = NormalizeChatLine(a);
    std::string const nb = NormalizeChatLine(b);
    if (na.empty() || nb.empty())
        return false;
    if (na == nb)
        return true;

    std::vector<std::string> ta = SplitString(na, ' ');
    std::vector<std::string> tb = SplitString(nb, ' ');
    if (ta.empty() || tb.empty())
        return false;

    unsigned shared = 0;
    for (std::string const& t : ta)
    {
        if (std::find(tb.begin(), tb.end(), t) != tb.end())
            ++shared;
    }
    unsigned const uni = static_cast<unsigned>(ta.size() + tb.size() - shared);
    if (!uni)
        return false;
    return (shared * 100) / uni >= 70;
}

// Drop stop words and turn unknown tokens (zone/quest names) into # so
// "anyone doing quests in un goro" and "anyone doing quests in searing gorge"
// share a signature. Real players notice the template, not the place name.
inline std::string ChatLineSignature(std::string const& text)
{
    static std::unordered_set<std::string> const stop = {
        "a", "an", "the", "in", "on", "at", "of", "to", "and"
    };
    static std::unordered_set<std::string> const vocab = {
        "anyone", "anybody", "else", "doing", "quest", "quests", "grind", "grinding",
        "xp", "tank", "tanks", "pala", "pally", "paladin", "warrior", "mage", "priest", "druid",
        "rogue", "hunter", "shaman", "warlock", "dk", "heal", "healer", "healing", "dps",
        "grp", "group", "lfg", "lfm", "lf1m", "lf2m", "lf3m", "ah", "bank", "trainer",
        "fp", "inn", "port", "fly", "glyphs", "repair", "bill", "prices", "trash",
        "vendor", "vendored", "junk", "town", "chain", "turnin", "turn", "hand",
        "stuck", "soloable", "bugged", "worth", "slow", "forever", "elite", "mobs",
        "drops", "run", "rez", "ready", "pst", "busy", "down", "instance", "dungeon",
        "need", "still", "wanna", "want", "got", "know", "where", "what", "how",
        "which", "is", "are", "do", "does", "even", "good", "sucks", "suck", "here",
        "too", "same", "yeah", "im", "i", "me", "you", "u", "we", "this", "that",
        "it", "my", "spec", "class", "lvl", "respec", "lag", "fps", "isp", "dc",
        "queue", "client", "hitching", "froze", "spike", "heat", "rain", "storm",
        "thunder", "news", "game", "watching", "coffee", "sleep", "chair", "back",
        "room", "loud", "eyes", "smoke", "power", "monday", "friday", "blizzard",
        "servers", "brb", "g2g", "afk", "logging", "food", "ty", "thx", "np", "yw",
        "gz", "gj", "wb", "gl", "gg", "wp", "inv", "whisper", "room", "later",
        "almost", "done", "out", "heading", "packed", "awful", "insane", "up",
        "from", "around", "together", "healer", "easy", "slow", "rough", "feels",
        "taking", "annoying", "skip", "or", "no", "yes", "yet", "again", "bit",
        "sec", "huh", "wdym", "n", "lol", "lmao", "true", "yep", "kk", "ok",
        "cya", "gn", "bb", "night", "hey", "hi", "yo", "sup", "rip", "oof", "f",
        "re", "welc", "grats", "nice", "thx", "tyvm", "guys", "man", "too",
        "watching", "cant", "focus", "either", "wild", "isnt", "dying", "killing",
        "brutal", "flickered", "working", "freezing", "loud", "joke", "spike",
        "one", "more", "then", "im", "out", "for", "if", "its", "this", "one",
        "instance", "dungeon", "heroic", "naxx", "kara", "heroic", "pst", "grouped",
        "questing", "flying", "combat", "pulling", "ghost", "dead", "corpse",
        "sell", "selling", "glyphs", "mage", "port", "trainer", "fly",
        "holy", "disc", "resto", "bear", "heroic", "spots", "grouped",
        "summon", "wipe", "food", "buffs", "buffing", "neighbors", "fan",
        "bags", "rares", "grey", "rotation", "reroll", "packed", "crowded",
        "maze", "greens", "gold", "sink", "ages", "spare", "east", "guard",
        "boat", "stomach", "water", "keyboard", "desk", "phone", "sun",
        "empty", "buzzing", "bio", "addon", "error", "loading", "screen",
        "can", "tank", "heal", "dps", "pala", "pally", "priest", "lvl"
    };

    std::vector<std::string> tokens = SplitString(NormalizeChatLine(text), ' ');
    std::string out;
    bool lastHash = false;
    for (std::string const& t : tokens)
    {
        if (stop.count(t))
            continue;
        std::string const piece = vocab.count(t) ? t : "#";
        if (piece == "#" && lastHash)
            continue;
        if (!out.empty())
            out.push_back(' ');
        out += piece;
        lastHash = (piece == "#");
    }
    return out;
}

inline bool ChatLineShapesMatch(std::string const& a, std::string const& b)
{
    std::string const sa = ChatLineSignature(a);
    std::string const sb = ChatLineSignature(b);
    if (sa.empty() || sb.empty() || sa != sb)
        return false;
    // Subject-first takes ("yowler is aids" vs "pterrordax is aids") share a
    // slang frame. The subject is the joke — do not realm-block those.
    if (sa[0] == '#')
        return false;
    // A single leftover token ("#", "tank") is too weak to block the whole realm.
    unsigned parts = 1;
    for (char c : sa)
    {
        if (c == ' ')
            ++parts;
    }
    return parts >= 2 || (sa != "#" && sa.size() >= 4);
}

// Sanitize a string to be valid UTF-8 by removing or replacing invalid bytes
inline std::string SanitizeUTF8(const std::string& str)
{
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); )
    {
        unsigned char c = static_cast<unsigned char>(str[i]);
        
        // Single-byte character (0xxxxxxx)
        if (c <= 0x7F)
        {
            result.push_back(str[i]);
            i++;
        }
        // Two-byte character (110xxxxx 10xxxxxx)
        else if ((c & 0xE0) == 0xC0)
        {
            if (i + 1 < str.size() && (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80)
            {
                result.push_back(str[i]);
                result.push_back(str[i + 1]);
                i += 2;
            }
            else
            {
                // Invalid sequence, replace with space
                result.push_back(' ');
                i++;
            }
        }
        // Three-byte character (1110xxxx 10xxxxxx 10xxxxxx)
        else if ((c & 0xF0) == 0xE0)
        {
            if (i + 2 < str.size() &&
                (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(str[i + 2]) & 0xC0) == 0x80)
            {
                result.push_back(str[i]);
                result.push_back(str[i + 1]);
                result.push_back(str[i + 2]);
                i += 3;
            }
            else
            {
                // Invalid sequence, replace with space
                result.push_back(' ');
                i++;
            }
        }
        // Four-byte character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        else if ((c & 0xF8) == 0xF0)
        {
            if (i + 3 < str.size() &&
                (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(str[i + 2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(str[i + 3]) & 0xC0) == 0x80)
            {
                result.push_back(str[i]);
                result.push_back(str[i + 1]);
                result.push_back(str[i + 2]);
                result.push_back(str[i + 3]);
                i += 4;
            }
            else
            {
                // Invalid sequence, replace with space
                result.push_back(' ');
                i++;
            }
        }
        else
        {
            // Invalid UTF-8 start byte, replace with space
            result.push_back(' ');
            i++;
        }
    }
    
    return result;
}

#endif // BOT_CHAT_UTILS_H
