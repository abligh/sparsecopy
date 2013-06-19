/* (c) 2010 Flexiant Limited
 *
 * This file is released under the GPL v2
 *
 * See under usage() for a complete description of this program.
 */

#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

/* for BLKRRPART*/
#ifdef __linux__
#include <linux/fs.h>      
#endif

#define NUMCHARS 40

/* State passed to copy routines */

static int truncate_flag =1;
static int nocheck_flag = 0;
static int quiet_flag = 0;
static int finalsize_flag = 0;
static int check_flag = 0;
static int progress_flag = 0;
static int errorremainder_flag =0;
static off_t blocksize = 512;
static off_t finalsize = 0;
static off_t seekpos = 0;
static off_t maxwrite = -1;
static off_t syncevery = -1;
static int source;
static int dest;
static off_t origdestextent;
static off_t origsourceextent;

/* Statistics flowing back again */

static long long int real=0, sparse=0, nonzerodest=0, total=0, towrite=0;

void usage()
{
  fprintf(stderr,"\
sparsecopy v0.10\n\
\n\
Usage:\n\
     sparsecopy [OPTIONS] SOURCE DEST\n\
\n\
Options:\n\
     -o, --overlay          Overlay existing file DEST (rather than truncating)\n\
     -n, --nocheck          Skip blank check\n\
     -q, --quiet            Quiet\n\
     -m, --max SIZE         Copy SIZE bytes maximum from SOURCE\n\
     -e, --errorremainder   Error if there is more to write beyond --max value\n\
     -b, --blocksize SIZE   Use SIZE blocksize in bytes (default 512)\n\
     -s, --seek POS         Start writing SOURCE at position POS in DEST\n\
     -f, --finalsize SIZE   Set size of DEST to SIZE when done (no effect on block devices)\n\
     -c, --check            Check the first block in the destination is zero, else abort\n\
     -p, --progress         Display progress indicator on stderr\n\
     -y, --syncevery SIZE   Issue an fdatasync() every SIZE byets\n\
     -h, --help             Display usage\n\
\n\
Sparsecopy copies from a source file (either sparse or non-sparse) to a destination file\n\
making the destination file sparse on the way. If the destination file already exists\n\
then the source file can be superimposed on top of it by using the -o switch (ignored\n\
if the destination file doesn't exist); in this case sparsecopy will (unless -n is\n\
specified as well) check that any blocks to be overlayed with zero (i.e. made sparse)\n\
are already zero in the destination images. When used with block devices, sparsecopy\n\
will not affect the size of the device.\n\
\n\
SOURCE can be \"-\" to indicate stdin. SOURCE or DEST can be raw devices if you wish.\n\
If you want to make a sparse file, either use /dev/zero for SOURCE and -m, or (quicker) use\n\
/dev/null for SOURCE and -f.\n\
\n\
SIZE and POS can be specified in blocks (default), or use the following suffixes:\n\
     B  Bytes      (2^0  bytes)\n\
     K  Kilobytes  (2^10 bytes)\n\
     M  Megabytes  (2^20 bytes)\n\
     G  Gigabtytes (2^30 bytes)\n\
     T  Terabytes  (2^40 bytes)\n\
     P  Perabytes  (2^50 bytes)\n\
     E  Exabytes   (2^60 bytes)\n\
\n\
Note that blocksize=1024 will set blocksize to 1024 512byte blocks (use 1024B if this is not\n\
what you mean). Also note that disk capacity is often measured using decimal megabytes etc.;\n\
we do not adopt this convention for compatibility with dd.\n");
}

/*
 * getsize() returns the size of an argument that may have a sizing suffix
 */

off_t getsize(char * arg)
{
  off_t param=0;
  char *end, *found;
  const char *suffix = "bkmgtpe";
  param = strtoull(arg, &end, 10);

  if (*end == '\0')
    {
      param *= blocksize;
    }
  else if ((found = strchr(suffix, tolower(*end))))
    {
      param <<= (10 * (found-suffix));
    }
  else
    {
      fprintf(stderr,"sparsecopy: Bad parameter\n");
      exit(1);
    }
  return param;
}


  /* First parse our options */
void parse_command_line(int argc, char **argv)
{
  /* We can't evaluate these in the while() loop
   * As blocksize might not have been altered yet
   */
  char * maxwrite_arg=NULL;
  char * blocksize_arg=NULL;
  char * seekpos_arg=NULL;
  char * finalsize_arg=NULL;
  char * syncevery_arg=NULL;
  
  while (1)
    {
      static struct option long_options[] =
	{
	  {"overlay",   no_argument,       0, 'o'},
	  {"nocheck",   no_argument,       0, 'n'},
	  {"quiet",     no_argument,       0, 'q'},
	  {"check",     no_argument,       0, 'c'},
	  {"progress",  no_argument,       0, 'p'},
	  {"help",      no_argument,       0, 'h'},
	  {"errorremainder", no_argument,  0, 'e'},
	  {"maxwrite",  required_argument, 0, 'm'},
	  {"blocksize", required_argument, 0, 'b'},
	  {"seek",      required_argument, 0, 's'},
	  {"finalsize", required_argument, 0, 'f'},
	  {"syncevery", required_argument, 0, 'y'},
	  {0, 0, 0, 0}
	};
      /* getopt_long stores the option index here. */
      int option_index = 0;
      int c;
      
      c = getopt_long (argc, argv, "onqcphem:b:s:f:y:",
		       long_options, &option_index);
      
      /* Detect the end of the options. */
      if (c == -1)
	break;
      
      switch (c)
	{
	case 0:
	  /* If this option set a flag, do nothing else now. */
	  break;
	  
	case 'o':
	  truncate_flag=0;
	  break;

	case 'n':
	  nocheck_flag=1;
	  break;

	case 'q':
	  quiet_flag=1;
	  break;

	case 'c':
	  check_flag=1;
	  break;

	case 'p':
	  if (isatty(STDERR_FILENO))
	    progress_flag=1;
	  break;

	case 'h':
	  usage();
	  exit(0);
	  break;

	case 'e':
	  errorremainder_flag = 1;
	  break;
	  
	case 'b':
	  blocksize_arg = strdup(optarg);
	  break;
	  
	case 's':
	  seekpos_arg = strdup(optarg);
	  break;
	  
	case 'f':
	  finalsize_arg = strdup(optarg);
	  break;
	  
	case 'm':
	  maxwrite_arg = strdup(optarg);
	  break;
	  
	case 'y':
	  syncevery_arg = strdup(optarg);
	  break;
	  
	default:
	  usage();
	  exit(1);
	}
    }

  /* Do blocksize first */
  if (blocksize_arg)
    {
      blocksize = getsize(blocksize_arg);
      if (blocksize<=0)
	{
	  fprintf(stderr,"sparsecopy: Bad block size %lld\n",(long long int)blocksize);
	  exit(1);
	}
      free(blocksize_arg);
    }

  if (seekpos_arg)
    {
      seekpos = getsize(seekpos_arg);
      if (seekpos<0)
	{
	  fprintf(stderr,"sparsecopy: Bad seek position %lld\n",(long long int)seekpos);
	  exit(1);
	}
      free(seekpos_arg);
    }

  if (finalsize_arg)
    {
      finalsize = getsize(finalsize_arg);
      if (finalsize<0)
	{
	  fprintf(stderr,"sparsecopy: Bad final size %lld\n",(long long int)finalsize);
	  exit(1);
	}
      finalsize_flag++;
      free(finalsize_arg);
    }

  if (maxwrite_arg)
    {
      maxwrite = getsize(maxwrite_arg);
      if (maxwrite<0)
	{
	  fprintf(stderr,"sparsecopy: Bad max write %lld\n",(long long int)maxwrite);
	  exit(1);
	}
      free(maxwrite_arg);
    }

  if (syncevery_arg)
    {
      syncevery = getsize(syncevery_arg);
      if (syncevery<0)
	{
	  fprintf(stderr,"sparsecopy: Bad sync %lld\n",(long long int)syncevery);
	  exit(1);
	}
      free(syncevery_arg);
    }

  /* Check we have exactly 2 remaining parameters */
  if ((optind+2) != argc)
    {
      usage();
      exit(1);
    }

  /* Print any remaining command line arguments (not options). */

  if (!strcmp(argv[optind], "-"))
    {
      if (-1 == (source = dup(STDIN_FILENO)))
	{
	  perror("dup() Could not open source file");
	  exit(2);
	}
    }
  else
    {
      if (-1 == (source = open(argv[optind], O_RDONLY)))
	{
	  perror("open() Could not open source file");
	  exit(2);
	}
    }

  if (-1 == (dest = open(argv[optind+1],
			 ((check_flag || !nocheck_flag)?O_RDWR:O_WRONLY)|
			 O_CREAT|
			 (truncate_flag?O_TRUNC:0),
			 0666
			 )))
    {
      perror("open() Could not open destination file");
      exit(3);
    }

  if (-1 == (origdestextent = lseek(dest, 0, SEEK_END)))
    {
      perror("lseek(dest) Seek on destination file failed");
      exit(4);
    }

  if (-1 == (lseek(dest, seekpos, SEEK_SET)))
    {
      perror("lseek(dest) Seek on destination file failed");
      exit(4);
    }

  if (-1 == (origsourceextent = lseek(source, 0, SEEK_END)))
    {
      /* Seek will fail on STDIN */
      progress_flag=0;
    }
  else if (-1 == (lseek(source, 0, SEEK_SET)))
    {
      perror("lseek(source) Seek on source file failed");
      exit(4);
    }
  
  towrite = origsourceextent;
  if ((maxwrite>0) && (maxwrite < origsourceextent))
    {
      towrite = maxwrite;
    }
}

void dosparsecopy(const int source, const int dest)
{
  char *zero, *incoming, *check;
  size_t available;
  size_t writtensincesync = 0;
  int skipwrite;
  double lastpercent = -1.0;

  if (!(zero = calloc(1, blocksize)) || !(incoming = calloc(1, blocksize)) || !(check = calloc(1, blocksize)))
    {
      perror("calloc()/malloc() failed");
      exit(5);
    }

  /* If they have specified the check option, check the first block of the dest file is zero*/
  if (check_flag)
    {
      long long checklen=0;
      if (-1 == (checklen = read(dest, check, blocksize)))
	{
	  perror ("read(dest) Check read of destination failed");
	  exit(6);
	}
      /* Restore file pointer */
      if (-1 == lseek(dest, 0, SEEK_SET))
	{
	  perror("lseek(dest) Can't set destination file pointer");
	  exit(7);
	}

      /* Check for zeroes */
      if (checklen && memcmp(check, zero, checklen))
	{
	  fprintf(stderr, "Destination file failed initial zero block check\n");
	  exit(11);
	}
    }


  /*
   * This is the main loop
   */

  while ((-1 != (available = read(source, incoming, (maxwrite>=0)?((maxwrite<blocksize)?maxwrite:blocksize):blocksize)))
	 && (available > 0)
	 && maxwrite)
    {
      skipwrite = 0;
      if (maxwrite>0)
	{
	  maxwrite-=available;
	}
      if(!memcmp(incoming, zero, available))
	{
	  /* It is a zero block in the input stream */

	  off_t destpos;
	  if (-1 == (destpos = lseek(dest, 0, SEEK_CUR)))
	    {
	      perror("lseek(dest) Get desitination file position failed");
	      exit(4);
	    }

	  skipwrite = 1;

	  /* We don't do the check if nocheck_flag is set, or if we are beyond the
	   * end of the original file
	   */

	  if (!(nocheck_flag || (destpos>=origdestextent)))
	    {
	      /*
	       * We need to check the resultant block is zero in the output stream is zero
	       * It might be crud left from a previous write if we are overwriting a file
	       *
	       * Note if overlay (!trunc_flag) was set, then origdestextent will be zero,
	       * and this code will never be executed
	       */
	      long long checklen=0;
	      if (-1 == (checklen = read(dest, check, blocksize)))
		{
		  perror ("read(dest) Check read of destination failed");
		  exit(6);
		}
	      /* Restore file pointer */
	      if (-1 == lseek(dest, destpos, SEEK_SET))
		{
		  perror("lseek(dest) Can't set destination file pointer");
		  exit(7);
		}
	      if (checklen && memcmp(check, zero, checklen))
		{
		  skipwrite=0;
		  nonzerodest += available;
		}
	    }
	}
      
      if (skipwrite)
	{
	  /* Everything we read was zero, so we can just seek past it */
	  if(-1 == lseek(dest, available, SEEK_CUR))
	    {
	      perror("lseek(dest) sparse skip failed");
	      exit(8);
	    }
	  sparse += available;
	}
      else
	{
	  /* Non-zero block, or non-zero destination just write it out */
	  if(write(dest, incoming, available) < available)
	    {
	      perror("write(dest) failed");
	      exit(9);
	    }
	  real += available;
	  writtensincesync += available;
	  if ((syncevery >0) && (writtensincesync >= syncevery))
	    {
	      fdatasync(dest); /* ignore errors */
	      writtensincesync = 0;
	    }
	}

      total = real + sparse;

      if (progress_flag)
	{
	  double percent = 100;
	  if (towrite)
	    percent = round((total * 1000.0) / towrite)/10;
	  
	  if (lastpercent != percent)
	    {

	      char display[NUMCHARS+1];
	      int i;
	      int j;
	      j = round(NUMCHARS*percent/100.0);
	      display[NUMCHARS]=0;

	      lastpercent = percent;

	      for (i=0; i<NUMCHARS; i++)
		{
		  display[i]=(i<j)?'=':((i==j)?'>':'-');
		}
	      fprintf (stderr, "|%s| %3.1f%% %.3fMB of %.3fMB  \r",
		       display,
		       percent,
		       (total*1.0/1024.0/1024.0),
		       (towrite*1.0/1024.0/1024.0)
		       );
	    }
	}
    }
  if (progress_flag)
    fprintf(stderr, "\n");
  
  if ((maxwrite == 0) && errorremainder_flag)
    {
      available = read(source, incoming, 1);
      if (available)
	{
	  fprintf (stderr, "More data supplied than maximum permissible\n");
	  exit (12);
	}
    }
  
}

  /*
   * We may have lseek'd beyond the end of the file; this doesn't set the
   * file length, so we need to call ftruncate(). But this errors on
   * block devices, so we don't do it if the dest is a block device
   */
void force_length(int dest, off_t desired)
{
  struct stat sb;

  if (-1 == fstat(dest, &sb))
    {
      perror("stat failed");
      exit(9);
    }

  close(source);

  if (S_ISBLK(sb.st_mode))
    {
      close(dest);
      /* Block device, may need to reread partition table */
#ifdef __linux__
      int retries=40;
      if (!quiet_flag)
	fprintf(stderr,"Rereading partition table\n");
      sync();
      while (ioctl(dest, BLKRRPART) && (errno==EBUSY) && retries--)
	{
	  /* Never let it be said that linux partition handling is a bodge */
	  usleep(250*1000);
	}
#endif
    }
  else if (S_ISREG(sb.st_mode))
    {
      off_t truncpos;
      if (-1 == (truncpos = lseek(dest, 0, SEEK_CUR)))
	{
	  perror("lseek(dest) Get desitination file position failed");
	  exit(4);
	}
     
      if (origdestextent > truncpos)
	{
	  truncpos = origdestextent;
	}

      if (finalsize_flag)
	{
	  truncpos = finalsize;
	}

      if (-1 == ftruncate(dest, truncpos))
	{
	  perror("ftruncate(dest) failed");
	  exit(10);
	}
      close(dest);
    }
}

int main(int argc, char **argv, char *env)
{
  parse_command_line(argc, argv);
  dosparsecopy(source, dest);
  force_length(dest, finalsize);

  if (!quiet_flag)
    {
      fprintf(stderr, "sparsecopy: total bytes = %lld (real = %lld, sparse = %lld), nonzerodest = %lld\n",
	      real + sparse, real, sparse, nonzerodest);
    }
  exit (0);
}
