#include "rhizofs.h"

static int
Rhizofs_readdir(const char * path, void * buf, 
    fuse_fill_dir_t filler, off_t offset, struct fuse_file_info * fi)
{
    return -EIO;    
}


static struct fuse_operations rhizofs_oper = { 
	.readdir	= Rhizofs_readdir,
};


int
Rhizofs_run(int argc, char * argv[])
{
	return fuse_main(argc, argv, &rhizofs_oper, NULL);
}
