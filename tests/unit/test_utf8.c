/* Unit tests for UTF-8 functions */
#include "../../include/utf8.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

/* Test UTF-8 byte length detection */
TEST(utf8_byte_length_ascii) {
    assert(utf8_byte_length('A') == 1);
    assert(utf8_byte_length('z') == 1);
    assert(utf8_byte_length('0') == 1);
}

TEST(utf8_byte_length_multibyte) {
    assert(utf8_byte_length(0xC3) == 2);  /* é first byte */
    assert(utf8_byte_length(0xE4) == 3);  /* 中 first byte */
    assert(utf8_byte_length(0xF0) == 4);  /* 𝕏 first byte */
}

TEST(utf8_byte_length_invalid) {
    assert(utf8_byte_length(0xFF) == 1);  /* Invalid UTF-8 */
    assert(utf8_byte_length(0x80) == 1);  /* Continuation byte */
}

/* Test UTF-8 decoding */
TEST(utf8_decode_ascii) {
    int bytes_read;
    assert(utf8_decode("A", &bytes_read) == 'A');
    assert(bytes_read == 1);
}

TEST(utf8_decode_2byte) {
    int bytes_read;
    /* é = U+00E9 = 0xC3 0xA9 */
    const char *e_acute = "\xC3\xA9";
    uint32_t codepoint = utf8_decode(e_acute, &bytes_read);
    assert(codepoint == 0x00E9);
    assert(bytes_read == 2);
}

TEST(utf8_decode_3byte) {
    int bytes_read;
    /* 中 = U+4E2D = 0xE4 0xB8 0xAD */
    const char *zhong = "\xE4\xB8\xAD";
    uint32_t codepoint = utf8_decode(zhong, &bytes_read);
    assert(codepoint == 0x4E2D);
    assert(bytes_read == 3);
}

TEST(utf8_decode_4byte) {
    int bytes_read;
    /* 𝕏 = U+1D54F = 0xF0 0x9D 0x95 0x8F */
    const char *math_x = "\xF0\x9D\x95\x8F";
    uint32_t codepoint = utf8_decode(math_x, &bytes_read);
    assert(codepoint == 0x1D54F);
    assert(bytes_read == 4);
}

/* Test character width calculation */
TEST(utf8_char_width_ascii) {
    assert(utf8_char_width('A') == 1);
    assert(utf8_char_width(' ') == 1);
    assert(utf8_char_width('0') == 1);
}

TEST(utf8_char_width_cjk) {
    assert(utf8_char_width(0x4E2D) == 2);  /* 中 */
    assert(utf8_char_width(0x6587) == 2);  /* 文 */
    assert(utf8_char_width(0x5B57) == 2);  /* 字 */
}

TEST(utf8_char_width_hangul) {
    assert(utf8_char_width(0xAC00) == 2);  /* 가 */
    assert(utf8_char_width(0xD7A3) == 2);  /* 힣 */
}

TEST(utf8_char_width_hiragana) {
    assert(utf8_char_width(0x3042) == 2);  /* あ */
    assert(utf8_char_width(0x3093) == 2);  /* ん */
}

TEST(utf8_char_width_katakana) {
    assert(utf8_char_width(0x30A2) == 2);  /* ア */
    assert(utf8_char_width(0x30F3) == 2);  /* ン */
}

/* Test string width calculation */
TEST(utf8_string_width_ascii) {
    assert(utf8_string_width("Hello") == 5);
    assert(utf8_string_width("") == 0);
    assert(utf8_string_width("Test123") == 7);
}

TEST(utf8_string_width_mixed) {
    /* "Hello世界" = 5 ASCII + 2*2 CJK = 9 */
    assert(utf8_string_width("Hello世界") == 9);

    /* "测试Test" = 2*2 CJK + 4 ASCII = 8 */
    assert(utf8_string_width("测试Test") == 8);
}

TEST(utf8_string_width_cjk_only) {
    /* "中文字符" = 4 * 2 = 8 */
    assert(utf8_string_width("中文字符") == 8);
}

TEST(utf8_ansi_string_width_ignores_escape_sequences) {
    assert(utf8_ansi_string_width("\033[1;36mHello\033[0m") == 5);
    assert(utf8_ansi_string_width("\033[31m支持\033[0m") == 4);
    assert(utf8_ansi_string_width("A\033[7;33m中\033[0mB") == 4);
}

TEST(utf8_ansi_truncate_preserves_escape_sequences) {
    char out[64];

    utf8_ansi_truncate("\033[31mHello世界\033[0m", out, sizeof(out), 7);
    assert(strcmp(out, "\033[31mHello世\033[0m") == 0);

    utf8_ansi_truncate("A\033[7;33m中文\033[0mB", out, sizeof(out), 5);
    assert(strcmp(out, "A\033[7;33m中文\033[0m") == 0);
}

/* Test backspace handling */
TEST(utf8_remove_last_char) {
    char buffer[256];

    /* Test ASCII */
    strcpy(buffer, "Hello");
    assert(utf8_remove_last_char(buffer, 5) == 4);
    assert(strcmp(buffer, "Hell") == 0);

    /* Test empty string */
    strcpy(buffer, "");
    assert(utf8_remove_last_char(buffer, 0) == 0);
    assert(strcmp(buffer, "") == 0);

    /* Test single char */
    strcpy(buffer, "A");
    assert(utf8_remove_last_char(buffer, 1) == 0);
    assert(strcmp(buffer, "") == 0);
}

TEST(utf8_remove_last_char_multibyte) {
    char buffer[256];

    /* Test 2-byte UTF-8 */
    strcpy(buffer, "café");
    assert(utf8_remove_last_char(buffer, strlen(buffer)) == 3);
    assert(strcmp(buffer, "caf") == 0);

    /* Test 3-byte UTF-8 (CJK) */
    strcpy(buffer, "你好");
    assert(utf8_remove_last_char(buffer, strlen(buffer)) == 3);
    assert(strcmp(buffer, "你") == 0);
}

/* Test word removal (Ctrl+W) */
TEST(utf8_remove_last_word) {
    char buffer[256];

    /* Test simple case */
    strcpy(buffer, "hello world");
    assert(utf8_remove_last_word(buffer, 11) == 6);
    assert(strcmp(buffer, "hello ") == 0);

    /* Test multiple words */
    strcpy(buffer, "one two three");
    assert(utf8_remove_last_word(buffer, 13) == 8);
    assert(strcmp(buffer, "one two ") == 0);

    /* Test trailing spaces */
    strcpy(buffer, "hello   ");
    assert(utf8_remove_last_word(buffer, 8) == 0);
    assert(strcmp(buffer, "") == 0);

    /* Test single word */
    strcpy(buffer, "word");
    assert(utf8_remove_last_word(buffer, 4) == 0);
    assert(strcmp(buffer, "") == 0);

    /* Test empty string */
    strcpy(buffer, "");
    assert(utf8_remove_last_word(buffer, 0) == 0);
    assert(strcmp(buffer, "") == 0);
}

/* Test input validation */
TEST(utf8_is_valid_sequence) {
    /* Valid sequences */
    assert(utf8_is_valid_sequence("A", 1) == true);
    assert(utf8_is_valid_sequence("\xC3\xA9", 2) == true);  /* é */
    assert(utf8_is_valid_sequence("\xE4\xB8\xAD", 3) == true);  /* 中 */

    /* Invalid sequences */
    assert(utf8_is_valid_sequence("\xFF", 1) == false);  /* Invalid start */
    assert(utf8_is_valid_sequence("\xC3\xFF", 2) == false);  /* Invalid continuation */

    /* Invalid lengths */
    assert(utf8_is_valid_sequence("", 0) == false);
    assert(utf8_is_valid_sequence("ABCDE", 5) == false);  /* Too long */
    assert(utf8_is_valid_sequence(NULL, 1) == false);
}

TEST(utf8_control_character_detection) {
    assert(!utf8_contains_control("plain text 中"));
    assert(!utf8_contains_control(NULL));
    assert(utf8_contains_control("line\nnext"));
    assert(utf8_contains_control("\033[2J"));
    assert(utf8_contains_control("delete\x7f"));
    assert(utf8_contains_control("next\xC2\x85line"));
    assert(utf8_contains_control("osc\xC2\x9Dpayload"));
}

/* Test boundary cases */
TEST(utf8_boundary_cases) {
    /* Maximum valid codepoints */
    assert(utf8_char_width(0x10FFFF) == 1);  /* Max Unicode codepoint */

    /* BMP boundary */
    assert(utf8_char_width(0xFFFF) == 1);

    /* CJK range boundaries */
    assert(utf8_char_width(0x4DFF) == 1);   /* Just before CJK Extension A */
    assert(utf8_char_width(0x4E00) == 2);   /* Start of CJK Unified */
    assert(utf8_char_width(0x9FFF) == 2);   /* End of CJK Unified */
    assert(utf8_char_width(0xA000) == 1);   /* Just after CJK Unified */
}

TEST(utf8_width_emoji_is_two_columns) {
    assert(utf8_string_width("😌") == 2);
    assert(utf8_string_width("🎲") == 2);
    assert(utf8_string_width("a😌b") == 4);
}

TEST(utf8_width_zero_width_codepoints) {
    /* e + U+0301 COMBINING ACUTE ACCENT renders in one column. */
    assert(utf8_string_width("e\xCC\x81") == 1);
    /* U+200B ZERO WIDTH SPACE */
    assert(utf8_string_width("\xE2\x80\x8B") == 0);
}

TEST(utf8_width_variation_selector_promotes_to_emoji) {
    /* U+2764 alone is a narrow symbol; with U+FE0F it is emoji-wide. */
    assert(utf8_string_width("\xE2\x9D\xA4") == 1);
    assert(utf8_string_width("\xE2\x9D\xA4\xEF\xB8\x8F") == 2);
}

TEST(utf8_cluster_zwj_sequence_is_one_unit) {
    const char *family = "👨\xE2\x80\x8D👩\xE2\x80\x8D👧";
    assert(utf8_cluster_length(family) == strlen(family));
    assert(utf8_cluster_width(family) == 2);
    assert(utf8_string_width(family) == 2);
}

TEST(utf8_cluster_regional_indicator_pair_is_one_flag) {
    const char *flag = "🇨🇳";
    assert(utf8_cluster_length(flag) == strlen(flag));
    assert(utf8_cluster_width(flag) == 2);
}

TEST(utf8_cluster_length_plain_characters) {
    assert(utf8_cluster_length("A") == 1);
    assert(utf8_cluster_length("中") == 3);
    assert(utf8_cluster_length("") == 0);
    assert(utf8_cluster_length(NULL) == 0);
}

TEST(utf8_truncate_never_splits_a_cluster) {
    char buf[64];

    /* Two flags, each two columns.  A three-column budget must keep one. */
    snprintf(buf, sizeof(buf), "🇨🇳🇯🇵");
    utf8_truncate(buf, 3);
    assert(strcmp(buf, "🇨🇳") == 0);

    /* A ZWJ family is indivisible: a one-column budget keeps nothing. */
    snprintf(buf, sizeof(buf), "👨\xE2\x80\x8D👩");
    utf8_truncate(buf, 1);
    assert(buf[0] == '\0');
}

int main(void) {
    printf("Running UTF-8 unit tests...\n\n");

    RUN_TEST(utf8_byte_length_ascii);
    RUN_TEST(utf8_byte_length_multibyte);
    RUN_TEST(utf8_byte_length_invalid);
    RUN_TEST(utf8_decode_ascii);
    RUN_TEST(utf8_decode_2byte);
    RUN_TEST(utf8_decode_3byte);
    RUN_TEST(utf8_decode_4byte);
    RUN_TEST(utf8_char_width_ascii);
    RUN_TEST(utf8_char_width_cjk);
    RUN_TEST(utf8_char_width_hangul);
    RUN_TEST(utf8_char_width_hiragana);
    RUN_TEST(utf8_char_width_katakana);
    RUN_TEST(utf8_string_width_ascii);
    RUN_TEST(utf8_string_width_mixed);
    RUN_TEST(utf8_string_width_cjk_only);
    RUN_TEST(utf8_ansi_string_width_ignores_escape_sequences);
    RUN_TEST(utf8_ansi_truncate_preserves_escape_sequences);
    RUN_TEST(utf8_remove_last_char);
    RUN_TEST(utf8_remove_last_char_multibyte);
    RUN_TEST(utf8_remove_last_word);
    RUN_TEST(utf8_is_valid_sequence);
    RUN_TEST(utf8_control_character_detection);
    RUN_TEST(utf8_boundary_cases);
    RUN_TEST(utf8_width_emoji_is_two_columns);
    RUN_TEST(utf8_width_zero_width_codepoints);
    RUN_TEST(utf8_width_variation_selector_promotes_to_emoji);
    RUN_TEST(utf8_cluster_zwj_sequence_is_one_unit);
    RUN_TEST(utf8_cluster_regional_indicator_pair_is_one_flag);
    RUN_TEST(utf8_cluster_length_plain_characters);
    RUN_TEST(utf8_truncate_never_splits_a_cluster);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
