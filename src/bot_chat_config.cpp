#include "bot_chat_config.h"
// sentiment removed
// rag removed
#include "bot_chat_knowledge.h"
#include "bot_chat_thread.h"
#include "Config.h"
#include "Log.h"
// ollama api removed
#include <fmt/core.h>
#include <sstream>
#include <fstream>


// --------------------------------------------
// Distance/Range Configuration
// --------------------------------------------
float      g_SayDistance       = 30.0f;
float      g_YellDistance      = 100.0f;
float      g_RandomChatterRealPlayerDistance = 40.0f;
float      g_EventChatterRealPlayerDistance = 40.0f;

// --------------------------------------------
// Bot/Player Chatter Probability & Limits
// --------------------------------------------
// Per-channel-type reply chances
uint32_t   g_PlayerReplyChance_Say     = 90;
uint32_t   g_BotReplyChance_Say        = 10;
uint32_t   g_PlayerReplyChance_Channel = 50;
uint32_t   g_BotReplyChance_Channel    = 5;
uint32_t   g_PlayerReplyChance_Party   = 90;
uint32_t   g_BotReplyChance_Party      = 10;
uint32_t   g_PlayerReplyChance_Guild   = 70;
uint32_t   g_BotReplyChance_Guild      = 5;

uint32_t   g_MaxBotsToPick     = 2;
bool       g_EnableSocialConventions = true;
uint32_t   g_SocialCueSeconds  = 45;
uint32_t   g_RandomChatterBotCommentChance   = 18;
bool        g_ScaleWithPopulation            = true;
bool        g_AdultEnable                    = true;
uint32_t    g_AdultHour                      = 21;
uint32_t    g_Toxicity                       = 0;
uint32_t    g_BlowupChance                   = 28;
uint32_t    g_BlowupSeconds                  = 600;
uint32_t   g_RandomChatterMaxBotsPerPlayer   = 2;
uint32_t   g_EventChatterBotCommentChance    = 15;
uint32_t   g_EventChatterBotSelfCommentChance = 5;
uint32_t   g_EventChatterMaxBotsPerPlayer    = 2;

// --------------------------------------------
// Ollama LLM API Configuration
// --------------------------------------------
std::string g_OllamaUrl        = "http://127.0.0.1:11434/api/generate";
std::string g_OllamaModel      = "llama3.1:8b";
uint32_t    g_OllamaNumPredict = 64;
float       g_OllamaTemperature = 0.95f;
uint32_t    g_OllamaTimeout    = 15;
float       g_OllamaTopP = 0.95f;
float       g_OllamaRepeatPenalty = 1.1f;
uint32_t    g_OllamaNumCtx = 0;
uint32_t    g_OllamaNumThreads = 0;
std::string g_OllamaStop = "";
std::string g_OllamaSystemPrompt = "";
std::string g_OllamaSeed = "";

// --------------------------------------------
// Concurrency/Queueing
// --------------------------------------------
uint32_t    g_MaxConcurrentQueries = 1;

// --------------------------------------------
// Feature Toggles & Core Settings
// --------------------------------------------
bool        g_Enable                          = true;
bool        g_EnableLLM                       = true;
bool        g_DisableRepliesInCombat          = true;
bool        g_EnableRandomChatter             = true;
bool        g_EnableEventChatter              = true;
bool        g_EnableRPPersonalities           = false;
bool        g_EnableWhisperReplies            = true;
bool        g_DebugEnabled                    = false;
bool        g_DebugShowFullPrompt             = false;

// --------------------------------------------
// Think Mode Support
// --------------------------------------------
bool g_ThinkModeEnableForModule = false;

// --------------------------------------------
// Random Chatter Timing
// --------------------------------------------
uint32_t    g_MinRandomInterval               = 45;
uint32_t    g_MaxRandomInterval               = 180;

// --------------------------------------------
// Conversation History Settings
// --------------------------------------------
uint32_t    g_MaxConversationHistory          = 5;
uint32_t    g_ConversationHistorySaveInterval = 10;

// --------------------------------------------
// Prompt Templates
// --------------------------------------------
std::string g_RandomChatterPromptTemplate;
std::vector<std::string> g_RandomChatterPromptVariations;
std::vector<std::string> g_RandomChatterQuestionVariations;
std::string g_EventChatterPromptTemplate;
std::string g_ChatPromptTemplate;
std::string g_ChatExtraInfoTemplate;

// --------------------------------------------
// Personality and Prompt Data
// --------------------------------------------
std::unordered_map<uint64_t, std::string> g_BotPersonalityList;
std::unordered_map<std::string, std::string> g_PersonalityPrompts;
std::vector<std::string> g_PersonalityKeys;
std::vector<std::string> g_PersonalityKeysRandomOnly;
std::string g_DefaultPersonalityPrompt;

// --------------------------------------------
// Chat History Templates and Toggles
// --------------------------------------------
bool        g_EnableChatHistory = true;
std::string g_ChatHistoryHeaderTemplate;
std::string g_ChatHistoryLineTemplate;
std::string g_ChatHistoryFooterTemplate;

// --------------------------------------------
// Chatbot Snapshot Template
// --------------------------------------------
bool        g_EnableChatBotSnapshotTemplate  = false;
std::string g_ChatBotSnapshotTemplate;

// --------------------------------------------
// Conversation History Store and Mutex
// --------------------------------------------
std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::deque<std::pair<std::string, std::string>>>> g_BotConversationHistory;
std::mutex g_ConversationHistoryMutex;
time_t g_LastHistorySaveTime = 0;

// --------------------------------------------
// Bot-Player Sentiment Tracking System
// --------------------------------------------
bool        g_EnableSentimentTracking = true;
float       g_SentimentDefaultValue = 0.5f;              // Default sentiment value (0.5 = neutral)
float       g_SentimentAdjustmentStrength = 0.1f;        // How much to adjust sentiment per message
uint32_t    g_SentimentSaveInterval = 10;                // How often to save sentiment to DB (minutes)
std::string g_SentimentAnalysisPrompt = "Analyze the sentiment of this message: \"{message}\". Respond only with: POSITIVE, NEGATIVE, or NEUTRAL.";
std::string g_SentimentPromptTemplate = "Your relationship sentiment with {player_name} is {sentiment_value} (0.0=hostile, 0.5=neutral, 1.0=friendly). Use this to guide your tone and response.";

// In-memory sentiment storage and mutex
std::unordered_map<uint64_t, std::unordered_map<uint64_t, float>> g_BotPlayerSentiments;
std::mutex g_SentimentMutex;
time_t g_LastSentimentSaveTime = 0;

// --------------------------------------------
// RAG (Retrieval-Augmented Generation) System
// --------------------------------------------
bool        g_EnableRAG = false;
std::string g_RAGDataPath = "rag/";
uint32_t    g_RAGMaxRetrievedItems = 3;
float       g_RAGSimilarityThreshold = 0.3f;
std::string g_RAGPromptTemplate;

class BotChatRAGSystem;
BotChatRAGSystem* g_RAGSystem = nullptr;

// --------------------------------------------
// Channel conversation threads
// --------------------------------------------
bool        g_EnableChannelThreads = true;
uint32_t    g_ChannelThreadMaxLines = 48;
uint32_t    g_TopicIdleSeconds = 180;
uint32_t    g_ContinueTopicChance = 40;
bool        g_PreferThreadRegulars = true;
std::string g_ChannelThreadHeaderTemplate;
std::string g_ChannelThreadLineTemplate;
std::string g_RandomChatterContinueTemplate;

// --------------------------------------------
// Live game knowledge lookup
// --------------------------------------------
bool        g_EnableGameKnowledge = true;
uint32_t    g_GameKnowledgeMaxFacts = 4;
uint32_t    g_QuestionReplyChanceBonus = 25;
bool        g_AlwaysReplyToPlayerQuestions = true;
std::string g_GameKnowledgePromptTemplate;
std::string g_GameKnowledgeNoneTemplate;

// --------------------------------------------
// Blacklist: Prefixes for Commands (not chat)
// --------------------------------------------
std::vector<std::string> g_BlacklistCommands = {
    ".playerbots",
    "playerbot",
    "questie",
    "Questie:",
};

// --------------------------------------------
// Environment/Contextual Random Chatter Templates
// --------------------------------------------
std::vector<std::string> g_EnvCommentCreature;
std::vector<std::string> g_EnvCommentGameObject;
std::vector<std::string> g_EnvCommentEquippedItem;
std::vector<std::string> g_EnvCommentBagItem;
std::vector<std::string> g_EnvCommentBagItemSell;
std::vector<std::string> g_EnvCommentSpell;
std::vector<std::string> g_EnvCommentQuestArea;
std::vector<std::string> g_EnvCommentVendor;
std::vector<std::string> g_EnvCommentQuestgiver;
std::vector<std::string> g_EnvCommentBagSlots;
std::vector<std::string> g_EnvCommentDungeon;
std::vector<std::string> g_EnvCommentUnfinishedQuest;

// --------------------------------------------
// Guild-Specific Random Chatter Templates
// --------------------------------------------
std::vector<std::string> g_GuildEnvCommentGuildMember;
std::vector<std::string> g_GuildEnvCommentGuildRank;
std::vector<std::string> g_GuildEnvCommentGuildBank;
std::vector<std::string> g_GuildEnvCommentGuildMOTD;
std::vector<std::string> g_GuildEnvCommentGuildInfo;
std::vector<std::string> g_GuildEnvCommentGuildOnlineMembers;
std::vector<std::string> g_GuildEnvCommentGuildRaid;
std::vector<std::string> g_GuildEnvCommentGuildEndgame;
std::vector<std::string> g_GuildEnvCommentGuildStrategy;
std::vector<std::string> g_GuildEnvCommentGuildGroup;
std::vector<std::string> g_GuildEnvCommentGuildPvP;
std::vector<std::string> g_GuildEnvCommentGuildCommunity;

// --------------------------------------------
// Guild-Specific Random Chatter Configuration
// --------------------------------------------
bool        g_EnableGuildEventChatter             = true;
bool        g_EnableGuildRandomAmbientChatter      = true;
uint32_t    g_GuildRandomChatterChance             = 10;
uint32_t    g_GuildChatterBotCommentChance          = 25;
uint32_t    g_GuildChatterMaxBotsPerEvent           = 2;

// --------------------------------------------
// Guild-Specific Event Chatter Templates
// --------------------------------------------
std::string g_GuildEventTypeLevelUp = "";
std::string g_GuildEventTypeDungeonComplete = "";
std::string g_GuildEventTypeEpicGear = "";
std::string g_GuildEventTypeRareGear = "";
std::string g_GuildEventTypeGuildJoin = "";
std::string g_GuildEventTypeGuildLeave = "";
std::string g_GuildEventTypeGuildPromotion = "";
std::string g_GuildEventTypeGuildDemotion = "";
std::string g_GuildEventTypeGuildLogin = "";
std::string g_GuildEventTypeGuildAchievement = "";

// --------------------------------------------
// Event Chatter Templates
// --------------------------------------------
std::string g_EventTypeDefeated;           // "defeated"
std::string g_EventTypeDefeatedPlayer;     // "defeated player"
std::string g_EventTypePetDefeated;        // "pet defeated"
std::string g_EventTypeGotItem;            // "got item"
std::string g_EventTypeDied;               // "died"
std::string g_EventTypeCompletedQuest;     // "completed quest"
std::string g_EventTypeLearnedSpell;       // "learned spell"
std::string g_EventTypeRequestedDuel;      // "requested to duel"
std::string g_EventTypeStartedDueling;     // "started dueling"
std::string g_EventTypeWonDuel;            // "won duel against"
std::string g_EventTypeLeveledUp;          // "leveled up"
std::string g_EventTypeAchievement;        // "earned achievement"
std::string g_EventTypeUsedObject;         // "used object"

// Chance variables for normal events
int g_EventTypeDefeated_Chance = 0;
int g_EventTypeDefeatedPlayer_Chance = 0;
int g_EventTypePetDefeated_Chance = 0;
int g_EventTypeGotItem_Chance = 0;
int g_EventTypeDied_Chance = 0;
int g_EventTypeCompletedQuest_Chance = 0;
int g_EventTypeLearnedSpell_Chance = 0;
int g_EventTypeRequestedDuel_Chance = 0;
int g_EventTypeStartedDueling_Chance = 0;
int g_EventTypeWonDuel_Chance = 0;
int g_EventTypeLeveledUp_Chance = 0;
int g_EventTypeAchievement_Chance = 0;
int g_EventTypeUsedObject_Chance = 0;

// Chance variables for guild events
int g_GuildEventTypeEpicGear_Chance = 0;
int g_GuildEventTypeRareGear_Chance = 0;
int g_GuildEventTypeGuildJoin_Chance = 0;
int g_GuildEventTypeGuildLogin_Chance = 0;
int g_GuildEventTypeGuildLeave_Chance = 0;
int g_GuildEventTypeGuildPromotion_Chance = 0;
int g_GuildEventTypeGuildDemotion_Chance = 0;
int g_GuildEventTypeGuildAchievement_Chance = 0;
int g_GuildEventTypeLevelUp_Chance = 0;
int g_GuildEventTypeDungeonComplete_Chance = 0;

// Event Cooldown
uint32_t g_EventCooldownTime = 10;

// --------------------------------------------
// Channel Disable Settings
// --------------------------------------------
bool g_DisableForCustomChannels = false;
bool g_DisableForSayYell = false;
bool g_DisableForGuild = false;
bool g_DisableForParty = false;

// --------------------------------------------
// Typing Simulation Settings
// --------------------------------------------
bool g_EnableTypingSimulation = false;
uint32_t g_TypingSimulationBaseDelay = 1000;     // 1000ms base delay
uint32_t g_TypingSimulationDelayPerChar = 250;   // 250ms per character (4 chars/sec)


static std::vector<std::string> SplitString(const std::string& str, char delim)
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

// Load Bot Personalities from Database
void LoadBotPersonalityList()
{    
    // Let's make sure our user has sourced the required sql file to add the new table
    QueryResult tableExists = CharacterDatabase.Query("SELECT * FROM information_schema.tables WHERE table_schema = 'acore_characters' AND table_name = 'mod_bot_chat_personality' LIMIT 1");
    if (!tableExists)
    {
        LOG_ERROR("server.loading", "[Bot Chat] Please source the required database table first");
        return;
    }

    QueryResult result = CharacterDatabase.Query("SELECT guid,personality FROM mod_bot_chat_personality");

    if (!result)
    {
        return;
    }
    if (result->GetRowCount() == 0)
    {
        return;
    }    

    if(g_DebugEnabled)
    {
        LOG_INFO("server.loading", "[Bot Chat] Fetching Bot Personality List into array");
    }

    do
    {
        uint64_t personalityBotGUID = result->Fetch()[0].Get<uint64_t>();
        std::string personalityKey = result->Fetch()[1].Get<std::string>();
        g_BotPersonalityList[personalityBotGUID] = personalityKey;
    } while (result->NextRow());
}

std::string GetMultiLineConfigValue(const std::string& configFilePath, const std::string& key)
{
    std::ifstream infile(configFilePath);
    if (!infile) return "";

    std::string line;
    std::string value;
    bool foundKey = false;
    while (std::getline(infile, line))
    {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        if (trimmed.empty() || trimmed[0] == '#')
            continue;
        size_t pos = trimmed.find('=');
        if (!foundKey && pos != std::string::npos) {
            std::string possibleKey = trimmed.substr(0, pos);
            possibleKey.erase(possibleKey.find_last_not_of(" \t\r\n") + 1);
            if (possibleKey == key) {
                foundKey = true;
                std::string afterEq = trimmed.substr(pos + 1);
                afterEq.erase(0, afterEq.find_first_not_of(" \t\r\n"));
                value += afterEq;
                continue;
            }
        }
        else if (foundKey) {
            // New config key or section
            if (trimmed.find('=') != std::string::npos && trimmed.find('[') == std::string::npos)
                break;
            if (!value.empty()) value += "\n";
            value += trimmed;
        }
    }

    return value;
}

// Missing keys use the coded default instead of logging
// "Config: Missing property" for every leftover ollama-chat knob.
template <typename T>
T BotChatOpt(char const* name, T const& def)
{
    return sConfigMgr->GetOption<T>(name, def, false);
}

void LoadBotChatConfig()
{
    g_SayDistance                     = BotChatOpt<float>("BotChat.SayDistance", 30.0f);
    g_YellDistance                    = BotChatOpt<float>("BotChat.YellDistance", 100.0f);
    
    // Load per-channel-type reply chances
    g_PlayerReplyChance_Say           = BotChatOpt<uint32_t>("BotChat.PlayerReplyChance.Say", 90);
    g_BotReplyChance_Say              = BotChatOpt<uint32_t>("BotChat.BotReplyChance.Say", 10);
    g_PlayerReplyChance_Channel       = BotChatOpt<uint32_t>("BotChat.PlayerReplyChance.Channel", 80);
    g_BotReplyChance_Channel          = BotChatOpt<uint32_t>("BotChat.BotReplyChance.Channel", 18);
    g_PlayerReplyChance_Party         = BotChatOpt<uint32_t>("BotChat.PlayerReplyChance.Party", 90);
    g_BotReplyChance_Party            = BotChatOpt<uint32_t>("BotChat.BotReplyChance.Party", 10);
    g_PlayerReplyChance_Guild         = BotChatOpt<uint32_t>("BotChat.PlayerReplyChance.Guild", 70);
    g_BotReplyChance_Guild            = BotChatOpt<uint32_t>("BotChat.BotReplyChance.Guild", 5);
    
    g_MaxBotsToPick                   = BotChatOpt<uint32_t>("BotChat.MaxBotsToPick", 1);
    g_EnableSocialConventions         = BotChatOpt<bool>("BotChat.EnableSocialConventions", true);
    g_SocialCueSeconds                = BotChatOpt<uint32_t>("BotChat.SocialCueSeconds", 45);
    g_OllamaUrl                       = BotChatOpt<std::string>("BotChat.Url", "http://127.0.0.1:11434/api/generate");
    g_OllamaModel                     = BotChatOpt<std::string>("BotChat.Model", "llama3.1:8b");
    g_OllamaNumPredict                = BotChatOpt<uint32_t>("BotChat.NumPredict", 64);
    g_OllamaTemperature               = BotChatOpt<float>("BotChat.Temperature", 0.95f);
    g_OllamaTimeout                   = BotChatOpt<uint32_t>("BotChat.LLMTimeout", 15);
    g_OllamaTopP                      = BotChatOpt<float>("BotChat.TopP", 0.95f);
    g_OllamaRepeatPenalty             = BotChatOpt<float>("BotChat.RepeatPenalty", 1.1f);
    g_OllamaNumCtx                    = BotChatOpt<uint32_t>("BotChat.NumCtx", 0);
    g_OllamaNumThreads                = BotChatOpt<uint32_t>("BotChat.NumThreads", 0);
    g_OllamaStop                      = BotChatOpt<std::string>("BotChat.Stop", "");
    g_OllamaSystemPrompt              = BotChatOpt<std::string>("BotChat.SystemPrompt", "");
    g_OllamaSeed                      = BotChatOpt<std::string>("BotChat.Seed", "");

    g_MaxConcurrentQueries            = BotChatOpt<uint32_t>("BotChat.MaxConcurrentQueries", 1);

    g_Enable                          = BotChatOpt<bool>("BotChat.Enable", true);
    g_EnableLLM                       = BotChatOpt<bool>("BotChat.EnableLLM", true);
    g_DisableRepliesInCombat          = BotChatOpt<bool>("BotChat.DisableRepliesInCombat", true);
    g_EnableRandomChatter             = BotChatOpt<bool>("BotChat.EnableRandomChatter", true);
    g_EnableEventChatter              = BotChatOpt<bool>("BotChat.EnableEventChatter", true);
    g_EnableWhisperReplies            = BotChatOpt<bool>("BotChat.EnableWhisperReplies", true);

    g_DebugEnabled                    = BotChatOpt<bool>("BotChat.DebugEnabled", false);
    g_DebugShowFullPrompt             = BotChatOpt<bool>("BotChat.DebugShowFullPrompt", false);

    g_MinRandomInterval               = BotChatOpt<uint32_t>("BotChat.MinRandomInterval", 45);
    g_MaxRandomInterval               = BotChatOpt<uint32_t>("BotChat.MaxRandomInterval", 180);
    g_RandomChatterRealPlayerDistance = BotChatOpt<float>("BotChat.RandomChatterRealPlayerDistance", 40.0f);
    g_RandomChatterBotCommentChance   = BotChatOpt<uint32_t>("BotChat.RandomChatterBotCommentChance", 18);
    g_RandomChatterMaxBotsPerPlayer   = BotChatOpt<uint32_t>("BotChat.RandomChatterMaxBotsPerPlayer", 2);

    g_EnableGuildRandomAmbientChatter = BotChatOpt<bool>("BotChat.EnableGuildRandomAmbientChatter", true);
    g_GuildRandomChatterChance        = BotChatOpt<uint32_t>("BotChat.GuildRandomChatterChance", 10);

    g_EventChatterRealPlayerDistance = BotChatOpt<float>("BotChat.EventChatterRealPlayerDistance", 40.0f);
    g_EventChatterBotCommentChance   = BotChatOpt<uint32_t>("BotChat.EventChatterBotCommentChance", 15);
    g_EventChatterBotSelfCommentChance = BotChatOpt<uint32_t>("BotChat.EventChatterBotSelfCommentChance", 5);
    g_EventChatterMaxBotsPerPlayer   = BotChatOpt<uint32_t>("BotChat.EventChatterMaxBotsPerPlayer", 2);

    g_EnableRPPersonalities           = BotChatOpt<bool>("BotChat.EnableRPPersonalities", false);

    g_RandomChatterPromptTemplate     = BotChatOpt<std::string>("BotChat.RandomChatterPromptTemplate", "");

    // Load random chatter prompt variations
    std::string variationsStr = BotChatOpt<std::string>("BotChat.RandomChatterPromptVariations", "");
    g_RandomChatterPromptVariations.clear();
    if (!variationsStr.empty())
    {
        std::stringstream ss(variationsStr);
        std::string variation;
        while (std::getline(ss, variation, '|'))
        {
            if (!variation.empty())
            {
                g_RandomChatterPromptVariations.push_back(variation);
            }
        }
    }

    // Load random chatter question variations
    std::string questionsStr = BotChatOpt<std::string>("BotChat.RandomChatterQuestionVariations", "");
    g_RandomChatterQuestionVariations.clear();
    if (!questionsStr.empty())
    {
        std::stringstream ss(questionsStr);
        std::string question;
        while (std::getline(ss, question, '|'))
        {
            if (!question.empty())
            {
                g_RandomChatterQuestionVariations.push_back(question);
            }
        }
    }

    g_EventChatterPromptTemplate     = BotChatOpt<std::string>("BotChat.EventChatterPromptTemplate", "");

    g_ChatPromptTemplate              = BotChatOpt<std::string>("BotChat.ChatPromptTemplate", "");
    
    g_ChatExtraInfoTemplate           = BotChatOpt<std::string>("BotChat.ChatExtraInfoTemplate", "");

    g_DefaultPersonalityPrompt        = BotChatOpt<std::string>("BotChat.DefaultPersonalityPrompt", "");

    g_MaxConversationHistory          = BotChatOpt<uint32_t>("BotChat.MaxConversationHistory", 5);
    g_ConversationHistorySaveInterval = BotChatOpt<uint32_t>("BotChat.ConversationHistorySaveInterval", 10);

    g_ChatHistoryHeaderTemplate       = BotChatOpt<std::string>("BotChat.ChatHistoryHeaderTemplate", "");
    g_ChatHistoryLineTemplate         = BotChatOpt<std::string>("BotChat.ChatHistoryLineTemplate", "");
    g_ChatHistoryFooterTemplate       = BotChatOpt<std::string>("BotChat.ChatHistoryFooterTemplate", "");

    g_EnableChatBotSnapshotTemplate   = BotChatOpt<bool>("BotChat.EnableChatBotSnapshotTemplate", false);
    g_ChatBotSnapshotTemplate         = BotChatOpt<std::string>("BotChat.ChatBotSnapshotTemplate", "");

    g_EnableChatHistory               = BotChatOpt<bool>("BotChat.EnableChatHistory", true);

    // Bot-Player Sentiment Tracking
    g_EnableSentimentTracking         = BotChatOpt<bool>("BotChat.EnableSentimentTracking", true);
    g_SentimentDefaultValue           = BotChatOpt<float>("BotChat.SentimentDefaultValue", 0.5f);
    g_SentimentAdjustmentStrength     = BotChatOpt<float>("BotChat.SentimentAdjustmentStrength", 0.1f);
    g_SentimentSaveInterval           = BotChatOpt<uint32_t>("BotChat.SentimentSaveInterval", 10);
    g_SentimentAnalysisPrompt         = BotChatOpt<std::string>("BotChat.SentimentAnalysisPrompt", "Analyze the sentiment of this message: \"{message}\". Respond only with: POSITIVE, NEGATIVE, or NEUTRAL.");
    g_SentimentPromptTemplate         = BotChatOpt<std::string>("BotChat.SentimentPromptTemplate", "Your relationship sentiment with {player_name} is {sentiment_value} (0.0=hostile, 0.5=neutral, 1.0=friendly). Use this to guide your tone and response.");

    // RAG (Retrieval-Augmented Generation) System
    g_EnableRAG                       = BotChatOpt<bool>("BotChat.EnableRAG", true);
    g_RAGDataPath                     = BotChatOpt<std::string>("BotChat.RAGDataPath", "rag/");
    g_RAGMaxRetrievedItems            = BotChatOpt<uint32_t>("BotChat.RAGMaxRetrievedItems", 3);
    g_RAGSimilarityThreshold          = BotChatOpt<float>("BotChat.RAGSimilarityThreshold", 0.25f);
    g_RAGPromptTemplate               = BotChatOpt<std::string>("BotChat.RAGPromptTemplate", "RELEVANT INFORMATION:\n{rag_info}\nUse this for general WoW knowledge. Do not invent specific coordinates or quest givers from it.");

    g_EnableChannelThreads            = BotChatOpt<bool>("BotChat.EnableChannelThreads", true);
    g_ChannelThreadMaxLines           = BotChatOpt<uint32_t>("BotChat.ChannelThreadMaxLines", 48);
    g_TopicIdleSeconds                = BotChatOpt<uint32_t>("BotChat.TopicIdleSeconds", 180);
    g_ContinueTopicChance             = BotChatOpt<uint32_t>("BotChat.ContinueTopicChance", 40);
    g_ScaleWithPopulation             = BotChatOpt<bool>("BotChat.ScaleWithPopulation", true);
    g_AdultEnable                     = BotChatOpt<bool>("BotChat.AdultEnable", true);
    g_AdultHour                       = BotChatOpt<uint32_t>("BotChat.AdultHour", 21);
    if (g_AdultHour > 23)
        g_AdultHour = 21;
    g_Toxicity                        = BotChatOpt<uint32_t>("BotChat.Toxicity", 0);
    if (g_Toxicity > 3)
        g_Toxicity = 3;
    g_BlowupChance                    = BotChatOpt<uint32_t>("BotChat.BlowupChance", 28);
    if (g_BlowupChance > 100)
        g_BlowupChance = 100;
    g_BlowupSeconds                   = BotChatOpt<uint32_t>("BotChat.BlowupSeconds", 600);
    if (g_BlowupSeconds < 120)
        g_BlowupSeconds = 120;
    if (g_BlowupSeconds > 1800)
        g_BlowupSeconds = 1800;
    g_PreferThreadRegulars            = BotChatOpt<bool>("BotChat.PreferThreadRegulars", true);
    g_ChannelThreadHeaderTemplate     = BotChatOpt<std::string>("BotChat.ChannelThreadHeaderTemplate", "RECENT CHANNEL CHAT (continue this conversation, do not greet or start a new topic):\n");
    g_ChannelThreadLineTemplate       = BotChatOpt<std::string>("BotChat.ChannelThreadLineTemplate", "[{speaker}]: {message}\n");
    g_RandomChatterContinueTemplate   = BotChatOpt<std::string>("BotChat.RandomChatterContinueTemplate", "Continue this channel conversation naturally. Add one short line on the same topic or a small tangent. Do not greet. Do not repeat what was just said.");

    g_EnableGameKnowledge             = BotChatOpt<bool>("BotChat.EnableGameKnowledge", true);
    g_GameKnowledgeMaxFacts           = BotChatOpt<uint32_t>("BotChat.GameKnowledgeMaxFacts", 4);
    g_QuestionReplyChanceBonus        = BotChatOpt<uint32_t>("BotChat.QuestionReplyChanceBonus", 25);
    g_AlwaysReplyToPlayerQuestions    = BotChatOpt<bool>("BotChat.AlwaysReplyToPlayerQuestions", true);
    g_GameKnowledgePromptTemplate     = BotChatOpt<std::string>("BotChat.GameKnowledgePromptTemplate", "KNOWN FACTS (use only these, do not invent numbers or locations):\n{facts}\nIf a fact is missing, say you don't remember the exact spot.");
    g_GameKnowledgeNoneTemplate       = BotChatOpt<std::string>("BotChat.GameKnowledgeNoneTemplate", "NO RELIABLE FACTS found for this question. Do not invent a location, quest giver, or coordinates. Say you don't remember the exact spot or ask for the exact name.");

    g_ThinkModeEnableForModule        = BotChatOpt<bool>("BotChat.ThinkModeEnableForModule", false);

    // Typing Simulation
    g_EnableTypingSimulation          = BotChatOpt<bool>("BotChat.EnableTypingSimulation", true);
    g_TypingSimulationBaseDelay       = BotChatOpt<uint32_t>("BotChat.TypingSimulationBaseDelay", 800);
    g_TypingSimulationDelayPerChar    = BotChatOpt<uint32_t>("BotChat.TypingSimulationDelayPerChar", 60);

    g_EventTypeDefeated           = BotChatOpt<std::string>("BotChat.EventTypeDefeated", "");
    g_EventTypeDefeatedPlayer     = BotChatOpt<std::string>("BotChat.EventTypeDefeatedPlayer", "");
    g_EventTypePetDefeated        = BotChatOpt<std::string>("BotChat.EventTypePetDefeated", "");
    g_EventTypeGotItem            = BotChatOpt<std::string>("BotChat.EventTypeGotItem", "");
    g_EventTypeDied               = BotChatOpt<std::string>("BotChat.EventTypeDied", "");
    g_EventTypeCompletedQuest     = BotChatOpt<std::string>("BotChat.EventTypeCompletedQuest", "");
    g_EventTypeLearnedSpell       = BotChatOpt<std::string>("BotChat.EventTypeLearnedSpell", "");
    g_EventTypeRequestedDuel      = BotChatOpt<std::string>("BotChat.EventTypeRequestedDuel", "");
    g_EventTypeStartedDueling     = BotChatOpt<std::string>("BotChat.EventTypeStartedDueling", "");
    g_EventTypeWonDuel            = BotChatOpt<std::string>("BotChat.EventTypeWonDuel", "");
    g_EventTypeLeveledUp          = BotChatOpt<std::string>("BotChat.EventTypeLeveledUp", "");
    g_EventTypeAchievement        = BotChatOpt<std::string>("BotChat.EventTypeAchievement", "");
    g_EventTypeUsedObject         = BotChatOpt<std::string>("BotChat.EventTypeUsedObject", "");


    // Load extra blacklist commands from config (comma-separated list)
    g_BlacklistCommands = { ".playerbots", "playerbot", "questie", "Questie:" };
    std::string extraBlacklist = BotChatOpt<std::string>("BotChat.BlacklistCommands", "");
    if (!extraBlacklist.empty())
    {
        std::vector<std::string> extraList = SplitString(extraBlacklist, ',');
        for (std::string const& cmd : extraList)
        {
            if (!cmd.empty())
                g_BlacklistCommands.push_back(cmd);
        }
    }

    // Personality templates and Ollama query manager are not used.

    // Loads the environment random chatter message templates for each type.
    // Each config option is a pipe-separated list of string templates,
    // using {} as a placeholder for named substitutions.
    // Helper to load a multi-line config option into a std::vector<std::string>
    auto LoadEnvCommentVector = [](const char* key, const std::vector<std::string>& defaults = {}) -> std::vector<std::string>
    {
        std::string val = BotChatOpt<std::string>(key, "");
        std::vector<std::string> result;
        std::istringstream iss(val);
        std::string token;
        while (std::getline(iss, token, '|')) { // Split by '|'
            // Trim whitespace from token
            size_t start = token.find_first_not_of(" \t\r\n");
            size_t end = token.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos)
                result.push_back(token.substr(start, end - start + 1));
        }
        if (result.empty() && !defaults.empty())
            return defaults;
        return result;
    };

    g_EnvCommentCreature        = LoadEnvCommentVector("BotChat.EnvCommentCreature", { "" });
    g_EnvCommentGameObject      = LoadEnvCommentVector("BotChat.EnvCommentGameObject", { "" });
    g_EnvCommentEquippedItem    = LoadEnvCommentVector("BotChat.EnvCommentEquippedItem", { "" });
    g_EnvCommentBagItem         = LoadEnvCommentVector("BotChat.EnvCommentBagItem", { "" });
    g_EnvCommentBagItemSell     = LoadEnvCommentVector("BotChat.EnvCommentBagItemSell", { "" });
    g_EnvCommentSpell           = LoadEnvCommentVector("BotChat.EnvCommentSpell", { "" });
    g_EnvCommentQuestArea       = LoadEnvCommentVector("BotChat.EnvCommentQuestArea", { "" });
    g_EnvCommentVendor          = LoadEnvCommentVector("BotChat.EnvCommentVendor", { "" });
    g_EnvCommentQuestgiver      = LoadEnvCommentVector("BotChat.EnvCommentQuestgiver", { "" });
    g_EnvCommentBagSlots        = LoadEnvCommentVector("BotChat.EnvCommentBagSlots", { "" });
    g_EnvCommentDungeon         = LoadEnvCommentVector("BotChat.EnvCommentDungeon", { "" });
    g_EnvCommentUnfinishedQuest = LoadEnvCommentVector("BotChat.EnvCommentUnfinishedQuest", { "" });

    // Guild-specific random chatter templates
    g_GuildEnvCommentGuildMember = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildMember", { "" });
    g_GuildEnvCommentGuildRank = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildRank", { "" });
    g_GuildEnvCommentGuildBank = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildBank", { "" });
    g_GuildEnvCommentGuildMOTD = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildMOTD", { "" });
    g_GuildEnvCommentGuildInfo = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildInfo", { "" });
    g_GuildEnvCommentGuildOnlineMembers = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildOnlineMembers", { "" });
    g_GuildEnvCommentGuildRaid = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildRaid", { "" });
    g_GuildEnvCommentGuildEndgame = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildEndgame", { "" });
    g_GuildEnvCommentGuildStrategy = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildStrategy", { "" });
    g_GuildEnvCommentGuildGroup = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildGroup", { "" });
    g_GuildEnvCommentGuildPvP = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildPvP", { "" });
    g_GuildEnvCommentGuildCommunity = LoadEnvCommentVector("BotChat.GuildEnvCommentGuildCommunity", { "" });

    // Guild-specific configuration
    g_EnableGuildEventChatter = BotChatOpt<bool>("BotChat.EnableGuildEventChatter", true);
    g_GuildChatterBotCommentChance = BotChatOpt<uint32_t>("BotChat.GuildChatterBotCommentChance", 25);
    g_GuildChatterMaxBotsPerEvent = BotChatOpt<uint32_t>("BotChat.GuildChatterMaxBotsPerEvent", 2);

    // Guild-specific event templates
    g_GuildEventTypeLevelUp = BotChatOpt<std::string>("BotChat.GuildEventTypeLevelUp", "");
    g_GuildEventTypeDungeonComplete = BotChatOpt<std::string>("BotChat.GuildEventTypeDungeonComplete", "");
    g_GuildEventTypeEpicGear = BotChatOpt<std::string>("BotChat.GuildEventTypeEpicGear", "");
    g_GuildEventTypeRareGear = BotChatOpt<std::string>("BotChat.GuildEventTypeRareGear", "");
    g_GuildEventTypeGuildJoin = BotChatOpt<std::string>("BotChat.GuildEventTypeGuildJoin", "");
    g_GuildEventTypeGuildLogin = BotChatOpt<std::string>("BotChat.GuildEventTypeGuildLogin", "");
    g_GuildEventTypeGuildLeave = BotChatOpt<std::string>("BotChat.GuildEventTypeGuildLeave", "");
    g_GuildEventTypeGuildPromotion = BotChatOpt<std::string>("BotChat.GuildEventTypeGuildPromotion", "");
    g_GuildEventTypeGuildDemotion = BotChatOpt<std::string>("BotChat.GuildEventTypeGuildDemotion", "");

    // Load chance variables for normal events
    g_EventTypeDefeated_Chance = BotChatOpt<int>("BotChat.EventTypeDefeated_Chance", 0);
    g_EventTypeDefeatedPlayer_Chance = BotChatOpt<int>("BotChat.EventTypeDefeatedPlayer_Chance", 0);
    g_EventTypePetDefeated_Chance = BotChatOpt<int>("BotChat.EventTypePetDefeated_Chance", 0);
    g_EventTypeGotItem_Chance = BotChatOpt<int>("BotChat.EventTypeGotItem_Chance", 0);
    g_EventTypeDied_Chance = BotChatOpt<int>("BotChat.EventTypeDied_Chance", 0);
    g_EventTypeCompletedQuest_Chance = BotChatOpt<int>("BotChat.EventTypeCompletedQuest_Chance", 0);
    g_EventTypeLearnedSpell_Chance = BotChatOpt<int>("BotChat.EventTypeLearnedSpell_Chance", 0);
    g_EventTypeRequestedDuel_Chance = BotChatOpt<int>("BotChat.EventTypeRequestedDuel_Chance", 0);
    g_EventTypeStartedDueling_Chance = BotChatOpt<int>("BotChat.EventTypeStartedDueling_Chance", 0);
    g_EventTypeWonDuel_Chance = BotChatOpt<int>("BotChat.EventTypeWonDuel_Chance", 0);
    g_EventTypeLeveledUp_Chance = BotChatOpt<int>("BotChat.EventTypeLeveledUp_Chance", 0);
    g_EventTypeAchievement_Chance = BotChatOpt<int>("BotChat.EventTypeAchievement_Chance", 0);
    g_EventTypeUsedObject_Chance = BotChatOpt<int>("BotChat.EventTypeUsedObject_Chance", 0);

    // Load chance variables for guild events
    g_GuildEventTypeEpicGear_Chance = BotChatOpt<int>("BotChat.GuildEventTypeEpicGear_Chance", 0);
    g_GuildEventTypeRareGear_Chance = BotChatOpt<int>("BotChat.GuildEventTypeRareGear_Chance", 0);
    g_GuildEventTypeGuildJoin_Chance = BotChatOpt<int>("BotChat.GuildEventTypeGuildJoin_Chance", 0);
    g_GuildEventTypeGuildLogin_Chance = BotChatOpt<int>("BotChat.GuildEventTypeGuildLogin_Chance", 0);
    g_GuildEventTypeGuildLeave_Chance = BotChatOpt<int>("BotChat.GuildEventTypeGuildLeave_Chance", 0);
    g_GuildEventTypeGuildPromotion_Chance = BotChatOpt<int>("BotChat.GuildEventTypeGuildPromotion_Chance", 0);
    g_GuildEventTypeGuildDemotion_Chance = BotChatOpt<int>("BotChat.GuildEventTypeGuildDemotion_Chance", 0);
    g_GuildEventTypeGuildAchievement_Chance = BotChatOpt<int>("BotChat.GuildEventTypeGuildAchievement_Chance", 0);
    g_GuildEventTypeLevelUp_Chance = BotChatOpt<int>("BotChat.GuildEventTypeLevelUp_Chance", 0);
    g_GuildEventTypeDungeonComplete_Chance = BotChatOpt<int>("BotChat.GuildEventTypeDungeonComplete_Chance", 0);


    // Cooldown time for events
    g_EventCooldownTime = BotChatOpt<uint32_t>("BotChat.EventCooldownTime", 10);

    // Channel disable settings
    g_DisableForCustomChannels = BotChatOpt<bool>("BotChat.DisableForCustomChannels", false);
    g_DisableForSayYell = BotChatOpt<bool>("BotChat.DisableForSayYell", false);
    g_DisableForGuild = BotChatOpt<bool>("BotChat.DisableForGuild", false);
    g_DisableForParty = BotChatOpt<bool>("BotChat.DisableForParty", false);

    LOG_INFO("server.loading",
             "[Bot Chat] Config loaded: Enabled = {}, LLM = {}, SayDistance = {}, YellDistance = {}, "
             "Reply Chances - Say: P{}%/B{}%, Channel: P{}%/B{}%, Party: P{}%/B{}%, Guild: P{}%/B{}%, MaxBotsToPick = {}, "
             "Url = {}, Model = {}, MaxConcurrentQueries = {}, EnableRandomChatter = {}, MinRandInt = {}, MaxRandInt = {}, RandomChatterRealPlayerDistance = {}, "
             "RandomChatterBotCommentChance = {}. Adult = {} after {}. Toxicity = {}. Blowup = {}%/{}s. MaxConcurrentQueries = {}. Extra blacklist commands: {}",
             g_Enable, g_EnableLLM, g_SayDistance, g_YellDistance,
             g_PlayerReplyChance_Say, g_BotReplyChance_Say,
             g_PlayerReplyChance_Channel, g_BotReplyChance_Channel,
             g_PlayerReplyChance_Party, g_BotReplyChance_Party,
             g_PlayerReplyChance_Guild, g_BotReplyChance_Guild,
             g_MaxBotsToPick,
             g_OllamaUrl, g_OllamaModel, g_MaxConcurrentQueries,
             g_EnableRandomChatter, g_MinRandomInterval, g_MaxRandomInterval, g_RandomChatterRealPlayerDistance,
             g_RandomChatterBotCommentChance, g_AdultEnable, g_AdultHour, g_Toxicity,
             g_BlowupChance, g_BlowupSeconds, g_MaxConcurrentQueries, extraBlacklist);
}

void LoadPersonalityTemplatesFromDB()
{
    g_PersonalityPrompts.clear();
    g_PersonalityKeys.clear();
    g_PersonalityKeysRandomOnly.clear();

    QueryResult result = CharacterDatabase.Query("SELECT `key`, `prompt`, `manual_only` FROM `mod_bot_chat_personality_templates`");
    if (!result)
    {
        LOG_ERROR("server.loading", "[Bot Chat] No personality templates found in the database!");
        return;
    }

    do
    {
        std::string key = (*result)[0].Get<std::string>();
        std::string prompt = (*result)[1].Get<std::string>();
        bool manualOnly = (*result)[2].Get<bool>();
        
        g_PersonalityPrompts[key] = prompt;
        g_PersonalityKeys.push_back(key);
        
        // Only add to random pool if not manual_only
        if (!manualOnly)
        {
            g_PersonalityKeysRandomOnly.push_back(key);
        }
    } while (result->NextRow());

    LOG_INFO("server.loading", "[Bot Chat] Cached {} personalities ({} available for random assignment).", 
             g_PersonalityKeys.size(), g_PersonalityKeysRandomOnly.size());
}

void LoadBotConversationHistoryFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, player_guid, player_message, bot_reply FROM mod_bot_chat_history ORDER BY timestamp ASC"
    );
    if (!result)
        return;

    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);
    g_BotConversationHistory.clear();

    do {
        uint64_t botGuid = (*result)[0].Get<uint64_t>();
        uint64_t playerGuid = (*result)[1].Get<uint64_t>();
        std::string playerMsg = (*result)[2].Get<std::string>();
        std::string botReply = (*result)[3].Get<std::string>();

        auto& playerHistory = g_BotConversationHistory[botGuid][playerGuid];
        playerHistory.push_back({ playerMsg, botReply });
        while (playerHistory.size() > g_MaxConversationHistory)
        {
            playerHistory.pop_front();
        }

    } while (result->NextRow());

}


// Definition of the configuration WorldScript.
BotChatConfigWorldScript::BotChatConfigWorldScript() : WorldScript("BotChatConfigWorldScript", {
    WORLDHOOK_ON_STARTUP,
    WORLDHOOK_ON_SHUTDOWN,
    WORLDHOOK_ON_AFTER_CONFIG_LOAD
}) { }

void BotChatConfigWorldScript::OnAfterConfigLoad(bool reload)
{
    if (reload)
        LoadBotChatConfig();
}

void BotChatConfigWorldScript::OnStartup()
{
    LoadBotChatConfig();
    if (g_EnableGameKnowledge)
        InitializeGameKnowledge();
    if (g_EnableLLM)
        LOG_INFO("server.loading", "[Bot Chat] Engine ready. LLM wording layer: {} at {}", g_OllamaModel, g_OllamaUrl);
    else
        LOG_INFO("server.loading", "[Bot Chat] Engine ready (canned only).");
}

void BotChatConfigWorldScript::OnShutdown()
{
}
