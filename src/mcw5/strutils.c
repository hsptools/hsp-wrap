#ifndef HSP_STRUTILS_H__
#define HSP_STRUTILS_H__

#include <string.h>

int
str_ends_with(const char *str, const char *suffix) {
  size_t lstr, lsuffix;

  if (!str || !suffix) {
    return 0;
  }
  
  lstr    = strlen(str);
  lsuffix = strlen(suffix);
  
  // String too small for suffix -- impossible match
  if (lstr < lsuffix) {
    return 0;
  }
  
  return !strncmp(str + lstr - lsuffix, suffix, lsuffix);
}

int
str_cnt_chr(const char *str, char chr) {
  int i = 0;
  while( str[i] ) {
    if( str[i] == chr ) {
      ++i;
    } else {
      ++str;
    }
  }
  return i;
}

#endif
