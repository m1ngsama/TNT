#include "i18n.h"

typedef struct {
    const char *en;
    const char *zh;
} i18n_text_entry_t;

static const i18n_text_entry_t text_catalog[I18N_TEXT_COUNT] = {
    [I18N_USERNAME_PROMPT] = {
        "  Enter display name (blank for anonymous): ",
        "  请输入用户名 (留空 anonymous): "
    },
    [I18N_INVALID_USERNAME] = {
        "Invalid username. Using 'anonymous' instead.\r\n",
        "用户名无效，已改用 anonymous。\r\n"
    },
    [I18N_ROOM_FULL] = {
        "Room is full\r\n",
        "房间已满\r\n"
    },
    [I18N_WELCOME_SUBTITLE] = {
        "anonymous chat · SSH",
        "匿名聊天室 · SSH"
    },
    [I18N_WELCOME_TAGLINE] = {
        "keyboard-first terminal chat",
        "键盘友好的终端交流"
    },
    [I18N_WELCOME_FALLBACK_FORMAT] = {
        "TNT %s - anonymous chat over SSH\r\n\r\n",
        "TNT %s - SSH 匿名聊天室\r\n\r\n"
    },
    [I18N_INSERT_HINT_WIDE] = {
        "Enter send · Esc browse · :help",
        "Enter 发送 · Esc 浏览 · :help"
    },
    [I18N_INSERT_HINT_NARROW] = {
        "Enter · Esc · :help",
        "Enter · Esc · :help"
    },
    [I18N_NORMAL_LATEST] = {
        "G latest",
        "G 最新"
    },
    [I18N_NORMAL_NEW_MESSAGES] = {
        "new",
        "新消息"
    },
    [I18N_HELP_TITLE] = {
        " KEYS ",
        " 按键 "
    },
    [I18N_HELP_STATUS_FORMAT] = {
        "-- KEY REFERENCE -- (%d/%d) j/k:scroll g/G:top/bottom e/z:lang q:close",
        "-- 按键参考 -- (%d/%d) j/k:滚动 g/G:首尾 e/z:语言 q:关闭"
    },
    [I18N_COMMAND_OUTPUT_TITLE] = {
        " COMMAND OUTPUT ",
        " 命令输出 "
    },
    [I18N_COMMAND_OUTPUT_STATUS_FORMAT] = {
        "-- COMMAND OUTPUT -- (%d/%d) j/k:scroll Ctrl-D/U:half g/G:top/bottom q:close",
        "-- 命令输出 -- (%d/%d) j/k:滚动 Ctrl-D/U:半页 g/G:首尾 q:关闭"
    },
    [I18N_MOTD_TITLE] = {
        " NOTICE ",
        " 公告 "
    },
    [I18N_MOTD_CONTINUE_HINT] = {
        " Press any key ",
        " 按任意键继续 "
    },
    [I18N_TITLE_ONLINE_FORMAT] = {
        "online %d",
        "在线 %d"
    },
    [I18N_TITLE_MUTED] = {
        "muted",
        "静音"
    },
    [I18N_TITLE_HELP_HINT] = {
        "? keys",
        "? 按键"
    },
    [I18N_IDLE_TIMEOUT_FORMAT] = {
        "\r\n\033[33mDisconnected: idle timeout (%d min)\033[0m\r\n",
        "\r\n\033[33m已断开: 空闲超时 (%d 分钟)\033[0m\r\n"
    },
    [I18N_SYSTEM_USERNAME] = {
        "system",
        "系统"
    },
    [I18N_SYSTEM_JOIN_FORMAT] = {
        "%s joined the room",
        "%s 加入了聊天室"
    },
    [I18N_SYSTEM_LEAVE_FORMAT] = {
        "%s left the room",
        "%s 离开了聊天室"
    },
    [I18N_SYSTEM_NICK_FORMAT] = {
        "%s renamed to %s",
        "%s 更名为 %s"
    },
    [I18N_USERS_TITLE] = {
        "Online users",
        "在线用户"
    },
    [I18N_MSG_USAGE] = {
        "Usage: msg <user> <message>\n"
        "       w <user> <message>\n",
        "用法: msg <user> <message>\n"
        "      w <user> <message>\n"
    },
    [I18N_MSG_SENT_FORMAT] = {
        "Whisper sent to %s\n",
        "悄悄话已发送给 %s\n"
    },
    [I18N_MSG_USER_NOT_FOUND_FORMAT] = {
        "User '%s' not found\n",
        "未找到用户 '%s'\n"
    },
    [I18N_INBOX_TITLE] = {
        "Whispers",
        "悄悄话"
    },
    [I18N_INBOX_EMPTY] = {
        "(empty)",
        "(空)"
    },
    [I18N_NICK_USAGE] = {
        "Usage: nick <name>\n",
        "用法: nick <name>\n"
    },
    [I18N_NICK_INVALID] = {
        "Invalid username\n",
        "用户名无效\n"
    },
    [I18N_NICK_TAKEN_FORMAT] = {
        "Nickname '%s' is already taken\n",
        "昵称 '%s' 已被使用\n"
    },
    [I18N_NICK_UNCHANGED] = {
        "Nickname unchanged\n",
        "昵称未变化\n"
    },
    [I18N_NICK_CHANGED_FORMAT] = {
        "Nickname changed: %s -> %s\n",
        "昵称已修改: %s -> %s\n"
    },
    [I18N_LAST_USAGE] = {
        "Usage: last [N]  (N: 1-50, default 10)\n",
        "用法: last [N]  (N: 1-50，默认 10)\n"
    },
    [I18N_LAST_HEADER_FORMAT] = {
        "--- Last %d message(s) ---\n",
        "--- 最近 %d 条消息 ---\n"
    },
    [I18N_SEARCH_USAGE] = {
        "Usage: search <keyword>\n",
        "用法: search <keyword>\n"
    },
    [I18N_SEARCH_HEADER_FORMAT] = {
        "--- Search: \"%s\" (%d match(es)) ---\n",
        "--- 搜索: \"%s\" (%d 条匹配) ---\n"
    },
    [I18N_MUTE_JOINS_FORMAT] = {
        "Join/leave notifications: %s\n",
        "加入/离开提示: %s\n"
    },
    [I18N_MUTE_JOINS_MUTED] = {
        "muted",
        "已静音"
    },
    [I18N_MUTE_JOINS_UNMUTED] = {
        "unmuted",
        "已开启"
    },
    [I18N_CLEAR_DONE] = {
        "Command output cleared\n",
        "命令输出已清空\n"
    },
    [I18N_LANG_CURRENT_FORMAT] = {
        "Current language: %s\n"
        "Usage: lang <en|zh>\n",
        "当前语言: %s\n"
        "用法: lang <en|zh>\n"
    },
    [I18N_LANG_SET_FORMAT] = {
        "Language set to: %s\n",
        "语言已切换为: %s\n"
    },
    [I18N_LANG_UNSUPPORTED_FORMAT] = {
        "Unsupported language: %s\n"
        "Usage: lang <en|zh>\n",
        "不支持的语言: %s\n"
        "用法: lang <en|zh>\n"
    },
    [I18N_UNKNOWN_COMMAND_FORMAT] = {
        "Unknown command: %s\n",
        "未知命令: %s\n"
    },
    [I18N_DID_YOU_MEAN_FORMAT] = {
        "Did you mean :%s?\n",
        "你是想输入 :%s 吗?\n"
    },
    [I18N_UNKNOWN_GUIDANCE] = {
        "Type :help for help\n",
        "输入 :help 查看帮助\n"
    },
    [I18N_EXEC_HELP] = {
        "TNT exec interface\n"
        "Commands:\n"
        "  help            Show this help\n"
        "  health          Print service health\n"
        "  users [--json]  List online users\n"
        "  stats [--json]  Print room statistics\n"
        "  tail [N]        Print recent messages\n"
        "  tail -n N       Print recent messages\n"
        "  post MESSAGE    Post a message non-interactively\n"
        "  post \"/me act\"  Post an action message\n"
        "  exit            Exit successfully\n",
        "TNT exec 接口\n"
        "命令:\n"
        "  help            显示此帮助\n"
        "  health          输出服务健康状态\n"
        "  users [--json]  列出在线用户\n"
        "  stats [--json]  输出房间统计\n"
        "  tail [N]        输出最近消息\n"
        "  tail -n N       输出最近消息\n"
        "  post MESSAGE    非交互发送消息\n"
        "  post \"/me act\" 发送动作消息\n"
        "  exit            成功退出\n"
    },
    [I18N_EXEC_USERS_USAGE] = {
        "users: usage: users [--json]\n",
        "users: 用法: users [--json]\n"
    },
    [I18N_EXEC_STATS_USAGE] = {
        "stats: usage: stats [--json]\n",
        "stats: 用法: stats [--json]\n"
    },
    [I18N_EXEC_TAIL_USAGE] = {
        "tail: usage: tail [N] | tail -n N\n",
        "tail: 用法: tail [N] | tail -n N\n"
    },
    [I18N_EXEC_POST_USAGE] = {
        "post: usage: post MESSAGE\n",
        "post: 用法: post MESSAGE\n"
    },
    [I18N_EXEC_POST_EMPTY] = {
        "post: message cannot be empty\n",
        "post: 消息不能为空\n"
    },
    [I18N_EXEC_POST_INVALID_UTF8] = {
        "post: invalid UTF-8 input\n",
        "post: 输入不是有效 UTF-8\n"
    },
    [I18N_EXEC_UNKNOWN_COMMAND_FORMAT] = {
        "Unknown command: %s\n",
        "未知命令: %s\n"
    }
};

const char *i18n_text(ui_lang_t lang, i18n_text_id_t id) {
    if (id < 0 || id >= I18N_TEXT_COUNT) {
        return "";
    }

    const i18n_text_entry_t *entry = &text_catalog[id];
    if (lang == UI_LANG_ZH && entry->zh) {
        return entry->zh;
    }
    if (entry->en) {
        return entry->en;
    }
    return "";
}
