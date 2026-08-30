# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# mod-bot-chat

A module for the [Playerbots AzerothCore fork](https://github.com/mod-playerbots/azerothcore-wotlk/tree/Playerbot) and [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Makes playerbots talk in Say, General, Party, and Guild like people on a Wrath private server. `gz` / `ty` / `wb` / `pst`, quest and flight-path answers from live game data, and ambient lines filled from the bot's zone, quest log, class, and dungeon. There is no Ollama and no LLM.

Trade ads (`WTS` / `LFM`) are [mod-trade-chat](https://github.com/muntedpissmole/mod-trade-chat). Pass-by `/hi` is [mod-well-met](https://github.com/muntedpissmole/mod-well-met). Do not run this next to [mod-ollama-chat](https://github.com/DustinHendrickson/mod-ollama-chat); they both hook chat.

Combat silence is on by default. Questie party announces and playerbot commands are ignored. Trade (`/2`) is treated as a board, not a conversation.

## Installation

```
1. Place the module under the `modules` directory of your Playerbots AzerothCore source.
2. Recompile ./acore.sh compiler all
3. Enable mod in the conf and restart worldserver
```

If you still have `mod-ollama-chat` in `modules/`, disable it (`OllamaChat.Enable = 0`, or `-DDISABLED_AC_MODULES=mod-ollama-chat`).

## Module Configuration

```
BotChat.Enable = 1
BotChat.EnableRandomChatter = 1
BotChat.RandomChatterBotCommentChance = 18
BotChat.EnableGuildRandomAmbientChatter = 1
BotChat.GuildRandomChatterChance = 12
BotChat.EnableEventChatter = 1
BotChat.EnableGameKnowledge = 1
BotChat.DisableRepliesInCombat = 1
```

| Option | Function |
| --- | --- |
| `BotChat.Enable` | Turns the module on or off. |
| `BotChat.EnableRandomChatter` | Ambient lines in General / Say / Guild when a real player can hear them. |
| `BotChat.RandomChatterBotCommentChance` | Percent chance a due bot posts ambient in General/Say. |
| `BotChat.MinRandomInterval` / `MaxRandomInterval` | Seconds between a given bot's ambient attempts. |
| `BotChat.EnableGuildRandomAmbientChatter` | Guild `/g` ambient when a real player is in that guild. |
| `BotChat.GuildRandomChatterChance` | Percent chance for guild ambient (not stacked on the General chance). |
| `BotChat.ContinueTopicChance` | Percent chance ambient continues the current channel thread instead of a new topic. |
| `BotChat.TopicIdleSeconds` | Seconds before a thread is stale and a new topic may start. |
| `BotChat.EnableSocialConventions` | Canned `gz`/`ty`/`wb`/`pst` and activity replies instead of free text. |
| `BotChat.EnableGameKnowledge` | Answer `where fp` / `how do I get to X` from live taxis, NPCs, and quests. |
| `BotChat.EnableEventChatter` | Canned `gz`/`rip`/`gg` on dings, deaths, duels. Not dumped into General. |
| `BotChat.DisableRepliesInCombat` | Do not reply while the bot is in combat. |
| `BotChat.DisableForCustomChannels` | Silence General (and other custom channels). |
| `BotChat.DisableForSayYell` | Silence Say/Yell. |
| `BotChat.DisableForGuild` | Silence Guild/Officer. |
| `BotChat.DisableForParty` | Silence Party/Raid. |
| `BotChat.BlacklistCommands` | Prefixes and addon announces to ignore (playerbot commands, Questie, …). |

## License

This code and content is released under the [GNU AGPL v3](LICENSE).

## Credits

- AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/)
- AzerothCore (Playerbots fork): [repository](https://github.com/mod-playerbots/azerothcore-wotlk)
- Playerbots: [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
