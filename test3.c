#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesystem.h"

// RUN formatfs before conducting this test!
  
int main(int argc, char *argv[]) {
  int i;
  File f;
  char c='A';
  unsigned long ret, maxlen;
  char *buf, *buf2;

  // see how large a single file can be
  f=create_file("superfile");
  printf("ret from create_file(\"superfile\") = %p\n",
	 f);
  fs_print_error();

  if (! f) {
    goto fail;
  }

  printf("Please wait, finding max file size.  This may take a while.\n");
  
  while (1) {
    ret = write_file(f, &c, 1);
    c++;
    if (c > 'Z') {
      c='A';
    }

    maxlen=file_length(f);
    
    if (! ret) {
      fs_print_error();
      printf("Maximum file size appears to be %lu bytes.\n", maxlen);
      break;
    }
  }

  close_file(f);
  printf("Executed close_file(f).\n");
  fs_print_error();

  delete_file("superfile");
  printf("Executed delete_file(\"superfile\").\n");
  fs_print_error();

  // now create one file of maximum length, with a single write
  f=create_file("superfile-max");
  printf("ret from create_file(\"superfile-max\") = %p\n",
	 f);
  fs_print_error();
  if (!f) {
    goto fail;
  }
  
  buf=malloc(maxlen+1);
  buf2=malloc(maxlen+1);
  c='A';
  for (i=0; i < maxlen; i++) {
    buf[i]=c;
    c++;
    if (c > 'Z') {
      c='A';
    }
  }

  ret = write_file(f, buf, maxlen);
  printf("ret from write_file(f, buf, %lu) = %lu\n",
	 maxlen, ret);
    fs_print_error();

  printf("Seeking to beginning of file.\n");
  seek_file(f, 0);
  fs_print_error();

  ret = read_file(f, buf2, maxlen);
  printf("ret from read_file(f, buf2, %lu) = %lu\n",
	 maxlen, ret);
  fs_print_error();
  
  close_file(f);
  printf("Executed close_file(f).\n");
  fs_print_error();

  printf("Read / write buffers for large file %s.\n",
	 ! memcmp(buf, buf2, maxlen) ? "match" : "don't match");

  return 0;

 fail:
  printf("FAIL. Was formatfs run before this test?.\n");
  return 0;
}