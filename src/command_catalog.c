#include "command_catalog.h"

#include <string.h>

typedef struct {
    tnt_command_spec_t spec;
    const char *full_usage_en;
    const char *full_usage_zh;
    const char *summary_en;
    const char *summary_zh;
    const char *manual_usage_en;
    const char *manual_usage_zh;
    int manual_group;
} command_catalog_entry_t;

static const command_catalog_entry_t entries[] = {
    {
        {TNT_COMMAND_USERS, "users", {"users", "list", "who", NULL}, false},
        ":users, :list, :who", ":users, :list, :who",
        "Show online users", "显示在线用户",
        ":users", ":users", 1
    },
    {
        {TNT_COMMAND_MSG, "msg", {"msg", "w", NULL}, true},
        ":msg <user> <text>, :w <user> <text>",
        ":msg <用户> <文本>, :w <用户> <文本>",
        "Whisper to user", "私聊",
        ":msg <user> <text>", ":msg <用户> <文本>", 2
    },
    {
        {TNT_COMMAND_INBOX, "inbox", {"inbox", NULL}, false},
        ":inbox", ":inbox",
        "Show whispers", "查看私聊",
        ":inbox", ":inbox", 2
    },
    {
        {TNT_COMMAND_NICK, "nick", {"nick", "name", NULL}, true},
        ":nick <name>, :name <name>", ":nick <名字>, :name <名字>",
        "Change nickname", "更改昵称",
        ":nick <name>", ":nick <名字>", 2
    },
    {
        {TNT_COMMAND_LAST, "last", {"last", NULL}, true},
        ":last [N]", ":last [N]",
        "Show last N messages (max 50)", "显示最后 N 条消息(最多50)",
        ":last [N]", ":last [N]", 1
    },
    {
        {TNT_COMMAND_SEARCH, "search", {"search", NULL}, true},
        ":search <keyword>", ":search <关键词>",
        "Search message history", "搜索消息历史",
        ":search <keyword>", ":search <词>", 1
    },
    {
        {TNT_COMMAND_MUTE_JOINS, "mute-joins", {"mute-joins", "mute", NULL}, false},
        ":mute-joins, :mute", ":mute-joins, :mute",
        "Toggle join/leave notices", "切换加入/离开提示",
        ":mute-joins", ":mute-joins", 3
    },
    {
        {TNT_COMMAND_HELP, "help", {"help", NULL}, false},
        ":help", ":help",
        "Show concise manual", "显示简明手册",
        NULL, NULL, 0
    },
    {
        {TNT_COMMAND_LANG, "lang", {"lang", "language", NULL}, true},
        ":lang <en|zh>", ":lang <en|zh>",
        "Switch UI language", "切换界面语言",
        NULL, NULL, 0
    },
    {
        {TNT_COMMAND_CLEAR, "clear", {"clear", "cls", NULL}, false},
        ":clear, :cls", ":clear, :cls",
        "Clear command output", "清空命令输出",
        ":clear", ":clear", 3
    },
    {
        {TNT_COMMAND_QUIT, "q", {"q", "quit", "exit", NULL}, false},
        ":q, :quit, :exit", ":q, :quit, :exit",
        "Disconnect", "断开连接",
        ":q", ":q", 3
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
            if (candidate_args && candidate_args[0] != '\0' &&
                !spec->accepts_args) {
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
                                 help_lang_t lang) {
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const char *usage = lang == LANG_ZH ? entries[i].full_usage_zh
                                            : entries[i].full_usage_en;
        const char *summary = lang == LANG_ZH ? entries[i].summary_zh
                                              : entries[i].summary_en;
        buffer_appendf(buffer, buf_size, pos, "  %-40s - %s\n",
                       usage, summary);
    }
}

void command_catalog_append_manual(char *buffer, size_t buf_size, size_t *pos,
                                   help_lang_t lang) {
    for (int group = 1; group <= 3; group++) {
        bool first = true;

        buffer_appendf(buffer, buf_size, pos, "  ");
        for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
            const char *usage;
            if (entries[i].manual_group != group) {
                continue;
            }
            usage = lang == LANG_ZH ? entries[i].manual_usage_zh
                                    : entries[i].manual_usage_en;
            if (!usage) {
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
