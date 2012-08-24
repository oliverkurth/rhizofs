#include "posix.h"

#include "dbg.h"


int
posix_current_user_in_group(gid_t gid)
{
    int n_groups;
    gid_t * group_ids = NULL;
    int is_in_group = 0;

    n_groups = getgroups(0, NULL);
    check((n_groups != -1), "Could not get group list");

    group_ids = calloc(sizeof(gid_t),n_groups);
    check_mem(group_ids);
    check((getgroups(n_groups, group_ids) != -1), "Could not fetch groups");

    for (int i=0; i<n_groups; i++) {
        if (group_ids[i] == gid) {
            is_in_group = 1;
            break;
        }
    }
    free(group_ids);
    return is_in_group;

error:
    free(group_ids);
    return -1;
}
