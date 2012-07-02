#include "../dbg.h"
#include "../fs/rhizofs.h"


int main(int argc, char *argv[])
{
    dbg_set_logfile(stderr);
    dbg_enable_syslog();

    umask(0);
    return Rhizofs_run(argc, argv);
}
