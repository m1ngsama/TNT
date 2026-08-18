#include "common.h"

#include <assert.h>
#include <stdio.h>

static void test_username_validation(void) {
    assert(is_valid_username("alice"));
    assert(is_valid_username("用户"));
    assert(!is_valid_username(""));
    assert(!is_valid_username("-alice"));
    assert(!is_valid_username("ali|ce"));
    assert(!is_valid_username("ali\tce"));
    assert(!is_valid_username("ali\x7f" "ce"));
    assert(!is_valid_username("ali\xC2\x85" "ce"));
    assert(!is_valid_username("ali\xC2\x9B" "ce"));
    assert(!is_valid_username("*"));
    assert(!is_valid_username("system"));
    assert(!is_valid_username("系统"));
    assert(!is_valid_username("module:roll-module"));
}

int main(void) {
    printf("Running common unit tests...\n\n");
    test_username_validation();
    printf("All 1 common tests passed.\n");
    return 0;
}
