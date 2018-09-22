#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


/*
   DT_BLK      This is a block device.
   DT_CHR      This is a character device.
   DT_DIR      This is a directory.
   DT_FIFO     This is a named pipe (FIFO).
   DT_LNK      This is a symbolic link.
   DT_REG      This is a regular file.
   DT_SOCK     This is a UNIX domain socket.
   DT_UNKNOWN  The file type is unknown.
   */

void walk(void)
{
	DIR *d;
	struct dirent *dir;

	d = opendir(".");

	if (d) {
		while ((dir = readdir(d)) != NULL) {
			struct stat sb;

			printf("%d %s\n", dir->d_ino, dir->d_name);
			lstat(dir->d_name, &sb);

			switch (sb.st_mode & S_IFMT) {
				case S_IFBLK:  printf("block device\n");            break;
				case S_IFCHR:  printf("character device\n");        break;
				case S_IFDIR: 
					       if (strcmp(dir->d_name, ".") == 0)
						       break;
					       if (strcmp(dir->d_name, "..") == 0)
							break;
					       printf("directory\n");
					       chdir(dir->d_name);
					       walk();
					       chdir("..");
					       break;
				case S_IFIFO:  printf("FIFO/pipe\n");               break;
				case S_IFLNK:  printf("symlink\n");                 break;
				case S_IFREG:  printf("regular file\n");            break;
				case S_IFSOCK: printf("socket\n");                  break;
				default:       printf("unknown?\n");                break;
			}

		}
		closedir(d);
	}
}

int main()
{
	walk();
}
