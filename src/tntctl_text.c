#include "tntctl_text.h"

#include "exec_catalog.h"
#include "i18n.h"

static const i18n_string_t text_catalog[TNTCTL_TEXT_COUNT] = {
    [TNTCTL_TEXT_INVALID_PORT] = I18N_STRING(
        "invalid port", "端口无效"
    ),
    [TNTCTL_TEXT_INVALID_LOGIN] = I18N_STRING(
        "invalid login", "登录名无效"
    ),
    [TNTCTL_TEXT_INVALID_HOST_KEY_MODE] = I18N_STRING(
        "invalid host-key checking mode", "主机密钥检查模式无效"
    ),
    [TNTCTL_TEXT_INVALID_KNOWN_HOSTS] = I18N_STRING(
        "invalid known_hosts path", "known_hosts 路径无效"
    ),
    [TNTCTL_TEXT_UNKNOWN_OPTION_FORMAT] = I18N_STRING(
        "unknown option: %s", "未知选项: %s"
    ),
    [TNTCTL_TEXT_MISSING_HOST] = I18N_STRING(
        "missing host", "缺少 host"
    ),
    [TNTCTL_TEXT_INVALID_HOST] = I18N_STRING(
        "invalid host", "host 无效"
    ),
    [TNTCTL_TEXT_LOGIN_HOST_CONFLICT] = I18N_STRING(
        "use either --login or user@host, not both",
        "只能使用 --login 或 user@host 之一"
    ),
    [TNTCTL_TEXT_UNKNOWN_COMMAND] = I18N_STRING(
        "unknown or missing command", "未知命令或缺少命令"
    ),
    [TNTCTL_TEXT_INVALID_REMOTE_COMMAND] = I18N_STRING(
        "invalid or too-long command", "命令无效或过长"
    ),
    [TNTCTL_TEXT_DESTINATION_TOO_LONG] = I18N_STRING(
        "destination too long", "目标地址过长"
    ),
    [TNTCTL_TEXT_INVALID_DESTINATION] = I18N_STRING(
        "invalid destination", "目标地址无效"
    ),
    [TNTCTL_TEXT_OUT_OF_MEMORY] = I18N_STRING(
        "out of memory", "内存不足"
    ),
    [TNTCTL_TEXT_HOST_KEY_OPTION_TOO_LONG] = I18N_STRING(
        "host-key option too long", "主机密钥选项过长"
    ),
    [TNTCTL_TEXT_KNOWN_HOSTS_OPTION_TOO_LONG] = I18N_STRING(
        "known_hosts option too long", "known_hosts 选项过长"
    )
};
typedef char text_catalog_must_cover_enum[
    sizeof(text_catalog) / sizeof(text_catalog[0]) == TNTCTL_TEXT_COUNT ? 1 : -1
];

void tntctl_text_append_usage(char *buffer, size_t buf_size, size_t *pos,
                              ui_lang_t lang) {
    static const i18n_string_t before_commands = I18N_STRING(
        "Usage: tntctl [options] host command [args...]\n"
        "\n"
        "Options:\n"
        "  -p, --port PORT        SSH port (default: 2222)\n"
        "  -l, --login USER       SSH login name for exec identity\n"
        "  --host-key-checking MODE\n"
        "                         OpenSSH host-key mode: yes, accept-new, no\n"
        "  --known-hosts FILE     OpenSSH known_hosts file\n"
        "  -V, --version          Print version and exit\n"
        "  -h, --help             Print this help and exit\n"
        "\n"
        "Commands:\n"
        "  ",
        "用法: tntctl [options] host command [args...]\n"
        "\n"
        "选项:\n"
        "  -p, --port PORT        SSH 端口 (默认: 2222)\n"
        "  -l, --login USER       SSH 登录名，用作 exec 身份\n"
        "  --host-key-checking MODE\n"
        "                         OpenSSH 主机密钥模式: yes, accept-new, no\n"
        "  --known-hosts FILE     OpenSSH known_hosts 文件\n"
        "  -V, --version          输出版本并退出\n"
        "  -h, --help             输出此帮助并退出\n"
        "\n"
        "命令:\n"
        "  "
    );

    buffer_appendf(buffer, buf_size, pos, "%s",
                   i18n_string(before_commands, lang));
    exec_catalog_append_command_list(buffer, buf_size, pos);
    buffer_appendf(buffer, buf_size, pos, "\n");
}

const char *tntctl_text(ui_lang_t lang, tntctl_text_id_t id) {
    if (id < 0 || id >= TNTCTL_TEXT_COUNT) {
        return "";
    }
    return i18n_string(text_catalog[id], lang);
}
