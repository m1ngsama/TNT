#include "i18n.h"

#include <ctype.h>

static bool starts_with_lang(const char *value, const char *prefix) {
    if (!value || !prefix) return false;

    while (*prefix) {
        if (tolower((unsigned char)*value) !=
            tolower((unsigned char)*prefix)) {
            return false;
        }
        value++;
        prefix++;
    }

    return *value == '\0' || *value == '_' || *value == '-' || *value == '.';
}

bool i18n_try_parse_lang(const char *value, help_lang_t *lang) {
    if (!value || value[0] == '\0') {
        return false;
    }

    if (starts_with_lang(value, "zh") ||
        starts_with_lang(value, "cn") ||
        starts_with_lang(value, "chinese")) {
        if (lang) *lang = LANG_ZH;
        return true;
    }

    if (starts_with_lang(value, "en") ||
        starts_with_lang(value, "c") ||
        starts_with_lang(value, "posix")) {
        if (lang) *lang = LANG_EN;
        return true;
    }

    return false;
}

help_lang_t i18n_parse_lang(const char *value, help_lang_t fallback) {
    help_lang_t lang;
    if (i18n_try_parse_lang(value, &lang)) {
        return lang;
    }
    return fallback;
}

help_lang_t i18n_default_lang(void) {
    const char *explicit_lang = getenv("TNT_LANG");
    if (explicit_lang && explicit_lang[0] != '\0') {
        return i18n_parse_lang(explicit_lang, LANG_EN);
    }

    const char *locale = getenv("LC_ALL");
    if (!locale || locale[0] == '\0') {
        locale = getenv("LC_MESSAGES");
    }
    if (!locale || locale[0] == '\0') {
        locale = getenv("LANG");
    }

    return i18n_parse_lang(locale, LANG_EN);
}

const char *i18n_lang_code(help_lang_t lang) {
    return lang == LANG_ZH ? "zh" : "en";
}

const char *i18n_text(help_lang_t lang, i18n_text_id_t id) {
    if (lang == LANG_ZH) {
        switch (id) {
            case I18N_USERNAME_PROMPT:
                return "  请输入用户名 (留空 anonymous): ";
            case I18N_INVALID_USERNAME:
                return "用户名无效，已改用 anonymous。\r\n";
            case I18N_ROOM_FULL:
                return "房间已满\r\n";
            case I18N_WELCOME_SUBTITLE:
                return "匿名聊天室 · SSH";
            case I18N_WELCOME_TAGLINE:
                return "键盘友好的终端交流";
            case I18N_WELCOME_FALLBACK_FORMAT:
                return "TNT %s - SSH 匿名聊天室\r\n\r\n";
            case I18N_INSERT_HINT_WIDE:
                return "Enter 发送 · Esc 浏览 · :support";
            case I18N_INSERT_HINT_NARROW:
                return "Enter · Esc · :support";
            case I18N_NORMAL_LATEST:
                return "G 最新";
            case I18N_NORMAL_NEW_MESSAGES:
                return "新消息";
            case I18N_HELP_TITLE:
                return " 帮助 ";
            case I18N_HELP_STATUS_FORMAT:
                return "-- 帮助 -- (%d/%d) j/k:滚动 g/G:首尾 e/z:语言 q:关闭";
            case I18N_COMMAND_OUTPUT_TITLE:
                return " 命令输出 ";
            case I18N_MOTD_TITLE:
                return " 公告 ";
            case I18N_MOTD_CONTINUE_HINT:
                return " 按任意键继续 ";
            case I18N_TITLE_ONLINE_FORMAT:
                return "在线 %d";
            case I18N_TITLE_MUTED:
                return "静音";
            case I18N_TITLE_HELP_HINT:
                return "? 帮助";
            case I18N_SYSTEM_USERNAME:
                return "系统";
            case I18N_SYSTEM_JOIN_FORMAT:
                return "%s 加入了聊天室";
            case I18N_SYSTEM_LEAVE_FORMAT:
                return "%s 离开了聊天室";
            case I18N_SYSTEM_NICK_FORMAT:
                return "%s 更名为 %s";
            case I18N_USERS_TITLE:
                return "在线用户";
            case I18N_MSG_USAGE:
                return "用法: msg <用户名> <消息>\n"
                       "      w <用户名> <消息>\n";
            case I18N_MSG_SENT_FORMAT:
                return "悄悄话已发送给 %s\n";
            case I18N_MSG_USER_NOT_FOUND_FORMAT:
                return "未找到用户 '%s'\n";
            case I18N_INBOX_TITLE:
                return "悄悄话";
            case I18N_INBOX_EMPTY:
                return "(空)";
            case I18N_NICK_USAGE:
                return "用法: nick <新用户名>\n";
            case I18N_NICK_INVALID:
                return "用户名无效\n";
            case I18N_NICK_TAKEN_FORMAT:
                return "昵称 '%s' 已被使用\n";
            case I18N_NICK_UNCHANGED:
                return "昵称未变化\n";
            case I18N_NICK_CHANGED_FORMAT:
                return "昵称已修改: %s -> %s\n";
            case I18N_LAST_USAGE:
                return "用法: last [N]  (N: 1-50，默认 10)\n";
            case I18N_LAST_HEADER_FORMAT:
                return "--- 最近 %d 条消息 ---\n";
            case I18N_SEARCH_USAGE:
                return "用法: search <关键词>\n";
            case I18N_SEARCH_HEADER_FORMAT:
                return "--- 搜索: \"%s\" (%d 条匹配) ---\n";
            case I18N_MUTE_JOINS_FORMAT:
                return "加入/离开提示: %s\n";
            case I18N_MUTE_JOINS_MUTED:
                return "已静音";
            case I18N_MUTE_JOINS_UNMUTED:
                return "已开启";
            case I18N_CLEAR_DONE:
                return "命令输出已清空\n";
            case I18N_LANG_CURRENT_FORMAT:
                return "当前语言: %s\n"
                       "用法: lang <en|zh>\n";
            case I18N_LANG_SET_FORMAT:
                return "语言已切换为: %s\n";
            case I18N_LANG_UNSUPPORTED_FORMAT:
                return "不支持的语言: %s\n"
                       "用法: lang <en|zh>\n";
            case I18N_UNKNOWN_COMMAND_FORMAT:
                return "未知命令: %s\n";
            case I18N_DID_YOU_MEAN_FORMAT:
                return "你是想输入 :%s 吗?\n";
            case I18N_UNKNOWN_GUIDANCE:
                return "输入 :support 查看引导，或 :help 查看命令\n";
            case I18N_EXEC_HELP:
                return "TNT exec 接口\n"
                       "命令:\n"
                       "  help            显示此帮助\n"
                       "  health          输出服务健康状态\n"
                       "  users [--json]  列出在线用户\n"
                       "  stats [--json]  输出房间统计\n"
                       "  tail [N]        输出最近消息\n"
                       "  tail -n N       输出最近消息\n"
                       "  post MESSAGE    非交互发送消息\n"
                       "  post \"/me act\" 发送动作消息\n"
                       "  support         显示快速支持指南\n"
                       "  exit            成功退出\n";
            case I18N_EXEC_USERS_USAGE:
                return "users: 用法: users [--json]\n";
            case I18N_EXEC_STATS_USAGE:
                return "stats: 用法: stats [--json]\n";
            case I18N_EXEC_TAIL_USAGE:
                return "tail: 用法: tail [N] | tail -n N\n";
            case I18N_EXEC_POST_USAGE:
                return "post: 用法: post MESSAGE\n";
            case I18N_EXEC_POST_EMPTY:
                return "post: 消息不能为空\n";
            case I18N_EXEC_POST_INVALID_UTF8:
                return "post: 输入不是有效 UTF-8\n";
            case I18N_EXEC_UNKNOWN_COMMAND_FORMAT:
                return "未知命令: %s\n";
            case I18N_CONTINUE_PROMPT:
                return "\n按任意键继续...";
        }
    }

    switch (id) {
        case I18N_USERNAME_PROMPT:
            return "  Enter display name (blank for anonymous): ";
        case I18N_INVALID_USERNAME:
            return "Invalid username. Using 'anonymous' instead.\r\n";
        case I18N_ROOM_FULL:
            return "Room is full\r\n";
        case I18N_WELCOME_SUBTITLE:
            return "anonymous chat · SSH";
        case I18N_WELCOME_TAGLINE:
            return "keyboard-first terminal chat";
        case I18N_WELCOME_FALLBACK_FORMAT:
            return "TNT %s - anonymous chat over SSH\r\n\r\n";
        case I18N_INSERT_HINT_WIDE:
            return "Enter send · Esc browse · :support";
        case I18N_INSERT_HINT_NARROW:
            return "Enter · Esc · :support";
        case I18N_NORMAL_LATEST:
            return "G latest";
        case I18N_NORMAL_NEW_MESSAGES:
            return "new";
        case I18N_HELP_TITLE:
            return " HELP ";
        case I18N_HELP_STATUS_FORMAT:
            return "-- HELP -- (%d/%d) j/k:scroll g/G:top/bottom e/z:lang q:close";
        case I18N_COMMAND_OUTPUT_TITLE:
            return " COMMAND OUTPUT ";
        case I18N_MOTD_TITLE:
            return " NOTICE ";
        case I18N_MOTD_CONTINUE_HINT:
            return " Press any key ";
        case I18N_TITLE_ONLINE_FORMAT:
            return "online %d";
        case I18N_TITLE_MUTED:
            return "muted";
        case I18N_TITLE_HELP_HINT:
            return "? help";
        case I18N_SYSTEM_USERNAME:
            return "system";
        case I18N_SYSTEM_JOIN_FORMAT:
            return "%s joined the room";
        case I18N_SYSTEM_LEAVE_FORMAT:
            return "%s left the room";
        case I18N_SYSTEM_NICK_FORMAT:
            return "%s renamed to %s";
        case I18N_USERS_TITLE:
            return "Online users";
        case I18N_MSG_USAGE:
            return "Usage: msg <username> <message>\n"
                   "       w <username> <message>\n";
        case I18N_MSG_SENT_FORMAT:
            return "Whisper sent to %s\n";
        case I18N_MSG_USER_NOT_FOUND_FORMAT:
            return "User '%s' not found\n";
        case I18N_INBOX_TITLE:
            return "Whispers";
        case I18N_INBOX_EMPTY:
            return "(empty)";
        case I18N_NICK_USAGE:
            return "Usage: nick <new_username>\n";
        case I18N_NICK_INVALID:
            return "Invalid username\n";
        case I18N_NICK_TAKEN_FORMAT:
            return "Nickname '%s' is already taken\n";
        case I18N_NICK_UNCHANGED:
            return "Nickname unchanged\n";
        case I18N_NICK_CHANGED_FORMAT:
            return "Nickname changed: %s -> %s\n";
        case I18N_LAST_USAGE:
            return "Usage: last [N]  (N: 1-50, default 10)\n";
        case I18N_LAST_HEADER_FORMAT:
            return "--- Last %d message(s) ---\n";
        case I18N_SEARCH_USAGE:
            return "Usage: search <keyword>\n";
        case I18N_SEARCH_HEADER_FORMAT:
            return "--- Search: \"%s\" (%d match(es)) ---\n";
        case I18N_MUTE_JOINS_FORMAT:
            return "Join/leave notifications: %s\n";
        case I18N_MUTE_JOINS_MUTED:
            return "muted";
        case I18N_MUTE_JOINS_UNMUTED:
            return "unmuted";
        case I18N_CLEAR_DONE:
            return "Command output cleared\n";
        case I18N_LANG_CURRENT_FORMAT:
            return "Current language: %s\n"
                   "Usage: lang <en|zh>\n";
        case I18N_LANG_SET_FORMAT:
            return "Language set to: %s\n";
        case I18N_LANG_UNSUPPORTED_FORMAT:
            return "Unsupported language: %s\n"
                   "Usage: lang <en|zh>\n";
        case I18N_UNKNOWN_COMMAND_FORMAT:
            return "Unknown command: %s\n";
        case I18N_DID_YOU_MEAN_FORMAT:
            return "Did you mean :%s?\n";
        case I18N_UNKNOWN_GUIDANCE:
            return "Type :support for guidance or :help for commands\n";
        case I18N_EXEC_HELP:
            return "TNT exec interface\n"
                   "Commands:\n"
                   "  help            Show this help\n"
                   "  health          Print service health\n"
                   "  users [--json]  List online users\n"
                   "  stats [--json]  Print room statistics\n"
                   "  tail [N]        Print recent messages\n"
                   "  tail -n N       Print recent messages\n"
                   "  post MESSAGE    Post a message non-interactively\n"
                   "  post \"/me act\"  Post an action message\n"
                   "  support         Show quick support guide\n"
                   "  exit            Exit successfully\n";
        case I18N_EXEC_USERS_USAGE:
            return "users: usage: users [--json]\n";
        case I18N_EXEC_STATS_USAGE:
            return "stats: usage: stats [--json]\n";
        case I18N_EXEC_TAIL_USAGE:
            return "tail: usage: tail [N] | tail -n N\n";
        case I18N_EXEC_POST_USAGE:
            return "post: usage: post MESSAGE\n";
        case I18N_EXEC_POST_EMPTY:
            return "post: message cannot be empty\n";
        case I18N_EXEC_POST_INVALID_UTF8:
            return "post: invalid UTF-8 input\n";
        case I18N_EXEC_UNKNOWN_COMMAND_FORMAT:
            return "Unknown command: %s\n";
        case I18N_CONTINUE_PROMPT:
            return "\nPress any key to continue...";
    }

    return "";
}
