#include "exec_catalog.h"

typedef struct {
    const char *usage;
    const char *summary_en;
    const char *summary_zh;
} exec_catalog_entry_t;

static const exec_catalog_entry_t entries[] = {
    {"help", "Show this help", "显示此帮助"},
    {"health", "Print service health", "输出服务健康状态"},
    {"users [--json]", "List online users", "列出在线用户"},
    {"stats [--json]", "Print room statistics", "输出房间统计"},
    {"tail [N]", "Print recent messages", "输出最近消息"},
    {"tail -n N", "Print recent messages", "输出最近消息"},
    {"post MESSAGE", "Post a message non-interactively", "非交互发送消息"},
    {"post \"/me act\"", "Post an action message", "发送动作消息"},
    {"exit", "Exit successfully", "成功退出"}
};

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
