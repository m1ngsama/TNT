#include "cli_text.h"

#include "i18n.h"

void cli_text_append_help(char *buffer, size_t buf_size, size_t *pos,
                          const char *program_name, ui_lang_t lang) {
    static const i18n_string_t help_format = I18N_STRING(
        "tnt %s - anonymous SSH chat server\n\n"
        "Usage: %s [options]\n\n"
        "Options:\n"
        "  -p, --port PORT              Listen on PORT (default: %d)\n"
        "  -d, --state-dir DIR          Store host key and logs in DIR\n"
        "      --bind ADDR              Bind to ADDR (default: 0.0.0.0)\n"
        "      --public-host HOST       Show HOST in startup connection hints\n"
        "      --max-connections N      Global connection limit (default: 64)\n"
        "      --max-conn-per-ip N      Per-IP concurrent session limit\n"
        "      --max-conn-rate-per-ip N Per-IP connection-rate limit\n"
        "      --rate-limit 0|1         Disable/enable rate-based blocking\n"
        "      --idle-timeout SECONDS   Idle disconnect timeout\n"
        "      --ssh-log-level LEVEL    libssh log level 0..4\n"
        "      --log-check FILE         Check messages.log v1 records\n"
        "      --log-recover FILE       Write valid records to stdout\n"
        "  -V, --version                Show version\n"
        "  -h, --help                   Show this help\n"
        "\n"
        "Environment:\n"
        "  PORT                  Default listening port\n"
        "  TNT_STATE_DIR         State directory\n"
        "  TNT_ACCESS_TOKEN      Require this password for SSH auth\n"
        "  TNT_LANG              UI language: en or zh (default: locale)\n"
        "  TNT_MAX_CONNECTIONS   Global connection limit (default: 64)\n"
        "  TNT_RATE_LIMIT        Set to 0 to disable rate limiting\n"
        "  TNT_IDLE_TIMEOUT      Idle disconnect timeout in seconds (default: 1800)\n",
        "tnt %s - 匿名 SSH 聊天服务器\n\n"
        "用法: %s [options]\n\n"
        "选项:\n"
        "  -p, --port PORT              监听 PORT (默认: %d)\n"
        "  -d, --state-dir DIR          将主机密钥和日志存放在 DIR\n"
        "      --bind ADDR              绑定到 ADDR (默认: 0.0.0.0)\n"
        "      --public-host HOST       在启动提示中显示 HOST\n"
        "      --max-connections N      全局连接数限制 (默认: 64)\n"
        "      --max-conn-per-ip N      单 IP 并发会话限制\n"
        "      --max-conn-rate-per-ip N 单 IP 连接速率限制\n"
        "      --rate-limit 0|1         禁用/启用速率封禁\n"
        "      --idle-timeout SECONDS   空闲断开时间\n"
        "      --ssh-log-level LEVEL    libssh 日志级别 0..4\n"
        "      --log-check FILE         检查 messages.log v1 记录\n"
        "      --log-recover FILE       将有效记录写入 stdout\n"
        "  -V, --version                显示版本\n"
        "  -h, --help                   显示此帮助\n"
        "\n"
        "环境变量:\n"
        "  PORT                  默认监听端口\n"
        "  TNT_STATE_DIR         状态目录\n"
        "  TNT_ACCESS_TOKEN      要求 SSH 认证使用此密码\n"
        "  TNT_LANG              UI 语言: en 或 zh (默认跟随 locale)\n"
        "  TNT_MAX_CONNECTIONS   全局连接数限制 (默认: 64)\n"
        "  TNT_RATE_LIMIT        设为 0 可禁用速率限制\n"
        "  TNT_IDLE_TIMEOUT      空闲断开时间，单位秒 (默认: 1800)\n"
    );
    const char *program = (program_name && program_name[0] != '\0')
                              ? program_name
                              : "tnt";

    buffer_appendf(buffer, buf_size, pos, i18n_string(help_format, lang),
                   TNT_VERSION, program, DEFAULT_PORT);
}

const char *cli_text_invalid_port_format(ui_lang_t lang) {
    static const i18n_string_t text =
        I18N_STRING("Invalid port: %s\n", "端口无效: %s\n");
    return i18n_string(text, lang);
}

const char *cli_text_invalid_value_format(ui_lang_t lang) {
    static const i18n_string_t text =
        I18N_STRING("Invalid %s: %s\n", "%s 无效: %s\n");
    return i18n_string(text, lang);
}

const char *cli_text_option_requires_arg_format(ui_lang_t lang) {
    static const i18n_string_t text =
        I18N_STRING("Option requires argument: %s\n",
                    "选项需要参数: %s\n");
    return i18n_string(text, lang);
}

const char *cli_text_unknown_option_format(ui_lang_t lang) {
    static const i18n_string_t text =
        I18N_STRING("Unknown option: %s\n", "未知选项: %s\n");
    return i18n_string(text, lang);
}

const char *cli_text_short_usage_format(ui_lang_t lang) {
    static const i18n_string_t text =
        I18N_STRING("Usage: %s [options]\n",
                    "用法: %s [options]\n");
    return i18n_string(text, lang);
}
