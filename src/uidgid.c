#include "uidgid.h"
#include "dbg.h"

#include "unistd.h"
#include "stdio.h"
#include "stdlib.h"



int
uidgid_in_group(gid_t gid, int * result)
{
    int n_groups;
    int i;
    gid_t *group_ids = NULL;

    n_groups = getgroups(0, NULL);
    check((n_groups != -1), "Could not get group list");

    group_ids = calloc(sizeof(gid_t),n_groups);
    check_mem(group_ids);
    check((getgroups(n_groups, group_ids) != -1), "Could not fetch groups");

    (*result) = 0;
    for (i=0; i<n_groups; i++) {
        if (group_ids[i] == gid) {
            (*result) = 1;
            break;
        }
    }

    free(group_ids);
    return 0;

error:

    free(group_ids);
    return -1;
}
