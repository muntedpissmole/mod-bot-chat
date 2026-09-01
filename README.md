# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# mod-bot-chat

Bots chat like Wrath in 2009.

Log in and General has people looking for a tank, asking where the flight path is, talking about the game. Guild is wb, gz, and someone trying to get TOC together. Type at a bot and they answer you, including if you then whisper them.

General:

```
anyone doing quests in ungoro
lf1m tank SM cath
where is the un'goro fp
ah prices are insane
tbc was better
so true
need a heal for ZF
how do i get to stormwind
```

Guild:

```
wb
ty
whats everyone up to
im down for whatever
lfm toc 10 need 1 heal
i can heal
pst
gz
```

If a rare is actually up in the zone, General calls it (`tlpd up`). In a group you get party chat (`otw`, `ready`, `inc`, `gz`).

Talking to someone:

```
You: where fp
Bot: stormwind fp is down by the canals
You: /w what
Bot: the gryphon master, canals
```

```
[General] Bot: lfm tank
You: what for
Bot: pit of saron
You: im tank
Bot: pst
```

`gz` / `ty` / `wb` / `pst`, flight paths, and ding/death comments come from the game. Replies to you come from a local Ollama model (`llama3.1:8b`).

With [mod-circadian-bots](https://github.com/muntedpissmole/mod-circadian-bots) the room follows the same hours as the rest of the realm. Busy evening, dead at 3am.

Needs Ollama on the game box with `llama3.1:8b` (or another 7B/8B).

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
| `BotChat.AdultEnable` / `AdultHour` | After this local hour, General/guild mix in swearing. Ramps through the first hour. Sunday starts an hour later. Bots still do not slur first. |
| `BotChat.Toxicity` | 0-3. 1 = loot/parse/RMT salt anytime. 2 = identity pile-ons after AdultHour. 3 = KYS/doxx-bluff/slur-first plus LGBT/trans pile-on after AdultHour. 2-3 are canned; the model does not write them. |
| `BotChat.BlowupChance` / `BlowupSeconds` | At most one General free-for-all per evening after AdultHour (9pm, Sunday 10pm). Chance that evening is eligible; a live argument still has to spark. About 10 minutes of max register (swearing, tox 3, CAPS). |
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
