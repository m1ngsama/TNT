#include "support.h"

void support_append_interactive_panel(char *buffer, size_t buf_size,
                                      size_t *pos) {
    if (!buffer || !pos) return;

    buffer_appendf(buffer, buf_size, pos,
                   "\033[1;36m支持 · support\033[0m\n"
                   "\n"
                   "\033[1;37m第一次进来\033[0m\n"
                   "  INSERT  输入消息，Enter 发送，ESC 进入 NORMAL\n"
                   "  NORMAL  浏览消息，G 回到最新，i 继续输入\n"
                   "  COMMAND 按 : 输入命令，q/ESC 关闭当前面板\n"
                   "\n"
                   "\033[1;37m我想...\033[0m\n"
                   "  看谁在线        :users\n"
                   "  看最近历史      :last 20\n"
                   "  搜索聊天记录    :search <keyword>\n"
                   "  回到最新消息    G 或 End\n"
                   "  私聊某个人      :msg <user> <text>\n"
                   "  查看私聊收件箱  :inbox\n"
                   "  静音进出提示    :mute-joins\n"
                   "\n"
                   "\033[1;37m遇到问题\033[0m\n"
                   "  看不到新消息: 在 NORMAL 按 G 或 End 回到最新\n"
                   "  粘贴多行文本: 直接粘贴，TNT 会等 Enter 后一次发送\n"
                   "  输入太长: 状态行接近限制时会提示，超出会响铃\n"
                   "  命令不记得: 输入 :help 看列表，输入 :support 回到这里\n"
                   "  连接断开: 可能是空闲超时、连接数限制或网络重连\n"
                   "\n"
                   "\033[2;37m更多: ? 打开完整按键帮助，:help 查看命令列表\033[0m\n");
}

void support_append_exec_panel(char *buffer, size_t buf_size, size_t *pos) {
    if (!buffer || !pos) return;

    buffer_appendf(buffer, buf_size, pos,
                   "TNT support\n"
                   "\n"
                   "Interactive use:\n"
                   "  ssh -p 2222 HOST\n"
                   "  INSERT: type and press Enter to send\n"
                   "  NORMAL: press G for latest, k/PageUp for older messages\n"
                   "  COMMAND: press : then run users, last, search, msg, inbox\n"
                   "\n"
                   "Non-interactive checks:\n"
                   "  ssh -p 2222 HOST health\n"
                   "  ssh -p 2222 HOST stats --json\n"
                   "  ssh -p 2222 HOST users --json\n"
                   "  ssh -p 2222 HOST 'tail -n 20'\n"
                   "  ssh -p 2222 USER@HOST post 'message'\n"
                   "\n"
                   "Troubleshooting:\n"
                   "  Connection closes early: check rate limits, idle timeout,\n"
                   "  global connection capacity, per-IP limits, and firewall rules.\n");
}
