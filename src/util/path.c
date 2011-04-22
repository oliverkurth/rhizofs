#include "path.h"

#define PATH_SEP '/'

int
path_join(const char * p1, const char * p2, char ** pj)
{
    if ((!p1) || (!p2)) {
        return -1;
    }

    int p1_l = strlen(p1);
    int p2_l = strlen(p2);
    int pj_l = 0;
    int add_sep = 0;

    if ((p1_l != 0)) {
        pj_l = p1_l;
        if (p1[p1_l-1] != PATH_SEP) {
            ++pj_l;
            add_sep = 1;
        }
    }
    if ((p2_l != 0)) {
        pj_l += p2_l;
        if (p2[0] == PATH_SEP) {
            --pj_l;
            add_sep = 0;
        }
    }

    *pj = calloc(sizeof(char *), pj_l+1);
    if (pj == NULL) {
        return -1;
    }
    strcpy(*pj, p1);
    if (add_sep==1) {
        (*pj)[p1_l] = PATH_SEP;
    }
    strcpy((*pj)+(sizeof(char)*(p1_l+add_sep)), p2);

    return 0;
}



int
path_join_real(const char * p1, const char * p2, char ** pj)
{
    char * realp = NULL;
    int rc = 0;

    if ((rc = path_join(p1, p2, &realp)) != 0) {
        return rc;
    } 

    if (((*pj) = realpath(realp, NULL)) == NULL) {
        rc = -1;
    }
    free(realp);
    return rc;
}
