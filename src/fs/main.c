#include <sys/stat.h>

#include "../dbg.h"
#include "../fs/rhizofs.h"


int main(int argc, char *argv[])
{
    // logging configuration
    dbg_set_logfile(NULL);
    dbg_enable_syslog();

    umask(0);
    return Rhizofs_run(argc, argv);
}
