# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# mod-bot-chat

A module for the [Playerbots AzerothCore fork](https://github.com/mod-playerbots/azerothcore-wotlk/tree/Playerbot) and [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Playerbots talk in Say, General, Party, Guild, and Whisper. When a real player speaks, a local Ollama model (`llama3.1:8b` by default) writes a short reply from the recent thread. The same bot stays on that conversation if you switch channels. Idle General and guild chatter is filled from the bot's zone, quests, class, and dungeon, and the volume follows [mod-circadian-bots](https://github.com/muntedpissmole/mod-circadian-bots) population.

`gz` / `ty` / `wb` / `pst`, ding and death comments, and answers like `where fp` or `how do I get to stormwind` come from live game data. Trade ads are [mod-trade-chat](https://github.com/muntedpissmole/mod-trade-chat). Pass-by `/hi` is [mod-well-met](https://github.com/muntedpissmole/mod-well-met).

Needs Ollama running on the game box with `llama3.1:8b` (or another 7B/8B).

## Installation

```
1. Place the module under the `modules` directory of your Playerbots AzerothCore source.
2. Recompile ./acore.sh compiler all
3. Enable mod in the conf and restart worldserver
```

## Module Configuration

```
BotChat.Enable = 1
BotChat.EnableLLM = 1
BotChat.Model = llama3.1:8b
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
| `BotChat.EnableLLM` | Local Ollama writes replies when a real player talks. Off = canned lines only. |
| `BotChat.Model` | Ollama model. 7B/8B recommended. |
| `BotChat.EnableRandomChatter` | Ambient lines in General / Say / Guild when a real player can hear them. |
| `BotChat.RandomChatterBotCommentChance` | Percent chance a due bot posts ambient in General/Say. |
| `BotChat.MinRandomInterval` / `MaxRandomInterval` | Seconds between a given bot's ambient attempts. |
| `BotChat.EnableGuildRandomAmbientChatter` | Guild `/g` ambient when a real player is in that guild. |
| `BotChat.GuildRandomChatterChance` | Percent chance for guild ambient. |
| `BotChat.ContinueTopicChance` | Percent chance ambient continues the current channel thread. |
| `BotChat.TopicIdleSeconds` | Seconds before a thread is stale and a new topic may start. |
| `BotChat.ScaleWithPopulation` | Ambient chance/interval follow random-bot online count. Player replies stay the same. |
| `BotChat.EnableSocialConventions` | `gz`/`ty`/`wb`/`pst` and activity replies. |
| `BotChat.EnableGameKnowledge` | Answers `where fp` / `how do I get to X` from live taxis, NPCs, and quests. |
| `BotChat.EnableEventChatter` | `gz`/`rip`/`gg` on dings, deaths, and duels. |
| `BotChat.DisableRepliesInCombat` | Combat silence for ambient. Player `/g` `/p` `/w` still get an answer. |
| `BotChat.EnableWhisperReplies` | Answer real-player whispers, keeping guild/party/say context. |
| `BotChat.DisableForCustomChannels` | Turns off General (and other custom channels). |
| `BotChat.DisableForSayYell` | Turns off Say/Yell. |
| `BotChat.DisableForGuild` | Turns off Guild/Officer. |
| `BotChat.DisableForParty` | Turns off Party/Raid. |
| `BotChat.BlacklistCommands` | Prefixes and addon announces to skip (playerbot commands, Questie, …). |

## License

This code and content is released under the [GNU AGPL v3](LICENSE).

## Credits

- AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/)
- AzerothCore (Playerbots fork): [repository](https://github.com/mod-playerbots/azerothcore-wotlk)
- Playerbots: [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
