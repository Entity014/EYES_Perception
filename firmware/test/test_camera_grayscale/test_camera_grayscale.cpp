#include <unity.h>
#include "camera.h"

void test_grayscale_mode_map(void)
{
    TEST_ASSERT_EQUAL_INT(2, cam::grayscaleSpecialEffect(true));
    TEST_ASSERT_EQUAL_INT(0, cam::grayscaleSpecialEffect(false));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_grayscale_mode_map);
    return UNITY_END();
}
