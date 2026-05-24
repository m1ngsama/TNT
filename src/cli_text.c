#include "cli_text.h"

void cli_text_append_help(char *buffer, size_t buf_size, size_t *pos,
                          const char *program_name, ui_lang_t lang) {
    const char *program = (program_name && program_name[0] != '\0')
                              ? program_name
                              : "tnt";

    if (lang == UI_LANG_ZH) {
        buffer_appendf(buffer, buf_size, pos,
                       "tnt %s - 匿名 SSH 聊天服务器\n\n"
                       "用法: %s [options]\n\n"
                       "选项:\n"
                       "  -p, --port PORT       监听 PORT (默认: %d)\n"
                       "  -d, --state-dir DIR   将主机密钥和日志存放在 DIR\n"
                       "  -V, --version         显示版本\n"
                       "  -h, --help            显示此帮助\n"
                       "\n"
                       "环境变量:\n"
                       "  PORT                  默认监听端口\n"
                       "  TNT_STATE_DIR         状态目录\n"
                       "  TNT_ACCESS_TOKEN      要求 SSH 认证使用此密码\n"
                       "  TNT_LANG              UI 语言: en 或 zh (默认跟随 locale)\n"
                       "  TNT_MAX_CONNECTIONS   全局连接数限制 (默认: 64)\n"
                       "  TNT_RATE_LIMIT        设为 0 可禁用速率限制\n"
                       "  TNT_IDLE_TIMEOUT      空闲断开时间，单位秒 (默认: 1800)\n",
                       TNT_VERSION, program, DEFAULT_PORT);
        return;
    }

    buffer_appendf(buffer, buf_size, pos,
                   "tnt %s - anonymous SSH chat server\n\n"
                   "Usage: %s [options]\n\n"
                   "Options:\n"
                   "  -p, --port PORT       Listen on PORT (default: %d)\n"
                   "  -d, --state-dir DIR   Store host key and logs in DIR\n"
                   "  -V, --version         Show version\n"
                   "  -h, --help            Show this help\n"
                   "\n"
                   "Environment:\n"
                   "  PORT                  Default listening port\n"
                   "  TNT_STATE_DIR         State directory\n"
                   "  TNT_ACCESS_TOKEN      Require this password for SSH auth\n"
                   "  TNT_LANG              UI language: en or zh (default: locale)\n"
                   "  TNT_MAX_CONNECTIONS   Global connection limit (default: 64)\n"
                   "  TNT_RATE_LIMIT        Set to 0 to disable rate limiting\n"
                   "  TNT_IDLE_TIMEOUT      Idle disconnect timeout in seconds (default: 1800)\n",
                   TNT_VERSION, program, DEFAULT_PORT);
}

const char *cli_text_invalid_port_format(ui_lang_t lang) {
    return lang == UI_LANG_ZH ? "端口无效: %s\n" : "Invalid port: %s\n";
}

const char *cli_text_unknown_option_format(ui_lang_t lang) {
    return lang == UI_LANG_ZH ? "未知选项: %s\n" : "Unknown option: %s\n";
}

const char *cli_text_short_usage_format(ui_lang_t lang) {
    return lang == UI_LANG_ZH ? "用法: %s [-p PORT] [-d DIR] [-h]\n"
                           : "Usage: %s [-p PORT] [-d DIR] [-h]\n";
}
