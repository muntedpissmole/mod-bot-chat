#ifndef BOT_CHAT_UTILS_H
#define BOT_CHAT_UTILS_H

#include <string>
#include <fmt/format.h>
#include "Log.h"
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

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
