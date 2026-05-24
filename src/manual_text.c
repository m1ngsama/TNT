#include "manual_text.h"

#include "command_catalog.h"

void manual_text_append_interactive(char *buffer, size_t buf_size,
                                    size_t *pos, help_lang_t lang) {
    if (lang == LANG_ZH) {
        buffer_appendf(buffer, buf_size, pos,
                       "\033[1;36mTNT(1) 帮助\033[0m\n"
                       "\n"
                       "\033[1;37m名称\033[0m\n"
                       "  TNT - SSH 终端聊天室\n"
                       "\n"
                       "\033[1;37m使用\033[0m\n"
                       "  输入消息并 Enter 发送；Esc 浏览历史；G 最新；i 输入\n"
                       "  : 运行命令；? 打开完整按键参考\n"
                       "\n"
                       "\033[1;37m命令\033[0m\n");
        command_catalog_append_manual(buffer, buf_size, pos, lang);
        buffer_appendf(buffer, buf_size, pos,
                       "\n"
                       "\033[1;37m语言\033[0m\n"
                       "  :lang                  显示当前语言\n"
                       "  :lang en|zh            切换语言\n"
                       "\n"
                       "\033[1;37m参见\033[0m\n"
                       "  ?                      完整按键参考\n");
        return;
    }

    buffer_appendf(buffer, buf_size, pos,
                   "\033[1;36mTNT(1) help\033[0m\n"
                   "\n"
                   "\033[1;37mName\033[0m\n"
                   "  TNT - SSH terminal chat room\n"
                   "\n"
                   "\033[1;37mUse\033[0m\n"
                   "  Type a message and press Enter; Esc browses; G latest; i types\n"
                   "  : runs commands; ? opens the full key reference\n"
                   "\n"
                   "\033[1;37mCommands\033[0m\n");
    command_catalog_append_manual(buffer, buf_size, pos, lang);
    buffer_appendf(buffer, buf_size, pos,
                   "\n"
                   "\033[1;37mLanguage\033[0m\n"
                   "  :lang                  show current language\n"
                   "  :lang en|zh            switch language\n"
                   "\n"
                   "\033[1;37mSee also\033[0m\n"
                   "  ?                      full key reference\n");
}
