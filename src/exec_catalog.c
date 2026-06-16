#include "exec_catalog.h"

#include "i18n.h"

typedef struct {
    tnt_exec_command_id_t id;
    const char *name;
    const char *alias;
    const char *usage;
    const char *usage_syntax;
    i18n_string_t summary;
    bool no_args;
    bool optional_json;
    bool requires_args;
} exec_catalog_entry_t;

static const exec_catalog_entry_t entries[] = {
    {TNT_EXEC_COMMAND_HELP, "help", "--help",
     "help", "help", I18N_STRING("Show this help", "显示此帮助"),
     true, false, false},
    {TNT_EXEC_COMMAND_HEALTH, "health", NULL,
     "health", "health",
     I18N_STRING("Print service health", "输出服务健康状态"),
     true, false, false},
    {TNT_EXEC_COMMAND_USERS, "users", NULL,
     "users [--json]", "users [--json]",
     I18N_STRING("List online users", "列出在线用户"),
     false, true, false},
    {TNT_EXEC_COMMAND_STATS, "stats", NULL,
     "stats [--json]", "stats [--json]",
     I18N_STRING("Print room statistics", "输出房间统计"),
     false, true, false},
    {TNT_EXEC_COMMAND_TAIL, "tail", NULL,
     "tail [N]", "tail [N] | tail -n N",
     I18N_STRING("Print recent messages", "输出最近消息"),
     false, false, false},
    {TNT_EXEC_COMMAND_TAIL, "tail", NULL,
     "tail -n N", "tail [N] | tail -n N",
     I18N_STRING("Print recent messages", "输出最近消息"),
     false, false, false},
    {TNT_EXEC_COMMAND_DUMP, "dump", NULL,
     "dump [N]", "dump [N] | dump -n N | dump --all",
     I18N_STRING("Export persisted messages", "导出持久化消息"),
     false, false, false},
    {TNT_EXEC_COMMAND_DUMP, "dump", NULL,
     "dump -n N", "dump [N] | dump -n N | dump --all",
     I18N_STRING("Export persisted messages", "导出持久化消息"),
     false, false, false},
    {TNT_EXEC_COMMAND_DUMP, "dump", NULL,
     "dump --all", "dump [N] | dump -n N | dump --all",
     I18N_STRING("Export persisted messages", "导出持久化消息"),
     false, false, false},
    {TNT_EXEC_COMMAND_POST, "post", NULL,
     "post MESSAGE", "post MESSAGE",
     I18N_STRING("Post a message non-interactively", "非交互发送消息"),
     false, false, true},
    {TNT_EXEC_COMMAND_POST, "post", NULL,
     "post \"/me act\"", "post MESSAGE",
     I18N_STRING("Post an action message", "发送动作消息"),
     false, false, true},
    {TNT_EXEC_COMMAND_EXIT, "exit", NULL,
     "exit", "exit", I18N_STRING("Exit successfully", "成功退出"),
     true, false, false}
};

static const exec_catalog_entry_t *entry_for_id(tnt_exec_command_id_t id) {
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (entries[i].id == id) {
            return &entries[i];
        }
    }
    return NULL;
}

static const char *skip_spaces(const char *value) {
    while (value && *value && (*value == ' ' || *value == '\t')) {
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
    if (line[len] != '\0' && line[len] != ' ' && line[len] != '\t') {
        return false;
    }

    if (args) {
        const char *candidate_args = skip_spaces(line + len);
        *args = candidate_args && candidate_args[0] != '\0'
                    ? candidate_args
                    : NULL;
    }
    return true;
}

bool exec_catalog_match(const char *line, tnt_exec_command_id_t *id,
                        const char **args) {
    line = skip_spaces(line);
    if (!line || line[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (!name_matches(line, entries[i].name, args) &&
            !name_matches(line, entries[i].alias, args)) {
            continue;
        }

        if (id) {
            *id = entries[i].id;
        }
        return true;
    }

    return false;
}

bool exec_catalog_args_valid(tnt_exec_command_id_t id, const char *args) {
    const exec_catalog_entry_t *entry = entry_for_id(id);

    if (!entry) {
        return false;
    }
    if (entry->no_args) {
        return !args || args[0] == '\0';
    }
    if (entry->optional_json) {
        return !args || strcmp(args, "--json") == 0;
    }
    if (entry->requires_args) {
        return args && args[0] != '\0';
    }
    return true;
}

void exec_catalog_append_help(char *buffer, size_t buf_size, size_t *pos,
                              ui_lang_t lang) {
    static const i18n_string_t header =
        I18N_STRING("TNT exec interface\nCommands:\n",
                    "TNT exec 接口\n命令:\n");

    buffer_appendf(buffer, buf_size, pos, "%s", i18n_string(header, lang));

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const char *summary = i18n_string(entries[i].summary, lang);
        buffer_appendf(buffer, buf_size, pos, "  %-15s %s\n",
                       entries[i].usage, summary);
    }
}

void exec_catalog_append_command_list(char *buffer, size_t buf_size,
                                      size_t *pos) {
    bool seen[TNT_EXEC_COMMAND_COUNT] = {0};
    size_t count = 0;

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        tnt_exec_command_id_t id = entries[i].id;

        if (id < 0 || id >= TNT_EXEC_COMMAND_COUNT || seen[id]) {
            continue;
        }
        if (count > 0) {
            buffer_appendf(buffer, buf_size, pos, ", ");
        }
        buffer_appendf(buffer, buf_size, pos, "%s", entries[i].name);
        seen[id] = true;
        count++;
    }
}

void exec_catalog_append_usage(char *buffer, size_t buf_size, size_t *pos,
                               tnt_exec_command_id_t id, ui_lang_t lang) {
    const exec_catalog_entry_t *entry = entry_for_id(id);
    static const i18n_string_t usage_format =
        I18N_STRING("%s: usage: %s\n", "%s: 用法: %s\n");

    if (!entry) {
        return;
    }
    buffer_appendf(buffer, buf_size, pos, i18n_string(usage_format, lang),
                   entry->name, entry->usage_syntax);
}
