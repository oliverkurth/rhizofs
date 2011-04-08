#include "io.h"

// check for memory and set response error on failure
#define check_mem_response(A) if(!(A)) { log_err("Out of memory."); response->errortype = RHIZOFS__ERROR_TYPE__NO_MEMORY ; errno=0; ; goto error; }


int
io_readdir(Rhizofs__Response **resp, const char* path)
{
    DIR *dir = NULL;
    struct dirent *de;
    size_t entry_count = 0;
    int i;
    Rhizofs__Response * response = (*resp);

    dir = opendir(path);
    if (dir == NULL) {
        response->fs_errno = errno;
        log_warn("Could not open directory %s", path);
    }

    // count the entries in the directory
    while (readdir(dir) != NULL) {
        ++entry_count;
    }

    response->directory_entries = (char**)calloc(sizeof(char *), entry_count);
    check_mem_response(response->directory_entries);

    rewinddir(dir);
    i = 0;
    while ((de = readdir(dir)) != NULL) {
        debug("found directory entry %s",  de->d_name);

        response->directory_entries[i] = (char *)calloc(sizeof(char), (strlen(de->d_name)+1) );
        check_mem_response(response->directory_entries[i]);
        strcpy(response->directory_entries[i], de->d_name);

        ++i;
    }
    response->n_directory_entries = i;

    closedir(dir);
    return 0;

error:

    // leave the directory_entries when an error occurs.
    // they will get free'd when calling Response_destroy

    if (dir != NULL) {
        closedir(dir);
    }
    return -1;
}
