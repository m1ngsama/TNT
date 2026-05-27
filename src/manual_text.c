#include "manual_text.h"

#include "command_catalog.h"
#include "i18n.h"

void manual_text_append_interactive(char *buffer, size_t buf_size,
                                    size_t *pos, ui_lang_t lang) {
    static const i18n_string_t intro = I18N_STRING(
        "\033[1;36mTNT(1) help\033[0m\n"
        "\n"
        "\033[1;37mName\033[0m\n"
        "  TNT - SSH terminal chat room\n"
        "\n"
        "\033[1;37mUse\033[0m\n"
        "  Type, Enter sends; Up/Down recalls; Tab completes @mentions\n"
        "  Esc browses; / searches; G latest; i types; : commands; ? keys\n"
        "\n"
        "\033[1;37mCommands\033[0m\n",
        "\033[1;36mTNT(1) 帮助\033[0m\n"
        "\n"
        "\033[1;37m名称\033[0m\n"
        "  TNT - SSH 终端聊天室\n"
        "\n"
        "\033[1;37m使用\033[0m\n"
        "  输入并 Enter 发送；Up/Down 调出消息；Tab 补全 @mention\n"
        "  Esc 浏览；/ 搜索；G 最新；i 输入；: 命令；? 按键\n"
        "\n"
        "\033[1;37m命令\033[0m\n"
    );
    static const i18n_string_t outro = I18N_STRING(
        "\n"
        "\033[1;37mLanguage\033[0m\n"
        "  :lang                  show current language\n"
        "  :lang en|zh            switch language\n"
        "\n"
        "\033[1;37mSee also\033[0m\n"
        "  ?                      full key reference\n",
        "\n"
        "\033[1;37m语言\033[0m\n"
        "  :lang                  显示当前语言\n"
        "  :lang en|zh            切换语言\n"
        "\n"
        "\033[1;37m参见\033[0m\n"
        "  ?                      完整按键参考\n"
    );

    buffer_appendf(buffer, buf_size, pos, "%s", i18n_string(intro, lang));
    command_catalog_append_manual(buffer, buf_size, pos, lang);
    buffer_appendf(buffer, buf_size, pos, "%s", i18n_string(outro, lang));
}
