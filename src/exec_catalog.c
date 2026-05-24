#include "exec_catalog.h"

typedef struct {
    tnt_exec_command_id_t id;
    const char *name;
    const char *alias;
    const char *usage;
    const char *summary_en;
    const char *summary_zh;
} exec_catalog_entry_t;

static const exec_catalog_entry_t entries[] = {
    {TNT_EXEC_COMMAND_HELP, "help", "--help",
     "help", "Show this help", "显示此帮助"},
    {TNT_EXEC_COMMAND_HEALTH, "health", NULL,
     "health", "Print service health", "输出服务健康状态"},
    {TNT_EXEC_COMMAND_USERS, "users", NULL,
     "users [--json]", "List online users", "列出在线用户"},
    {TNT_EXEC_COMMAND_STATS, "stats", NULL,
     "stats [--json]", "Print room statistics", "输出房间统计"},
    {TNT_EXEC_COMMAND_TAIL, "tail", NULL,
     "tail [N]", "Print recent messages", "输出最近消息"},
    {TNT_EXEC_COMMAND_TAIL, "tail", NULL,
     "tail -n N", "Print recent messages", "输出最近消息"},
    {TNT_EXEC_COMMAND_POST, "post", NULL,
     "post MESSAGE", "Post a message non-interactively", "非交互发送消息"},
    {TNT_EXEC_COMMAND_POST, "post", NULL,
     "post \"/me act\"", "Post an action message", "发送动作消息"},
    {TNT_EXEC_COMMAND_EXIT, "exit", NULL,
     "exit", "Exit successfully", "成功退出"}
};

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

void exec_catalog_append_help(char *buffer, size_t buf_size, size_t *pos,
                              ui_lang_t lang) {
    if (lang == UI_LANG_ZH) {
        buffer_appendf(buffer, buf_size, pos, "TNT exec 接口\n命令:\n");
    } else {
        buffer_appendf(buffer, buf_size, pos, "TNT exec interface\nCommands:\n");
    }

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const char *summary = lang == UI_LANG_ZH ? entries[i].summary_zh
                                                 : entries[i].summary_en;
        buffer_appendf(buffer, buf_size, pos, "  %-15s %s\n",
                       entries[i].usage, summary);
    }
}
