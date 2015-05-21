/* main.c -- transpress main

   multiprocess compression format converter

   Copyright (C) 2010 bryanr@bryanr.org
   License: GPLv2

   revision history:
   2010-10-10 initial
   2010-10-23 xz + null (for decoder benchmarking)
*/

#define _BSD_SOURCE
#define _XOPEN_SOURCE 600

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <bzlib.h>
#include <zlib.h>
#include <lzma.h>

enum fmt
{
	FMT_RAW = 0,
	FMT_GZIP = 1,
	FMT_BZIP2 = 2,
	FMT_XZ = 3,
	FMT_NULL = 4
};

static int str2fmt(char *fmt)
{
	int ret;
    if(strcmp(fmt, "bz2") == 0)
    {
    	ret = FMT_BZIP2;
    }
    else if(strcmp(fmt, "gz") == 0)
    {
    	ret = FMT_GZIP;
    }
    else if(strcmp(fmt, "xz") == 0)
    {
    	ret = FMT_XZ;
    }
    else if(strcmp(fmt, "null") == 0)
    {
    	ret = FMT_NULL;
    }
    else
    {
    	ret = FMT_RAW;
    }
    return ret;
}

#define BUFLEN (64*1024)

/* xz helper functions

   /usr/share/doc/liblzma-dev/xz-file-format.txt.gz
   /usr/share/doc/liblzma-dev/examples/
*/

int xz_enc_init(lzma_stream *str)
{
	lzma_ret ret_xz;
    ret_xz = lzma_easy_encoder(str, LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC64);
    return ret_xz;
}

int xz_enc_write(lzma_stream *str, char *buf_in, uint32_t len_in, FILE *fp_out)
{
	uint8_t buf_out[BUFLEN];
	lzma_ret ret_xz;

    str->next_in = (uint8_t *)buf_in;
    str->avail_in = len_in;

    while(1)
    {
    	str->next_out = buf_out;
    	str->avail_out = BUFLEN;

    	ret_xz = lzma_code(str, len_in ? LZMA_RUN : LZMA_FINISH);
    	if((ret_xz != LZMA_OK) && (ret_xz != LZMA_STREAM_END))
        {
        	fprintf(stderr, "lzma_code ret %d\n", ret_xz);
        	break;
        }

        fwrite(buf_out, BUFLEN - str->avail_out, 1, fp_out);

    	if(str->avail_out != 0)
        {
        	/* didn't use it all, so we're done */
        	break;
        }
    }

	return ret_xz;
}

int xz_enc_exit(lzma_stream *str, FILE *fp_out)
{
	int ret;
	ret = xz_enc_write(str, NULL, 0, fp_out);
	lzma_end(str);
	return ret;
}


int xz_dec_init(lzma_stream *str)
{
	lzma_ret ret_xz;
    ret_xz = lzma_stream_decoder(str, UINT64_MAX, LZMA_CONCATENATED);
    return ret_xz;
}

int xz_dec_read(lzma_stream *str, char *buf_in, int *len_in, char *buf_out, int *len_out)
{
	lzma_ret ret_xz;

	return -1; /* TODO */

    str->next_in = (uint8_t *)buf_in;
    str->avail_in = *len_in;

    while(1)
    {
    	str->next_out = (uint8_t *)buf_out;
    	str->avail_out = BUFLEN;

    	ret_xz = lzma_code(str, len_in ? LZMA_RUN : LZMA_FINISH);
    	if((ret_xz != LZMA_OK) && (ret_xz != LZMA_STREAM_END))
        {
        	fprintf(stderr, "lzma_code ret %d\n", ret_xz);
        	break;
        }

    	if(str->avail_out != 0)
        {
        	/* didn't use it all, so we're done */
        	break;
        }
    }

	return ret_xz;
}

int xz_dec_exit(lzma_stream *str, FILE *fp_out)
{
	lzma_end(str);
	return 0;
}



static int file_load(char *fn_in, char *infmt, char *outfmt)
{
    char buf_in[BUFLEN], buf_out[BUFLEN];
    FILE *fp_in, *fp_out;
    BZFILE *bzf_in, *bzf_out;
    gzFile gzf_in, gzf_out;
    lzma_stream lzma_str = LZMA_STREAM_INIT;
	struct timeval tv_start, tv_stop;
    int ret, err, fmt_in, fmt_out, sl, buf_in_pos, buf_in_len, buf_out_len;
    uint64_t bytes, usec;

    fp_in = fp_out = bzf_in = bzf_out = gzf_in = gzf_out = NULL;

    fmt_in = str2fmt(infmt);
    fmt_out = str2fmt(outfmt);

    //printf("fmt_in %u fmt_out %u\n", fmt_in, fmt_out);
    if(fmt_in == fmt_out)
    {
    	fprintf(stderr, "fmt_in %d == fmt_out %d. not supported.\n", fmt_in, fmt_out);
    	return -1;
    }

    ret = gettimeofday(&tv_start, NULL);
    if(ret != 0)
    {
    	fprintf(stderr, "gettimeofday failed\n");
    }

    if(fmt_in == FMT_GZIP)
    {
        gzf_in = gzopen(fn_in, "rb");
        if(!gzf_in)
        {
            printf("file_load: gzopen failed\n");
            return -1;
        }
    }
    else
    {
    	fp_in = fopen(fn_in, "rb");
    	if(!fp_in)
        {
        	printf("file_load: fopen failed\n");
        	return -1;
        }

        if(fmt_in == FMT_BZIP2)
        {
            bzf_in = BZ2_bzReadOpen(&err, fp_in, 0, 0, NULL, 0);
            if(!bzf_in)
            {
                fprintf(stderr, "bzReadOpen bzerr %d\n", err);
                fclose(fp_in);
                return -1;
            }
        }
        else if(fmt_in == FMT_XZ)
        {
        	ret = xz_dec_init(&lzma_str);
        	if(ret != LZMA_OK)
            {
            	printf("xz_enc_init failed %d\n", ret);
            	fclose(fp_out);
            	return -1;
            }
        }
    }

    sl = strlen(fn_in);
    fn_in[sl-strlen(infmt)-1] = 0;
    snprintf(buf_out, FILENAME_MAX, "%s.%s", fn_in, outfmt);
    buf_out[FILENAME_MAX-1] = 0;
    fn_in[sl-strlen(infmt)-1] = '.';
    printf("%s -> %s", fn_in, buf_out);

    if(fmt_out == FMT_GZIP)
    {
        gzf_out = gzopen(buf_out, "wb9");
        if(gzf_out == NULL)
        {
            printf("file_load: gzopen failed\n");
            return -1;
        }
    }
    else
    {
        fp_out = fopen(buf_out, "wb");
        if(!fp_out)
        {
        	printf("fp_out fopen failed\n");
            return 1;
        }

        if(fmt_out == FMT_BZIP2)
        {
            bzf_out = BZ2_bzWriteOpen(&err, fp_out, 9, 0, 0);
            if(!bzf_out)
            {
                printf("bzerr %d\n", err);
                fclose(fp_out);
                return -1;
            }
        }
        else if(fmt_out == FMT_XZ)
        {
        	ret = xz_enc_init(&lzma_str);
        	if(ret != LZMA_OK)
            {
            	printf("xz_enc_init failed %d\n", ret);
            	fclose(fp_out);
            	return -1;
            }
        }
    }
    
    buf_in_pos = 0;
    buf_in_len = 0;

    bytes = 0;
    while(1)
    {
    	switch(fmt_in)
        {
        	case FMT_BZIP2:
        	{
                ret = BZ2_bzRead(&err, bzf_in, buf_out, BUFLEN);
                break;
            }

            case FMT_GZIP:
            {
                ret = gzread(gzf_in, buf_out, BUFLEN);
                err = BZ_OK;
                break;
            }

            case FMT_RAW:
            {
            	ret = fread(buf_out, BUFLEN, 1, fp_in);
                err = BZ_OK;
                break;
            }

            case FMT_XZ:
            {
            	if(buf_in_len == 0)
                {
                	ret = fread(buf_in, BUFLEN, 1, fp_in);
                }

            	ret = xz_dec_read(&lzma_str, buf_in + buf_in_pos, &buf_in_len, buf_out, &buf_out_len);
            	break;
            }

            default:
            {
            	printf("unknown fmt_in\n");
            	ret = 0;
            	break;
            }
        }

        /* process here */
        if((ret > 0) && ((err == BZ_OK) || (err == BZ_STREAM_END)))
        {
            bytes += ret;

            switch(fmt_out)
            {
                case FMT_BZIP2:
                {
                    BZ2_bzWrite(&err, bzf_out, buf_out, ret);
                    break;
                }

                case FMT_GZIP:
                {
                    ret = gzwrite(gzf_out, buf_out, ret);
                    break;
                }

                case FMT_XZ:
                {
                	ret = xz_enc_write(&lzma_str, buf_out, ret, fp_out);
                	break;
                }

                case FMT_RAW:
                {
                    ret = fwrite(buf_out, ret, 1, fp_out);
                    break;
                }

                case FMT_NULL:
                default:
                {
                    break;
                }
            }
        }
        else
        {
        	break;
        }
    }

    switch(fmt_in)
    {
    	case FMT_BZIP2:
    	{
            BZ2_bzReadClose(&err, bzf_in);
            if(err != BZ_OK)
            {
                printf("close bzerr %d\n", err);
            }
    
            fclose(fp_in);

    		break;
        }

        case FMT_GZIP:
        {
            gzclose(gzf_in);
        	break;
        }

        case FMT_RAW:
        {
            fclose(fp_in);
        	break;
        }

        default:
        {
        	break;
        }
    }

    switch(fmt_out)
    {
    	case FMT_BZIP2:
    	{
            BZ2_bzWriteClose(&err, bzf_out, 0, NULL, NULL);
            if(err != BZ_OK)
            {
                printf("close bzerr %d\n", err);
            }
    
            fclose(fp_out);
    		break;
        }

        case FMT_GZIP:
        {
            gzclose(gzf_out);
        	break;
        }

        case FMT_XZ:
        {
        	ret = xz_enc_exit(&lzma_str, fp_out);
        	fclose(fp_out);
        	break;
        }

        case FMT_RAW:
        {
            fclose(fp_out);
        	break;
        }

        default:
        {
        	break;
        }
    }

    ret = gettimeofday(&tv_stop, NULL);
    if(ret != 0)
    {
    	printf("WARN: gettimeofday failed\n");
    }

    usec = (tv_stop.tv_sec - tv_start.tv_sec) * 1000000 + (tv_stop.tv_usec - tv_start.tv_usec);

    printf(" [%" PRIu64 " bytes; %" PRIu64 " MiB/s]\n", bytes, bytes / usec);

    return 0;
}



static int fn_compare(const void *a, const void *b)
{
    int ret;

    ret = strcmp(*(char **)a, *(char **)b);

    return ret;
}


int dir_load(char *dn, char *infmt, char *outfmt)
{
    char fn[FILENAME_MAX];
    char **fns;
    DIR *dir;
    struct dirent *de;
    pid_t pid;
    uint32_t i, n, files, fns_alloc, procs;
    int sl, infmtlen;

    procs = sysconf(_SC_NPROCESSORS_ONLN);
    printf("Detected %u CPUs\n", procs);

    fns_alloc = 1024;

    infmtlen = strlen(infmt);
    if(infmtlen == 0)
    {
    	printf("infmt cannot be empty\n");
    	return -1;
    }

    dir = opendir(dn);
    if(dir == NULL)
    {
        return -1;
    }

    fns = malloc(fns_alloc * sizeof(char *));
    if(fns == NULL)
    {
    	closedir(dir);
        return -1;
    }

    files = 0;
    while(1)
    {
        de = readdir(dir);
        if(de == NULL)
        {
            break;
        }

        sl = strlen(de->d_name);
        if((sl > infmtlen) && (strcmp(&de->d_name[sl-infmtlen], infmt) == 0))
        {
            /* assume it's a regular file or symlink, not a dir */
            snprintf(fn, FILENAME_MAX, "%s/%s", dn, de->d_name);
            fn[FILENAME_MAX-1] = 0;
            fns[files] = strdup(fn);

            files++;
            if(files == fns_alloc)
            {
                fns_alloc *= 2;
                printf("reallocating fns %u\n", fns_alloc);
                fns = realloc(fns, fns_alloc * sizeof(char *));
                if(fns == NULL)
                {
                    printf("fns realloc failed!\n");
                    /* leaks */
                    closedir(dir);
                    return -1;
                }
            }
        }
    }

    closedir(dir);

    /* on ext4 and other filesystems, readdir() returns files in random order.
       with chronological data (2010-01-01-00.bz2, 2010-01-01-01.bz2, ...)
       it's nicer to process in order
    */
    qsort(fns, files, sizeof(char *), fn_compare);

    printf("Processing %u files..\n", files);

    for(i = 0; i < procs; i++)
    {
        pid = fork();
        if(pid == 0)
        {
        	/* TODO: better load balancer. if the files are not all roughly
        	   the same size, some processes may finish before others
        	*/
            for(n = i; n < files; n+= procs)
            {
                /*puts(fns[n]);*/
                file_load(fns[n], infmt, outfmt);
            }

            /* just so valgrind is clean. free even in the worker threads */
            for(n = 0; n < files; n++)
            {
                free(fns[n]);
            }
            free(fns);

            exit(0);
        }
        else if(pid > 0)
        {
            //printf("proc %u: pid %u\n", i, pid);
        }
        else
        {
            printf("fork failed\n");
        }
    }

    for(i = 0; i < procs; i++)
    {
        pid = waitpid(-1, &sl, 0);
        if(pid > 0)
        {
            //printf("pid %u exited\n", pid);
        }
    }

    for(n = 0; n < files; n++)
    {
        free(fns[n]);
    }
    free(fns);

    return 0;
}

int main(int argc, char *argv[])
{
    if(argc < 4)
    {
        puts("transpress [dir] [infmt] [outfmt]");
        return 1;
    }

    printf("transpress: %s %s -> %s\n", argv[1], argv[2], argv[3]);

    return dir_load(argv[1], argv[2], argv[3]);
}

