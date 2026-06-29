/* Unit tests for per-session colour themes */

#include "../../include/theme.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("\xE2\x9C\x93\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

TEST(has_multiple_themes) {
    assert(theme_count() >= 4);
}

TEST(default_theme_is_valid_and_in_range) {
    assert(theme_default() != NULL);
    assert(theme_default_index() < theme_count());
    assert(theme_at(theme_default_index()) == theme_default());
}

TEST(every_theme_has_complete_slots) {
    for (size_t i = 0; i < theme_count(); i++) {
        const theme_t *t = theme_at(i);
        assert(t != NULL);
        assert(t->name && t->name[0] != '\0');
        assert(t->accent && t->accent[0] != '\0');
        assert(t->accent_bold && t->accent_bold[0] != '\0');
        assert(t->accent_dim && t->accent_dim[0] != '\0');
        assert(t->accent_italic && t->accent_italic[0] != '\0');
    }
}

TEST(theme_at_out_of_range_is_null) {
    assert(theme_at(theme_count()) == NULL);
    assert(theme_at(theme_count() + 100) == NULL);
}

TEST(find_known_theme) {
    const theme_t *t = theme_find("green");
    assert(t != NULL);
    assert(strcmp(t->name, "green") == 0);
}

TEST(find_default_by_name) {
    assert(theme_find(theme_default()->name) == theme_default());
}

TEST(find_unknown_or_null_is_null) {
    assert(theme_find("chartreuse") == NULL);
    assert(theme_find("") == NULL);
    assert(theme_find(NULL) == NULL);
}

TEST(resolve_clamps_out_of_range_to_default) {
    assert(theme_resolve(-1) == theme_default());
    assert(theme_resolve((int)theme_count()) == theme_default());
    assert(theme_resolve(999) == theme_default());
}

TEST(resolve_returns_selected_in_range) {
    assert(theme_resolve(0) == theme_at(0));
    assert(theme_resolve((int)theme_count() - 1) ==
           theme_at(theme_count() - 1));
}

TEST(names_list_contains_known_names) {
    char buf[256] = {0};
    size_t pos = 0;
    theme_append_names(buf, sizeof(buf), &pos);
    assert(strstr(buf, theme_default()->name) != NULL);
    assert(strstr(buf, "green") != NULL);
    assert(strstr(buf, ", ") != NULL);
}

int main(void) {
    RUN_TEST(has_multiple_themes);
    RUN_TEST(default_theme_is_valid_and_in_range);
    RUN_TEST(every_theme_has_complete_slots);
    RUN_TEST(theme_at_out_of_range_is_null);
    RUN_TEST(find_known_theme);
    RUN_TEST(find_default_by_name);
    RUN_TEST(find_unknown_or_null_is_null);
    RUN_TEST(resolve_clamps_out_of_range_to_default);
    RUN_TEST(resolve_returns_selected_in_range);
    RUN_TEST(names_list_contains_known_names);

    printf("\nAll %d theme tests passed!\n", tests_passed);
    return 0;
}
