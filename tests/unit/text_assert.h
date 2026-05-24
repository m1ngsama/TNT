#ifndef TEST_TEXT_ASSERT_H
#define TEST_TEXT_ASSERT_H

#include <assert.h>

static void assert_ascii_angle_placeholders(const char *text) {
    int in_placeholder = 0;

    while (text && *text) {
        unsigned char ch = (unsigned char)*text;

        if (ch == '<') {
            in_placeholder = 1;
        } else if (ch == '>') {
            in_placeholder = 0;
        } else if (in_placeholder) {
            assert(ch < 128);
        }

        text++;
    }
}

#endif /* TEST_TEXT_ASSERT_H */
