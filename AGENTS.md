# mod-bot-chat

Feel like a Wrath private-server room, not a bot script. Engine owns whether to talk. Local Ollama (7B/8B) only writes the sentence when a real player spoke.

- Engine: `bot_chat_social.cpp`, `bot_chat_knowledge.cpp`, `bot_chat_thread.cpp`, ambient in `bot_chat_random.cpp`.
- Wording layer: `bot_chat_llm.cpp` for player chat, bot-to-bot continues, and guild hangout starters. New General topics stay canned. LFG/help stay canned. Not Trade. Not Questie.
- Player chat: keep the same bot, pass the thread, match tone (including swearing/slurs). Closer then quiet if it loops. `what`/`huh`/`where` after banter is LLM, not a swallowed help follow-up.
- A player answering a bot on another channel (`/w`, `/s`, `/p`) is the same conversation. Whisper 1:1, stick to the partner, pass guild/party/say lines into the prompt. Do not greet as if you just met.
- Gold standard is `/s` 1:1 with one nearby bot (Sadirn). Copy that stickiness everywhere — including `/p` after grouping and `/g` when a real player talks. Guild is jovial (`whats everyone up to`, `im down for whatever`) plus `lfm`. Never `dc'd brb`. General is a real MMO zone channel: in-game asks, sometimes watercooler, ~16 words — not `heading out`.
- LFG/tank asks are canned (`im tank` / `what for` / `pst`). Never `i know a paladin in stormwind`. Not an RP realm.
- Do not reuse a line or the same template with a different zone/quest. Real players notice. Silence if nothing fresh is left.
- Never `repair then ah in stormwind city`. Already in that General. City = `ah is a scam`. Zone nicks only (`ungoro`, `stv`), never itineraries.
- Ambient: live slots start a topic. Guild hangout starters may be LLM-worded from a canned vibe (`TryBotChatLlmStart`). Continues are replies (LLM, canned fallback that matches the last line). Never overlay a new canned topic on a live thread. LFG stays canned.
- Do not `wb`/`hey` the real player unsolicited. If they say `hi` in the party, someone answers. Combat silence is ambient, not the party or guild they just spoke in. Same map + live zone for General. Quest lines must match this zone.
- `PickFrom` already calls `NoteSpokenLine`. Do not check `LineRecentlySpoken` again on send or every `hi`/`ty`/`gz` is dropped.
- Post only to `General - <live zone>`. First-General membership is stale after teleports. One continue per thread per tick; LLM busy = silence, not a second canned line.
- Volume follows circadian random-bot count. Player-initiated replies do not.
- Runtime conf: `env/dist/etc/modules/mod_bot_chat.conf`. Dist: `conf/mod_bot_chat.conf.dist`.
- Trade ads: mod-trade-chat. Pass-by `/hi`: mod-well-met.
- Do not compile `mod-ollama-chat` alongside this (`MODULE_MOD-OLLAMA-CHAT=disabled`).
- Do not put llama3.2:3b on player-facing chat.

Test after compile: `where fp`, `how do I get to stormwind?` then `what u mean` (knowledge, not model). Guild bot says `client froze again`, `/g what` — someone answers on that thread, not silence. Whisper that bot `where`, or `/s what` if they are nearby — they stay on that topic, not a fresh `hi`. Questie ignored. Trade `stfu` silent. Idle General has zone/quest lines that do not repeat. Talk to a bot twice — it stays on the thread. Flame/slur gets the same register back, not `shut up <name>`. Group with randoms, `/p hi` — someone answers. 3am chatter is sparse.
