//COSC 361 Fall 2018
//FUSE Project Template
//Ben Siravantha
//bsiravan
//3/29/2019

#ifndef __cplusplus
#error "You must compile this using C++"
#endif
#include <fuse.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fs.h>
#include <map>
#include <vector>
#include <string.h>
#include <algorithm>

using namespace std;

//Use debugf() and NOT printf() for your messages.
//Uncomment #define DEBUG in block.h if you want messages to show

//Here is a list of error codes you can return for
//the fs_xxx() functions
//
//EPERM          1      /* Operation not permitted */
//ENOENT         2      /* No such file or directory */
//ESRCH          3      /* No such process */
//EINTR          4      /* Interrupted system call */
//EIO            5      /* I/O error */
//ENXIO          6      /* No such device or address */
//ENOMEM        12      /* Out of memory */
//EACCES        13      /* Permission denied */
//EFAULT        14      /* Bad address */
//EBUSY         16      /* Device or resource busy */
//EEXIST        17      /* File exists */
//ENOTDIR       20      /* Not a directory */
//EISDIR        21      /* Is a directory */
//EINVAL        22      /* Invalid argument */
//ENFILE        23      /* File table overflow */
//EMFILE        24      /* Too many open files */
//EFBIG         27      /* File too large */
//ENOSPC        28      /* No space left on device */
//ESPIPE        29      /* Illegal seek */
//EROFS         30      /* Read-only file system */
//EMLINK        31      /* Too many links */
//EPIPE         32      /* Broken pipe */
//ENOTEMPTY     36      /* Directory not empty */
//ENAMETOOLONG  40      /* The name given is too long */

//Use debugf and NOT printf() to make your
//debug outputs. Do not modify this function.
#if defined(DEBUG)
int debugf(const char *fmt, ...)
{
	int bytes = 0;
	va_list args;
	va_start(args, fmt);
	bytes = vfprintf(stderr, fmt, args);
	va_end(args);
	return bytes;
}
#else
int debugf(const char *fmt, ...)
{
	return 0;
}
#endif

//when using map.find, it will compare pointers instead of contents
//overloaded operator compares strings in map instead of addresses of pointers
struct compareString{
	bool operator()(char *left, char *right){
		return strcmp(left, right) < 0;
	}
};

//global structures to hold info
map<char *, PNODE, compareString> inodes;
vector<PBLOCK> blocks;
vector<PBLOCK> free_blocks; //keep track of free blocks when deleted
BLOCK_HEADER header;

//////////////////////////////////////////////////////////////////
//
// START HERE W/ fs_drive()
//
//////////////////////////////////////////////////////////////////
//Read the hard drive file specified by dname
//into memory. You may have to use globals to store
//the nodes and / or blocks.
//Return 0 if you read the hard drive successfully (good MAGIC, etc).
//If anything fails, return the proper error code (-EWHATEVER)
//Right now this returns -EIO, so you'll get an Input/Output error
//if you try to run this program without programming fs_drive.
//////////////////////////////////////////////////////////////////
int fs_drive(const char *dname)
{
	debugf("fs_drive: %s\n", dname);
	//open drive
	int fd = open(dname, O_RDONLY);
	if(fd < 0) return -ENXIO;
	
	//read header
	//PBLOCK_HEADER header;
	int input = read(fd, &header, sizeof(BLOCK_HEADER));
	if(strncmp(header.magic, MAGIC, 8) != 0){
		return -ENXIO;
	}
	//read nodes
	uint64_t num_blocks;
	for(uint64_t i = 0; i < header.nodes; i++){
		PNODE n = (PNODE)malloc(ONDISK_NODE_SIZE);
		input = read(fd, n, ONDISK_NODE_SIZE);
		if(input == 0) return -EIO;
		if(n->size == 0) num_blocks = 0;
		else{
			num_blocks = n->size/header.block_size + 1;
		}
		n->blocks = (uint64_t *)malloc(sizeof(uint64_t) * num_blocks);
		input = read(fd, n->blocks, sizeof(uint64_t) * num_blocks);
		
		inodes[n->name] = n;
	}

	//store blocks
	for(uint64_t j = 0; j < header.blocks; j++){
		PBLOCK b = (PBLOCK)malloc(sizeof(BLOCK));
		b->data = (char *)malloc(header.block_size);
		input = read(fd, b->data, header.block_size);
		if(input == 0) return -EIO;
		
		blocks.push_back(b);
	}

	//print out map values
	map<char *,PNODE>::iterator it;
	for(it = inodes.begin(); it != inodes.end(); it++){
		debugf("printing: %s\n", it->first);
	}

	close(fd);
	
	return 0;
}

//////////////////////////////////////////////////////////////////
//Open a file <path>. This really doesn't have to do anything
//except see if the file exists. If the file does exist, return 0,
//otherwise return -ENOENT
//////////////////////////////////////////////////////////////////
int fs_open(const char *path, struct fuse_file_info *fi)
{
	debugf("fs_open: %s\n", path);
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()){
		return -ENOENT;
	}
	
	return 0;
}

//////////////////////////////////////////////////////////////////
//Read a file <path>. You will be reading from the block and
//writing into <buf>, this buffer has a size of <size>. You will
//need to start the reading at the offset given by <offset> and
//write the data into *buf up to size bytes.
//////////////////////////////////////////////////////////////////
int fs_read(const char *path, char *buf, size_t size, off_t offset,
	    struct fuse_file_info *fi)
{
	debugf("fs_read: %s\n", path);
	
	//find node
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	PNODE n = it->second;
	uint64_t num_blocks = n->size/header.block_size + 1;	
	
	vector<PBLOCK> file_blocks;
	//loop through offsets
	//i like vectors
	for(uint64_t i = 0; i < num_blocks; i++){
		file_blocks.push_back(blocks[n->blocks[i]]);
	}

	//find what block offset starts on
	int block_index = 0;
	while(offset > header.block_size){
		offset = offset - header.block_size;
		block_index++;
	}
	
	//check if file is smaller than bytes to read
	if(n->size < size) size = n->size;
	int bytes = 0;
	int total_bytes = 0;
	//find out bytes needed to read, and loop until no more bytes
	while(size > 0){
		bytes = header.block_size - offset;
		//check if whole block is needed
		if(size < bytes) bytes = size;
		debugf("Bytes to read: %d\n", bytes);
		debugf("Size to read: %d\n", size);

		memcpy(buf + total_bytes, file_blocks[block_index]->data + offset, bytes);
		size = size - bytes;
		offset = 0;
		block_index++;
		total_bytes += bytes;
	}
	
	n->atime = time(NULL);

	return total_bytes;
}

//////////////////////////////////////////////////////////////////
//Write a file <path>. If the file doesn't exist, it is first
//created with fs_create. You need to write the data given by
//<data> and size <size> into this file block. You will also need
//to write data starting at <offset> in your file. If there is not
//enough space, return -ENOSPC. Finally, if we're a read only file
//system (fi->flags & O_RDONLY), then return -EROFS
//If all works, return the number of bytes written.
//////////////////////////////////////////////////////////////////
int fs_write(const char *path, const char *data, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
	debugf("fs_write: %s\n", path);

	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	PNODE n = it->second;
	uint64_t num_blocks = n->size/header.block_size + 1;
	
	vector<PBLOCK> file_blocks;
	//loop through offsets
	for(uint64_t i = 0; i < num_blocks; i++){
		file_blocks.push_back(blocks[n->blocks[i]]);
	}

	//calculate blocks to create
	uint64_t blocks_to_create = ((size + offset)/header.block_size + 1) - num_blocks;
	for(uint64_t j = 0; j < blocks_to_create; j++){
		//allocate new blocks to current file
		PBLOCK new_block = (PBLOCK)malloc(sizeof(BLOCK));
		blocks.push_back(new_block);
		file_blocks.push_back(blocks[header.blocks]);
		n->blocks = (uint64_t *)realloc(n->blocks, (num_blocks + 1) * sizeof(uint64_t));
		n->blocks[num_blocks] = header.blocks;
		num_blocks++;
		header.blocks++;
	}

	//find what block offset starts on
	int block_index = 0;
	while(offset > header.block_size){
		offset = offset - header.block_size;
		block_index++;
	}

	int bytes = 0;
	int total_bytes = 0;
	//write bytes
	while(size > 0){
		bytes = header.block_size - offset;
		if(size < bytes) bytes = size;
		memcpy(file_blocks[block_index]->data + offset, data + total_bytes, bytes);
		size = size - bytes;
		offset = 0;
		block_index++;
		total_bytes += bytes;
	}

	n->size += total_bytes;
	n->atime = time(NULL);
	n->mtime = time(NULL);

	return total_bytes;
}

//////////////////////////////////////////////////////////////////
//Create a file <path>. Create a new file and give it the mode
//given by <mode> OR'd with S_IFREG (regular file). If the name
//given by <path> is too long, return -ENAMETOOLONG. As with
//fs_write, if we're a read only file system
//(fi->flags & O_RDONLY), then return -EROFS.
//Otherwise, return 0 if all succeeds.
//////////////////////////////////////////////////////////////////
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	debugf("fs_create: %s\n", path);
	
	//make sure it doesn't already exist
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it != inodes.end()) return -EEXIST;

	//make sure name isn't too long
	if(strlen(path) > NAME_SIZE) return -ENAMETOOLONG;

	//make new node
	PNODE n = (PNODE)malloc(ONDISK_NODE_SIZE);
	strcpy(n->name, path);
	n->uid = getuid();
	n->gid = getgid();
	n->mode = mode | S_IFREG;
	n->ctime = time(NULL);
	n->atime = time(NULL);
	n->mtime = time(NULL);
	n->size = 0;

	//make new block
	PBLOCK b = (PBLOCK)malloc(sizeof(BLOCK));
	blocks.push_back(b);
	n->blocks = (uint64_t *)malloc(sizeof(uint64_t));
	n->blocks[0] = header.blocks;
	header.blocks++;

	//insert it
	inodes[n->name] = n;

	return 0;
}

//////////////////////////////////////////////////////////////////
//Get the attributes of file <path>. A static structure is passed
//to <s>, so you just have to fill the individual elements of s:
//s->st_mode = node->mode
//s->st_atime = node->atime
//s->st_uid = node->uid
//s->st_gid = node->gid
// ...
//Most of the names match 1-to-1, except the stat structure
//prefixes all fields with an st_*
//Please see stat for more information on the structure. Not
//all fields will be filled by your filesystem.
//////////////////////////////////////////////////////////////////
int fs_getattr(const char *path, struct stat *s)
{
	debugf("fs_getattr: %s\n", path);
	
	//find node
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()){ debugf("poop\n");return -ENOENT;}
	PNODE n = it->second;
	
	debugf("getattr: %s\n", it->first);
	//set stat
	s->st_mode = n->mode;
	s->st_ctime = n->ctime;
	s->st_atime = n->atime;
	s->st_mtime = n->mtime;
	s->st_uid = n->uid;
	s->st_gid = n->gid;
	s->st_size = n->size;
	s->st_nlink = 1;

	return 0;
}

//////////////////////////////////////////////////////////////////
//Read a directory <path>. This uses the function <filler> to
//write what directories and/or files are presented during an ls
//(list files).
//
//filler(buf, "somefile", 0, 0);
//
//You will see somefile when you do an ls
//(assuming it passes fs_getattr)
//////////////////////////////////////////////////////////////////
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	       off_t offset, struct fuse_file_info *fi)
{
	debugf("fs_readdir: %s\n", path);
	
	//filler(buf, <name of file/directory>, 0, 0)
	filler(buf, ".", 0, 0);
	filler(buf, "..", 0, 0);

	//You MUST make sure that there is no front slashes in the name (second parameter to filler)
	//Otherwise, this will FAIL.
	
	vector<char *> added; //check if added already
	char file[NAME_SIZE];
	char *tok;
	map<char *,PNODE>::iterator it;
	for(it = inodes.begin(); it != inodes.end(); it++){
		//check if path is a part of name
		if(strncmp(it->first, path, strlen(path)) == 0){
			strcpy(file, it->first + strlen(path));
			tok = strtok(file, "/");
			if(tok){ 
				//check if already added
				bool found = false;
				for(int i = 0; i < added.size(); i++){
					if(strcmp(added[i], tok) == 0){
						found = true;
					}
				}
				if(found == false){
					added.push_back(strdup(tok));
					debugf("%s\n", tok);
					filler(buf, tok, 0, 0);
				}
			}
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////
//Open a directory <path>. This is analagous to fs_open in that
//it just checks to see if the directory exists. If it does,
//return 0, otherwise return -ENOENT
//////////////////////////////////////////////////////////////////
int fs_opendir(const char *path, struct fuse_file_info *fi)
{
	debugf("fs_opendir: %s\n", path);

	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()){
		return -ENOENT;
	}
	
	return 0;
}

//////////////////////////////////////////////////////////////////
//Change the mode (permissions) of <path> to <mode>
//////////////////////////////////////////////////////////////////
int fs_chmod(const char *path, mode_t mode)
{
	//find node
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	it->second->mode = (uint64_t)mode;
	return 0;
}

//////////////////////////////////////////////////////////////////
//Change the ownership of <path> to user id <uid> and group id <gid>
//////////////////////////////////////////////////////////////////
int fs_chown(const char *path, uid_t uid, gid_t gid)
{
	debugf("fs_chown: %s\n", path);
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	it->second->uid = (uint64_t)uid;
	it->second->gid = (uint64_t)gid;
	return -EIO;
}

//////////////////////////////////////////////////////////////////
//Unlink a file <path>. This function should return -EISDIR if a
//directory is given to <path> (do not unlink directories).
//Furthermore, you will not need to check O_RDONLY as this will
//be handled by the operating system.
//Otherwise, delete the file <path> and return 0.
//////////////////////////////////////////////////////////////////
int fs_unlink(const char *path)
{
	debugf("fs_unlink: %s\n", path);
	
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	if(it->second->mode == S_IFDIR) return -EISDIR;

	//delete blocks associated with file
	uint64_t num_blocks = it->second->size / header.block_size + 1;
	for(uint64_t i = 0; i < num_blocks; i++){
		free_blocks.push_back(blocks[it->second->blocks[i]]);
	}

	inodes.erase(it);
	
	return 0;
}

//////////////////////////////////////////////////////////////////
//Make a directory <path> with the given permissions <mode>. If
//the directory already exists, return -EEXIST. If this function
//succeeds, return 0.
//////////////////////////////////////////////////////////////////
int fs_mkdir(const char *path, mode_t mode)
{
	debugf("fs_mkdir: %s\n", path);
	
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it != inodes.end()) return -EEXIST;

	PNODE n = (PNODE)malloc(ONDISK_NODE_SIZE);
	strcpy(n->name, path);
	n->mode = S_IFDIR | mode;
	n->size = 0;
	n->ctime = time(NULL);
	n->atime = time(NULL);
	n->mtime = time(NULL);

	inodes[n->name] = n;
	
	return 0;
}

//////////////////////////////////////////////////////////////////
//Remove a directory. You have to check to see if it is
//empty first. If it isn't, return -ENOTEMPTY, otherwise
//remove the directory and return 0.
//////////////////////////////////////////////////////////////////
int fs_rmdir(const char *path)
{
	debugf("fs_rmdir: %s\n", path);
	
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	if(it->second->mode != S_IFDIR) return -ENOTDIR;

	//check if it is empty
	//
	//
	
	//remove
	inodes.erase(it);

	return 0;
}

//////////////////////////////////////////////////////////////////
//Rename the file given by <path> to <new_name>
//Both <path> and <new_name> contain the full path. If
//the new_name's path doesn't exist return -ENOENT. If
//you were able to rename the node, then return 0.
//////////////////////////////////////////////////////////////////
int fs_rename(const char *path, const char *new_name)
{
	debugf("fs_rename: %s -> %s\n", path, new_name);
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;
	
	//check if valid name
	//
	//
	strcpy(it->second->name, new_name);

	return 0;
}

//////////////////////////////////////////////////////////////////
//Reduce the size of the file <path> down to size. This will
//potentially remove blocks from the file. Make sure you properly
//handle reducing the size of a file and properly updating
//the node with the number of blocks it will take. Return 0 
//if successful
//////////////////////////////////////////////////////////////////
int fs_truncate(const char *path, off_t size)
{
	debugf("fs_truncate: %s to size %d\n", path, size);
	
	map<char *,PNODE>::iterator it = inodes.find((char *)path);
	if(it == inodes.end()) return -ENOENT;

	PNODE n = it->second;
	uint64_t num_blocks = n->size/header.block_size + 1;
	uint64_t new_num_blocks = size/header.block_size + 1;

	n->blocks = (uint64_t *)realloc(n->blocks, new_num_blocks);
	
	return 0;
}

//////////////////////////////////////////////////////////////////
//fs_destroy is called when the mountpoint is unmounted
//this should save the hard drive back into <filename>
//////////////////////////////////////////////////////////////////
void fs_destroy(void *ptr)
{
	const char *filename = (const char *)ptr;
	char *test_filename = "hard_drive2";
	debugf("fs_destroy: %s\n", filename);

	//Save the internal data to the hard drive
	//specified by <filename>
	
	//open drive
	int fd = open(test_filename, O_WRONLY);
	write(fd, &header, sizeof(header));

	//write nodes
	map<char *, PNODE>::iterator it;
	for(it = inodes.begin(); it != inodes.end(); it++){
		write(fd, it->second, ONDISK_NODE_SIZE);
		//write block offsets
		uint64_t num_blocks = it->second->size/header.block_size + 1;
		write(fd, it->second->blocks, num_blocks * sizeof(uint64_t));
	}
	
	//write blocks
	for(int i = 0; i < blocks.size(); i++){
		write(fd, blocks[i], header.block_size);		
	}

}

//////////////////////////////////////////////////////////////////
//int main()
//DO NOT MODIFY THIS FUNCTION
//////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
	fuse_operations *fops;
	char *evars[] = { "./fs", "-f", "mnt", NULL };
	int ret;

	if ((ret = fs_drive(HARD_DRIVE)) != 0) {
		debugf("Error reading hard drive: %s\n", strerror(-ret));
		return ret;
	}
	//FUSE operations
	fops = (struct fuse_operations *) calloc(1, sizeof(struct fuse_operations));
	fops->getattr = fs_getattr;
	fops->readdir = fs_readdir;
	fops->opendir = fs_opendir;
	fops->open = fs_open;
	fops->read = fs_read;
	fops->write = fs_write;
	fops->create = fs_create;
	fops->chmod = fs_chmod;
	fops->chown = fs_chown;
	fops->unlink = fs_unlink;
	fops->mkdir = fs_mkdir;
	fops->rmdir = fs_rmdir;
	fops->rename = fs_rename;
	fops->truncate = fs_truncate;
	fops->destroy = fs_destroy;

	debugf("Press CONTROL-C to quit\n\n");

	return fuse_main(sizeof(evars) / sizeof(evars[0]) - 1, evars, fops,
			 (void *)HARD_DRIVE);
}
