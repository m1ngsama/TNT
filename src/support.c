#include "support.h"

void support_append_interactive_panel(char *buffer, size_t buf_size,
                                      size_t *pos) {
    if (!buffer || !pos) return;

    buffer_appendf(buffer, buf_size, pos,
                   "\033[1;36m支持 · support\033[0m\n"
                   "\n"
                   "\033[1;37m快速开始\033[0m\n"
                   "  INSERT  输入消息，Enter 发送，ESC 进入 NORMAL\n"
                   "  NORMAL  浏览消息，G 回到最新，i 继续输入\n"
                   "  COMMAND 按 : 输入命令，q/ESC 关闭当前面板\n"
                   "\n"
                   "\033[1;37m常用动作\033[0m\n"
                   "  :users              查看在线用户\n"
                   "  :last 20            查看最近 20 条历史\n"
                   "  :search <keyword>   搜索聊天记录\n"
                   "  :msg <user> <text>  私聊\n"
                   "  :inbox              查看私聊收件箱\n"
                   "  :mute-joins         静音加入/离开提示\n"
                   "\n"
                   "\033[1;37m遇到问题\033[0m\n"
                   "  看不到新消息: 在 NORMAL 按 G 或 End 回到最新\n"
                   "  粘贴多行文本: 直接粘贴，TNT 会等 Enter 后一次发送\n"
                   "  输入太长: 状态行接近限制时会提示，超出会响铃\n"
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
