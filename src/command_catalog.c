#include "command_catalog.h"

#include "i18n.h"

#include <string.h>

typedef struct {
    tnt_command_spec_t spec;
    i18n_string_t full_usage;
    i18n_string_t summary;
    i18n_string_t manual_usage;
    i18n_string_t error_usage;
    int manual_group;
    bool no_args;
    bool requires_args;
} command_catalog_entry_t;

static const command_catalog_entry_t entries[] = {
    {
        {TNT_COMMAND_USERS, "users", {"users", "list", "who", NULL}},
        I18N_STRING(":users, :list, :who", ":users, :list, :who"),
        I18N_STRING("Show online users", "显示在线用户"),
        I18N_STRING(":users", ":users"),
        I18N_STRING("Usage: users\n", "用法: users\n"),
        1, true, false
    },
    {
        {TNT_COMMAND_MSG, "msg", {"msg", "w", NULL}},
        I18N_STRING(":msg <user> <message>, :w <user> <message>",
                    ":msg <user> <message>, :w <user> <message>"),
        I18N_STRING("Send private message", "发送私信"),
        I18N_STRING(":msg <user> <message>", ":msg <user> <message>"),
        I18N_STRING("Usage: msg <user> <message>\n"
                    "       w <user> <message>\n",
                    "用法: msg <user> <message>\n"
                    "      w <user> <message>\n"),
        2, false, true
    },
    {
        {TNT_COMMAND_REPLY, "reply", {"reply", "r", NULL}},
        I18N_STRING(":reply <message>, :r <message>",
                    ":reply <message>, :r <message>"),
        I18N_STRING("Reply to latest private message", "回复最近私信"),
        I18N_STRING(":reply <message>", ":reply <message>"),
        I18N_STRING("Usage: reply <message>\n"
                    "       r <message>\n",
                    "用法: reply <message>\n"
                    "      r <message>\n"),
        2, false, true
    },
    {
        {TNT_COMMAND_INBOX, "inbox", {"inbox", NULL}},
        I18N_STRING(":inbox", ":inbox"),
        I18N_STRING("Show private messages", "查看私信"),
        I18N_STRING(":inbox", ":inbox"),
        I18N_STRING("Usage: inbox\n", "用法: inbox\n"),
        2, true, false
    },
    {
        {TNT_COMMAND_NICK, "nick", {"nick", "name", NULL}},
        I18N_STRING(":nick <name>, :name <name>",
                    ":nick <name>, :name <name>"),
        I18N_STRING("Change nickname", "更改昵称"),
        I18N_STRING(":nick <name>", ":nick <name>"),
        I18N_STRING("Usage: nick <name>\n", "用法: nick <name>\n"),
        2, false, true
    },
    {
        {TNT_COMMAND_LAST, "last", {"last", NULL}},
        I18N_STRING(":last [N]", ":last [N]"),
        I18N_STRING("Show last N messages (max 50)",
                    "显示最后 N 条消息(最多50)"),
        I18N_STRING(":last [N]", ":last [N]"),
        I18N_STRING("Usage: last [N]  (N: 1-50, default 10)\n",
                    "用法: last [N]  (N: 1-50，默认 10)\n"),
        1, false, false
    },
    {
        {TNT_COMMAND_SEARCH, "search", {"search", NULL}},
        I18N_STRING(":search <keyword>", ":search <keyword>"),
        I18N_STRING("Search message history", "搜索消息历史"),
        I18N_STRING(":search <keyword>", ":search <keyword>"),
        I18N_STRING("Usage: search <keyword>\n", "用法: search <keyword>\n"),
        1, false, true
    },
    {
        {TNT_COMMAND_MUTE_JOINS, "mute-joins", {"mute-joins", "mute", NULL}},
        I18N_STRING(":mute-joins, :mute", ":mute-joins, :mute"),
        I18N_STRING("Toggle join/leave notices", "切换加入/离开提示"),
        I18N_STRING(":mute-joins", ":mute-joins"),
        I18N_STRING("Usage: mute-joins\n", "用法: mute-joins\n"),
        3, true, false
    },
    {
        {TNT_COMMAND_HELP, "help", {"help", NULL}},
        I18N_STRING(":help", ":help"),
        I18N_STRING("Show concise manual", "显示简明手册"),
        I18N_STRING(NULL, NULL),
        I18N_STRING("Usage: help\n", "用法: help\n"),
        0, true, false
    },
    {
        {TNT_COMMAND_LANG, "lang", {"lang", "language", NULL}},
        I18N_STRING(":lang <en|zh>", ":lang <en|zh>"),
        I18N_STRING("Switch UI language", "切换界面语言"),
        I18N_STRING(NULL, NULL),
        I18N_STRING("Usage: lang <en|zh>\n", "用法: lang <en|zh>\n"),
        0, false, false
    },
    {
        {TNT_COMMAND_CLEAR, "clear", {"clear", "cls", NULL}},
        I18N_STRING(":clear, :cls", ":clear, :cls"),
        I18N_STRING("Clear command output", "清空命令输出"),
        I18N_STRING(":clear", ":clear"),
        I18N_STRING("Usage: clear\n", "用法: clear\n"),
        3, true, false
    },
    {
        {TNT_COMMAND_QUIT, "q", {"q", "quit", "exit", NULL}},
        I18N_STRING(":q, :quit, :exit", ":q, :quit, :exit"),
        I18N_STRING("Disconnect", "断开连接"),
        I18N_STRING(":q", ":q"),
        I18N_STRING("Usage: q\n", "用法: q\n"),
        3, true, false
    }
};

static const command_catalog_entry_t *entry_for_id(tnt_command_id_t id) {
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (entries[i].spec.id == id) {
            return &entries[i];
        }
    }
    return NULL;
}

static const char *skip_spaces(const char *value) {
    while (value && *value == ' ') {
        value++;
    }
    return value;
}

static bool name_matches(const char *line, const char *name,
                         const char **args) {
    size_t len;

    if (!line || !name) {
        return false;
    }

    len = strlen(name);
    if (strncmp(line, name, len) != 0) {
        return false;
    }
    if (line[len] != '\0' && line[len] != ' ') {
        return false;
    }

    if (args) {
        *args = skip_spaces(line + len);
    }
    return true;
}

static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int edit_distance(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    int prev[32];
    int curr[32];

    if (la >= 32 || lb >= 32) {
        return 99;
    }

    for (size_t j = 0; j <= lb; j++) {
        prev[j] = (int)j;
    }

    for (size_t i = 1; i <= la; i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = min3(prev[j] + 1, curr[j - 1] + 1,
                           prev[j - 1] + cost);
        }
        for (size_t j = 0; j <= lb; j++) {
            prev[j] = curr[j];
        }
    }

    return prev[lb];
}

const tnt_command_spec_t *command_catalog_get(tnt_command_id_t id) {
    const command_catalog_entry_t *entry = entry_for_id(id);
    return entry ? &entry->spec : NULL;
}

bool command_catalog_match(const char *line, tnt_command_id_t *id,
                           const char **args) {
    line = skip_spaces(line);
    if (!line || line[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const tnt_command_spec_t *spec = &entries[i].spec;
        for (size_t n = 0; n < sizeof(spec->names) / sizeof(spec->names[0]); n++) {
            const char *candidate_args = NULL;
            if (!spec->names[n]) {
                break;
            }
            if (!name_matches(line, spec->names[n], &candidate_args)) {
                continue;
            }
            if (id) {
                *id = spec->id;
            }
            if (args) {
                *args = candidate_args ? candidate_args : "";
            }
            return true;
        }
    }

    return false;
}

bool command_catalog_args_valid(tnt_command_id_t id, const char *args) {
    const command_catalog_entry_t *entry = entry_for_id(id);
    args = skip_spaces(args);

    if (!entry) {
        return false;
    }
    if (entry->no_args) {
        return !args || args[0] == '\0';
    }
    if (entry->requires_args) {
        return args && args[0] != '\0';
    }
    return true;
}

const char *command_catalog_suggest(const char *name) {
    const char *best = NULL;
    int best_distance = 99;

    if (!name || !*name) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const tnt_command_spec_t *spec = &entries[i].spec;
        for (size_t n = 0; n < sizeof(spec->names) / sizeof(spec->names[0]); n++) {
            int distance;
            if (!spec->names[n]) {
                break;
            }
            distance = edit_distance(name, spec->names[n]);
            if (distance < best_distance) {
                best_distance = distance;
                best = spec->canonical;
            }
        }
    }

    return best_distance <= 2 ? best : NULL;
}

void command_catalog_append_full(char *buffer, size_t buf_size, size_t *pos,
                                 ui_lang_t lang) {
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const char *usage = i18n_string(entries[i].full_usage, lang);
        const char *summary = i18n_string(entries[i].summary, lang);
        buffer_appendf(buffer, buf_size, pos, "  %-40s - %s\n",
                       usage, summary);
    }
}

void command_catalog_append_manual(char *buffer, size_t buf_size, size_t *pos,
                                   ui_lang_t lang) {
    for (int group = 1; group <= 3; group++) {
        bool first = true;

        buffer_appendf(buffer, buf_size, pos, "  ");
        for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
            const char *usage;
            if (entries[i].manual_group != group) {
                continue;
            }
            usage = i18n_string(entries[i].manual_usage, lang);
            if (!usage || usage[0] == '\0') {
                continue;
            }
            if (!first) {
                buffer_appendf(buffer, buf_size, pos, ", ");
            }
            buffer_appendf(buffer, buf_size, pos, "%s", usage);
            first = false;
        }
        buffer_appendf(buffer, buf_size, pos, "\n");
    }
}

void command_catalog_append_usage(char *buffer, size_t buf_size, size_t *pos,
                                  tnt_command_id_t id, ui_lang_t lang) {
    const command_catalog_entry_t *entry = entry_for_id(id);
    const char *usage;

    if (!entry) {
        return;
    }

    usage = i18n_string(entry->error_usage, lang);
    buffer_appendf(buffer, buf_size, pos, "%s", usage);
}
