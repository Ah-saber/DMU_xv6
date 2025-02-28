#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
    //static char buf[DIRSIZ+1];
    char *p;

    for(p=path+strlen(path); p >= path && *p != '/'; p --);
    p ++;

    //printf("in fmtname %s\n", p);

    return p;
}


void 
find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    //printf("path: %s\n %d", path, sizeof(path));
    if((fd = open(path, 0)) < 0){
		fprintf(2, "find: cannot open %s\n", path);
		return;
	}

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
        case T_FILE:
            if(strcmp(target, fmtname(path)) == 0){
                printf("%s\n", path);
            }
		    break;
        case T_DIR:
            if(strcmp(target, fmtname(path)) == 0){
                //if match
                printf("%s\n", path);
            }
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
                printf("find: path too long\n");
                break;
              }
            strcpy(buf, path);
            p = buf+strlen(buf);
            if(strcmp(buf, "/") != 0){
                *p++ = '/';
            }
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0 || !strcmp(de.name, ".") || !strcmp(de.name, ".."))
                    continue;
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                
                //printf("now in %s, is ./sh %d\n", buf, strcmp("./sh", buf));
               
                find(buf, target); // 递归查找
                
            }
            break;
    }
    close(fd);    //记得关描述符，否则报错，实验报告点
}

int
main(int argc, char **argv)
{
    if(argc < 2){
        printf("find: miss filename");
        exit(0);
    }
    
    if(argc < 3){
        find("", argv[1]);
        exit(0);
    }

    find(argv[1], argv[2]);
    exit(0);
}

