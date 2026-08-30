# mod-bot-chat

No LLM. Types + live slots. Do not add Ollama back.

- Engine: `bot_chat_social.cpp`, `bot_chat_knowledge.cpp`, `bot_chat_thread.cpp`, ambient in `bot_chat_random.cpp`.
- Runtime conf: `env/dist/etc/modules/mod_bot_chat.conf`. Dist: `conf/mod_bot_chat.conf.dist`.
- Trade ads: mod-trade-chat. Pass-by `/hi`: mod-well-met.
- Do not compile `mod-ollama-chat` alongside this (`DISABLED_AC_MODULES=mod-ollama-chat`).

Test after compile: see former ollama-chat tests — `where fp`, `how do I get to stormwind?` then `what u mean`, Questie ignored, Trade `stfu` silent, idle General has zone/quest lines.
