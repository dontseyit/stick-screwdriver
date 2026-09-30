// -----------------------------------------------------------------------------
//  Host tests for the spoken code.  pio test -e native
//
//  A code is the only name a recording gets on a device with no clock, so it
//  has to be pronounceable and stable: the same seed must always spell the same
//  word, or a label stops being a label.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <cstring>

#include "mathx/Words.h"

using namespace sd;

namespace {

bool isVowel(char c) { return std::strchr("AEIOU", c) != nullptr; }

}  // namespace

void test_the_same_seed_always_spells_the_same_code() {
    char a[4], b[4];
    sayCode(0xDEADBEEFu, a);
    sayCode(0xDEADBEEFu, b);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(a, b, 4));

    char c[4];
    sayCode(0xDEADBEF0u, c);
    TEST_ASSERT_TRUE(std::memcmp(a, c, 4) != 0);
}

void test_a_code_alternates_consonants_and_vowels() {
    // Every seed, not a sampled few: a code that cannot be said aloud is
    // useless, and the failure would only show on the one seed nobody tried.
    for (uint32_t i = 0; i < 20000u; ++i) {
        char out[4] = {0, 0, 0, 0};
        sayCode(i * 7919u + 13u, out);
        TEST_ASSERT_FALSE(isVowel(out[0]));
        TEST_ASSERT_TRUE(isVowel(out[1]));
        TEST_ASSERT_FALSE(isVowel(out[2]));
        TEST_ASSERT_TRUE(isVowel(out[3]));
        for (int k = 0; k < 4; ++k) {
            TEST_ASSERT_TRUE(out[k] >= 'A' && out[k] <= 'Z');
        }
    }
}

void test_the_whole_alphabet_gets_used() {
    // A code space smaller than it claims would collide far more often than
    // kCodeSpace promises, and nothing else would say so.
    bool seenCons[26] = {false}, seenVows[26] = {false};
    for (uint32_t i = 0; i < 50000u; ++i) {
        char out[4];
        sayCode(i * 2654435761u, out);
        seenCons[out[0] - 'A'] = true;
        seenVows[out[1] - 'A'] = true;
    }
    int cons = 0, vows = 0;
    for (int i = 0; i < 26; ++i) {
        if (seenCons[i]) ++cons;
        if (seenVows[i]) ++vows;
    }
    TEST_ASSERT_EQUAL_INT(18, cons);
    TEST_ASSERT_EQUAL_INT(5, vows);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cons * vows * cons * vows),
                             kCodeSpace);
}

void test_a_null_destination_is_survivable() {
    sayCode(1u, nullptr);  // must not crash; asserted by arriving here
    TEST_ASSERT_TRUE(true);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_same_seed_always_spells_the_same_code);
    RUN_TEST(test_a_code_alternates_consonants_and_vowels);
    RUN_TEST(test_the_whole_alphabet_gets_used);
    RUN_TEST(test_a_null_destination_is_survivable);
    return UNITY_END();
}
