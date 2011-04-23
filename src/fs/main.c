#include "dbg.h"
#include "fs/rhizofs.h"

FILE *LOG_FILE = NULL;

int main(int argc, char *argv[])
{
    LOG_FILE = stderr;


	umask(0);
	return Rhizofs_run(argc, argv);
}
