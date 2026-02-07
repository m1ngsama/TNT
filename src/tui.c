#include "tui.h"
#include "ssh_server.h"
#include "chat_room.h"
#include "utf8.h"
#include <stdarg.h>
#include <unistd.h>

/* Clear the screen */
void tui_clear_screen(client_t *client) {
    if (!client || !client->connected) return;
    const char *clear = ANSI_CLEAR ANSI_HOME;
    client_send(client, clear, strlen(clear));
}

/* Render the main screen */
void tui_render_screen(client_t *client) {
    if (!client || !client->connected) return;

    char buffer[8192];
    int pos = 0;

    /* Acquire all data in one lock to prevent TOCTOU */
    pthread_rwlock_rdlock(&g_room->lock);
    int online = g_room->client_count;
    int msg_count = g_room->message_count;

    /* Calculate which messages to show */
    int msg_height = client->height - 3;
    if (msg_height < 1) msg_height = 1;

    int start = 0;
    if (client->mode == MODE_NORMAL) {
        start = client->scroll_pos;
        if (start > msg_count - msg_height) {
            start = msg_count - msg_height;
        }
        if (start < 0) start = 0;
    } else {
        /* INSERT mode: show latest */
        if (msg_count > msg_height) {
            start = msg_count - msg_height;
        }
    }

    int end = start + msg_height;
    if (end > msg_count) end = msg_count;

    /* Create snapshot of messages to display */
    message_t *msg_snapshot = NULL;
    int snapshot_count = end - start;

    if (snapshot_count > 0) {
        msg_snapshot = calloc(snapshot_count, sizeof(message_t));
        if (msg_snapshot) {
            memcpy(msg_snapshot, &g_room->messages[start],
                   snapshot_count * sizeof(message_t));
        }
    }

    pthread_rwlock_unlock(&g_room->lock);

    /* Now render using snapshot (no lock held) */

    /* Move to top (Home) - Do NOT clear screen to prevent flicker */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_HOME);

    /* Title bar */
    const char *mode_str = (client->mode == MODE_INSERT) ? "INSERT" :
                          (client->mode == MODE_NORMAL) ? "NORMAL" :
                          (client->mode == MODE_COMMAND) ? "COMMAND" : "HELP";

    char title[256];
    snprintf(title, sizeof(title),
             " 聊天室 | 在线: %d | 模式: %s | Ctrl+C 退出 | ? 帮助 ",
             online, mode_str);

    int title_width = utf8_string_width(title);
    int padding = client->width - title_width;
    if (padding < 0) padding = 0;

    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_REVERSE "%s", title);
    for (int i = 0; i < padding; i++) {
        buffer[pos++] = ' ';
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_RESET "\033[K\r\n");

    /* Render messages from snapshot */
    if (msg_snapshot) {
        for (int i = 0; i < snapshot_count; i++) {
            char msg_line[1024];
            message_format(&msg_snapshot[i], msg_line, sizeof(msg_line), client->width);
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s\033[K\r\n", msg_line);
        }
        free(msg_snapshot);
    }

    /* Fill empty lines and clear them */
    for (int i = snapshot_count; i < msg_height; i++) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\033[K\r\n");
    }

    /* Separator - use box drawing character */
    for (int i = 0; i < client->width && pos < (int)sizeof(buffer) - 10; i++) {
        const char *line_char = "─";  /* U+2500 box drawing */
        int len = strlen(line_char);
        memcpy(buffer + pos, line_char, len);
        pos += len;
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\033[K\r\n");

    /* Status/Input line */
    if (client->mode == MODE_INSERT) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "> \033[K");
    } else if (client->mode == MODE_NORMAL) {
        int total = msg_count;
        int scroll_pos = client->scroll_pos + 1;
        if (total == 0) scroll_pos = 0;
        pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                       "-- NORMAL -- (%d/%d)\033[K", scroll_pos, total);
    } else if (client->mode == MODE_COMMAND) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                       ":%s\033[K", client->command_input);
    }

    client_send(client, buffer, pos);
}

/* Render the input line */
void tui_render_input(client_t *client, const char *input) {
    if (!client || !client->connected) return;

    char buffer[2048];
    int input_width = utf8_string_width(input);

    /* Truncate from start if too long */
    char display[1024];
    strncpy(display, input, sizeof(display) - 1);
    display[sizeof(display) - 1] = '\0';

    if (input_width > client->width - 3) {
        /* Find where to start displaying */
        int excess = input_width - (client->width - 3);
        int skip_width = 0;
        const char *p = input;
        int bytes_read;

        while (*p && skip_width < excess) {
            uint32_t cp = utf8_decode(p, &bytes_read);
            skip_width += utf8_char_width(cp);
            p += bytes_read;
        }

        strncpy(display, p, sizeof(display) - 1);
    }

    /* Move to input line and clear it, then write input */
    snprintf(buffer, sizeof(buffer), "\033[%d;1H" ANSI_CLEAR_LINE "> %s",
             client->height, display);

    client_send(client, buffer, strlen(buffer));
}

/* Render the command output screen */
void tui_render_command_output(client_t *client) {
    if (!client || !client->connected) return;

    char buffer[4096];
    int pos = 0;

    /* Clear screen */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_CLEAR ANSI_HOME);

    /* Title */
    const char *title = " COMMAND OUTPUT ";
    int title_width = strlen(title);
    int padding = client->width - title_width;
    if (padding < 0) padding = 0;

    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_REVERSE "%s", title);
    for (int i = 0; i < padding; i++) {
        buffer[pos++] = ' ';
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_RESET "\r\n");

    /* Command output - use a copy to avoid strtok corruption */
    char output_copy[2048];
    strncpy(output_copy, client->command_output, sizeof(output_copy) - 1);
    output_copy[sizeof(output_copy) - 1] = '\0';

    char *line = strtok(output_copy, "\n");
    int line_count = 0;
    int max_lines = client->height - 2;

    while (line && line_count < max_lines) {
        char truncated[1024];
        strncpy(truncated, line, sizeof(truncated) - 1);
        truncated[sizeof(truncated) - 1] = '\0';

        if (utf8_string_width(truncated) > client->width) {
            utf8_truncate(truncated, client->width);
        }

        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s\r\n", truncated);
        line = strtok(NULL, "\n");
        line_count++;
    }

    client_send(client, buffer, pos);
}

/* Get help text based on language */
const char* tui_get_help_text(help_lang_t lang) {
    if (lang == LANG_EN) {
        return "TERMINAL CHAT ROOM - HELP\n"
               "\n"
               "OPERATING MODES:\n"
               "  INSERT  - Type and send messages (default)\n"
               "  NORMAL  - Browse message history\n"
               "  COMMAND - Execute commands\n"
               "\n"
               "INSERT MODE KEYS:\n"
               "  ESC        - Enter NORMAL mode\n"
               "  Enter      - Send message\n"
               "  Backspace  - Delete character\n"
               "  Ctrl+W     - Delete last word\n"
               "  Ctrl+U     - Delete line\n"
               "  Ctrl+C     - Enter NORMAL mode\n"
               "\n"
               "NORMAL MODE KEYS:\n"
               "  i          - Return to INSERT mode\n"
               "  :          - Enter COMMAND mode\n"
               "  j          - Scroll down (older messages)\n"
               "  k          - Scroll up (newer messages)\n"
               "  g          - Jump to top (oldest)\n"
               "  G          - Jump to bottom (newest)\n"
               "  ?          - Show this help\n"
               "  Ctrl+C     - Exit chat\n"
               "\n"
               "COMMAND MODE KEYS:\n"
               "  Enter      - Execute command\n"
               "  ESC        - Cancel, return to NORMAL\n"
               "  Backspace  - Delete character\n"
               "  Ctrl+W     - Delete last word\n"
               "  Ctrl+U     - Delete line\n"
               "\n"
               "AVAILABLE COMMANDS:\n"
               "  list, users, who  - Show online users\n"
               "  help, commands    - Show available commands\n"
               "  clear, cls        - Clear command output\n"
               "\n"
               "HELP SCREEN KEYS:\n"
               "  q, ESC     - Close help\n"
               "  j          - Scroll down\n"
               "  k          - Scroll up\n"
               "  g          - Jump to top\n"
               "  G          - Jump to bottom\n"
               "  e, E       - Switch to English\n"
               "  z, Z       - Switch to Chinese\n";
    } else {
        return "终端聊天室 - 帮助\n"
               "\n"
               "操作模式:\n"
               "  INSERT  - 输入和发送消息(默认)\n"
               "  NORMAL  - 浏览消息历史\n"
               "  COMMAND - 执行命令\n"
               "\n"
               "INSERT 模式按键:\n"
               "  ESC        - 进入 NORMAL 模式\n"
               "  Enter      - 发送消息\n"
               "  Backspace  - 删除字符\n"
               "  Ctrl+W     - 删除上个单词\n"
               "  Ctrl+U     - 删除整行\n"
               "  Ctrl+C     - 进入 NORMAL 模式\n"
               "\n"
               "NORMAL 模式按键:\n"
               "  i          - 返回 INSERT 模式\n"
               "  :          - 进入 COMMAND 模式\n"
               "  j          - 向下滚动(更早的消息)\n"
               "  k          - 向上滚动(更新的消息)\n"
               "  g          - 跳到顶部(最早)\n"
               "  G          - 跳到底部(最新)\n"
               "  ?          - 显示此帮助\n"
               "  Ctrl+C     - 退出聊天\n"
               "\n"
               "COMMAND 模式按键:\n"
               "  Enter      - 执行命令\n"
               "  ESC        - 取消,返回 NORMAL 模式\n"
               "  Backspace  - 删除字符\n"
               "  Ctrl+W     - 删除上个单词\n"
               "  Ctrl+U     - 删除整行\n"
               "\n"
               "可用命令:\n"
               "  list, users, who  - 显示在线用户\n"
               "  help, commands    - 显示可用命令\n"
               "  clear, cls        - 清空命令输出\n"
               "\n"
               "帮助界面按键:\n"
               "  q, ESC     - 关闭帮助\n"
               "  j          - 向下滚动\n"
               "  k          - 向上滚动\n"
               "  g          - 跳到顶部\n"
               "  G          - 跳到底部\n"
               "  e, E       - 切换到英文\n"
               "  z, Z       - 切换到中文\n";
    }
}

/* Render the help screen */
void tui_render_help(client_t *client) {
    if (!client || !client->connected) return;

    char buffer[8192];
    int pos = 0;

    /* Clear screen */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_CLEAR ANSI_HOME);

    /* Title */
    const char *title = " HELP ";
    int title_width = strlen(title);
    int padding = client->width - title_width;
    if (padding < 0) padding = 0;

    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_REVERSE "%s", title);
    for (int i = 0; i < padding; i++) {
        buffer[pos++] = ' ';
    }
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, ANSI_RESET "\r\n");

    /* Help content */
    const char *help_text = tui_get_help_text(client->help_lang);
    char help_copy[4096];
    strncpy(help_copy, help_text, sizeof(help_copy) - 1);
    help_copy[sizeof(help_copy) - 1] = '\0';

    /* Split into lines and display with scrolling */
    char *lines[100];
    int line_count = 0;
    char *line = strtok(help_copy, "\n");
    while (line && line_count < 100) {
        lines[line_count++] = line;
        line = strtok(NULL, "\n");
    }

    int content_height = client->height - 2;
    int start = client->help_scroll_pos;
    int end = start + content_height - 1;
    if (end > line_count) end = line_count;

    for (int i = start; i < end && (i - start) < content_height - 1; i++) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s\r\n", lines[i]);
    }

    /* Fill remaining lines */
    for (int i = end - start; i < content_height - 1; i++) {
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }

    /* Status line */
    int max_scroll = line_count - content_height + 1;
    if (max_scroll < 0) max_scroll = 0;

    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                   "-- HELP -- (%d/%d) j/k:scroll g/G:top/bottom e/z:lang q:close",
                   client->help_scroll_pos + 1, max_scroll + 1);

    client_send(client, buffer, pos);
}
