#include "i18n.h"

static const i18n_string_t text_catalog[I18N_TEXT_COUNT] = {
    [I18N_USERNAME_PROMPT] = I18N_STRING(
        "  Enter display name (blank for anonymous): ",
        "  请输入用户名 (留空 anonymous): "
    ),
    [I18N_INVALID_USERNAME] = I18N_STRING(
        "Invalid username. Using 'anonymous' instead.\r\n",
        "用户名无效，已改用 anonymous。\r\n"
    ),
    [I18N_ROOM_FULL] = I18N_STRING(
        "Room is full\r\n",
        "房间已满\r\n"
    ),
    [I18N_WELCOME_SUBTITLE] = I18N_STRING(
        "anonymous chat · SSH",
        "匿名聊天室 · SSH"
    ),
    [I18N_WELCOME_TAGLINE] = I18N_STRING(
        "keyboard-first terminal chat",
        "键盘友好的终端交流"
    ),
    [I18N_WELCOME_FALLBACK_FORMAT] = I18N_STRING(
        "TNT %s - anonymous chat over SSH\r\n\r\n",
        "TNT %s - SSH 匿名聊天室\r\n\r\n"
    ),
    [I18N_INSERT_HINT_WIDE] = I18N_STRING(
        "Enter send · Esc NORMAL",
        "Enter 发送 · Esc NORMAL"
    ),
    [I18N_INSERT_HINT_NARROW] = I18N_STRING(
        "Enter · Esc",
        "Enter · Esc"
    ),
    [I18N_NORMAL_LATEST] = I18N_STRING(
        "G latest",
        "G 最新"
    ),
    [I18N_NORMAL_NEW_MESSAGES] = I18N_STRING(
        "new",
        "新消息"
    ),
    [I18N_HELP_TITLE] = I18N_STRING(
        " KEYS ",
        " 按键 "
    ),
    [I18N_HELP_STATUS_FORMAT] = I18N_STRING(
        "-- KEY REFERENCE -- (%d/%d) j/k:scroll g/G:top/bottom l:lang q:close",
        "-- 按键参考 -- (%d/%d) j/k:滚动 g/G:首尾 l:语言 q:关闭"
    ),
    [I18N_COMMAND_OUTPUT_TITLE] = I18N_STRING(
        " COMMAND OUTPUT ",
        " 命令输出 "
    ),
    [I18N_COMMAND_OUTPUT_STATUS_FORMAT] = I18N_STRING(
        "-- COMMAND OUTPUT -- (%d/%d) j/k:scroll Ctrl-D/U:half g/G:top/bottom q:close",
        "-- 命令输出 -- (%d/%d) j/k:滚动 Ctrl-D/U:半页 g/G:首尾 q:关闭"
    ),
    [I18N_COMMAND_OUTPUT_REFRESH_STATUS_FORMAT] = I18N_STRING(
        "-- COMMAND OUTPUT -- (%d/%d) j/k:scroll Ctrl-D/U:half g/G:top/bottom r:refresh q:close",
        "-- 命令输出 -- (%d/%d) j/k:滚动 Ctrl-D/U:半页 g/G:首尾 r:刷新 q:关闭"
    ),
    [I18N_MOTD_TITLE] = I18N_STRING(
        " NOTICE ",
        " 公告 "
    ),
    [I18N_MOTD_CONTINUE_HINT] = I18N_STRING(
        " Press any key ",
        " 按任意键继续 "
    ),
    [I18N_TITLE_ONLINE_FORMAT] = I18N_STRING(
        "online %d",
        "在线 %d"
    ),
    [I18N_TITLE_MUTED] = I18N_STRING(
        "muted",
        "静音"
    ),
    [I18N_TITLE_HELP_HINT] = I18N_STRING(
        "? keys",
        "? 按键"
    ),
    [I18N_EMPTY_ROOM] = I18N_STRING(
        "No messages yet",
        "暂无消息"
    ),
    [I18N_EMPTY_FILTERED] = I18N_STRING(
        "No visible messages",
        "暂无可见消息"
    ),
    [I18N_IDLE_TIMEOUT_FORMAT] = I18N_STRING(
        "\r\n\033[33mDisconnected: idle timeout (%d min)\033[0m\r\n",
        "\r\n\033[33m已断开: 空闲超时 (%d 分钟)\033[0m\r\n"
    ),
    [I18N_SYSTEM_USERNAME] = I18N_STRING(
        "system",
        "系统"
    ),
    [I18N_SYSTEM_JOIN_FORMAT] = I18N_STRING(
        "%s joined the room",
        "%s 加入了聊天室"
    ),
    [I18N_SYSTEM_LEAVE_FORMAT] = I18N_STRING(
        "%s left the room",
        "%s 离开了聊天室"
    ),
    [I18N_SYSTEM_NICK_FORMAT] = I18N_STRING(
        "%s renamed to %s",
        "%s 更名为 %s"
    ),
    [I18N_USERS_TITLE] = I18N_STRING(
        "Online users",
        "在线用户"
    ),
    [I18N_MSG_SENT_FORMAT] = I18N_STRING(
        "Private message sent to %s\n",
        "私信已发送给 %s\n"
    ),
    [I18N_MSG_USER_NOT_FOUND_FORMAT] = I18N_STRING(
        "User '%s' not found\n",
        "未找到用户 '%s'\n"
    ),
    [I18N_REPLY_NO_TARGET] = I18N_STRING(
        "No private message to reply to\n",
        "没有可回复的私信\n"
    ),
    [I18N_INBOX_TITLE] = I18N_STRING(
        "Private messages",
        "私信"
    ),
    [I18N_INBOX_EMPTY] = I18N_STRING(
        "(empty)",
        "(空)"
    ),
    [I18N_INBOX_SENT_TO_FORMAT] = I18N_STRING(
        "you -> %s",
        "你 -> %s"
    ),
    [I18N_NICK_INVALID] = I18N_STRING(
        "Invalid username\n",
        "用户名无效\n"
    ),
    [I18N_NICK_TAKEN_FORMAT] = I18N_STRING(
        "Nickname '%s' is already taken\n",
        "昵称 '%s' 已被使用\n"
    ),
    [I18N_NICK_UNCHANGED] = I18N_STRING(
        "Nickname unchanged\n",
        "昵称未变化\n"
    ),
    [I18N_NICK_CHANGED_FORMAT] = I18N_STRING(
        "Nickname changed: %s -> %s\n",
        "昵称已修改: %s -> %s\n"
    ),
    [I18N_LAST_HEADER_FORMAT] = I18N_STRING(
        "--- Last %d message(s) ---\n",
        "--- 最近 %d 条消息 ---\n"
    ),
    [I18N_LAST_EMPTY] = I18N_STRING(
        "No messages to show\n",
        "没有可显示的消息\n"
    ),
    [I18N_SEARCH_HEADER_FORMAT] = I18N_STRING(
        "--- Search: \"%s\" (showing last %d match(es)) ---\n",
        "--- 搜索: \"%s\" (显示最近 %d 条匹配) ---\n"
    ),
    [I18N_SEARCH_EMPTY] = I18N_STRING(
        "No matches\n",
        "没有匹配结果\n"
    ),
    [I18N_MUTE_JOINS_FORMAT] = I18N_STRING(
        "Join/leave notifications: %s\n",
        "加入/离开提示: %s\n"
    ),
    [I18N_MUTE_JOINS_MUTED] = I18N_STRING(
        "muted",
        "已静音"
    ),
    [I18N_MUTE_JOINS_UNMUTED] = I18N_STRING(
        "unmuted",
        "已开启"
    ),
    [I18N_CLEAR_DONE] = I18N_STRING(
        "Command output cleared\n",
        "命令输出已清空\n"
    ),
    [I18N_LANG_CURRENT_FORMAT] = I18N_STRING(
        "Current language: %s\n"
        "Usage: lang <en|zh>\n",
        "当前语言: %s\n"
        "用法: lang <en|zh>\n"
    ),
    [I18N_LANG_SET_FORMAT] = I18N_STRING(
        "Language set to: %s\n",
        "语言已切换为: %s\n"
    ),
    [I18N_LANG_UNSUPPORTED_FORMAT] = I18N_STRING(
        "Unsupported language: %s\n"
        "Usage: lang <en|zh>\n",
        "不支持的语言: %s\n"
        "用法: lang <en|zh>\n"
    ),
    [I18N_UNKNOWN_COMMAND_FORMAT] = I18N_STRING(
        "Unknown command: %s\n",
        "未知命令: %s\n"
    ),
    [I18N_DID_YOU_MEAN_FORMAT] = I18N_STRING(
        "Did you mean :%s?\n",
        "你是想输入 :%s 吗?\n"
    ),
    [I18N_UNKNOWN_GUIDANCE] = I18N_STRING(
        "Type :help for help\n",
        "输入 :help 查看帮助\n"
    ),
    [I18N_EXEC_POST_EMPTY] = I18N_STRING(
        "post: message cannot be empty\n",
        "post: 消息不能为空\n"
    ),
    [I18N_EXEC_POST_INVALID_UTF8] = I18N_STRING(
        "post: invalid UTF-8 input\n",
        "post: 输入不是有效 UTF-8\n"
    ),
    [I18N_EXEC_POST_TOO_LONG] = I18N_STRING(
        "post: message too long\n",
        "post: 消息过长\n"
    ),
    [I18N_EXEC_POST_PERSIST_FAILED] = I18N_STRING(
        "post: failed to persist message\n",
        "post: 消息持久化失败\n"
    ),
    [I18N_EXEC_COMMAND_TOO_LONG] = I18N_STRING(
        "exec: command too long\n",
        "exec: 命令过长\n"
    ),
    [I18N_EXEC_UNKNOWN_COMMAND_FORMAT] = I18N_STRING(
        "Unknown command: %s\n",
        "未知命令: %s\n"
    )
};

const char *i18n_text(ui_lang_t lang, i18n_text_id_t id) {
    if (id < 0 || id >= I18N_TEXT_COUNT) {
        return "";
    }

    const i18n_string_t *entry = &text_catalog[id];
    return i18n_string(*entry, lang);
}
