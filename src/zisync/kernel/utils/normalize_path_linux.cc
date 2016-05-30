// Copyright 2014, zisync.com
#include <cstring>
#include "zisync/kernel/utils/normalize_path.h"

namespace zs {

enum TOKEN_TYPE {
  TOKEN_START = 0,
  TOKEN_ONE_POINT,
  TOKEN_TWO_POINT,
  TOKEN_FILE_NAME,
};

struct token_t{
  int type;
  char* begin;
  char* end;
};

static struct token_t* get_next_token(struct token_t *tok)
{
  enum STAT_INNER{
    STAT_TOKEN_START = 0,
    STAT_ONE_POINT,
    STAT_TWO_POINT,
    STAT_CHAR,
    STAT_TOKEN_END,
  };

  while(*tok->end != '\0' && *tok->end == '/'){
    tok->end++;
  }

  int stat = STAT_TOKEN_START;
  tok->begin = tok->end;
  while (1){
    switch(stat){
      case STAT_TOKEN_START:
        if(*tok->end == '.'){
          stat = STAT_ONE_POINT;
        }else if(*tok->end == '\0'){
          return NULL;
        }else{
          stat = STAT_CHAR;
        }
        break;

      case STAT_ONE_POINT:
        if(*tok->end == '.'){
          stat = STAT_TWO_POINT;
        }else if(*tok->end == '/' || *tok->end == '\0'){
          tok->type = TOKEN_ONE_POINT;
          stat = STAT_TOKEN_END;
        }else{
          stat = STAT_CHAR;
        }
        break;

      case TOKEN_TWO_POINT:
        if(*tok->end == '/' || *tok->end == '\0'){
          tok->type = TOKEN_TWO_POINT;
          stat = STAT_TOKEN_END;
        }else{
          stat = STAT_CHAR;
        }
        break;

      default:
        if(*tok->end == '/' || *tok->end == '\0'){
          tok->type = TOKEN_FILE_NAME;
          stat = STAT_TOKEN_END;
        }else{
          stat = STAT_CHAR;
        }
    }

    if (stat != STAT_TOKEN_END) {
      tok->end ++;
    } else {
      break;
    }
  }

  return tok;
}

static char *go_back_previous_slash(char *write_head, char *path)
{
  char *ch_index;
  if(write_head == path){
    return NULL;
  }else{
    *write_head = '\0';
    ch_index = strrchr(path, '/');	
    if(ch_index == NULL){
      write_head = path;
    }else if(ch_index == path){
      write_head = ch_index + 1;
    }else{
      write_head = ch_index;
    }
  }

  return write_head;
}

char *normalize_path_for_linux(char *path)
{
  struct token_t path_tok, *token_ret = NULL;
  char *write_head = path;

  path_tok.begin = path_tok.end = path;
  if(path[0] == '/'){
    write_head++;
    path_tok.end++;
  }//start

  while(1){
    token_ret = get_next_token(&path_tok);
    if(token_ret == NULL){
      break;
    }

    switch(path_tok.type){
      case TOKEN_ONE_POINT:
        break;
      case TOKEN_TWO_POINT:
        write_head = go_back_previous_slash(write_head, path);
        if(!write_head)
          return NULL;
        break;
      case TOKEN_FILE_NAME:
        if(path_tok.begin == write_head){
          write_head = path_tok.end;
        } else {
          if(write_head != path && *(write_head - 1) != '/'){
            *write_head++ = '/';
          }	
          strncpy(write_head, path_tok.begin,
                  path_tok.end - path_tok.begin);
          write_head += path_tok.end - path_tok.begin;
        }
        break;
      default:
        return NULL;
    }
  }
  *write_head = '\0';

  return path;	
}

}  // namespace zs

