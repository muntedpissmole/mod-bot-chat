# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# mod-bot-chat

This module emulates World of Warcraft chat as it was in Wrath of the Lich King, back in the glory days — overlapping General, a guild that lumbers along, and a real conversation when you talk to someone.

Bots talk in Say, General, Party, Guild, and Whisper. General is overlapping short lines: quests, arguments, jokes, trash talk. Two topics can run at once:

```
anyone in redridge
uninstall wow pls
tbc was better fight me
need a tank for SM cath
ulduar is one raid
so true
leeroy jenkins
found the pally
who asked
died to pterrordax lmao
ah is a scam
lfm means look for ninja
close the game
```

An argument can stick around for a few minutes:

```
wrath is faceroll
you like it because its easy
vanilla was harder
you didnt even play tbc
naxx 40 was a zoo
still better than this
reported
```

Guild lumbers as one conversation. At peak a line every 10–30 seconds:

```
wb
ty
whats everyone up to
im down for whatever
lfm toc 10 need 1 heal
i can heal
pst
gz
client froze again
oof
```

Talk to a bot and it answers the actual line, then stays on that conversation if you switch channels:

```
You: where fp
Bot: stormwind fp is down by the canals
You: /w what
Bot: the gryphon master, canals
```

```
You: /g client froze again
Bot: oof
You: /w what
Bot: the client, it froze
```

```
[General] Bot: lfm tank
You: what for
Bot: pit of saron
You: im tank
Bot: pst
```

`gz` / `ty` / `wb` / `pst`, ding and death comments, and directions come from live game data. When you roast or banter, a local Ollama model (`llama3.1:8b`) writes the reply.

If [mod-circadian-bots](https://github.com/muntedpissmole/mod-circadian-bots) is installed, chatter volume follows its hour tables — weekday, Saturday, and Sunday. 7pm is busy. Saturday stays late. Sunday drops like a school night. 3am is a ghost town.

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
| `BotChat.ScaleWithPopulation` | Ambient chance and interval follow the online bot count (and the circadian hour curve when that module is on). Player replies stay the same. |
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
