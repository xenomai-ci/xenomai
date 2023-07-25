// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <libgen.h>
#include <smokey/smokey.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *const argv[])
{
       struct smokey_test *t;
       int ret, fails = 0;

       if (pvlist_empty(&smokey_test_list))
               return 0;

       for_each_smokey_test(t) {
               ret = t->run(t, argc, argv);
               if (ret) {
                       fails++;
                       if (smokey_keep_going)
                               continue;
                       if (smokey_verbose_mode)
                               error(1, -ret, "test %s failed", t->name);
                       return 1;
               }
               smokey_note("%s OK", t->name);
       }

       return fails != 0;
}
