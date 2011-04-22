#include "dbg.h"
#include "fs/rhizofs.h"

FILE *LOG_FILE = NULL;

int main(int argc, char *argv[])
{
    // log to stdout
    LOG_FILE = stdout;


	umask(0);
	return Rhizofs_run(argc, argv);
}
