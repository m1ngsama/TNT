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

/* Test backspace handling */
TEST(utf8_remove_last_char) {
    char buffer[256];

    /* Test ASCII */
    strcpy(buffer, "Hello");
    utf8_remove_last_char(buffer);
    assert(strcmp(buffer, "Hell") == 0);

    /* Test empty string */
    strcpy(buffer, "");
    utf8_remove_last_char(buffer);
    assert(strcmp(buffer, "") == 0);

    /* Test single char */
    strcpy(buffer, "A");
    utf8_remove_last_char(buffer);
    assert(strcmp(buffer, "") == 0);
}

TEST(utf8_remove_last_char_multibyte) {
    char buffer[256];

    /* Test 2-byte UTF-8 */
    strcpy(buffer, "café");
    utf8_remove_last_char(buffer);
    assert(strcmp(buffer, "caf") == 0);

    /* Test 3-byte UTF-8 (CJK) */
    strcpy(buffer, "你好");
    utf8_remove_last_char(buffer);
    assert(strcmp(buffer, "你") == 0);
}

/* Test word removal (Ctrl+W) */
TEST(utf8_remove_last_word) {
    char buffer[256];

    /* Test simple case */
    strcpy(buffer, "hello world");
    utf8_remove_last_word(buffer);
    assert(strcmp(buffer, "hello ") == 0);

    /* Test multiple words */
    strcpy(buffer, "one two three");
    utf8_remove_last_word(buffer);
    assert(strcmp(buffer, "one two ") == 0);

    /* Test trailing spaces */
    strcpy(buffer, "hello   ");
    utf8_remove_last_word(buffer);
    assert(strcmp(buffer, "") == 0);

    /* Test single word */
    strcpy(buffer, "word");
    utf8_remove_last_word(buffer);
    assert(strcmp(buffer, "") == 0);

    /* Test empty string */
    strcpy(buffer, "");
    utf8_remove_last_word(buffer);
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
    RUN_TEST(utf8_remove_last_char);
    RUN_TEST(utf8_remove_last_char_multibyte);
    RUN_TEST(utf8_remove_last_word);
    RUN_TEST(utf8_is_valid_sequence);
    RUN_TEST(utf8_boundary_cases);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
